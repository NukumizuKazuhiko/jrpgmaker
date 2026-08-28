#include "jrpgmaker/ui/editor_resources.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_set>

namespace jrpgmaker::ui {
namespace {

void Error(std::vector<EditorResourceError>& errors, const char* code, std::string path) {
    errors.push_back({code, std::move(path)});
}

bool ValidId(const nlohmann::json& value) {
    if (!value.is_string() || value.get<std::string>().empty())
        return false;
    const auto id = value.get<std::string>();
    return std::all_of(id.begin(), id.end(),
                       [](unsigned char c) { return std::isalnum(c) != 0 || c == '.' || c == '-' || c == '_'; });
}

bool StringArray(const nlohmann::json& value, std::vector<std::string>& result,
                 std::vector<EditorResourceError>& errors, const char* path) {
    if (!value.is_array() || value.empty()) {
        Error(errors, "editor.resource.array_required", path);
        return false;
    }
    std::unordered_set<std::string> seen;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (!ValidId(value[i])) {
            Error(errors, "editor.resource.invalid_id", std::string(path) + "/" + std::to_string(i));
            continue;
        }
        const auto id = value[i].get<std::string>();
        if (!seen.insert(id).second)
            Error(errors, "editor.resource.duplicate_id", std::string(path) + "/" + std::to_string(i));
        result.push_back(id);
    }
    return true;
}

bool ManifestId(const nlohmann::json& document, const char* key,
                std::string& result, std::vector<EditorResourceError>& errors) {
    if (!document.contains(key) || !ValidId(document[key])) {
        Error(errors, "editor.resource.required_id", key);
        return false;
    }
    result = document[key].get<std::string>();
    return true;
}

bool ParseLayoutNode(const nlohmann::json& document, EditorLayoutNode& node,
                     std::unordered_set<std::string>& ids, std::size_t& count,
                     std::vector<EditorResourceError>& errors, const std::string& path) {
    if (++count > kMaxEditorLayoutNodes) {
        Error(errors, "editor.layout.node_limit", path);
        return false;
    }
    if (!document.is_object()) {
        Error(errors, "editor.layout.object_required", path);
        return false;
    }
    if (!ManifestId(document, "type", node.type, errors))
        return false;
    if (!ManifestId(document, "id", node.id, errors))
        return false;
    if (!ids.insert(node.id).second)
        Error(errors, "editor.layout.duplicate_id", path + "/id");
    if (document.contains("label_key")) {
        if (!document["label_key"].is_string())
            Error(errors, "editor.layout.invalid_label_key", path + "/label_key");
        else
            node.label_key = document["label_key"].get<std::string>();
    }
    if (document.contains("recipe")) {
        if (!document["recipe"].is_string())
            Error(errors, "editor.layout.invalid_recipe", path + "/recipe");
        else
            node.recipe = document["recipe"].get<std::string>();
    }
    if (document.contains("children")) {
        if (!document["children"].is_array()) {
            Error(errors, "editor.layout.children_array_required", path + "/children");
        } else {
            for (std::size_t i = 0; i < document["children"].size(); ++i) {
                EditorLayoutNode child;
                if (ParseLayoutNode(document["children"][i], child, ids, count, errors,
                                    path + "/children/" + std::to_string(i)))
                    node.children.push_back(std::move(child));
            }
        }
    }
    return true;
}

} // namespace

EditorResourceParseResult<EditorManifest> ParseEditorManifest(const nlohmann::json& document) {
    EditorResourceParseResult<EditorManifest> result;
    if (!document.is_object() || document.value("schema", 0) != 1)
        Error(result.errors, "editor.manifest.schema", "/schema");
    ManifestId(document, "id", result.value.id, result.errors);
    for (const char* key : {"default_locale", "default_theme", "default_layout"}) {
        if (!document.contains(key) || !ValidId(document[key]))
            Error(result.errors, "editor.manifest.required_id", key);
    }
    if (document.contains("default_locale") && ValidId(document["default_locale"]))
        result.value.default_locale = document["default_locale"].get<std::string>();
    if (document.contains("default_theme") && ValidId(document["default_theme"]))
        result.value.default_theme = document["default_theme"].get<std::string>();
    if (document.contains("default_layout") && ValidId(document["default_layout"]))
        result.value.default_layout = document["default_layout"].get<std::string>();
    if (document.contains("locale_fallbacks") && document["locale_fallbacks"].is_array())
        for (const auto& value : document["locale_fallbacks"])
            if (ValidId(value)) result.value.locale_fallbacks.push_back(value.get<std::string>());
    StringArray(document.value("available_locales", nlohmann::json{}), result.value.available_locales,
                result.errors, "/available_locales");
    StringArray(document.value("available_themes", nlohmann::json{}), result.value.available_themes,
                result.errors, "/available_themes");
    if (std::find(result.value.available_locales.begin(), result.value.available_locales.end(),
                  result.value.default_locale) == result.value.available_locales.end())
        Error(result.errors, "editor.manifest.default_locale_missing", "/default_locale");
    if (std::find(result.value.available_themes.begin(), result.value.available_themes.end(),
                  result.value.default_theme) == result.value.available_themes.end())
        Error(result.errors, "editor.manifest.default_theme_missing", "/default_theme");
    return result;
}

EditorResourceParseResult<EditorLocale> ParseEditorLocale(const nlohmann::json& document,
                                                           const EditorManifest& manifest) {
    EditorResourceParseResult<EditorLocale> result;
    if (!document.is_object() || document.value("schema", 0) != 1)
        Error(result.errors, "editor.locale.schema", "/schema");
    if (!document.contains("locale") || !ValidId(document["locale"]))
        Error(result.errors, "editor.locale.required_id", "/locale");
    else
        result.value.locale = document["locale"].get<std::string>();
    if (std::find(manifest.available_locales.begin(), manifest.available_locales.end(), result.value.locale) ==
        manifest.available_locales.end())
        Error(result.errors, "editor.locale.unregistered", "/locale");
    const auto* strings = document.contains("strings") ? &document["strings"] : nullptr;
    if (strings == nullptr || !strings->is_object()) {
        Error(result.errors, "editor.locale.strings_required", "/strings");
        return result;
    }
    if (strings->size() > kMaxEditorLocaleEntries)
        Error(result.errors, "editor.locale.entry_limit", "/strings");
    std::size_t bytes = 0;
    for (auto it = strings->begin(); it != strings->end(); ++it) {
        if (it.key().rfind("editor.", 0) != 0 || !it.value().is_string())
            Error(result.errors, "editor.locale.invalid_entry", "/strings/" + it.key());
        else {
            bytes += it.key().size() + it.value().get<std::string>().size();
            result.value.strings.emplace(it.key(), it.value().get<std::string>());
        }
    }
    if (bytes > kMaxEditorStringBytes)
        Error(result.errors, "editor.locale.byte_limit", "/strings");
    return result;
}

EditorResourceParseResult<EditorLayout> ParseEditorLayout(const nlohmann::json& document) {
    EditorResourceParseResult<EditorLayout> result;
    if (!document.is_object() || document.value("schema", 0) != 1)
        Error(result.errors, "editor.layout.schema", "/schema");
    ManifestId(document, "id", result.value.id, result.errors);
    if (!document.contains("root"))
        Error(result.errors, "editor.layout.root_required", "/root");
    else {
        std::unordered_set<std::string> ids;
        std::size_t count = 0;
        ParseLayoutNode(document["root"], result.value.root, ids, count, result.errors, "/root");
    }
    return result;
}

} // namespace jrpgmaker::ui
