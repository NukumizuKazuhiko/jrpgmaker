#include "jrpgmaker/editor/editor_user_settings.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace jrpgmaker::editor {
namespace {

constexpr std::size_t kMaxSettingsBytes = 64 * 1024;
constexpr float kLeftCenterMinimum = 0.12f;
constexpr float kLeftCenterMaximum = 0.40f;
constexpr float kSceneInspectorMinimum = 0.55f;
constexpr float kSceneInspectorMaximum = 0.92f;
constexpr float kDiagnosticsMinimum = 0.60f;
constexpr float kDiagnosticsMaximum = 0.92f;

void AddDiagnostic(std::vector<EditorUserSettingsDiagnostic>& diagnostics, std::string_view code,
                   std::string_view path) {
    diagnostics.push_back({std::string(code), std::string(path)});
}

EditorUserSettingsLoadResult Defaults(std::vector<EditorUserSettingsDiagnostic> diagnostics) {
    return {.settings = {}, .diagnostics = std::move(diagnostics), .used_defaults = true};
}

bool IsFiniteInRange(double value, float minimum, float maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool ReadRatio(const nlohmann::json& splitters, const char* id, float minimum, float maximum,
               float& target, std::vector<EditorUserSettingsDiagnostic>& diagnostics) {
    if (!splitters.contains(id)) {
        AddDiagnostic(diagnostics, "editor.settings.field_missing",
                      std::string("/splitters/") + id);
        return false;
    }
    const auto& value = splitters.at(id);
    if (!value.is_number()) {
        AddDiagnostic(diagnostics, "editor.settings.field_type", std::string("/splitters/") + id);
        return false;
    }
    double ratio = 0.0;
    try {
        ratio = value.get<double>();
    } catch (...) {
        AddDiagnostic(diagnostics, "editor.settings.field_type", std::string("/splitters/") + id);
        return false;
    }
    if (!IsFiniteInRange(ratio, minimum, maximum)) {
        AddDiagnostic(diagnostics, "editor.settings.field_out_of_range",
                      std::string("/splitters/") + id);
        return false;
    }
    target = static_cast<float>(ratio);
    return true;
}

bool ReadVisibility(const nlohmann::json& visibility, const char* id, bool& target,
                    std::vector<EditorUserSettingsDiagnostic>& diagnostics) {
    if (!visibility.contains(id)) {
        AddDiagnostic(diagnostics, "editor.settings.field_missing",
                      std::string("/visibility/") + id);
        return false;
    }
    const auto& value = visibility.at(id);
    if (!value.is_boolean()) {
        AddDiagnostic(diagnostics, "editor.settings.field_type", std::string("/visibility/") + id);
        return false;
    }
    try {
        target = value.get<bool>();
    } catch (...) {
        AddDiagnostic(diagnostics, "editor.settings.field_type", std::string("/visibility/") + id);
        return false;
    }
    return true;
}

bool ReadActiveLeftPanel(const nlohmann::json& document, std::string& target,
                         std::vector<EditorUserSettingsDiagnostic>& diagnostics) {
    if (!document.contains(kActiveLeftPanelField)) {
        AddDiagnostic(diagnostics, "editor.settings.field_missing",
                      std::string("/") + kActiveLeftPanelField);
        return false;
    }
    const auto& value = document.at(kActiveLeftPanelField);
    if (!value.is_string()) {
        AddDiagnostic(diagnostics, "editor.settings.field_type",
                      std::string("/") + kActiveLeftPanelField);
        return false;
    }
    try {
        target = value.get<std::string>();
    } catch (...) {
        AddDiagnostic(diagnostics, "editor.settings.field_type",
                      std::string("/") + kActiveLeftPanelField);
        return false;
    }
    if (target != kPanelProjectId && target != kPanelHierarchyId) {
        AddDiagnostic(diagnostics, "editor.settings.field_invalid",
                      std::string("/") + kActiveLeftPanelField);
        return false;
    }
    return true;
}

bool ValidateForSave(const EditorUserSettings& settings,
                     std::vector<EditorUserSettingsDiagnostic>& diagnostics) {
    if (!IsFiniteInRange(settings.splitter_left_center, kLeftCenterMinimum, kLeftCenterMaximum)) {
        AddDiagnostic(diagnostics, "editor.settings.field_out_of_range",
                      std::string("/splitters/") + kSplitterLeftCenterId);
    }
    if (!IsFiniteInRange(settings.splitter_scene_inspector, kSceneInspectorMinimum,
                         kSceneInspectorMaximum)) {
        AddDiagnostic(diagnostics, "editor.settings.field_out_of_range",
                      std::string("/splitters/") + kSplitterSceneInspectorId);
    }
    if (!IsFiniteInRange(settings.splitter_diagnostics, kDiagnosticsMinimum, kDiagnosticsMaximum)) {
        AddDiagnostic(diagnostics, "editor.settings.field_out_of_range",
                      std::string("/splitters/") + kSplitterDiagnosticsId);
    }
    if (settings.splitter_left_center > settings.splitter_scene_inspector - 0.20f) {
        AddDiagnostic(diagnostics, "editor.settings.layout_inconsistent", "/splitters");
    }
    if (settings.active_left_panel != kPanelProjectId &&
        settings.active_left_panel != kPanelHierarchyId) {
        AddDiagnostic(diagnostics, "editor.settings.field_invalid",
                      std::string("/") + kActiveLeftPanelField);
    }
    return diagnostics.empty();
}

nlohmann::json ToJson(const EditorUserSettings& settings) {
    return {
        {"schema", kEditorUserSettingsSchemaVersion},
        {"splitters",
         {{kSplitterLeftCenterId, settings.splitter_left_center},
          {kSplitterSceneInspectorId, settings.splitter_scene_inspector},
          {kSplitterDiagnosticsId, settings.splitter_diagnostics}}},
        {"visibility",
         {{kPanelProjectId, settings.project_visible},
          {kPanelHierarchyId, settings.hierarchy_visible},
          {kPanelInspectorId, settings.inspector_visible},
          {kPanelDiagnosticsId, settings.diagnostics_visible}}},
        {kActiveLeftPanelField, settings.active_left_panel},
    };
}

} // namespace

EditorUserSettingsLoadResult LoadEditorUserSettings(const std::filesystem::path& path) {
    std::vector<EditorUserSettingsDiagnostic> diagnostics;
    std::error_code error;
    if (!std::filesystem::exists(path, error) && !error) {
        AddDiagnostic(diagnostics, "editor.settings.file_missing", path.generic_string());
        return Defaults(std::move(diagnostics));
    }
    if (error || !std::filesystem::is_regular_file(path, error)) {
        AddDiagnostic(diagnostics, "editor.settings.file_unreadable", path.generic_string());
        return Defaults(std::move(diagnostics));
    }

    const auto size = std::filesystem::file_size(path, error);
    if (error || size > kMaxSettingsBytes) {
        AddDiagnostic(diagnostics, "editor.settings.file_too_large", path.generic_string());
        return Defaults(std::move(diagnostics));
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        AddDiagnostic(diagnostics, "editor.settings.file_unreadable", path.generic_string());
        return Defaults(std::move(diagnostics));
    }
    const std::string contents((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

    nlohmann::json document;
    try {
        document = nlohmann::json::parse(contents);
    } catch (...) {
        AddDiagnostic(diagnostics, "editor.settings.json_invalid", path.generic_string());
        return Defaults(std::move(diagnostics));
    }
    if (!document.is_object()) {
        AddDiagnostic(diagnostics, "editor.settings.document_type", "/");
        return Defaults(std::move(diagnostics));
    }
    int schema = 0;
    try {
        if (document.contains("schema") && document.at("schema").is_number_integer())
            schema = document.at("schema").get<int>();
    } catch (...) {
        schema = 0;
    }
    if (!document.contains("schema") || !document.at("schema").is_number_integer() ||
        (schema != kEditorUserSettingsSchemaVersion &&
         schema != kEditorUserSettingsPreviousSchemaVersion &&
         schema != kEditorUserSettingsLegacySchemaVersion)) {
        AddDiagnostic(diagnostics, "editor.settings.schema_invalid", "/schema");
        return Defaults(std::move(diagnostics));
    }
    if (!document.contains("splitters") || !document.at("splitters").is_object()) {
        AddDiagnostic(diagnostics, "editor.settings.field_type", "/splitters");
        return Defaults(std::move(diagnostics));
    }
    const bool migrated = schema != kEditorUserSettingsSchemaVersion;
    bool unknown_field = false;
    for (const auto& [key, value] : document.items()) {
        if (key != "schema" && key != "splitters" && key != "visibility" &&
            key != kActiveLeftPanelField) {
            AddDiagnostic(diagnostics, "editor.settings.field_unknown", "/" + key);
            unknown_field = true;
        }
    }
    const auto& splitters = document.at("splitters");
    for (const auto& [key, value] : splitters.items()) {
        if (key != kSplitterLeftCenterId && key != kSplitterSceneInspectorId &&
            key != kSplitterDiagnosticsId) {
            AddDiagnostic(diagnostics, "editor.settings.field_unknown", "/splitters/" + key);
            unknown_field = true;
        }
    }

    if (schema >= kEditorUserSettingsPreviousSchemaVersion &&
        (!document.contains("visibility") || !document.at("visibility").is_object())) {
        AddDiagnostic(diagnostics, "editor.settings.field_type", "/visibility");
        return Defaults(std::move(diagnostics));
    }
    if (migrated) {
        AddDiagnostic(diagnostics, "editor.settings.schema_migrated", "/schema");
    }

    EditorUserSettings settings;
    const bool left_ok = ReadRatio(splitters, kSplitterLeftCenterId, kLeftCenterMinimum,
                                   kLeftCenterMaximum, settings.splitter_left_center, diagnostics);
    const bool scene_ok =
        ReadRatio(splitters, kSplitterSceneInspectorId, kSceneInspectorMinimum,
                  kSceneInspectorMaximum, settings.splitter_scene_inspector, diagnostics);
    const bool diagnostics_ok =
        ReadRatio(splitters, kSplitterDiagnosticsId, kDiagnosticsMinimum, kDiagnosticsMaximum,
                  settings.splitter_diagnostics, diagnostics);
    if (left_ok && scene_ok &&
        settings.splitter_left_center > settings.splitter_scene_inspector - 0.20f) {
        AddDiagnostic(diagnostics, "editor.settings.layout_inconsistent", "/splitters");
    }
    bool visibility_ok = true;
    if (schema >= kEditorUserSettingsPreviousSchemaVersion) {
        const auto& visibility = document.at("visibility");
        for (const auto& [key, value] : visibility.items()) {
            if (key != kPanelProjectId && key != kPanelHierarchyId && key != kPanelInspectorId &&
                key != kPanelDiagnosticsId) {
                AddDiagnostic(diagnostics, "editor.settings.field_unknown", "/visibility/" + key);
                unknown_field = true;
            }
        }
        visibility_ok &=
            ReadVisibility(visibility, kPanelProjectId, settings.project_visible, diagnostics);
        visibility_ok &=
            ReadVisibility(visibility, kPanelHierarchyId, settings.hierarchy_visible, diagnostics);
        visibility_ok &=
            ReadVisibility(visibility, kPanelInspectorId, settings.inspector_visible, diagnostics);
        visibility_ok &= ReadVisibility(visibility, kPanelDiagnosticsId,
                                        settings.diagnostics_visible, diagnostics);
    }
    bool active_left_panel_ok = true;
    if (schema == kEditorUserSettingsSchemaVersion)
        active_left_panel_ok =
            ReadActiveLeftPanel(document, settings.active_left_panel, diagnostics);
    if (!left_ok || !scene_ok || !diagnostics_ok || !visibility_ok || !active_left_panel_ok ||
        (!migrated && unknown_field) ||
        std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.code != "editor.settings.field_unknown" &&
                   diagnostic.code != "editor.settings.schema_migrated";
        }))
        return Defaults(std::move(diagnostics));
    return {.settings = settings, .diagnostics = std::move(diagnostics), .used_defaults = false};
}

EditorUserSettingsSaveResult SaveEditorUserSettings(const std::filesystem::path& path,
                                                    const EditorUserSettings& settings) {
    EditorUserSettingsSaveResult result;
    if (path.empty()) {
        AddDiagnostic(result.diagnostics, "editor.settings.path_invalid", "/");
        return result;
    }
    if (!ValidateForSave(settings, result.diagnostics))
        return result;
    const auto parent = path.parent_path();
    std::error_code error;
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            AddDiagnostic(result.diagnostics, "editor.settings.directory_create_failed",
                          parent.generic_string());
            return result;
        }
    }
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            AddDiagnostic(result.diagnostics, "editor.settings.write_failed",
                          temporary.generic_string());
            return result;
        }
        file << ToJson(settings).dump(2) << '\n';
        file.flush();
        if (!file.good()) {
            AddDiagnostic(result.diagnostics, "editor.settings.write_failed",
                          temporary.generic_string());
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            return result;
        }
    }

#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        AddDiagnostic(result.diagnostics, "editor.settings.replace_failed", path.generic_string());
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return result;
    }
#else
    std::filesystem::rename(temporary, path, error);
    if (error) {
        AddDiagnostic(result.diagnostics, "editor.settings.replace_failed", path.generic_string());
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return result;
    }
#endif
    result.ok = true;
    return result;
}

} // namespace jrpgmaker::editor
