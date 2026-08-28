#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace jrpgmaker::ui {

struct EditorResourceError {
    std::string code;
    std::string path;
};

struct EditorManifest {
    std::uint32_t schema = 1;
    std::string id;
    std::string default_locale;
    std::vector<std::string> locale_fallbacks;
    std::string default_theme;
    std::string default_layout;
    std::vector<std::string> available_locales;
    std::vector<std::string> available_themes;
};

struct EditorLocale {
    std::uint32_t schema = 1;
    std::string locale;
    std::unordered_map<std::string, std::string> strings;
};

struct EditorColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct EditorThemeRecipe {
    std::unordered_map<std::string, std::string> states;
};

struct EditorTheme {
    std::uint32_t schema = 1;
    std::string id;
    std::unordered_map<std::string, EditorColor> colors;
    std::unordered_map<std::string, float> dimensions;
    std::unordered_map<std::string, std::string> semantic_tokens;
    std::unordered_map<std::string, EditorThemeRecipe> recipes;
};

struct EditorLayoutNode {
    std::string type;
    std::string id;
    std::string label_key;
    std::string recipe;
    std::vector<EditorLayoutNode> children;
};

struct EditorLayout {
    std::uint32_t schema = 1;
    std::string id;
    EditorLayoutNode root;
};

template <typename T>
struct EditorResourceParseResult {
    T value{};
    std::vector<EditorResourceError> errors;
    explicit operator bool() const { return errors.empty(); }
};

inline constexpr std::size_t kMaxEditorLocaleEntries = 4096;
inline constexpr std::size_t kMaxEditorLayoutNodes = 256;
inline constexpr std::size_t kMaxEditorStringBytes = 256 * 1024;
inline constexpr std::size_t kMaxEditorThemeEntries = 256;

[[nodiscard]] EditorResourceParseResult<EditorManifest>
ParseEditorManifest(const nlohmann::json& document);
[[nodiscard]] EditorResourceParseResult<EditorLocale>
ParseEditorLocale(const nlohmann::json& document, const EditorManifest& manifest);
[[nodiscard]] EditorResourceParseResult<EditorTheme>
ParseEditorTheme(const nlohmann::json& document);
[[nodiscard]] EditorResourceParseResult<EditorLayout>
ParseEditorLayout(const nlohmann::json& document);

} // namespace jrpgmaker::ui
