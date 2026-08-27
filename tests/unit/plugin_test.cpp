#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/plugin/battle.hpp"
#include "jrpgmaker/plugin/plugin.hpp"

namespace {
class TestPlugin final : public jrpgmaker::plugin::IPlugin {};

class TestBattleSession final : public jrpgmaker::plugin::IBattleSession {
public:
    jrpgmaker::plugin::BattleAdvanceResult
    Advance(const jrpgmaker::plugin::BattleFrameInput& input) override {
        if (const auto error = jrpgmaker::plugin::ValidateBattleInput(input); error.has_value()) {
            return {.ok = false, .output = {}, .error = error};
        }
        finished_ = std::find(input.action_ids.begin(), input.action_ids.end(), "finish") !=
                    input.action_ids.end();
        return {.ok = true,
                .output = {.finished = finished_,
                           .result_key = finished_ ? "done" : "running",
                           .presentation_commands = {},
                           .opaque_payload = {}},
                .error = std::nullopt};
    }

    jrpgmaker::plugin::BattleSnapshot Snapshot() const override {
        return {.active = !finished_, .finished = finished_, .opaque_payload = {}};
    }

private:
    bool finished_ = false;
};

class TestBattlePlugin final : public jrpgmaker::plugin::IBattlePlugin {
public:
    jrpgmaker::plugin::BattleSessionCreateResult
    CreateSession(const jrpgmaker::plugin::BattleLaunchContext& context) override {
        if (const auto error = jrpgmaker::plugin::ValidateBattleLaunch(context);
            error.has_value()) {
            return {.session = nullptr, .error = error};
        }
        return {.session = std::make_unique<TestBattleSession>(), .error = std::nullopt};
    }
};

#ifndef JRPGMAKER_ASSET_DIR
#error "JRPGMAKER_ASSET_DIR must be defined by the build"
#endif

nlohmann::json Manifest(const char* id = "test.render") {
    return {{"schema", 1},
            {"id", id},
            {"type", "render_style"},
            {"version", 1},
            {"engine_contract", 1},
            {"data_roots", nlohmann::json::array({"data"})},
            {"capabilities", nlohmann::json::array({"opaque_materials"})}};
}
} // namespace

TEST_CASE("committed project and style manifests parse from disk", "[plugin][p6]") {
    const std::filesystem::path assets =
        std::filesystem::path(JRPGMAKER_ASSET_DIR).lexically_normal();
    const std::filesystem::path root = assets.parent_path();
    std::ifstream project_file(assets / "data/project_demo.json");
    REQUIRE(project_file.good());
    const auto project =
        jrpgmaker::plugin::ParseProjectManifest(nlohmann::json::parse(project_file));
    REQUIRE(project);
    REQUIRE(project.manifest->render_style == "sample.unlit");
    REQUIRE(project.manifest->material_document == "assets/data/material_demo.json");

    std::ifstream style_file(root / "plugins/sample_unlit/plugin.json");
    REQUIRE(style_file.good());
    const auto style = jrpgmaker::plugin::ParseManifest(nlohmann::json::parse(style_file));
    REQUIRE(style);
    REQUIRE(style.manifest->type == jrpgmaker::plugin::PluginType::kRenderStyle);
}

TEST_CASE("plugin manifest parses a render style descriptor", "[plugin]") {
    const auto result = jrpgmaker::plugin::ParseManifest(Manifest());
    REQUIRE(result);
    REQUIRE(result.manifest->id == "test.render");
    REQUIRE(result.manifest->type == jrpgmaker::plugin::PluginType::kRenderStyle);
}

TEST_CASE("plugin manifest reports missing and unknown fields", "[plugin]") {
    auto document = Manifest();
    document.erase("engine_contract");
    REQUIRE_FALSE(jrpgmaker::plugin::ParseManifest(document));
    document = Manifest();
    document["type"] = "unknown";
    const auto result = jrpgmaker::plugin::ParseManifest(document);
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == "manifest.type");
}

TEST_CASE("plugin registry creates registered adapters and rejects duplicates", "[plugin]") {
    jrpgmaker::plugin::PluginRegistry registry;
    const auto parsed = jrpgmaker::plugin::ParseManifest(Manifest());
    REQUIRE(parsed);
    REQUIRE_FALSE(
        registry.Register(*parsed.manifest, [] { return std::make_unique<TestPlugin>(); }));
    REQUIRE(registry.size() == 1);
    REQUIRE(registry.Create("test.render", jrpgmaker::plugin::PluginType::kRenderStyle));
    const auto duplicate =
        registry.Register(*parsed.manifest, [] { return std::make_unique<TestPlugin>(); });
    REQUIRE(duplicate.has_value());
    REQUIRE(duplicate->code == "registry.duplicate");
}

TEST_CASE("plugin registry rejects incompatible contracts and type mismatches", "[plugin]") {
    jrpgmaker::plugin::PluginRegistry registry;
    auto parsed = jrpgmaker::plugin::ParseManifest(Manifest("test.render"));
    REQUIRE(parsed);
    parsed.manifest->engine_contract = 2;
    REQUIRE(registry.Register(*parsed.manifest, [] { return std::make_unique<TestPlugin>(); }));
    parsed.manifest->engine_contract = 1;
    REQUIRE_FALSE(
        registry.Register(*parsed.manifest, [] { return std::make_unique<TestPlugin>(); }));
    const auto result = registry.Create("test.render", jrpgmaker::plugin::PluginType::kBattle);
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == "registry.type");
}

TEST_CASE("project manifest selects a render style by data", "[plugin][p6]") {
    const auto result = jrpgmaker::plugin::ParseProjectManifest(
        {{"schema", 1},
         {"id", "demo"},
         {"render_style", "test.render"},
         {"plugins", nlohmann::json::array({"test.render"})},
         {"data_roots", nlohmann::json::array({"data"})}});
    REQUIRE(result);
    REQUIRE(result.manifest->render_style == "test.render");

    jrpgmaker::plugin::PluginRegistry registry;
    const auto plugin = jrpgmaker::plugin::ParseManifest(Manifest());
    REQUIRE(plugin);
    REQUIRE_FALSE(
        registry.Register(*plugin.manifest, [] { return std::make_unique<TestPlugin>(); }));
    REQUIRE(jrpgmaker::plugin::CreateProjectRenderStyle(*result.manifest, registry));
}

TEST_CASE("project manifest selects a battle plugin by data", "[plugin][battle][p5]") {
    const auto result = jrpgmaker::plugin::ParseProjectManifest(
        {{"schema", 1},
         {"id", "demo"},
         {"render_style", "test.render"},
         {"battle_plugin", "test.battle"},
         {"plugins", nlohmann::json::array({"test.render", "test.battle"})},
         {"data_roots", nlohmann::json::array({"data"})}});
    REQUIRE(result);
    jrpgmaker::plugin::PluginRegistry registry;
    auto render = jrpgmaker::plugin::ParseManifest(Manifest());
    REQUIRE(render);
    REQUIRE_FALSE(
        registry.Register(*render.manifest, [] { return std::make_unique<TestPlugin>(); }));
    auto battle_document = Manifest("test.battle");
    battle_document["type"] = "battle";
    auto battle = jrpgmaker::plugin::ParseManifest(battle_document);
    REQUIRE(battle);
    REQUIRE_FALSE(
        registry.Register(*battle.manifest, [] { return std::make_unique<TestPlugin>(); }));
    REQUIRE_FALSE(jrpgmaker::plugin::ValidateProjectPlugins(*result.manifest, registry));
    REQUIRE(jrpgmaker::plugin::CreateProjectBattlePlugin(*result.manifest, registry));
}

TEST_CASE("project manifest rejects duplicate or unlisted render style IDs", "[plugin][p6]") {
    auto document =
        nlohmann::json{{"schema", 1},
                       {"id", "demo"},
                       {"render_style", "test.render"},
                       {"plugins", nlohmann::json::array({"test.render", "test.render"})},
                       {"data_roots", nlohmann::json::array({"data"})}};
    REQUIRE_FALSE(jrpgmaker::plugin::ParseProjectManifest(document));
    document["plugins"] = nlohmann::json::array({"other"});
    const auto result = jrpgmaker::plugin::ParseProjectManifest(document);
    REQUIRE_FALSE(result);
    REQUIRE(result.error->code == "project.render_style");
}

TEST_CASE("project manifest accepts a safe material document path", "[plugin][p6]") {
    auto document = nlohmann::json{{"schema", 1},
                                   {"id", "demo"},
                                   {"render_style", "test.render"},
                                   {"plugins", nlohmann::json::array({"test.render"})},
                                   {"data_roots", nlohmann::json::array({"data"})},
                                   {"material_document", "data/material.json"}};
    const auto result = jrpgmaker::plugin::ParseProjectManifest(document);
    REQUIRE(result);
    REQUIRE(result.manifest->material_document == "data/material.json");
    document["material_document"] = "../outside.json";
    REQUIRE_FALSE(jrpgmaker::plugin::ParseProjectManifest(document));
}

TEST_CASE("project manifest resolves registered plugins and existing data roots", "[plugin][p5]") {
    const auto result = jrpgmaker::plugin::ParseProjectManifest(
        {{"schema", 1},
         {"id", "demo"},
         {"render_style", "test.render"},
         {"plugins", nlohmann::json::array({"test.render"})},
         {"data_roots", nlohmann::json::array({"data"})}});
    REQUIRE(result);
    jrpgmaker::plugin::PluginRegistry registry;
    const auto plugin = jrpgmaker::plugin::ParseManifest(Manifest());
    REQUIRE(plugin);
    REQUIRE_FALSE(
        registry.Register(*plugin.manifest, [] { return std::make_unique<TestPlugin>(); }));
    REQUIRE_FALSE(jrpgmaker::plugin::ValidateProjectPlugins(*result.manifest, registry));
    REQUIRE_FALSE(jrpgmaker::plugin::ValidateProjectDataRoots(
        *result.manifest, std::filesystem::path(JRPGMAKER_ASSET_DIR)));
    REQUIRE(jrpgmaker::plugin::ValidateProjectDataRoots(*result.manifest,
                                                        std::filesystem::path("does-not-exist"))
                .has_value());
}

TEST_CASE("battle seam supports opaque actions and deterministic completion",
          "[plugin][battle][p5]") {
    TestBattlePlugin plugin;
    const auto created =
        plugin.CreateSession({.encounter_id = "demo.encounter", .opaque_payload = {}});
    REQUIRE(created);
    REQUIRE(created.session->Snapshot().active);
    auto& session = *created.session;

    const auto running = session.Advance({.delta_seconds = 1.0,
                                          .action_ids = {"wait"},
                                          .opaque_payload = {},
                                          .cancel_requested = false});
    REQUIRE(running);
    REQUIRE_FALSE(running.output.finished);

    const auto finished = session.Advance({.delta_seconds = 0.0,
                                           .action_ids = {"finish"},
                                           .opaque_payload = {},
                                           .cancel_requested = false});
    REQUIRE(finished);
    REQUIRE(finished.output.finished);
    REQUIRE(finished.output.result_key == "done");
    REQUIRE_FALSE(session.Snapshot().active);
    REQUIRE(session.Snapshot().finished);
}

TEST_CASE("battle seam rejects invalid and over-budget values", "[plugin][battle][p5]") {
    REQUIRE(jrpgmaker::plugin::ValidateBattleLaunch({}).has_value());
    REQUIRE(jrpgmaker::plugin::ValidateBattleInput({.delta_seconds = -1.0,
                                                    .action_ids = {},
                                                    .opaque_payload = {},
                                                    .cancel_requested = false})
                .has_value());
    REQUIRE(jrpgmaker::plugin::ValidateBattleInput(
                {.delta_seconds = 0.0,
                 .action_ids = std::vector<std::string>(33, "action"),
                 .opaque_payload = {},
                 .cancel_requested = false})
                .has_value());
    REQUIRE(
        jrpgmaker::plugin::ValidateBattleOutput(
            {.finished = true, .result_key = {}, .presentation_commands = {}, .opaque_payload = {}})
            .has_value());
    REQUIRE(jrpgmaker::plugin::ValidateBattleSnapshot(
                {.active = true, .finished = true, .opaque_payload = {}})
                .has_value());
}
