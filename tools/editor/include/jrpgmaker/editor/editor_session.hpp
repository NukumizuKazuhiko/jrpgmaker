#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "jrpgmaker/editor/form_projection.hpp"
#include "jrpgmaker/editor/preview_process.hpp"
#include "jrpgmaker/project/workspace.hpp"
#include "jrpgmaker/ui/interaction.hpp"

namespace jrpgmaker::editor {

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
};

class EditorSession final {
public:
    explicit EditorSession(std::filesystem::path root);
    EditorSession(std::filesystem::path root, project::DocumentAdapterRegistry adapters);

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

    [[nodiscard]] const EditorSessionState& state() const { return state_; }

private:
    void SetDiagnostics(std::vector<project::Diagnostic> diagnostics);
    void RebuildProjection();
    void PublishTextFieldState();
    void SyncTextField(bool select_all);

    project::DocumentAdapterRegistry adapters_ = project::CreateDefaultDocumentAdapters();
    std::filesystem::path root_;
    project::ProjectWorkspace workspace_;
    ui::UiContext focus_context_;
    ui::TextFieldState text_field_;
    PreviewProcess preview_process_;
    std::optional<project::ProjectSnapshot> snapshot_;
    EditorSessionState state_;
};

} // namespace jrpgmaker::editor
