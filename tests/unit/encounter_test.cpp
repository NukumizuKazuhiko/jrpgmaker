#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/domain/encounter.hpp"

namespace {

jrpgmaker::domain::EventScript DemoEvents() {
    return jrpgmaker::domain::ParseEventScriptText(R"json(
        {"schema":1,"events":[
          {"id":"reward","instructions":[{"op":"dialog","speaker":"guide","text_key":"reward.text"}]}
        ]}
    )json");
}

} // namespace

TEST_CASE("encounter parser validates schema, fields and event targets",
          "[domain][encounter][p5]") {
    const auto points = jrpgmaker::domain::ParseEncounterPoints(nlohmann::json::parse(R"json(
        {"schema":1,"encounters":[{"id":"point","position":[0,1,0],"radius":2,
          "encounter_id":"demo","results":{"resolved":"reward"}}]}
    )json"));
    REQUIRE(points.size() == 1);
    REQUIRE(points.front().encounter_id == "demo");
    REQUIRE_NOTHROW(jrpgmaker::domain::ValidateEncounterTargets(points, DemoEvents()));

    auto invalid = points;
    invalid.front().result_event_ids["resolved"] = "missing";
    REQUIRE_THROWS(jrpgmaker::domain::ValidateEncounterTargets(invalid, DemoEvents()));
    REQUIRE_THROWS(jrpgmaker::domain::ParseEncounterPoints(
        nlohmann::json{{"schema", 2}, {"encounters", nlohmann::json::array()}}));
    REQUIRE_THROWS(jrpgmaker::domain::ParseEncounterPoints(nlohmann::json{
        {"schema", 1},
        {"encounters",
         nlohmann::json::array({nlohmann::json{{"id", "duplicate"},
                                               {"position", {0, 0, 0}},
                                               {"radius", 1},
                                               {"encounter_id", "a"},
                                               {"results", {{"done", "reward"}}}},
                                nlohmann::json{{"id", "duplicate"},
                                               {"position", {1, 0, 0}},
                                               {"radius", 1},
                                               {"encounter_id", "b"},
                                               {"results", {{"done", "reward"}}}}})}}));
}

TEST_CASE("encounter system selects nearest point and publishes only on entry",
          "[domain][encounter][p5]") {
    const auto points = jrpgmaker::domain::ParseEncounterPoints(nlohmann::json::parse(R"json(
        {"schema":1,"encounters":[
          {"id":"far","position":[1,0,0],"radius":2,"encounter_id":"far","results":{"done":"reward"}},
          {"id":"near","position":[0,0,0],"radius":2,"encounter_id":"near","results":{"done":"reward"}}
        ]}
    )json"));
    jrpgmaker::core::EventBus bus;
    std::vector<jrpgmaker::domain::EncounterRequested> requests;
    bus.Subscribe<jrpgmaker::domain::EncounterRequested>(
        [&requests](const auto& request) { requests.push_back(request); });
    jrpgmaker::domain::EncounterSystem system(points, bus);

    system.Update({0.0f, 0.0f, 0.0f});
    system.Update({0.0f, 0.0f, 0.0f});
    REQUIRE(requests.size() == 1);
    REQUIRE(requests.front().point_id == "near");

    system.Update({10.0f, 0.0f, 0.0f});
    system.Update({1.0f, 0.0f, 0.0f});
    REQUIRE(requests.size() == 2);
    REQUIRE(requests.back().point_id == "far");
}
