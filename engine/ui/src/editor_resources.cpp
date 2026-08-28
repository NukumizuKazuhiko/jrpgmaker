#include "jrpgmaker/ui/editor_resources.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <fstream>
#include <optional>
#include <set>
#include <string_view>
#include <unordered_set>
#include <utility>

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

bool ValidHexColor(const std::string& value) {
    if (value.size() != 7 && value.size() != 9 || value.front() != '#')
        return false;
    return std::all_of(value.begin() + 1, value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

std::optional<EditorColor> ParseColor(const std::string& value) {
    if (!ValidHexColor(value))
        return std::nullopt;
    auto component = [&value](std::size_t offset) {
        return static_cast<std::uint8_t>(std::stoul(value.substr(offset, 2), nullptr, 16));
    };
    return EditorColor{component(1), component(3), component(5),
                       value.size() == 9 ? component(7) : static_cast<std::uint8_t>(255)};
}

bool IsFiniteNonNegative(const nlohmann::json& value) {
    return value.is_number() && std::isfinite(value.get<double>()) && value.get<double>() >= 0.0 &&
           value.get<double>() <= 4096.0;
}

} // namespace

namespace {

void StartupError(std::vector<EditorStartupDiagnostic>& diagnostics, std::string code,
                  std::string path) {
    diagnostics.push_back({std::move(code), std::move(path)});
}

bool ReadJson(const std::filesystem::path& path, nlohmann::json& document,
              std::vector<EditorStartupDiagnostic>& diagnostics) {
    std::ifstream input(path);
    if (!input.is_open()) {
        StartupError(diagnostics, "editor.startup.file_open", path.string());
        return false;
    }
    try {
        input >> document;
    } catch (const std::exception&) {
        StartupError(diagnostics, "editor.startup.file_json", path.string());
        return false;
    }
    return true;
}

std::string ResourceFileName(std::string id) {
    std::replace(id.begin(), id.end(), '.', '_');
    std::replace(id.begin(), id.end(), '-', '_');
    return id + ".json";
}

void AddResourceErrors(const std::vector<EditorResourceError>& errors,
                       const std::filesystem::path& path,
                       std::vector<EditorStartupDiagnostic>& diagnostics) {
    for (const auto& error : errors)
        StartupError(diagnostics, error.code, path.string() + error.path);
}

std::set<std::string> PlaceholderNames(std::string_view value, bool& valid) {
    std::set<std::string> names;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '{')
            continue;
        if (index + 1 < value.size() && value[index + 1] == '{') {
            ++index;
            continue;
        }
        const auto end = value.find('}', index + 1);
        if (end == std::string_view::npos || end == index + 1) {
            valid = false;
            continue;
        }
        const auto name = value.substr(index + 1, end - index - 1);
        if (name.find('{') != std::string_view::npos || name.find('}') != std::string_view::npos)
            valid = false;
        else
            names.emplace(name);
        index = end;
    }
    return names;
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

EditorResourceParseResult<EditorTheme> ParseEditorTheme(const nlohmann::json& document) {
    EditorResourceParseResult<EditorTheme> result;
    if (!document.is_object() || document.value("schema", 0) != 1)
        Error(result.errors, "editor.theme.schema", "/schema");
    ManifestId(document, "id", result.value.id, result.errors);

    const auto* colors = document.contains("colors") ? &document["colors"] : nullptr;
    if (colors == nullptr || !colors->is_object()) {
        Error(result.errors, "editor.theme.colors_required", "/colors");
    } else if (colors->size() > kMaxEditorThemeEntries) {
        Error(result.errors, "editor.theme.entry_limit", "/colors");
    } else {
        for (auto it = colors->begin(); it != colors->end(); ++it) {
            if (!ValidId(it.key()) || !it.value().is_string()) {
                Error(result.errors, "editor.theme.invalid_color", "/colors/" + it.key());
                continue;
            }
            const auto color = ParseColor(it.value().get<std::string>());
            if (!color)
                Error(result.errors, "editor.theme.invalid_color", "/colors/" + it.key());
            else
                result.value.colors.emplace(it.key(), *color);
        }
    }

    const auto* dimensions = document.contains("dimensions") ? &document["dimensions"] : nullptr;
    if (dimensions == nullptr || !dimensions->is_object()) {
        Error(result.errors, "editor.theme.dimensions_required", "/dimensions");
    } else if (dimensions->size() > kMaxEditorThemeEntries) {
        Error(result.errors, "editor.theme.entry_limit", "/dimensions");
    } else {
        for (auto it = dimensions->begin(); it != dimensions->end(); ++it) {
            if (!ValidId(it.key()) || !IsFiniteNonNegative(it.value())) {
                Error(result.errors, "editor.theme.invalid_dimension", "/dimensions/" + it.key());
                continue;
            }
            result.value.dimensions.emplace(it.key(), it.value().get<float>());
        }
    }

    const auto* semantic = document.contains("semantic_tokens") ? &document["semantic_tokens"] : nullptr;
    if (semantic == nullptr || !semantic->is_object()) {
        Error(result.errors, "editor.theme.semantic_tokens_required", "/semantic_tokens");
    } else {
        for (auto it = semantic->begin(); it != semantic->end(); ++it) {
            if (!ValidId(it.key()) || !it.value().is_string() ||
                !result.value.colors.contains(it.value().get<std::string>()))
                Error(result.errors, "editor.theme.invalid_semantic_token", "/semantic_tokens/" + it.key());
            else
                result.value.semantic_tokens.emplace(it.key(), it.value().get<std::string>());
        }
    }

    const auto* recipes = document.contains("recipes") ? &document["recipes"] : nullptr;
    if (recipes == nullptr || !recipes->is_object()) {
        Error(result.errors, "editor.theme.recipes_required", "/recipes");
    } else if (recipes->size() > kMaxEditorThemeEntries) {
        Error(result.errors, "editor.theme.entry_limit", "/recipes");
    } else {
        constexpr const char* states[] = {"normal", "hover", "pressed", "focused", "disabled"};
        for (auto recipe = recipes->begin(); recipe != recipes->end(); ++recipe) {
            if (!ValidId(recipe.key()) || !recipe.value().is_object()) {
                Error(result.errors, "editor.theme.invalid_recipe", "/recipes/" + recipe.key());
                continue;
            }
            EditorThemeRecipe parsed;
            for (const char* state : states) {
                if (!recipe.value().contains(state) || !recipe.value()[state].is_string() ||
                    !result.value.semantic_tokens.contains(recipe.value()[state].get<std::string>()))
                    Error(result.errors, "editor.theme.recipe_state_invalid",
                          "/recipes/" + recipe.key() + "/" + state);
                else
                    parsed.states.emplace(state, recipe.value()[state].get<std::string>());
            }
            result.value.recipes.emplace(recipe.key(), std::move(parsed));
        }
    }
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

EditorStartupResult LoadEditorResources(const std::filesystem::path& root) {
    EditorStartupResult result;
    nlohmann::json manifest_document;
    if (!ReadJson(root / "editor.json", manifest_document, result.diagnostics))
        return result;
    const auto manifest = ParseEditorManifest(manifest_document);
    AddResourceErrors(manifest.errors, root / "editor.json", result.diagnostics);
    if (!manifest)
        return result;

    std::unordered_map<std::string, EditorLocale> locales;
    for (const auto& locale_id : manifest.value.available_locales) {
        nlohmann::json document;
        const auto path = root / "locales" / (locale_id + ".json");
        if (!ReadJson(path, document, result.diagnostics))
            continue;
        const auto parsed = ParseEditorLocale(document, manifest.value);
        AddResourceErrors(parsed.errors, path, result.diagnostics);
        if (parsed)
            locales.emplace(locale_id, parsed.value);
    }
    nlohmann::json theme_document;
    const auto theme_path = root / "themes" / ResourceFileName(manifest.value.default_theme);
    const auto theme_read = ReadJson(theme_path, theme_document, result.diagnostics);
    const auto theme = theme_read ? ParseEditorTheme(theme_document)
                                  : EditorResourceParseResult<EditorTheme>{};
    if (theme_read)
        AddResourceErrors(theme.errors, theme_path, result.diagnostics);

    nlohmann::json layout_document;
    const auto layout_path = root / "layouts" / ResourceFileName(manifest.value.default_layout);
    const auto layout_read = ReadJson(layout_path, layout_document, result.diagnostics);
    const auto layout = layout_read ? ParseEditorLayout(layout_document)
                                    : EditorResourceParseResult<EditorLayout>{};
    if (layout_read)
        AddResourceErrors(layout.errors, layout_path, result.diagnostics);

    const auto locale_it = locales.find(manifest.value.default_locale);
    if (locale_it == locales.end())
        StartupError(result.diagnostics, "editor.startup.default_locale_unavailable",
                     manifest.value.default_locale);
    else {
        bool placeholders_valid = true;
        const auto& baseline = locale_it->second.strings;
        for (const auto& [locale_id, locale] : locales) {
            if (locale.strings.size() != baseline.size())
                StartupError(result.diagnostics, "editor.locale.key_set_mismatch", locale_id);
            for (const auto& [key, text] : baseline) {
                const auto translated = locale.strings.find(key);
                if (translated == locale.strings.end()) {
                    StartupError(result.diagnostics, "editor.locale.key_missing", locale_id + "/" + key);
                    continue;
                }
                bool base_valid = true;
                bool translated_valid = true;
                const auto base_names = PlaceholderNames(text, base_valid);
                const auto translated_names = PlaceholderNames(translated->second, translated_valid);
                if (!base_valid || !translated_valid || base_names != translated_names)
                    StartupError(result.diagnostics, "editor.locale.placeholder_mismatch",
                                 locale_id + "/" + key);
            }
        }
        placeholders_valid = placeholders_valid && !baseline.empty();
        if (!placeholders_valid)
            StartupError(result.diagnostics, "editor.locale.empty", manifest.value.default_locale);
    }

    if (result.diagnostics.empty() && theme_read && theme && layout_read && layout) {
        result.bundle = EditorResourceBundle{manifest.value, locale_it->second, theme.value, layout.value};
    }
    return result;
}

} // namespace jrpgmaker::ui
