#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include "jrpgmaker/editor/editor_session.hpp"
#include "jrpgmaker/editor/editor_shell.hpp"
#include "jrpgmaker/project/plugin_registry.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"

namespace jrpgmaker::editor {

enum class EditorHostRequest : std::uint8_t { kNone, kOpenProjectDialog };

struct EditorInteractionResult {
    bool changed = false;
    EditorHostRequest host_request = EditorHostRequest::kNone;
};

struct EditorWorkspaceConfig {
    ui::EditorLayout layout;
    float form_row_height = 24.0f;
    float diagnostic_row_height = 16.0f;
    ui::MenuLayoutConfig menu;
    std::filesystem::path runtime_executable;
    std::vector<plugin::CompiledPluginFactory> plugin_factories;
};

class EditorWorkspaceController final {
public:
    explicit EditorWorkspaceController(EditorWorkspaceConfig config);

    [[nodiscard]] bool Resize(float width, float height);
    [[nodiscard]] bool OpenProject(const std::filesystem::path& root);
    [[nodiscard]] EditorInteractionResult PointerMove(float x, float y);
    [[nodiscard]] EditorInteractionResult PointerDown(float x, float y);
    [[nodiscard]] EditorInteractionResult KeyDown(std::string_view key);
    [[nodiscard]] EditorInteractionResult Dispatch(EditorAction action);
    [[nodiscard]] EditorInteractionResult DispatchCommand(std::string_view command,
                                                          std::string_view argument = {});
    [[nodiscard]] bool SetProjectFilter(std::string_view filter);
    [[nodiscard]] bool ApplyText(std::string_view text);
    [[nodiscard]] bool ApplyComposition(std::string_view text);
    [[nodiscard]] bool ApplyTextKey(std::string_view key);
    [[nodiscard]] bool Poll();

    [[nodiscard]] ui::DrawList BuildDrawList() const;
    [[nodiscard]] const EditorSessionState* state() const;
    [[nodiscard]] std::optional<ui::Rect> panel_bounds(std::string_view id) const;
    [[nodiscard]] std::string_view project_filter() const { return project_filter_; }
    [[nodiscard]] bool menu_open() const { return menu_.open(); }

private:
    [[nodiscard]] const ShellNode* FindPanel(std::string_view id) const;
    void RebuildMenuModel();
    [[nodiscard]] EditorInteractionResult DispatchMenuCommand(std::string_view command);

    EditorWorkspaceConfig config_;
    std::optional<ShellProjection> shell_;
    ui::MenuController menu_;
    std::string focused_panel_ = "workspace.scene";
    ui::TextFieldState project_filter_text_;
    std::string project_filter_;
    bool project_filter_focused_ = false;
    std::optional<EditorAction> hovered_toolbar_;
    std::optional<EditorAction> pressed_toolbar_;
    std::optional<EditorAction> focused_toolbar_;
    std::shared_ptr<const plugin::PluginRegistry> plugin_registry_;
    std::unique_ptr<EditorSession> session_;
};

} // namespace jrpgmaker::editor
