#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/ui/editor_resources.hpp"

namespace {

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
