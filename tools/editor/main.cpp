#include <filesystem>
#include <iostream>

#include <SDL3/SDL.h>

#include "jrpgmaker/editor/editor_session.hpp"
#include "jrpgmaker/editor/editor_shell.hpp"
#include "jrpgmaker/project/workspace.hpp"
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

const jrpgmaker::editor::ShellNode*
FindShellNode(const jrpgmaker::editor::ShellProjection& projection, std::string_view id) {
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

    const auto resources =
        jrpgmaker::ui::LoadEditorResources(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_ROOT));
    if (!resources) {
        PrintStartupDiagnostics(resources.diagnostics);
        return 1;
    }
    const auto shell = jrpgmaker::editor::BuildShellProjection(resources.bundle->layout);
    if (!shell)
        return 1;
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
            .vertex_shader = {
#if defined(_WIN32)
                jrpgmaker::shaders::kUiTextVsDxil, jrpgmaker::shaders::kUiTextVsDxil_size
#else
                jrpgmaker::shaders::kUiTextVsSpv, jrpgmaker::shaders::kUiTextVsSpv_size
#endif
            },
            .pixel_shader = {
#if defined(_WIN32)
                jrpgmaker::shaders::kUiTextPsDxil, jrpgmaker::shaders::kUiTextPsDxil_size
#else
                jrpgmaker::shaders::kUiTextPsSpv, jrpgmaker::shaders::kUiTextPsSpv_size
#endif
            },
            .color_format = jrpgmaker::rhi::Format::kB8G8R8A8Unorm,
            .vertex_input = {text_attributes, 3,
                             sizeof(jrpgmaker::render::UiTextVertex)},
            .sample_slot = 1};
        text_pipeline = device->CreatePipeline(text_pipeline_desc);
        if (text_pipeline == jrpgmaker::rhi::PipelineHandle::kInvalid)
            throw std::runtime_error("editor.ui.text_pipeline_creation_failed");
        auto draw_list = jrpgmaker::editor::BuildShellDrawList(*shell);
        if (session != nullptr) {
            const auto* tabs_node = FindShellNode(*shell, "workspace.tabs");
            if (tabs_node != nullptr) {
                const auto tabs_draw_list = jrpgmaker::editor::BuildDocumentTabsDrawList(
                    session->state().tabs, tabs_node->bounds);
                for (const auto& primitive : tabs_draw_list.primitives())
                    (void) draw_list.Add(primitive);
            }
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
            const auto* diagnostics_node = FindShellNode(*shell, "workspace.diagnostics");
            if (session != nullptr && diagnostics_node != nullptr) {
                const auto preview_row_height =
                    resources.bundle->theme.dimensions.at("font.caption") +
                    resources.bundle->theme.dimensions.at("space.xs");
                const auto preview_draw_list = jrpgmaker::editor::BuildPreviewDrawList(
                    session->state().preview, diagnostics_node->bounds, preview_row_height);
                for (const auto& primitive : preview_draw_list.primitives())
                    (void) draw_list.Add(primitive);
            }
            const auto* status_node = FindShellNode(*shell, "workspace.status");
            if (status_node != nullptr) {
                const auto status_draw_list = jrpgmaker::editor::BuildStatusBarDrawList(
                    {session != nullptr && session->state().open,
                     session != nullptr && session->state().dirty,
                     session != nullptr ? session->state().revision : 0},
                    status_node->bounds, status_node->recipe);
                for (const auto& primitive : status_draw_list.primitives())
                    (void) draw_list.Add(primitive);
            }
        }
        const auto packet = jrpgmaker::render::BuildUiDrawPacket(draw_list, resources.bundle->theme,
                                                                 {1280.0f, 720.0f});
        if (!packet.ok())
            throw std::runtime_error("editor.ui.draw_packet_invalid");
        gpu_batch = jrpgmaker::render::UploadUiDrawPacket(*device, packet);
        const auto text_draw = jrpgmaker::ui::BuildTextDrawList(
                draw_list, resources.bundle->locale, *fonts.front(), fallback_fonts, glyph_atlas,
                static_cast<std::uint32_t>(resources.bundle->theme.dimensions.at("font.body")));
        if (!text_draw.ok()) {
            for (const auto& diagnostic : text_draw.diagnostics)
                std::cerr << diagnostic.code << '\t' << diagnostic.primitive_index << '\n';
            throw std::runtime_error("editor.ui.text_draw_invalid");
        }
        const auto text_packet = jrpgmaker::render::BuildUiTextDrawPacket(
            text_draw.draw_list, {1280.0f, 720.0f});
        if (!text_packet.ok())
            throw std::runtime_error("editor.ui.text_packet_invalid");
        text_gpu_batch = jrpgmaker::render::UploadUiTextDrawPacket(*device, text_packet,
                                                                    glyph_atlas);
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
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && session != nullptr) {
                const auto* tabs_node = FindShellNode(*shell, "workspace.tabs");
                if (tabs_node != nullptr && !session->state().tabs.tabs.empty() &&
                    event.button.x >= tabs_node->bounds.x &&
                    event.button.x < tabs_node->bounds.x + tabs_node->bounds.width &&
                    event.button.y >= tabs_node->bounds.y &&
                    event.button.y < tabs_node->bounds.y + tabs_node->bounds.height) {
                    const auto tab_width = tabs_node->bounds.width /
                                           static_cast<float>(session->state().tabs.tabs.size());
                    const auto index = static_cast<std::size_t>(
                        (event.button.x - tabs_node->bounds.x) / tab_width);
                    if (index < session->state().tabs.tabs.size())
                        ui_dirty = session->SelectDocument(
                                       session->state().tabs.tabs[index].document_id) || ui_dirty;
                    continue;
                }
                const auto* diagnostics_node = FindShellNode(*shell, "workspace.diagnostics");
                if (diagnostics_node != nullptr &&
                    event.button.x >= diagnostics_node->bounds.x &&
                    event.button.x < diagnostics_node->bounds.x + diagnostics_node->bounds.width &&
                    event.button.y >= diagnostics_node->bounds.y &&
                    event.button.y < diagnostics_node->bounds.y + diagnostics_node->bounds.height) {
                    const auto row_height = resources.bundle->theme.dimensions.at("font.caption") +
                                            resources.bundle->theme.dimensions.at("space.xs");
                    if (row_height > 0.0f && event.button.y >= diagnostics_node->bounds.y) {
                        const auto index = static_cast<std::size_t>(
                            (event.button.y - diagnostics_node->bounds.y) / row_height);
                        ui_dirty = session->LocateDiagnostic(index) || ui_dirty;
                    }
                    continue;
                }
                const auto* form_node = FindShellNode(*shell, "workspace.form");
                const auto row_height = resources.bundle->theme.dimensions.at("font.body") +
                                        resources.bundle->theme.dimensions.at("space.sm");
                if (form_node != nullptr && row_height > 0.0f &&
                    event.button.x >= form_node->bounds.x &&
                    event.button.x < form_node->bounds.x + form_node->bounds.width &&
                    event.button.y >= form_node->bounds.y &&
                    event.button.y < form_node->bounds.y + form_node->bounds.height) {
                    const auto index = static_cast<std::size_t>(
                        (event.button.y - form_node->bounds.y) / row_height);
                    ui_dirty = session->SelectField(index) || ui_dirty;
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                if (session != nullptr) {
                    const auto key_name = std::string(SDL_GetKeyName(event.key.key));
                    if (key_name == "Left" || key_name == "Right" || key_name == "Backspace") {
                        // Text editing owns the key only when the focused field accepts it.
                        // Unsupported keys must continue through the data-driven action map;
                        // otherwise select fields can never receive choice_next/previous.
                        if (session->ApplySelectedKey(key_name)) {
                            ui_dirty = true;
                            continue;
                        }
                    }
                }
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
                } else if (*action == jrpgmaker::editor::EditorAction::kConfirm) {
                    ui_dirty = session->ApplySelectedKey("Enter") || ui_dirty;
                } else if (*action == jrpgmaker::editor::EditorAction::kCancel) {
                    ui_dirty = session->ApplySelectedKey("Escape") || ui_dirty;
                } else if (*action == jrpgmaker::editor::EditorAction::kIncrement) {
                    ui_dirty = session->AdjustSelectedInteger(1) || ui_dirty;
                } else if (*action == jrpgmaker::editor::EditorAction::kDecrement) {
                    ui_dirty = session->AdjustSelectedInteger(-1) || ui_dirty;
                } else if (*action == jrpgmaker::editor::EditorAction::kToggle) {
                    ui_dirty = session->ToggleSelectedBoolean() || ui_dirty;
                } else if (*action == jrpgmaker::editor::EditorAction::kChoiceNext) {
                    ui_dirty = session->CycleSelectedChoice(1) || ui_dirty;
                } else if (*action == jrpgmaker::editor::EditorAction::kChoicePrevious) {
                    ui_dirty = session->CycleSelectedChoice(-1) || ui_dirty;
                }
            } else if (event.type == SDL_EVENT_TEXT_INPUT && session != nullptr) {
                if (!session->ApplySelectedText(event.text.text))
                    for (const auto& diagnostic : session->state().diagnostics)
                        std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
                else
                    ui_dirty = true;
            } else if (event.type == SDL_EVENT_TEXT_EDITING && session != nullptr) {
                (void) session->ApplySelectedComposition(event.edit.text);
            }
        }
        if (session != nullptr)
            if (session->PollPreview())
                ui_dirty = true;
        if (ui_dirty) {
            device->WaitForGpuIdle();
            jrpgmaker::render::DestroyUiGpuBatch(*device, gpu_batch);
            auto draw_list = jrpgmaker::editor::BuildShellDrawList(*shell);
            const auto* tabs_node = FindShellNode(*shell, "workspace.tabs");
            if (session != nullptr && tabs_node != nullptr) {
                const auto tabs_draw_list = jrpgmaker::editor::BuildDocumentTabsDrawList(
                    session->state().tabs, tabs_node->bounds);
                for (const auto& primitive : tabs_draw_list.primitives())
                    (void) draw_list.Add(primitive);
            }
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
            const auto* diagnostics_node = FindShellNode(*shell, "workspace.diagnostics");
            if (session != nullptr && diagnostics_node != nullptr) {
                const auto preview_row_height =
                    resources.bundle->theme.dimensions.at("font.caption") +
                    resources.bundle->theme.dimensions.at("space.xs");
                const auto preview_draw_list = jrpgmaker::editor::BuildPreviewDrawList(
                    session->state().preview, diagnostics_node->bounds, preview_row_height);
                for (const auto& primitive : preview_draw_list.primitives())
                    (void) draw_list.Add(primitive);
            }
            const auto* status_node = FindShellNode(*shell, "workspace.status");
            if (status_node != nullptr) {
                const auto status_draw_list = jrpgmaker::editor::BuildStatusBarDrawList(
                    {session != nullptr && session->state().open,
                     session != nullptr && session->state().dirty,
                     session != nullptr ? session->state().revision : 0},
                    status_node->bounds, status_node->recipe);
                for (const auto& primitive : status_draw_list.primitives())
                    (void) draw_list.Add(primitive);
            }
            const auto packet = jrpgmaker::render::BuildUiDrawPacket(
                draw_list, resources.bundle->theme, {1280.0f, 720.0f});
            if (!packet.ok())
                throw std::runtime_error("editor.ui.draw_packet_invalid");
            gpu_batch = jrpgmaker::render::UploadUiDrawPacket(*device, packet);
            jrpgmaker::render::DestroyUiTextGpuBatch(*device, text_gpu_batch);
            const auto text_draw = jrpgmaker::ui::BuildTextDrawList(
                draw_list, resources.bundle->locale, *fonts.front(), fallback_fonts, glyph_atlas,
                static_cast<std::uint32_t>(resources.bundle->theme.dimensions.at("font.body")));
            if (!text_draw.ok())
                throw std::runtime_error("editor.ui.text_draw_invalid");
            const auto text_packet = jrpgmaker::render::BuildUiTextDrawPacket(
                text_draw.draw_list, {1280.0f, 720.0f});
            if (!text_packet.ok())
                throw std::runtime_error("editor.ui.text_packet_invalid");
            text_gpu_batch = jrpgmaker::render::UploadUiTextDrawPacket(*device, text_packet,
                                                                        glyph_atlas);
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
