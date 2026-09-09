#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "jrpgmaker/editor/editor_user_settings.hpp"
#include "jrpgmaker/editor/editor_window.hpp"
#include "jrpgmaker/editor/editor_workspace_controller.hpp"
#include "jrpgmaker/editor/rmlui_editor_document.hpp"
#include "jrpgmaker/editor/rmlui_editor_view.hpp"
#include "jrpgmaker/plugins/register.hpp"
#include "jrpgmaker/render/ui_draw_adapter.hpp"
#include "jrpgmaker/rhi/device_factory.hpp"
#include "jrpgmaker/rhi/swapchain.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"
#include "shaders_generated.hpp"
#include "ui_text_generated.hpp"

namespace {

void PrintStartupDiagnostics(
    const std::vector<jrpgmaker::ui::EditorStartupDiagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics)
        std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
}

void PrintProjectDiagnostics(const jrpgmaker::editor::EditorSessionState* state) {
    if (state == nullptr)
        return;
    for (const auto& diagnostic : state->diagnostics)
        std::cerr << diagnostic.code << '\t' << diagnostic.path << '\n';
}

void PrintUserSettingsDiagnostics(
    const std::vector<jrpgmaker::editor::EditorUserSettingsDiagnostic>& diagnostics) {
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

jrpgmaker::editor::RmlUiInputModifiers CurrentModifiers() {
    const auto modifiers = SDL_GetModState();
    return {.control = (modifiers & SDL_KMOD_CTRL) != 0,
            .shift = (modifiers & SDL_KMOD_SHIFT) != 0,
            .alt = (modifiers & SDL_KMOD_ALT) != 0,
            .caps_lock = (modifiers & SDL_KMOD_CAPS) != 0,
            .num_lock = (modifiers & SDL_KMOD_NUM) != 0};
}

std::string RmlSourceUrl(const std::filesystem::path& path) {
    auto result = path.generic_string();
    std::replace(result.begin(), result.end(), ':', '|');
    return result;
}

float WindowDisplayScale(SDL_Window* window) {
    const float scale = SDL_GetWindowDisplayScale(window);
    return scale > 0.0f && std::isfinite(scale) ? scale : 1.0f;
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
         .runtime_executable = JRPGMAKER_RUNTIME_EXECUTABLE,
         .plugin_factories = jrpgmaker::plugins::CompiledSamplePlugins()});
    if ((argc == 2 && !smoke) || argc == 3) {
        const auto project_argument = std::filesystem::path(argv[1]);
        if (!controller.OpenProject(project_argument))
            PrintProjectDiagnostics(controller.state());
    }

    const auto title = resources.bundle->locale.strings.at("editor.window.title");
    if (!SDL_Init(SDL_INIT_VIDEO))
        return 1;

    std::optional<std::filesystem::path> user_settings_path;
    if (char* preference_path = SDL_GetPrefPath("jrpgmaker", "jrpgmaker-editor");
        preference_path != nullptr) {
        user_settings_path = std::filesystem::path(preference_path) / "editor_user_settings.json";
        SDL_free(preference_path);
    } else {
        std::cerr << "editor.settings.preference_path_unavailable\n";
    }
    jrpgmaker::editor::EditorUserSettings user_settings;
    if (user_settings_path) {
        const auto loaded = jrpgmaker::editor::LoadEditorUserSettings(*user_settings_path);
        user_settings = loaded.settings;
        PrintUserSettingsDiagnostics(loaded.diagnostics);
    }

    const SDL_WindowFlags window_flags =
        static_cast<SDL_WindowFlags>(jrpgmaker::editor::EditorWindowFlags(
#if defined(_WIN32)
            false
#else
            true
#endif
            ));
    SDL_Window* window = SDL_CreateWindow(title.c_str(), 1280, 720, window_flags);
    if (window == nullptr) {
        SDL_Quit();
        return 1;
    }
    int pixel_width = 0;
    int pixel_height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height) || pixel_width <= 0 ||
        pixel_height <= 0) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::uint32_t window_width = static_cast<std::uint32_t>(pixel_width);
    std::uint32_t window_height = static_cast<std::uint32_t>(pixel_height);
    float display_scale = WindowDisplayScale(window);
    SDL_StartTextInput(window);

    ProjectDialogState project_dialog;
    std::unique_ptr<jrpgmaker::rhi::IDevice> device;
    jrpgmaker::rhi::ISwapchain* swapchain = nullptr;
    jrpgmaker::rhi::ICommandList* command_list = nullptr;
    jrpgmaker::rhi::PipelineHandle pipeline = jrpgmaker::rhi::PipelineHandle::kInvalid;
    jrpgmaker::rhi::PipelineHandle text_pipeline = jrpgmaker::rhi::PipelineHandle::kInvalid;
    jrpgmaker::editor::RmlUiEditorView view;
    bool ui_dirty = true;
    auto persisted_user_settings = user_settings;
    const auto persist_user_settings = [&] {
        if (!user_settings_path)
            return;
        const auto current = view.ExportSplitterRatios();
        if (current == persisted_user_settings)
            return;
        const auto saved = jrpgmaker::editor::SaveEditorUserSettings(*user_settings_path, current);
        PrintUserSettingsDiagnostics(saved.diagnostics);
        if (saved.ok)
            persisted_user_settings = current;
    };

    const auto open_project_dialog = [&] {
        std::lock_guard lock(project_dialog.mutex);
        if (!project_dialog.pending) {
            project_dialog.pending = true;
            SDL_ShowOpenFolderDialog(ProjectFolderDialogCallback, &project_dialog, window, nullptr,
                                     false);
        }
    };
    const auto rebuild_markup = [&] {
        return jrpgmaker::editor::BuildRmlUiEditorDocument(
            controller.state(), resources.bundle->locale, "editor_workspace.rcss",
            controller.project_filter(), view.ExportSettings(), view.focused_panel(),
            view.panel_maximized());
    };

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
        pipeline = device->CreatePipeline(
            {.vertex_shader =
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
             .vertex_input = {attributes, 2, sizeof(jrpgmaker::render::UiVertex)},
             .blend_mode = jrpgmaker::rhi::BlendMode::kAlpha});
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
        text_pipeline = device->CreatePipeline(
            {.vertex_shader =
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
             .blend_mode = jrpgmaker::rhi::BlendMode::kAlpha});
        if (pipeline == jrpgmaker::rhi::PipelineHandle::kInvalid ||
            text_pipeline == jrpgmaker::rhi::PipelineHandle::kInvalid)
            throw std::runtime_error("editor.rmlui.pipeline_creation_failed");
        if (!controller.Resize(static_cast<float>(window_width), static_cast<float>(window_height)))
            throw std::runtime_error("editor.ui.layout_invalid");
        if (!view.Initialize(
                *device,
                {.resource_root = JRPGMAKER_EDITOR_RESOURCE_ROOT, .font_paths = font_paths},
                [&](const jrpgmaker::editor::RmlUiCommand& command) {
                    if (command.name == "layout.reset_default") {
                        view.ImportSplitterRatios(jrpgmaker::editor::EditorUserSettings{});
                        ui_dirty = true;
                        persist_user_settings();
                        return;
                    }
                    if (command.name == "panel.toggle") {
                        if (view.TogglePanel(command.argument)) {
                            ui_dirty = true;
                            persist_user_settings();
                        }
                        return;
                    }
                    if (command.name == "dock.activate") {
                        persist_user_settings();
                        return;
                    }
                    if (command.name == "window.toggle_maximize") {
                        if (view.ToggleFocusedPanelMaximize())
                            ui_dirty = true;
                        return;
                    }
                    const auto result = controller.DispatchCommand(command.name, command.argument);
                    ui_dirty = ui_dirty || result.changed;
                    if (result.host_request ==
                        jrpgmaker::editor::EditorHostRequest::kOpenProjectDialog)
                        open_project_dialog();
                },
                window_width, window_height))
            throw std::runtime_error("editor.rmlui.initialise_failed");
        view.ImportSplitterRatios(user_settings);
        if (!view.SetDensityIndependentPixelRatio(display_scale))
            throw std::runtime_error("editor.rmlui.density_ratio_invalid");
        if (!view.SetMarkup(rebuild_markup(),
                            RmlSourceUrl(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_ROOT) /
                                         "rml/editor_workspace.rml")))
            throw std::runtime_error("editor.rmlui.document_load_failed");
        command_list = device->CreateCommandList();
        if (command_list == nullptr)
            throw std::runtime_error("editor.rhi.command_list_creation_failed");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (command_list != nullptr)
            device->DestroyCommandList(command_list);
        if (pipeline != jrpgmaker::rhi::PipelineHandle::kInvalid)
            device->DestroyPipeline(pipeline);
        if (text_pipeline != jrpgmaker::rhi::PipelineHandle::kInvalid)
            device->DestroyPipeline(text_pipeline);
        if (swapchain != nullptr)
            device->DestroySwapchain(swapchain);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    persisted_user_settings = view.ExportSplitterRatios();
    SDL_Event event{};
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                (void) view.ProcessMouseMove(event.motion.x, event.motion.y, CurrentModifiers());
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                ui_dirty = view.ProcessMouseButtonDown(event.button.x, event.button.y,
                                                       static_cast<int>(event.button.button - 1),
                                                       CurrentModifiers()) ||
                           ui_dirty;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                (void) view.ProcessMouseButtonUp(static_cast<int>(event.button.button - 1),
                                                 CurrentModifiers());
                persist_user_settings();
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                (void) view.ProcessMouseWheel(event.wheel.x, event.wheel.y, CurrentModifiers());
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                const auto key_name = std::string(SDL_GetKeyName(event.key.key));
                const bool panel_was_maximized = view.panel_maximized();
                if (view.ProcessKeyDown(key_name, CurrentModifiers())) {
                    const auto modifiers = SDL_GetModState();
                    const auto action = input_map.Translate(
                        key_name, true, (modifiers & SDL_KMOD_CTRL) != 0,
                        (modifiers & SDL_KMOD_SHIFT) != 0, (modifiers & SDL_KMOD_ALT) != 0);
                    if (action) {
                        const auto result = controller.Dispatch(*action);
                        ui_dirty = ui_dirty || result.changed;
                        if (result.host_request ==
                            jrpgmaker::editor::EditorHostRequest::kOpenProjectDialog)
                            open_project_dialog();
                    }
                }
                ui_dirty = ui_dirty || panel_was_maximized != view.panel_maximized();
            } else if (event.type == SDL_EVENT_KEY_UP) {
                (void) view.ProcessKeyUp(std::string(SDL_GetKeyName(event.key.key)),
                                         CurrentModifiers());
            } else if (event.type == SDL_EVENT_TEXT_INPUT) {
                if (view.ProcessTextInput(event.text.text) && controller.state() != nullptr) {
                    if (controller.ApplyText(event.text.text))
                        ui_dirty = true;
                }
            } else if (event.type == SDL_EVENT_TEXT_EDITING) {
                (void) view.ProcessTextEditing(event.edit.text, event.edit.start,
                                               event.edit.length);
            } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                window_width = static_cast<std::uint32_t>(std::max(1, event.window.data1));
                window_height = static_cast<std::uint32_t>(std::max(1, event.window.data2));
                display_scale = WindowDisplayScale(window);
                device->WaitForGpuIdle();
                swapchain->Resize(window_width, window_height);
                if (!controller.Resize(static_cast<float>(window_width),
                                       static_cast<float>(window_height))) {
                    std::cerr << "editor.ui.layout_invalid\n";
                    running = false;
                } else {
                    (void) view.Resize(window_width, window_height);
                    (void) view.SetDensityIndependentPixelRatio(display_scale);
                }
            } else if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                display_scale = WindowDisplayScale(window);
                (void) view.SetDensityIndependentPixelRatio(display_scale);
            }
        }

        std::optional<std::filesystem::path> selected_root;
        {
            std::lock_guard lock(project_dialog.mutex);
            selected_root = std::move(project_dialog.selected_root);
            project_dialog.selected_root.reset();
        }
        if (selected_root) {
            if (!controller.OpenProject(*selected_root))
                PrintProjectDiagnostics(controller.state());
            ui_dirty = true;
        }
        if (controller.Poll())
            ui_dirty = true;
        if (ui_dirty) {
            if (!view.SetMarkup(rebuild_markup(),
                                RmlSourceUrl(std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_ROOT) /
                                             "rml/editor_workspace.rml")))
                std::cerr << "editor.rmlui.document_refresh_failed\n";
            ui_dirty = false;
        }

        device->WaitForGpuIdle();
        (void) view.Update();
        (void) view.Render();
        const auto target = swapchain->AcquireTexture();
        command_list->Begin();
        command_list->BeginRendering(target, ThemeClearColor(resources.bundle->theme));
        if (!view.Record(*command_list, pipeline, text_pipeline))
            std::cerr << "editor.rmlui.record_failed\n";
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
    persist_user_settings();
    device->DestroyCommandList(command_list);
    device->DestroyPipeline(pipeline);
    device->DestroyPipeline(text_pipeline);
    device->DestroySwapchain(swapchain);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
