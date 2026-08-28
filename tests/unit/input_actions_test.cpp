#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/core/input_actions.hpp"

TEST_CASE("input action map parses stable action IDs and bindings", "[input][p9]") {
    const auto result = jrpgmaker::core::ParseInputActionMap(nlohmann::json::parse(R"json({
        "schema": 1,
        "actions": [
            {"id":"move.forward","keys":["W"]},
            {"id":"extension.confirm","keys":["E","Space"]}
        ]
    })json"));
    REQUIRE(result);
    REQUIRE(result.map->Find("move.forward") != nullptr);
    REQUIRE(result.map->Find("extension.confirm")->keys.size() == 2);
    REQUIRE(result.map->Find("missing") == nullptr);
}

TEST_CASE("input action map rejects invalid schema, duplicate IDs and empty keys", "[input][p9]") {
    REQUIRE_FALSE(jrpgmaker::core::ParseInputActionMap(
        nlohmann::json{{"schema", 2}, {"actions", nlohmann::json::array()}}));
    REQUIRE_FALSE(jrpgmaker::core::ParseInputActionMap(nlohmann::json::parse(R"json({
        "schema":1,"actions":[
          {"id":"confirm","keys":["E"]},
          {"id":"confirm","keys":["Space"]}
        ]
    })json")));
    REQUIRE_FALSE(jrpgmaker::core::ParseInputActionMap(nlohmann::json::parse(R"json({
        "schema":1,"actions":[{"id":"confirm","keys":[""]}]
    })json")));
}
