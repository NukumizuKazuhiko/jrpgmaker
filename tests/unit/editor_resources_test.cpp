#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "jrpgmaker/ui/editor_resources.hpp"

namespace {

nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    REQUIRE(input.good());
    nlohmann::json document;
    input >> document;
    return document;
}

nlohmann::json Manifest() {
    return {{"schema", 1}, {"id", "editor.desktop"}, {"default_locale", "zh-CN"},
            {"locale_fallbacks", {"en"}}, {"default_theme", "editor.default"},
            {"default_layout", "editor.workspace"}, {"available_locales", {"zh-CN", "en"}},
            {"available_themes", {"editor.default", "editor.high-contrast"}}};
}

} // namespace

TEST_CASE("editor resources parse manifest and locale", "[ui][editor]") {
    const auto manifest = jrpgmaker::ui::ParseEditorManifest(Manifest());
    REQUIRE(manifest);
    const auto locale = jrpgmaker::ui::ParseEditorLocale(
        nlohmann::json{{"schema", 1}, {"locale", "zh-CN"},
                       {"strings", {{"editor.window.title", "JRPGMaker 编辑器"}}}},
        manifest.value);
    REQUIRE(locale);
    REQUIRE(locale.value.strings.at("editor.window.title") == "JRPGMaker 编辑器");
}

TEST_CASE("editor resources reject unregistered locale and duplicate layout ids", "[ui][editor]") {
    const auto manifest = jrpgmaker::ui::ParseEditorManifest(Manifest());
    REQUIRE(manifest);
    const auto locale = jrpgmaker::ui::ParseEditorLocale(
        nlohmann::json{{"schema", 1}, {"locale", "ja"}, {"strings", {}}}, manifest.value);
    REQUIRE_FALSE(locale);
    const auto layout = jrpgmaker::ui::ParseEditorLayout(
        nlohmann::json{{"schema", 1}, {"id", "editor.workspace"},
                       {"root", {{"type", "Panel"}, {"id", "root"},
                                  {"children", {{{"type", "Text"}, {"id", "root"}}}}}}});
    REQUIRE_FALSE(layout);
}

TEST_CASE("editor manifest type errors remain structured", "[ui][editor]") {
    auto document = Manifest();
    document["default_locale"] = 7;
    const auto result = jrpgmaker::ui::ParseEditorManifest(document);
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(result.errors.empty());
}

TEST_CASE("editor resources reject layout node overflow", "[ui][editor]") {
    nlohmann::json node = {{"type", "Panel"}, {"id", "root"}};
    for (std::size_t i = 0; i < jrpgmaker::ui::kMaxEditorLayoutNodes; ++i)
        node["children"].push_back({{"type", "Panel"}, {"id", "node" + std::to_string(i)}});
    const auto layout = jrpgmaker::ui::ParseEditorLayout(
        nlohmann::json{{"schema", 1}, {"id", "editor.workspace"}, {"root", node}});
    REQUIRE_FALSE(layout);
}

TEST_CASE("editor theme resolves semantic tokens and recipe states", "[ui][editor]") {
    nlohmann::json document = {
        {"schema", 1},
        {"id", "editor.default"},
        {"colors", {{"canvas", "#1F2326"}, {"panel", "#343635"}, {"accent", "#26CDCB"}}},
        {"dimensions", {{"space.md", 8.0}, {"radius.md", 4.0}}},
        {"semantic_tokens", {{"surface.canvas", "canvas"}, {"surface.panel", "panel"},
                              {"text.accent", "accent"}}}};
    document["recipes"]["panel"] = {{"normal", "surface.panel"}, {"hover", "surface.panel"},
                                        {"pressed", "surface.panel"}, {"focused", "surface.panel"},
                                        {"disabled", "surface.panel"}};
    const auto theme = jrpgmaker::ui::ParseEditorTheme(document);
    REQUIRE(theme);
    REQUIRE(theme.value.colors.at("accent").g == 205);
    REQUIRE(theme.value.recipes.at("panel").states.size() == 5);
}

TEST_CASE("editor theme rejects invalid color and missing recipe state", "[ui][editor]") {
    auto document = nlohmann::json{{"schema", 1}, {"id", "editor.high-contrast"},
                                   {"colors", {{"canvas", "not-a-color"}}},
                                   {"dimensions", {{"space.md", 8.0}}},
                                   {"semantic_tokens", {{"surface.canvas", "canvas"}}}};
    document["recipes"]["panel"] = {{"normal", "surface.canvas"}};
    const auto theme = jrpgmaker::ui::ParseEditorTheme(document);
    REQUIRE_FALSE(theme);
    REQUIRE_FALSE(theme.errors.empty());
}

TEST_CASE("editor action map rejects duplicate keys and parses resource actions", "[ui][editor]") {
    const auto actions = jrpgmaker::ui::ParseEditorActionMap(nlohmann::json{
        {"schema", 1}, {"id", "editor.actions"},
        {"actions", {{"save", {"Ctrl+S"}}, {"open", {"Ctrl+O"}}}}});
    REQUIRE(actions);
    REQUIRE(actions.value.actions.at("save").front() == "Ctrl+S");
    const auto duplicate = jrpgmaker::ui::ParseEditorActionMap(nlohmann::json{
        {"schema", 1}, {"id", "editor.actions"},
        {"actions", {{"save", {"Ctrl+S"}}, {"open", {"Ctrl+S"}}}}});
    REQUIRE_FALSE(duplicate);
}

TEST_CASE("committed editor resources form a valid startup set", "[ui][editor]") {
    const auto root = std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR);
    const auto manifest = jrpgmaker::ui::ParseEditorManifest(ReadJson(root / "editor.json"));
    REQUIRE(manifest);
    const auto locale = jrpgmaker::ui::ParseEditorLocale(
        ReadJson(root / "locales/zh-CN.json"), manifest.value);
    const auto theme = jrpgmaker::ui::ParseEditorTheme(
        ReadJson(root / "themes/editor_default.json"));
    const auto high_contrast = jrpgmaker::ui::ParseEditorTheme(
        ReadJson(root / "themes/editor_high_contrast.json"));
    const auto layout = jrpgmaker::ui::ParseEditorLayout(
        ReadJson(root / "layouts/editor_workspace.json"));
    const auto action_map = jrpgmaker::ui::ParseEditorActionMap(
        ReadJson(root / "actions/editor.json"));
    REQUIRE(locale);
    REQUIRE(theme);
    REQUIRE(high_contrast);
    REQUIRE(layout);
    REQUIRE(action_map);
}

TEST_CASE("editor startup loader aggregates a complete resource bundle", "[ui][editor]") {
    const auto result = jrpgmaker::ui::LoadEditorResources(
        std::filesystem::path(JRPGMAKER_EDITOR_RESOURCE_DIR));
    REQUIRE(result);
    REQUIRE(result.bundle->manifest.default_theme == "editor.default");
    REQUIRE(result.bundle->locale.locale == "zh-CN");
    REQUIRE(result.bundle->action_map.actions.size() == 8);
    REQUIRE(result.bundle->layout.id == "editor.workspace");
}
