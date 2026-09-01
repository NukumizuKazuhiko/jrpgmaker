#pragma once

#include <string>
#include <string_view>

#include "jrpgmaker/editor/editor_session.hpp"
#include "jrpgmaker/editor/editor_user_settings.hpp"
#include "jrpgmaker/ui/editor_resources.hpp"

namespace jrpgmaker::editor {

[[nodiscard]] std::string
BuildRmlUiEditorDocument(const EditorSessionState* state, const ui::EditorLocale& locale,
                         std::string_view stylesheet_url, std::string_view project_filter = {},
                         const EditorUserSettings& settings = {},
                         std::string_view focused_panel = {}, bool panel_maximized = false);

} // namespace jrpgmaker::editor
