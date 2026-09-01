#include "jrpgmaker/editor/rmlui_editor_document.hpp"

#include <algorithm>
#include <format>
#include <initializer_list>
#include <string>
#include <utility>

#include "jrpgmaker/editor/editor_user_settings.hpp"

namespace jrpgmaker::editor {
namespace {

std::string Text(const ui::EditorLocale& locale, std::string_view key) {
    const auto it = locale.strings.find(std::string(key));
    return it == locale.strings.end() ? std::string(key) : it->second;
}

std::string
FormatText(const ui::EditorLocale& locale, std::string_view key,
           std::initializer_list<std::pair<std::string_view, std::string_view>> arguments) {
    std::string result = Text(locale, key);
    for (const auto& [name, value] : arguments) {
        const std::string placeholder = std::format("{{{}}}", name);
        std::size_t offset = 0;
        while ((offset = result.find(placeholder, offset)) != std::string::npos) {
            result.replace(offset, placeholder.size(), value);
            offset += value.size();
        }
    }
    return result;
}

bool HasDocument(const EditorSessionState& state, std::string_view document_id) {
    return std::any_of(state.tabs.tabs.begin(), state.tabs.tabs.end(),
                       [document_id](const auto& document) {
                           return document.document_id == document_id && document.editable;
                       });
}

bool HasPluginDocument(const EditorSessionState& state) {
    return std::any_of(state.tabs.tabs.begin(), state.tabs.tabs.end(), [](const auto& document) {
        return document.category_key == "editor.project.category.plugins" && document.editable;
    });
}

bool IsPanelVisible(const EditorUserSettings& settings, std::string_view panel_id) {
    if (panel_id == "workspace.scene")
        return true;
    if (panel_id == kPanelProjectId)
        return settings.project_visible;
    if (panel_id == kPanelHierarchyId)
        return settings.hierarchy_visible;
    if (panel_id == kPanelInspectorId)
        return settings.inspector_visible;
    if (panel_id == kPanelDiagnosticsId)
        return settings.diagnostics_visible;
    return false;
}

std::string_view ActiveLeftPanel(const EditorUserSettings& settings) {
    const bool project_visible = settings.project_visible;
    const bool hierarchy_visible = settings.hierarchy_visible;
    if (settings.active_left_panel == kPanelProjectId && project_visible)
        return kPanelProjectId;
    if (settings.active_left_panel == kPanelHierarchyId && hierarchy_visible)
        return kPanelHierarchyId;
    if (project_visible)
        return kPanelProjectId;
    if (hierarchy_visible)
        return kPanelHierarchyId;
    return {};
}

std::string Escape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        case '\'':
            result += "&apos;";
            break;
        default:
            result += character;
            break;
        }
    }
    return result;
}

void AppendMenu(std::string& target, const ui::EditorLocale& locale, std::string_view id,
                std::string_view label_key, std::string_view items) {
    target += std::format("<div class=\"menu-root\" data-menu-role=\"root\" id=\"{}\"><div "
                          "class=\"menu-label\" tabindex=\"0\">{}</div>"
                          "<div class=\"menu-popup\">{}</div></div>",
                          id, Escape(Text(locale, label_key)), items);
}

void AppendMenuItem(std::string& target, const ui::EditorLocale& locale, std::string_view name,
                    std::string_view argument, std::string_view label_key, bool enabled = true) {
    target += std::format("<div class=\"menu-item{}\" tabindex=\"-1\" data-enabled=\"{}\" "
                          "data-command=\"{}\" "
                          "data-argument=\"{}\">{}</div>",
                          enabled ? "" : " disabled", enabled ? "true" : "false", name,
                          Escape(argument), Escape(Text(locale, label_key)));
}

void AppendPanelMenuItem(std::string& target, const ui::EditorLocale& locale,
                         std::string_view element_id, std::string_view panel_id,
                         std::string_view label_key, bool visible) {
    target += std::format(
        "<div class=\"menu-item checkbox-item{}\" id=\"{}\" tabindex=\"-1\" "
        "role=\"menuitemcheckbox\" aria-checked=\"{}\" data-enabled=\"true\" "
        "data-command=\"panel.toggle\" data-argument=\"{}\">"
        "<span class=\"menu-check{}\" aria-hidden=\"true\">✓</span><span>{}</span></div>",
        visible ? " checked" : "", element_id, visible ? "true" : "false", panel_id,
        visible ? " checked" : "", Escape(Text(locale, label_key)));
}

bool AppendDocumentMenuItem(std::string& target, const EditorSessionState& state,
                            const ui::EditorLocale& locale, std::string_view document_id,
                            std::string_view label_key) {
    if (!HasDocument(state, document_id))
        return false;
    AppendMenuItem(target, locale, "document.select", document_id, label_key);
    return true;
}

bool AppendPluginMenuItem(std::string& target, const EditorSessionState& state,
                          const ui::EditorLocale& locale, std::string_view label_key) {
    if (!HasPluginDocument(state))
        return false;
    AppendMenuItem(target, locale, "document.plugin", {}, label_key);
    return true;
}

void AppendSubmenu(std::string& target, const ui::EditorLocale& locale, std::string_view id,
                   std::string_view label_key, std::string_view items) {
    target +=
        std::format("<div class=\"menu-item submenu-item\" data-menu-role=\"submenu\" "
                    "tabindex=\"0\" id=\"{}\"><span>{}</span><span>›</span><div class=\"menu-popup "
                    "submenu-popup\">{}</div></div>",
                    id, Escape(Text(locale, label_key)), items);
}

void AppendToolbarButton(std::string& target, const ui::EditorLocale& locale,
                         std::string_view command, std::string_view label_key, bool enabled) {
    target += std::format("<button class=\"toolbar-button{}\" data-enabled=\"{}\"{} "
                          "data-command=\"{}\">{}</button>",
                          enabled ? "" : " disabled", enabled ? "true" : "false",
                          enabled ? "" : " disabled", command, Escape(Text(locale, label_key)));
}

void AppendPanelTitle(std::string& target, std::string_view title) {
    target += std::format("<div class=\"panel-title\">{}</div>", Escape(title));
}

void AppendLeftDockTab(std::string& target, const ui::EditorLocale& locale, std::string_view suffix,
                       std::string_view panel_id, std::string_view label_key, bool active) {
    target += std::format(
        "<div id=\"left-dock-tab-{}\" class=\"left-dock-tab{}\" "
        "role=\"tab\" aria-controls=\"{}-panel\" aria-selected=\"{}\" tabindex=\"{}\" "
        "data-enabled=\"true\" data-command=\"dock.activate\" data-argument=\"{}\">{}</div>",
        suffix, active ? " active" : "", suffix, active ? "true" : "false", active ? "0" : "-1",
        panel_id, Escape(Text(locale, label_key)));
}

void AppendLeftDock(std::string& target, const ui::EditorLocale& locale,
                    const EditorUserSettings& settings) {
    if (!settings.project_visible && !settings.hierarchy_visible)
        return;
    const auto active = ActiveLeftPanel(settings);
    target += std::format("<div id=\"left-dock\" class=\"left-dock\"><div id=\"left-dock-tabs\" "
                          "class=\"left-dock-tabs\" role=\"tablist\" aria-label=\"{}\">",
                          Escape(Text(locale, "editor.workspace.left_dock")));
    if (settings.project_visible)
        AppendLeftDockTab(target, locale, "project", kPanelProjectId, "editor.workspace.project",
                          active == kPanelProjectId);
    if (settings.hierarchy_visible)
        AppendLeftDockTab(target, locale, "hierarchy", kPanelHierarchyId,
                          "editor.workspace.hierarchy", active == kPanelHierarchyId);
    target += "</div></div>";
}

void AppendDocumentRows(std::string& target, const EditorSessionState& state,
                        const ui::EditorLocale& locale, std::string_view project_filter) {
    target += std::format("<input id=\"project-filter\" class=\"project-filter\" type=\"text\" "
                          "data-command=\"project.filter\" value=\"{}\" placeholder=\"{}\" />",
                          Escape(project_filter),
                          Escape(Text(locale, "editor.project.search_placeholder")));
    const auto projection = BuildProjectPanelProjection(state.tabs, project_filter);
    for (const auto& row : projection.rows) {
        if (row.kind == ProjectPanelRowKind::kFilter)
            continue;
        if (row.kind == ProjectPanelRowKind::kCategory) {
            target += std::format("<div class=\"category-row\">{}</div>",
                                  Escape(Text(locale, row.category_key)));
            continue;
        }
        if (row.document_index >= state.tabs.tabs.size())
            continue;
        const auto& document = state.tabs.tabs[row.document_index];
        const auto classes = document.active ? "resource-row active" : "resource-row";
        target +=
            std::format("<div id=\"document-row-{}\" class=\"{}\" tabindex=\"-1\" "
                        "role=\"button\" data-command=\"document.select\" "
                        "data-argument=\"{}\" data-editable=\"{}\" aria-readonly=\"{}\">"
                        "<span class=\"resource-name\">{}</span><span "
                        "class=\"resource-path\">{}</span>{}</div>",
                        row.document_index, classes, Escape(document.document_id),
                        document.editable ? "true" : "false", document.editable ? "false" : "true",
                        Escape(Text(locale, document.label_key)), Escape(document.path),
                        document.dirty ? "<span class=\"dirty-dot\">●</span>" : "");
    }
    if (projection.rows.size() == 1)
        target += std::format("<div class=\"empty-row\">{}</div>",
                              Escape(Text(locale, "editor.project.no_matches")));
}

void AppendHierarchy(std::string& target, const EditorSessionState& state,
                     const ui::EditorLocale& locale) {
    if (!state.navigation) {
        target += std::format("<div class=\"empty-state\">{}</div>",
                              Escape(Text(locale, "editor.navigation.select_map")));
        return;
    }
    const auto& navigation = *state.navigation;
    const auto projected_cell_count =
        std::min(navigation.cells.size(), NavigationProjection::kMaxProjectedCells);
    const bool has_selection = navigation.selected && *navigation.selected < projected_cell_count;
    target += std::format(
        "<div id=\"hierarchy-tree\" class=\"hierarchy-tree\" role=\"tree\" "
        "aria-label=\"{}\"><div id=\"hierarchy-map\" class=\"tree-row "
        "root-row\" "
        "tabindex=\"{}\"{} role=\"treeitem\" aria-expanded=\"true\" "
        "data-command=\"hierarchy.toggle_navigation\"><span class=\"tree-glyph\">▾</span>"
        "<span class=\"tree-label\">{}</span></div>",
        Escape(Text(locale, "editor.workspace.hierarchy")), has_selection ? -1 : 0,
        has_selection ? "" : " autofocus", Escape(Text(locale, "editor.navigation.map")));
    target += std::format("<div id=\"hierarchy-children\" class=\"tree-children\" role=\"group\">"
                          "<div class=\"tree-row child-row tree-meta\"><span class=\"tree-glyph\" "
                          "aria-hidden=\"true\">◇</span>{} × {}</div>",
                          navigation.width, navigation.height);
    for (std::size_t index = 0; index < projected_cell_count; ++index) {
        const auto& cell = navigation.cells[index];
        const bool selected = navigation.selected && *navigation.selected == index;
        const auto status =
            Text(locale, cell.walkable ? "editor.navigation.on" : "editor.navigation.blocked");
        const auto label = FormatText(
            locale, "editor.hierarchy.cell",
            {{"x", std::to_string(cell.x)}, {"y", std::to_string(cell.y)}, {"walkable", status}});
        target += std::format(
            "<div id=\"hierarchy-cell-{}\" class=\"tree-row{} child-row\" "
            "tabindex=\"{}\" "
            "role=\"treeitem\" aria-selected=\"{}\" data-command=\"navigation.select\" "
            "data-argument=\"{}\"><span class=\"tree-glyph\" aria-hidden=\"true\">{}</span>"
            "<span class=\"tree-label\">{}</span></div>",
            index, selected ? " selected-row" : "", selected ? 0 : -1, selected ? "true" : "false",
            index, selected ? "◆" : "◇", Escape(label));
    }
    if (projected_cell_count == 0)
        target += std::format("<div class=\"empty-row child-row\">{}</div>",
                              Escape(Text(locale, "editor.navigation.no_selection")));
    target += "</div></div>";
}

void AppendScene(std::string& target, const EditorSessionState& state,
                 const ui::EditorLocale& locale) {
    if (!state.navigation || state.navigation->width <= 0 || state.navigation->height <= 0) {
        target += std::format("<div class=\"scene-empty\">{}</div>",
                              Escape(Text(locale, "editor.navigation.select_map")));
        return;
    }
    target += std::format(
        "<div class=\"scene-toolbar\"><span>{}</span><span class=\"legend\"><i "
        "class=\"legend-walkable\"></i> {} <i class=\"legend-blocked\"></i> {}</span></div>",
        Escape(Text(locale, "editor.navigation.grid")),
        Escape(Text(locale, "editor.navigation.walkable")),
        Escape(Text(locale, "editor.navigation.blocked")));
    target += "<div class=\"grid-canvas\">";
    const auto width = static_cast<float>(state.navigation->width);
    const auto height = static_cast<float>(state.navigation->height);
    for (std::size_t index = 0; index < state.navigation->cells.size(); ++index) {
        const auto& cell = state.navigation->cells[index];
        const auto classes = std::string(cell.selected ? "grid-cell selected " : "grid-cell ") +
                             (cell.walkable ? "walkable" : "blocked");
        const auto focus_attribute = cell.selected ? " autofocus" : "";
        target += std::format("<div id=\"navigation-cell-{}\" class=\"{}\" tabindex=\"{}\"{} "
                              "role=\"button\" data-command=\"navigation.select\" "
                              "data-argument=\"{}\" "
                              "style=\"left:{}%;top:{}%;width:{}%;height:{}%;\"></div>",
                              index, classes, cell.selected ? 0 : -1, focus_attribute, index,
                              100.0f * static_cast<float>(cell.x) / width,
                              100.0f * static_cast<float>(cell.y) / height, 100.0f / width,
                              100.0f / height);
    }
    target += "</div>";
}

void AppendInspector(std::string& target, const EditorSessionState& state,
                     const ui::EditorLocale& locale) {
    if (state.navigation && state.navigation->selected &&
        *state.navigation->selected < state.navigation->cells.size()) {
        const auto& cell = state.navigation->cells[*state.navigation->selected];
        target += std::format(
            "<div class=\"property-row readonly\"><span>{}</span><strong>({}, {})</strong></div>",
            Escape(Text(locale, "editor.navigation.coordinate")), cell.x, cell.y);
        target += std::format(
            "<div class=\"property-row\"><span>{}</span><div id=\"inspector-walkable\" "
            "class=\"toggle {}\" tabindex=\"-1\" role=\"checkbox\" "
            "aria-checked=\"{}\" "
            "data-command=\"navigation.toggle\"><span class=\"toggle-knob\"></span>{}</div></div>",
            Escape(Text(locale, "editor.navigation.walkable")), cell.walkable ? "on" : "off",
            cell.walkable ? "true" : "false",
            Escape(Text(locale, cell.walkable ? "editor.navigation.on" : "editor.navigation.off")));
        target += std::format("<div class=\"inspector-note\">{}</div>",
                              Escape(Text(locale, "editor.navigation.inspector_hint")));
        return;
    }
    target += std::format("<div class=\"empty-state\">{}</div>",
                          Escape(Text(locale, "editor.inspector.select_object")));
}

void AppendDiagnostics(std::string& target, const EditorSessionState& state,
                       const ui::EditorLocale& locale) {
    if (state.diagnostics.empty() && state.diff.changes.empty() &&
        state.preview.process_exit_code == 0 && state.preview.process_error.empty() &&
        state.preview.standard_error.empty()) {
        target += std::format("<div class=\"diagnostic-ok\">✓ {}</div>",
                              Escape(Text(locale, "editor.diagnostics.none")));
        return;
    }
    for (const auto& diagnostic : state.diagnostics)
        target += std::format("<div class=\"diagnostic "
                              "error\"><span>!</span><strong>{}</strong><span>{}</span></div>",
                              Escape(diagnostic.code), Escape(diagnostic.path));
    for (const auto& change : state.diff.changes)
        target += std::format("<div class=\"diagnostic "
                              "change\"><span>~</span><strong>{}</strong><span>{}:{}</span></div>",
                              Escape(Text(locale, "editor.diagnostics.diff")),
                              Escape(change.document_id), Escape(change.field_path));
    if (!state.preview.process_error.empty())
        target += std::format("<div class=\"diagnostic "
                              "error\"><span>!</span><strong>{}</strong><span>{}</span></div>",
                              Escape(Text(locale, "editor.preview.process")),
                              Escape(state.preview.process_error));
    if (!state.preview.standard_error.empty())
        target += std::format("<div class=\"diagnostic "
                              "error\"><span>!</span><strong>{}</strong><span>{}</span></div>",
                              Escape(Text(locale, "editor.preview.stderr")),
                              Escape(state.preview.standard_error));
    if (state.preview.process_exit_code != 0 && state.preview.process_error.empty())
        target += std::format("<div class=\"diagnostic "
                              "error\"><span>!</span><strong>{}</strong><span>{}</span></div>",
                              Escape(Text(locale, "editor.preview.process")),
                              state.preview.process_exit_code);
}

} // namespace

std::string BuildRmlUiEditorDocument(const EditorSessionState* state,
                                     const ui::EditorLocale& locale,
                                     std::string_view stylesheet_url,
                                     std::string_view project_filter,
                                     const EditorUserSettings& settings,
                                     std::string_view focused_panel, bool panel_maximized) {
    std::string result;
    result.reserve(64 * 1024);
    result += "<rml><head><link type=\"text/rcss\" href=\"";
    result += Escape(stylesheet_url);
    result += "\" /></head><body><div id=\"editor-root\">";

    if (state == nullptr) {
        result += std::format(
            "<div class=\"empty-project\"><h1>jrpgmaker</h1><p>{}</p><div "
            "class=\"empty-hint\">{}</div><button id=\"empty-project-open\" "
            "class=\"empty-project-open\" type=\"button\" tabindex=\"0\" role=\"button\" "
            "data-enabled=\"true\" data-command=\"file.open\">{}</button></div>",
            Escape(Text(locale, "editor.empty.open_project")),
            Escape(Text(locale, "editor.empty.open_project_hint")),
            Escape(Text(locale, "editor.action.open")));
        result += "</div></body></rml>";
        return result;
    }

    result += "<div id=\"menubar\">";
    std::string file_items;
    AppendMenuItem(file_items, locale, "file.open", {}, "editor.action.open");
    AppendMenuItem(file_items, locale, "file.save", {}, "editor.action.save", state->open);
    AppendMenuItem(file_items, locale, "file.refresh", {}, "editor.action.refresh");
    AppendMenu(result, locale, "menu-file", "editor.menu.file", file_items);
    std::string project_items;
    (void) AppendDocumentMenuItem(project_items, *state, locale, "project.manifest",
                                  "editor.menu.project_overview");
    AppendMenuItem(project_items, locale, "file.refresh", {}, "editor.menu.validate_project",
                   state->open);
    std::string settings_items;
    (void) AppendDocumentMenuItem(settings_items, *state, locale, "project.manifest",
                                  "editor.menu.general");
    (void) AppendDocumentMenuItem(settings_items, *state, locale, "core.material",
                                  "editor.menu.rendering");
    (void) AppendDocumentMenuItem(settings_items, *state, locale, "core.camera",
                                  "editor.menu.camera");
    (void) AppendDocumentMenuItem(settings_items, *state, locale, "app.input_actions",
                                  "editor.menu.input");
    (void) AppendDocumentMenuItem(settings_items, *state, locale, "domain.localization",
                                  "editor.menu.localization");
    (void) AppendPluginMenuItem(settings_items, *state, locale, "editor.menu.plugins");
    if (!settings_items.empty())
        AppendSubmenu(project_items, locale, "menu-project-settings",
                      "editor.menu.project_settings", settings_items);
    AppendMenu(result, locale, "menu-project", "editor.menu.project", project_items);
    std::string assets_items;
    (void) AppendDocumentMenuItem(assets_items, *state, locale, "project.resources",
                                  "editor.menu.all_resources");
    (void) AppendDocumentMenuItem(assets_items, *state, locale, "core.material",
                                  "editor.menu.rendering_resources");
    (void) AppendPluginMenuItem(assets_items, *state, locale, "editor.menu.plugin_data");
    if (!assets_items.empty())
        AppendMenu(result, locale, "menu-assets", "editor.menu.assets", assets_items);
    std::string window_items;
    AppendPanelMenuItem(window_items, locale, "menu-window-project", kPanelProjectId,
                        "editor.workspace.project", settings.project_visible);
    AppendPanelMenuItem(window_items, locale, "menu-window-hierarchy", kPanelHierarchyId,
                        "editor.workspace.hierarchy", settings.hierarchy_visible);
    AppendPanelMenuItem(window_items, locale, "menu-window-inspector", kPanelInspectorId,
                        "editor.workspace.inspector", settings.inspector_visible);
    AppendPanelMenuItem(window_items, locale, "menu-window-diagnostics", kPanelDiagnosticsId,
                        "editor.workspace.diagnostics", settings.diagnostics_visible);
    const bool maximize_enabled =
        panel_maximized || (IsPanelVisible(settings, focused_panel) && !focused_panel.empty());
    AppendMenuItem(window_items, locale, "window.toggle_maximize", {},
                   panel_maximized ? "editor.menu.restore_focused_panel"
                                   : "editor.menu.maximize_focused_panel",
                   maximize_enabled);
    std::string layout_items;
    AppendMenuItem(layout_items, locale, "layout.reset_default", {}, "editor.menu.reset_layout");
    AppendSubmenu(window_items, locale, "menu-window-layout", "editor.menu.layout", layout_items);
    AppendMenu(result, locale, "menu-window", "editor.menu.window", window_items);
    std::string run_items;
    AppendMenuItem(run_items, locale, "run.preview", {}, "editor.action.run_preview");
    AppendMenuItem(run_items, locale, "run.stop", {}, "editor.action.stop_preview");
    AppendMenu(result, locale, "menu-run", "editor.menu.run", run_items);
    result += "</div>";

    result += std::format(
        "<div id=\"toolbar\"><div class=\"toolbar-brand\">JRPGMAKER <span>{}</span></div>",
        Escape(Text(locale, "editor.brand.editor")));
    AppendToolbarButton(result, locale, "file.open", "editor.action.open", true);
    AppendToolbarButton(result, locale, "file.save", "editor.action.save", state->open);
    AppendToolbarButton(result, locale, "run.preview", "editor.action.run_preview",
                        state->open && state->preview.valid);
    AppendToolbarButton(result, locale, "run.stop", "editor.action.stop_preview",
                        state->preview.process_running);
    result += "</div>";

    AppendLeftDock(result, locale, settings);

    result += "<div id=\"project-panel\" class=\"panel\">";
    AppendPanelTitle(result, Text(locale, "editor.workspace.project"));
    result += std::format("<div class=\"panel-subtitle\">{}</div><div class=\"resource-list\">",
                          Escape(Text(locale, "editor.project.assets_documents")));
    AppendDocumentRows(result, *state, locale, project_filter);
    result += "</div></div>";

    result += "<div id=\"hierarchy-panel\" class=\"panel\">";
    AppendPanelTitle(result, Text(locale, "editor.workspace.hierarchy"));
    result += std::format("<div class=\"panel-subtitle\">{}</div><div class=\"hierarchy-list\">",
                          Escape(Text(locale, "editor.hierarchy.scene_structure")));
    AppendHierarchy(result, *state, locale);
    result += "</div></div>";

    result += "<div id=\"scene-panel\" class=\"panel\">";
    AppendPanelTitle(result, Text(locale, "editor.workspace.scene"));
    AppendScene(result, *state, locale);
    result += "</div>";

    result += "<div id=\"inspector-panel\" class=\"panel\">";
    AppendPanelTitle(result, Text(locale, "editor.workspace.inspector"));
    result += std::format("<div class=\"panel-subtitle\">{}</div><div class=\"inspector-content\">",
                          Escape(Text(locale, "editor.inspector.selection")));
    AppendInspector(result, *state, locale);
    result += "</div></div>";

    result +=
        "<div id=\"diagnostics-panel\" class=\"panel\"><div class=\"diagnostics-header\"><span>";
    result += Escape(Text(locale, "editor.workspace.diagnostics"));
    result += std::format(
        "</span><span class=\"diagnostic-count\">{}</span></div><div class=\"diagnostics-list\">",
        state->diagnostics.size() + state->diff.changes.size() +
            (state->preview.process_exit_code != 0 && state->preview.process_error.empty() ? 1
                                                                                           : 0) +
            (!state->preview.process_error.empty() ? 1 : 0) +
            (!state->preview.standard_error.empty() ? 1 : 0));
    AppendDiagnostics(result, *state, locale);
    result += "</div></div>";

    const EditorUserSettings defaults{};
    result += std::format("<div id=\"splitter-left-center\" class=\"splitter splitter-vertical\" "
                          "role=\"separator\" aria-orientation=\"vertical\" aria-valuemin=\"0.12\" "
                          "aria-valuemax=\"0.40\" aria-valuenow=\"{:.2f}\"></div>",
                          defaults.splitter_left_center);
    result +=
        std::format("<div id=\"splitter-scene-inspector\" class=\"splitter splitter-vertical\" "
                    "role=\"separator\" aria-orientation=\"vertical\" aria-valuemin=\"0.55\" "
                    "aria-valuemax=\"0.92\" aria-valuenow=\"{:.2f}\"></div>",
                    defaults.splitter_scene_inspector);
    result +=
        std::format("<div id=\"splitter-diagnostics\" class=\"splitter splitter-horizontal\" "
                    "role=\"separator\" aria-orientation=\"horizontal\" aria-valuemin=\"0.60\" "
                    "aria-valuemax=\"0.92\" aria-valuenow=\"{:.2f}\"></div>",
                    defaults.splitter_diagnostics);

    const auto selection = state->selection ? state->selection->object_path : "-";
    result += std::format(
        "<div id=\"statusbar\"><span class=\"status-project {}\">● {}</span><span>{}</span>"
        "<span>{} {}</span><span>{} {}</span><span "
        "class=\"status-spacer\"></span><span>{}</span></div>",
        state->open ? "ready" : "error",
        Escape(Text(locale,
                    state->open ? "editor.status.project_ready" : "editor.status.project_invalid")),
        Escape(Text(locale, state->dirty ? "editor.status.dirty_short" : "editor.status.saved")),
        Escape(Text(locale, "editor.status.revision")), state->revision,
        Escape(Text(locale, "editor.status.selection")), Escape(selection),
        Escape(Text(locale, state->preview.process_running ? "editor.status.preview_running"
                                                           : "editor.status.editor_ready")));
    result += "</div></body></rml>";
    return result;
}

} // namespace jrpgmaker::editor
