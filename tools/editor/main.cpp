#include <algorithm>
#include <filesystem>
#include <iostream>
#include <mutex>

#include <SDL3/SDL.h>

#include "jrpgmaker/editor/editor_workspace_controller.hpp"
#include "jrpgmaker/render/ui_draw_adapter.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/swapchain.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"
#include "jrpgmaker/ui/text_draw.hpp"
#include "shaders_generated.hpp"
#include "ui_text_generated.hpp"
#include <array>
#include <memory>
#include <optional>
#include <stdexcept>

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

std::vector<std::filesystem::path> FindEditorFonts(const jrpgmaker::ui::EditorTheme& theme) {
    std::vector<std::filesystem::path> result;
    for (const auto& path : theme.font_paths)
        if (std::filesystem::exists(path))
            result.emplace_back(path);
    return result;
}

struct ProjectDialogState {
    std::mutex mutex;
    std::optional<std::filesystem::path> selected_root;
    bool pending = false;
};

void SDLCALL ProjectFolderDialogCallback(void* userdata, const char* const* filelist, int) {
    auto& state = *static_cast<ProjectDialogState*>(userdata);
    std::lock_guard lock(state.mutex);
    state.pending = false;
    if (filelist != nullptr && filelist[0] != nullptr && filelist[0][0] != '\0')
        state.selected_root = std::filesystem::path(filelist[0]);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 3)
        return 2;
    const bool smoke = (argc == 2 && std::string(argv[1]) == "--smoke") ||
                       (argc == 3 && std::string(argv[2]) == "--smoke");
    if (argc == 3 && std::string(argv[2]) != "--smoke")
        return 2;

    const auto resources =
        jrpgmaker::ui::LoadEditorResources(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_ROOT));
    if (!resources) {
        PrintStartupDiagnostics(resources.diagnostics);
        return 1;
    }
    const auto input_map = jrpgmaker::editor::BuildInputMap(resources.bundle->action_map);
    const auto font_paths = FindEditorFonts(resources.bundle->theme);
    if (font_paths.empty()) {
        std::cerr << "editor.font.unavailable\n";
        return 1;
    }
    std::vector<std::unique_ptr<jrpgmaker::ui::Font>> fonts;
    std::vector<jrpgmaker::ui::Font*> fallback_fonts;
    for (const auto& font_path : font_paths) {
        auto font = std::make_unique<jrpgmaker::ui::Font>();
        if (font->Load(font_path.string())) {
            fallback_fonts.push_back(font.get());
            fonts.push_back(std::move(font));
        }
    }
    if (fonts.empty()) {
        std::cerr << "editor.font.load_failed\n";
        return 1;
    }
    jrpgmaker::ui::GlyphAtlas glyph_atlas(1024, 1024, 512);

    const auto form_row_height = resources.bundle->theme.dimensions.at("font.body") +
                                 resources.bundle->theme.dimensions.at("space.sm");
    const auto diagnostic_row_height = resources.bundle->theme.dimensions.at("font.caption") +
                                       resources.bundle->theme.dimensions.at("space.xs");
    jrpgmaker::editor::EditorWorkspaceController controller(
        {.layout = resources.bundle->layout,
         .form_row_height = form_row_height,
         .diagnostic_row_height = diagnostic_row_height,
         .menu = {.root_width = resources.bundle->theme.dimensions.at("menu.root_width"),
                  .row_height = resources.bundle->theme.dimensions.at("menu.row_height"),
                  .popup_width = resources.bundle->theme.dimensions.at("menu.popup_width")},
         .runtime_executable = JRPGMAKER_RUNTIME_EXECUTABLE});
    if ((argc == 2 && !smoke) || argc == 3) {
        const auto project_argument = std::filesystem::path(argv[1]);
        if (!controller.OpenProject(project_argument)) {
            const auto* state = controller.state();
            if (state != nullptr)
                for (const auto& diagnostic : state->diagnostics)
                    std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
            return 1;
        }
    }
    ProjectDialogState project_dialog;

    const auto title = resources.bundle->locale.strings.at("editor.window.title");
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;
    const SDL_WindowFlags window_flags =
#if defined(_WIN32)
        SDL_WINDOW_RESIZABLE;
#else
        static_cast<SDL_WindowFlags>(SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
#endif
    std::uint32_t window_width = 1280;
    std::uint32_t window_height = 720;
    SDL_Window* window = SDL_CreateWindow(title.c_str(), window_width, window_height, window_flags);
    if (window == nullptr) {
        SDL_Quit();
        return 1;
    }
    SDL_StartTextInput(window);

    std::unique_ptr<jrpgmaker::rhi::IDevice> device;
    jrpgmaker::rhi::ISwapchain* swapchain = nullptr;
    jrpgmaker::rhi::ICommandList* command_list = nullptr;
    jrpgmaker::rhi::PipelineHandle pipeline = jrpgmaker::rhi::PipelineHandle::kInvalid;
    jrpgmaker::rhi::PipelineHandle text_pipeline = jrpgmaker::rhi::PipelineHandle::kInvalid;
    jrpgmaker::render::UiGpuBatch gpu_batch;
    jrpgmaker::render::UiTextGpuBatch text_gpu_batch;
    try {
#if defined(_WIN32)
        device = jrpgmaker::rhi::CreateDevice(jrpgmaker::rhi::Backend::kD3D12);
#else
        device = jrpgmaker::rhi::CreateDevice(jrpgmaker::rhi::Backend::kVulkan);
#endif
        if (!device)
            throw std::runtime_error("editor.rhi.device_creation_failed");
        swapchain = device->CreateSwapchain(NativeWindowHandle(window), window_width, window_height,
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
            .vertex_shader =
                {
#if defined(_WIN32)
                    jrpgmaker::shaders::kUiVsDxil, jrpgmaker::shaders::kUiVsDxil_size
#else
                    jrpgmaker::shaders::kUiVsSpv, jrpgmaker::shaders::kUiVsSpv_size
#endif
                },
            .pixel_shader =
                {
#if defined(_WIN32)
                    jrpgmaker::shaders::kUiPsDxil, jrpgmaker::shaders::kUiPsDxil_size
#else
                    jrpgmaker::shaders::kUiPsSpv, jrpgmaker::shaders::kUiPsSpv_size
#endif
                },
            .color_format = jrpgmaker::rhi::Format::kB8G8R8A8Unorm,
            .vertex_input = {attributes, 2, sizeof(jrpgmaker::render::UiVertex)}};
        pipeline = device->CreatePipeline(pipeline_desc);
        const jrpgmaker::rhi::VertexAttribute text_attributes[] = {
            {.location = 0,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat3,
             .offset_bytes = 0,
             .semantic_name = "POSITION"},
            {.location = 1,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat2,
             .offset_bytes = sizeof(float) * 3,
             .semantic_name = "TEXCOORD"},
            {.location = 2,
             .format = jrpgmaker::rhi::VertexAttributeFormat::kFloat4,
             .offset_bytes = sizeof(float) * 5,
             .semantic_name = "COLOR"},
        };
        jrpgmaker::rhi::GraphicsPipelineDesc text_pipeline_desc{
            .vertex_shader =
                {
#if defined(_WIN32)
                    jrpgmaker::shaders::kUiTextVsDxil, jrpgmaker::shaders::kUiTextVsDxil_size
#else
                    jrpgmaker::shaders::kUiTextVsSpv, jrpgmaker::shaders::kUiTextVsSpv_size
#endif
                },
            .pixel_shader =
                {
#if defined(_WIN32)
                    jrpgmaker::shaders::kUiTextPsDxil, jrpgmaker::shaders::kUiTextPsDxil_size
#else
                    jrpgmaker::shaders::kUiTextPsSpv, jrpgmaker::shaders::kUiTextPsSpv_size
#endif
                },
            .color_format = jrpgmaker::rhi::Format::kB8G8R8A8Unorm,
            .vertex_input = {text_attributes, 3, sizeof(jrpgmaker::render::UiTextVertex)},
            .sample_slot = 1,
            .blend_mode = jrpgmaker::rhi::BlendMode::kAlpha};
        text_pipeline = device->CreatePipeline(text_pipeline_desc);
        if (text_pipeline == jrpgmaker::rhi::PipelineHandle::kInvalid)
            throw std::runtime_error("editor.ui.text_pipeline_creation_failed");
        if (!controller.Resize(static_cast<float>(window_width), static_cast<float>(window_height)))
            throw std::runtime_error("editor.ui.layout_invalid");
        const auto draw_list = controller.BuildDrawList();
        const auto text_draw = jrpgmaker::ui::BuildTextDrawList(
            draw_list, resources.bundle->locale, *fonts.front(), fallback_fonts, glyph_atlas,
            static_cast<std::uint32_t>(resources.bundle->theme.dimensions.at("font.body")));
        if (!text_draw.ok()) {
            for (const auto& diagnostic : text_draw.diagnostics)
                std::cerr << diagnostic.code << '\t' << diagnostic.primitive_index << '\n';
            throw std::runtime_error("editor.ui.text_draw_invalid");
        }
        const auto packet = jrpgmaker::render::BuildUiDrawPacket(
            text_draw.draw_list, resources.bundle->theme,
            {static_cast<float>(window_width), static_cast<float>(window_height)});
        if (!packet.ok())
            throw std::runtime_error("editor.ui.draw_packet_invalid");
        gpu_batch = jrpgmaker::render::UploadUiDrawPacket(*device, packet);
        const auto text_packet = jrpgmaker::render::BuildUiTextDrawPacket(
            text_draw.draw_list,
            {static_cast<float>(window_width), static_cast<float>(window_height)});
        if (!text_packet.ok())
            throw std::runtime_error("editor.ui.text_packet_invalid");
        text_gpu_batch =
            jrpgmaker::render::UploadUiTextDrawPacket(*device, text_packet, glyph_atlas);
        command_list = device->CreateCommandList();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (command_list != nullptr)
            device->DestroyCommandList(command_list);
        if (pipeline != jrpgmaker::rhi::PipelineHandle::kInvalid)
            device->DestroyPipeline(pipeline);
        if (text_pipeline != jrpgmaker::rhi::PipelineHandle::kInvalid)
            device->DestroyPipeline(text_pipeline);
        if (device != nullptr)
            jrpgmaker::render::DestroyUiGpuBatch(*device, gpu_batch);
        if (device != nullptr)
            jrpgmaker::render::DestroyUiTextGpuBatch(*device, text_gpu_batch);
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
            else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                const auto result = controller.PointerMove(event.motion.x, event.motion.y);
                ui_dirty = result.changed || ui_dirty;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                const auto result = controller.PointerDown(event.button.x, event.button.y);
                ui_dirty = result.changed || ui_dirty;
                if (result.host_request ==
                    jrpgmaker::editor::EditorHostRequest::kOpenProjectDialog) {
                    std::lock_guard lock(project_dialog.mutex);
                    if (!project_dialog.pending) {
                        project_dialog.pending = true;
                        SDL_ShowOpenFolderDialog(ProjectFolderDialogCallback, &project_dialog,
                                                 window, nullptr, false);
                    }
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                const auto key_name = std::string(SDL_GetKeyName(event.key.key));
                if (controller.menu_open() || key_name == "Alt") {
                    const auto result = controller.KeyDown(key_name);
                    ui_dirty = result.changed || ui_dirty;
                    if (result.host_request ==
                        jrpgmaker::editor::EditorHostRequest::kOpenProjectDialog) {
                        std::lock_guard lock(project_dialog.mutex);
                        if (!project_dialog.pending) {
                            project_dialog.pending = true;
                            SDL_ShowOpenFolderDialog(ProjectFolderDialogCallback, &project_dialog,
                                                     window, nullptr, false);
                        }
                    }
                    continue;
                }
                if (controller.state() != nullptr) {
                    if (key_name == "Left" || key_name == "Right" || key_name == "Backspace") {
                        if (controller.ApplyTextKey(key_name)) {
                            ui_dirty = true;
                            continue;
                        }
                    }
                }
                const auto modifiers = SDL_GetModState();
                const auto action = input_map.Translate(
                    SDL_GetKeyName(event.key.key), true, (modifiers & SDL_KMOD_CTRL) != 0,
                    (modifiers & SDL_KMOD_SHIFT) != 0, (modifiers & SDL_KMOD_ALT) != 0);
                if (!action)
                    continue;
                const auto result = controller.Dispatch(*action);
                ui_dirty = result.changed || ui_dirty;
                if (result.host_request ==
                    jrpgmaker::editor::EditorHostRequest::kOpenProjectDialog) {
                    std::lock_guard lock(project_dialog.mutex);
                    if (!project_dialog.pending) {
                        project_dialog.pending = true;
                        SDL_ShowOpenFolderDialog(ProjectFolderDialogCallback, &project_dialog,
                                                 window, nullptr, false);
                    }
                }
            } else if (event.type == SDL_EVENT_TEXT_INPUT && controller.state() != nullptr) {
                if (!controller.ApplyText(event.text.text))
                    for (const auto& diagnostic : controller.state()->diagnostics)
                        std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
                else
                    ui_dirty = true;
            } else if (event.type == SDL_EVENT_TEXT_EDITING && controller.state() != nullptr) {
                (void) controller.ApplyComposition(event.edit.text);
            } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                window_width = static_cast<std::uint32_t>(std::max(1, event.window.data1));
                window_height = static_cast<std::uint32_t>(std::max(1, event.window.data2));
                device->WaitForGpuIdle();
                swapchain->Resize(window_width, window_height);
                (void) controller.Resize(static_cast<float>(window_width),
                                         static_cast<float>(window_height));
                ui_dirty = true;
            }
        }
        std::optional<std::filesystem::path> selected_root;
        {
            std::lock_guard lock(project_dialog.mutex);
            selected_root = std::move(project_dialog.selected_root);
            project_dialog.selected_root.reset();
        }
        if (selected_root) {
            if (!controller.OpenProject(*selected_root) && controller.state() != nullptr)
                for (const auto& diagnostic : controller.state()->diagnostics)
                    std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
            ui_dirty = true;
        }
        if (controller.Poll())
            ui_dirty = true;
        if (ui_dirty) {
            device->WaitForGpuIdle();
            jrpgmaker::render::DestroyUiGpuBatch(*device, gpu_batch);
            jrpgmaker::render::DestroyUiTextGpuBatch(*device, text_gpu_batch);
            const auto draw_list = controller.BuildDrawList();
            const auto text_draw = jrpgmaker::ui::BuildTextDrawList(
                draw_list, resources.bundle->locale, *fonts.front(), fallback_fonts, glyph_atlas,
                static_cast<std::uint32_t>(resources.bundle->theme.dimensions.at("font.body")));
            if (!text_draw.ok())
                throw std::runtime_error("editor.ui.text_draw_invalid");
            const auto packet = jrpgmaker::render::BuildUiDrawPacket(
                text_draw.draw_list, resources.bundle->theme,
                {static_cast<float>(window_width), static_cast<float>(window_height)});
            if (!packet.ok())
                throw std::runtime_error("editor.ui.draw_packet_invalid");
            gpu_batch = jrpgmaker::render::UploadUiDrawPacket(*device, packet);
            const auto text_packet = jrpgmaker::render::BuildUiTextDrawPacket(
                text_draw.draw_list,
                {static_cast<float>(window_width), static_cast<float>(window_height)});
            if (!text_packet.ok())
                throw std::runtime_error("editor.ui.text_packet_invalid");
            text_gpu_batch =
                jrpgmaker::render::UploadUiTextDrawPacket(*device, text_packet, glyph_atlas);
            ui_dirty = false;
        }
        const auto target = swapchain->AcquireTexture();
        command_list->Begin();
        command_list->BeginRendering(target, ThemeClearColor(resources.bundle->theme));
        jrpgmaker::render::RecordUiDrawPacket(*command_list, pipeline, gpu_batch);
        jrpgmaker::render::RecordUiTextDrawPacket(*command_list, text_pipeline, text_gpu_batch);
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
    device->DestroyPipeline(text_pipeline);
    jrpgmaker::render::DestroyUiGpuBatch(*device, gpu_batch);
    jrpgmaker::render::DestroyUiTextGpuBatch(*device, text_gpu_batch);
    device->DestroySwapchain(swapchain);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
