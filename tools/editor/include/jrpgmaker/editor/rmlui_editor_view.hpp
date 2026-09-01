#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "jrpgmaker/editor/editor_user_settings.hpp"
#include "jrpgmaker/rhi/command_list.hpp"
#include "jrpgmaker/rhi/device.hpp"

namespace jrpgmaker::editor {

struct RmlUiInputModifiers {
    bool control = false;
    bool shift = false;
    bool alt = false;
    bool caps_lock = false;
    bool num_lock = false;
};

struct RmlUiEditorConfig {
    std::filesystem::path resource_root;
    std::vector<std::filesystem::path> font_paths;
    std::uint32_t max_geometries = 4096;
    std::uint32_t max_vertices_per_geometry = 16384;
    std::uint32_t max_indices_per_geometry = 32768;
};

struct RmlUiCommand {
    std::string name;
    std::string argument;
};

struct RmlUiInputPoint {
    int x = 0;
    int y = 0;
};

struct RmlUiElementLayout {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool visible = false;
};

// Converts SDL's logical-window coordinates into the physical-pixel
// coordinates expected by an RmlUi context with the given display scale.
[[nodiscard]] RmlUiInputPoint ToRmlUiInputPoint(float x, float y,
                                                float density_independent_pixel_ratio);

using RmlUiCommandHandler = std::function<void(const RmlUiCommand&)>;

// Editor-only RmlUi host. It owns the RmlUi context and translates its
// retained geometry into the existing engine/rhi UI pipelines. It does not
// own project data or editor selection state.
class RmlUiEditorView final {
public:
    RmlUiEditorView();
    ~RmlUiEditorView();

    RmlUiEditorView(const RmlUiEditorView&) = delete;
    RmlUiEditorView& operator=(const RmlUiEditorView&) = delete;

    [[nodiscard]] bool Initialize(rhi::IDevice& device, RmlUiEditorConfig config,
                                  RmlUiCommandHandler command_handler, std::uint32_t width,
                                  std::uint32_t height);
    [[nodiscard]] bool SetMarkup(std::string_view markup, std::string_view source_url);
    // Transfers only editor-owned session layout. The view never reads or writes preference files.
    void ImportSettings(const EditorUserSettings& settings);
    [[nodiscard]] EditorUserSettings ExportSettings() const;
    [[nodiscard]] bool TogglePanel(std::string_view panel_id);
    [[nodiscard]] bool panel_visible(std::string_view panel_id) const;
    void ImportSplitterRatios(const EditorUserSettings& settings);
    [[nodiscard]] EditorUserSettings ExportSplitterRatios() const;
    [[nodiscard]] bool Resize(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] bool SetDensityIndependentPixelRatio(float ratio);
    // Session-only focus/maximize state; it is deliberately excluded from user settings.
    [[nodiscard]] std::string_view focused_panel() const;
    [[nodiscard]] std::string_view maximized_panel() const;
    [[nodiscard]] bool panel_maximized() const;
    [[nodiscard]] bool hierarchy_expanded() const;
    [[nodiscard]] bool ToggleHierarchy();
    [[nodiscard]] bool ToggleFocusedPanelMaximize();
    [[nodiscard]] std::optional<RmlUiElementLayout> panel_layout(std::string_view id) const;
    [[nodiscard]] bool Update();
    [[nodiscard]] bool Render();
    [[nodiscard]] bool Record(rhi::ICommandList& command_list, rhi::PipelineHandle solid_pipeline,
                              rhi::PipelineHandle textured_pipeline);

    [[nodiscard]] bool ProcessMouseMove(float x, float y, const RmlUiInputModifiers& modifiers);
    [[nodiscard]] bool ProcessMouseButtonDown(float x, float y, int button,
                                              const RmlUiInputModifiers& modifiers);
    [[nodiscard]] bool ProcessMouseButtonDown(int button, const RmlUiInputModifiers& modifiers);
    [[nodiscard]] bool ProcessMouseButtonUp(int button, const RmlUiInputModifiers& modifiers);
    [[nodiscard]] bool ProcessMouseWheel(float x, float y, const RmlUiInputModifiers& modifiers);
    [[nodiscard]] bool ProcessMouseLeave();
    [[nodiscard]] bool ProcessKeyDown(std::string_view key, const RmlUiInputModifiers& modifiers);
    [[nodiscard]] bool ProcessKeyUp(std::string_view key, const RmlUiInputModifiers& modifiers);
    [[nodiscard]] bool ProcessTextInput(std::string_view text);
    [[nodiscard]] bool ProcessTextEditing(std::string_view text, int start, int length);

    [[nodiscard]] bool ready() const;
    // Returns the current session-only ratio for a stable splitter id, or zero
    // when the id is not one of the editor workspace splitters.
    [[nodiscard]] float splitter_ratio(std::string_view id) const;
    [[nodiscard]] const std::vector<std::string>& diagnostics() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace jrpgmaker::editor
