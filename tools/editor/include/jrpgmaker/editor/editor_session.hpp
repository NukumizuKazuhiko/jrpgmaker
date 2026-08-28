#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

#include "jrpgmaker/editor/form_projection.hpp"
#include "jrpgmaker/project/workspace.hpp"

namespace jrpgmaker::editor {

struct EditorSessionState {
    bool open = false;
    bool dirty = false;
    std::size_t selected_field = 0;
    std::uint64_t revision = 0;
    FormProjection form;
    PreviewProjection preview;
    std::vector<project::Diagnostic> diagnostics;
};

class EditorSession final {
public:
    explicit EditorSession(std::filesystem::path root);

    [[nodiscard]] bool Open();
    [[nodiscard]] bool Refresh();
    [[nodiscard]] bool SelectNext();
    [[nodiscard]] bool SelectPrevious();
    [[nodiscard]] bool ApplySelected(nlohmann::json value);
    [[nodiscard]] bool ApplySelectedText(std::string_view value);
    [[nodiscard]] bool Save();

    [[nodiscard]] const EditorSessionState& state() const { return state_; }

private:
    void SetDiagnostics(std::vector<project::Diagnostic> diagnostics);
    void RebuildProjection();

    project::DocumentAdapterRegistry adapters_ = project::CreateDefaultDocumentAdapters();
    project::ProjectWorkspace workspace_;
    std::optional<project::ProjectSnapshot> snapshot_;
    EditorSessionState state_;
};

} // namespace jrpgmaker::editor
