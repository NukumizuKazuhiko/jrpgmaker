#include <filesystem>
#include <iostream>

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include "jrpgmaker/editor/editor_shell.hpp"
#include "jrpgmaker/editor/editor_session.hpp"
#include "jrpgmaker/project/workspace.hpp"
#include "jrpgmaker/render/ui_draw_adapter.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/swapchain.hpp"
#include "shaders_generated.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"

namespace {

void PrintStartupDiagnostics(
    const std::vector<jrpgmaker::ui::EditorStartupDiagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics)
        std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
}

void* NativeWindowHandle(SDL_Window* window) {
#if defined(_WIN32)
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    return SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    return window;
#endif
}

jrpgmaker::rhi::ClearColor ThemeClearColor(const jrpgmaker::ui::EditorTheme& theme) {
    const auto color = theme.colors.find("canvas");
    if (color == theme.colors.end())
        throw std::runtime_error("editor.theme.canvas_color_missing");
    constexpr float scale = 1.0f / 255.0f;
    return {color->second.r * scale, color->second.g * scale, color->second.b * scale,
            color->second.a * scale};
}

const jrpgmaker::editor::ShellNode* FindShellNode(
    const jrpgmaker::editor::ShellProjection& projection, std::string_view id) {
    for (const auto& node : projection.nodes)
        if (node.id == id)
            return &node;
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 3)
        return 2;
    const bool smoke = argc == 2 && std::string(argv[1]) == "--smoke";
    if (argc == 3 && std::string(argv[2]) != "--smoke")
        return 2;

    const auto resources = jrpgmaker::ui::LoadEditorResources(
        std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_ROOT));
    if (!resources) {
        PrintStartupDiagnostics(resources.diagnostics);
        return 1;
    }
    const auto shell = jrpgmaker::editor::BuildShellProjection(resources.bundle->layout);
    if (!shell)
        return 1;
    const auto input_map = jrpgmaker::editor::BuildInputMap(resources.bundle->action_map);

    std::unique_ptr<jrpgmaker::editor::EditorSession> session;
    if ((argc == 2 && !smoke) || argc == 3) {
        const auto project_argument = std::filesystem::path(argv[1]);
        session = std::make_unique<jrpgmaker::editor::EditorSession>(project_argument);
        if (!session->Open()) {
            for (const auto& diagnostic : session->state().diagnostics)
                std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
            return 1;
        }
    }

    const auto title = resources.bundle->locale.strings.at("editor.window.title");
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;
    const SDL_WindowFlags window_flags =
#if defined(_WIN32)
        SDL_WINDOW_RESIZABLE;
#else
        static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
#endif
    SDL_Window* window = SDL_CreateWindow(title.c_str(), 1280, 720, window_flags);
    if (window == nullptr) {
        SDL_Quit();
        return 1;
    }
    SDL_StartTextInput(window);

    std::unique_ptr<jrpgmaker::rhi::IDevice> device;
    jrpgmaker::rhi::ISwapchain* swapchain = nullptr;
    jrpgmaker::rhi::ICommandList* command_list = nullptr;
    jrpgmaker::rhi::PipelineHandle pipeline = jrpgmaker::rhi::PipelineHandle::kInvalid;
    jrpgmaker::render::UiGpuBatch gpu_batch;
    try {
#if defined(_WIN32)
        device = jrpgmaker::rhi::CreateDevice(jrpgmaker::rhi::Backend::kD3D12);
#else
        device = jrpgmaker::rhi::CreateDevice(jrpgmaker::rhi::Backend::kVulkan);
#endif
        if (!device)
            throw std::runtime_error("editor.rhi.device_creation_failed");
        swapchain = device->CreateSwapchain(NativeWindowHandle(window), 1280, 720,
                                            jrpgmaker::rhi::Format::kB8G8R8A8Unorm);
        const jrpgmaker::rhi::VertexAttribute attributes[] = {
            {.location = 0,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat3,
             .offset_bytes = 0,
             .semantic_name = "POSITION"},
            {.location = 1,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat4,
             .offset_bytes = sizeof(float) * 3,
             .semantic_name = "COLOR"},
        };
        jrpgmaker::rhi::GraphicsPipelineDesc pipeline_desc{
            .vertex_shader = {
#if defined(_WIN32)
                jrpgmaker::shaders::kUiVsDxil, jrpgmaker::shaders::kUiVsDxil_size
#else
                jrpgmaker::shaders::kUiVsSpv, jrpgmaker::shaders::kUiVsSpv_size
#endif
            },
            .pixel_shader = {
#if defined(_WIN32)
                jrpgmaker::shaders::kUiPsDxil, jrpgmaker::shaders::kUiPsDxil_size
#else
                jrpgmaker::shaders::kUiPsSpv, jrpgmaker::shaders::kUiPsSpv_size
#endif
            },
            .color_format = jrpgmaker::rhi::Format::kB8G8R8A8Unorm,
            .vertex_input = {attributes, 2, sizeof(jrpgmaker::render::UiVertex)}};
        pipeline = device->CreatePipeline(pipeline_desc);
        auto draw_list = jrpgmaker::editor::BuildShellDrawList(*shell);
        if (session != nullptr) {
            const auto* form_node = FindShellNode(*shell, "workspace.form");
            const auto row_height = resources.bundle->theme.dimensions.at("font.body") +
                                    resources.bundle->theme.dimensions.at("space.sm");
            if (form_node != nullptr) {
                const auto form_draw_list = jrpgmaker::editor::BuildFormDrawList(
                    session->state().form, form_node->bounds, row_height,
                    session->state().selected_field);
                for (const auto& primitive : form_draw_list.primitives())
                    (void) draw_list.Add(primitive);
            }
        }
        const auto packet = jrpgmaker::render::BuildUiDrawPacket(
            draw_list, resources.bundle->theme, {1280.0f, 720.0f});
        if (!packet.ok())
            throw std::runtime_error("editor.ui.draw_packet_invalid");
        gpu_batch = jrpgmaker::render::UploadUiDrawPacket(*device, packet);
        command_list = device->CreateCommandList();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (command_list != nullptr)
            device->DestroyCommandList(command_list);
        if (pipeline != jrpgmaker::rhi::PipelineHandle::kInvalid)
            device->DestroyPipeline(pipeline);
        if (device != nullptr)
            jrpgmaker::render::DestroyUiGpuBatch(*device, gpu_batch);
        if (swapchain != nullptr)
            device->DestroySwapchain(swapchain);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    bool ui_dirty = false;
    SDL_Event event{};
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                const auto modifiers = SDL_GetModState();
                const auto action = input_map.Translate(
                    SDL_GetKeyName(event.key.key), true, (modifiers & SDL_KMOD_CTRL) != 0,
                    (modifiers & SDL_KMOD_SHIFT) != 0, (modifiers & SDL_KMOD_ALT) != 0);
                if (!action || session == nullptr)
                    continue;
                if (*action == jrpgmaker::editor::EditorAction::kRefresh) {
                    if (!session->Refresh())
                        for (const auto& diagnostic : session->state().diagnostics)
                            std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
                } else if (*action == jrpgmaker::editor::EditorAction::kSave) {
                    if (!session->Save())
                        for (const auto& diagnostic : session->state().diagnostics)
                            std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
                } else if (*action == jrpgmaker::editor::EditorAction::kPreview) {
                    if (!session->StartPreview(JRPGMAKER_RUNTIME_EXECUTABLE))
                        std::cerr << "editor.preview.process_start_failed\n";
                } else if (*action == jrpgmaker::editor::EditorAction::kSelectNext) {
                    ui_dirty = session->SelectNext() || ui_dirty;
                } else if (*action == jrpgmaker::editor::EditorAction::kSelectPrevious) {
                    ui_dirty = session->SelectPrevious() || ui_dirty;
                }
            } else if (event.type == SDL_EVENT_TEXT_INPUT && session != nullptr) {
                if (!session->ApplySelectedText(event.text.text))
                    for (const auto& diagnostic : session->state().diagnostics)
                        std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
                else
                    ui_dirty = true;
            }
        }
        if (session != nullptr)
            session->PollPreview();
        if (ui_dirty) {
            device->WaitForGpuIdle();
            jrpgmaker::render::DestroyUiGpuBatch(*device, gpu_batch);
            auto draw_list = jrpgmaker::editor::BuildShellDrawList(*shell);
            const auto* form_node = FindShellNode(*shell, "workspace.form");
            const auto row_height = resources.bundle->theme.dimensions.at("font.body") +
                                    resources.bundle->theme.dimensions.at("space.sm");
            if (session != nullptr && form_node != nullptr) {
                const auto form_draw_list = jrpgmaker::editor::BuildFormDrawList(
                    session->state().form, form_node->bounds, row_height,
                    session->state().selected_field);
                for (const auto& primitive : form_draw_list.primitives())
                    (void) draw_list.Add(primitive);
            }
            const auto packet = jrpgmaker::render::BuildUiDrawPacket(
                draw_list, resources.bundle->theme, {1280.0f, 720.0f});
            if (!packet.ok())
                throw std::runtime_error("editor.ui.draw_packet_invalid");
            gpu_batch = jrpgmaker::render::UploadUiDrawPacket(*device, packet);
            ui_dirty = false;
        }
        const auto target = swapchain->AcquireTexture();
        command_list->Begin();
        command_list->BeginRendering(target, ThemeClearColor(resources.bundle->theme));
        jrpgmaker::render::RecordUiDrawPacket(*command_list, pipeline, gpu_batch);
        command_list->EndRendering();
        command_list->End();
        device->Submit(*command_list);
        device->WaitForGpuIdle();
        swapchain->Present();
        if (smoke)
            break;
        SDL_Delay(8);
    }
    device->WaitForGpuIdle();
    device->DestroyCommandList(command_list);
    device->DestroyPipeline(pipeline);
    jrpgmaker::render::DestroyUiGpuBatch(*device, gpu_batch);
    device->DestroySwapchain(swapchain);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
