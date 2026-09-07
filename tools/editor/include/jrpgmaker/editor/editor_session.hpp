#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "jrpgmaker/editor/editor_shell.hpp"
#include "jrpgmaker/editor/form_projection.hpp"
#include "jrpgmaker/editor/preview_process.hpp"
#include "jrpgmaker/project/workspace.hpp"
#include "jrpgmaker/ui/interaction.hpp"

namespace jrpgmaker::editor {

struct SelectionTarget {
    std::string document_id;
    std::string object_path;
    std::string kind;
    friend bool operator==(const SelectionTarget&, const SelectionTarget&) = default;
};

struct EditorSessionState {
    bool open = false;
    bool dirty = false;
    std::size_t selected_field = 0;
    std::uint64_t revision = 0;
    FormProjection form;
    PreviewProjection preview;
    DocumentTabsProjection tabs;
    DiagnosticPanelProjection diagnostic_panel;
    DiffProjection diff;
    std::vector<project::Diagnostic> diagnostics;
    std::size_t text_selection_start = 0;
    std::size_t text_selection_end = 0;
    std::size_t text_caret = 0;
    bool text_composing = false;
    std::optional<NavigationProjection> navigation;
    std::optional<SelectionTarget> selection;
};

class EditorSession final {
public:
    explicit EditorSession(std::filesystem::path root);
    EditorSession(std::filesystem::path root, project::DocumentAdapterRegistry adapters,
                  const plugin::PluginRegistry* plugins = nullptr);

    [[nodiscard]] bool Open();
    [[nodiscard]] bool Open(std::filesystem::path root);
    [[nodiscard]] bool Refresh();
    [[nodiscard]] bool SelectDocument(std::string_view document_id);
    [[nodiscard]] bool SetDiagnosticFilter(std::string_view filter);
    [[nodiscard]] bool LocateDiagnostic(std::size_t index);
    [[nodiscard]] bool SelectNext();
    [[nodiscard]] bool SelectPrevious();
    [[nodiscard]] bool SelectField(std::size_t index);
    [[nodiscard]] bool ApplySelected(nlohmann::json value);
    [[nodiscard]] bool ApplySelectedText(std::string_view value);
    [[nodiscard]] bool ApplySelectedComposition(std::string_view value);
    [[nodiscard]] bool ApplySelectedKey(std::string_view key);
    [[nodiscard]] bool AdjustSelectedInteger(int delta);
    [[nodiscard]] bool ToggleSelectedBoolean();
    [[nodiscard]] bool CycleSelectedChoice(int direction);
    [[nodiscard]] bool Save();
    [[nodiscard]] bool StartPreview(const std::filesystem::path& executable);
    [[nodiscard]] bool PollPreview();
    [[nodiscard]] bool SelectNavigationCell(std::size_t index);
    [[nodiscard]] bool ToggleSelectedNavigationWalkable();
    void StopPreview();

    [[nodiscard]] const EditorSessionState& state() const { return state_; }

private:
    void SetDiagnostics(std::vector<project::Diagnostic> diagnostics);
    void RebuildDiagnostics();
    void RebuildProjection();
    void PublishTextFieldState();
    void SyncTextField(bool select_all);
    [[nodiscard]] std::vector<project::Diagnostic> LoadPluginEditorAdapters();

    project::DocumentAdapterRegistry adapters_ = project::CreateDefaultDocumentAdapters();
    const plugin::PluginRegistry* plugins_ = nullptr;
    std::filesystem::path root_;
    project::ProjectWorkspace workspace_;
    ui::UiContext focus_context_;
    ui::TextFieldState text_field_;
    PreviewProcess preview_process_;
    std::optional<project::ProjectSnapshot> snapshot_;
    EditorSessionState state_;
    std::vector<project::DocumentDescriptor> external_documents_;
    std::vector<project::Diagnostic> startup_plugin_diagnostics_;
    std::vector<project::Diagnostic> operation_diagnostics_;
};

} // namespace jrpgmaker::editor
