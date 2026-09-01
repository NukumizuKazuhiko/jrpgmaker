#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace jrpgmaker::editor {

inline constexpr int kEditorUserSettingsSchemaVersion = 3;
inline constexpr int kEditorUserSettingsPreviousSchemaVersion = 2;
inline constexpr int kEditorUserSettingsLegacySchemaVersion = 1;
inline constexpr const char* kSplitterLeftCenterId = "splitter-left-center";
inline constexpr const char* kSplitterSceneInspectorId = "splitter-scene-inspector";
inline constexpr const char* kSplitterDiagnosticsId = "splitter-diagnostics";
inline constexpr const char* kPanelProjectId = "workspace.project";
inline constexpr const char* kPanelHierarchyId = "workspace.hierarchy";
inline constexpr const char* kPanelInspectorId = "workspace.inspector";
inline constexpr const char* kPanelDiagnosticsId = "workspace.diagnostics";
inline constexpr const char* kActiveLeftPanelField = "active_left_panel";

struct EditorUserSettings {
    float splitter_left_center = 0.22f;
    float splitter_scene_inspector = 0.80f;
    float splitter_diagnostics = 0.86f;
    bool project_visible = true;
    bool hierarchy_visible = true;
    bool inspector_visible = true;
    bool diagnostics_visible = true;
    std::string active_left_panel = kPanelProjectId;

    bool operator==(const EditorUserSettings&) const = default;
};

struct EditorUserSettingsDiagnostic {
    std::string code;
    std::string path;
};

struct EditorUserSettingsLoadResult {
    EditorUserSettings settings;
    std::vector<EditorUserSettingsDiagnostic> diagnostics;
    bool used_defaults = false;
};

struct EditorUserSettingsSaveResult {
    std::vector<EditorUserSettingsDiagnostic> diagnostics;
    bool ok = false;
};

// Loads only editor-owned preferences. Missing or invalid input never escapes
// as partial state: the complete default layout is returned with diagnostics.
[[nodiscard]] EditorUserSettingsLoadResult
LoadEditorUserSettings(const std::filesystem::path& path);

// Writes editor-owned preferences through a bounded temporary file and an
// atomic replacement. A failed write leaves the previous file untouched.
[[nodiscard]] EditorUserSettingsSaveResult
SaveEditorUserSettings(const std::filesystem::path& path, const EditorUserSettings& settings);

} // namespace jrpgmaker::editor
