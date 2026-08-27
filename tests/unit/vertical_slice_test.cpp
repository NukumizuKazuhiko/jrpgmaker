#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/vertical_slice.hpp"

TEST_CASE("vertical slice requires thirty minutes and resolves event targets",
          "[vertical_slice][p7]") {
    const auto slice = jrpgmaker::domain::ParseVerticalSliceDefinition(
        {{"schema", 1},
         {"id", "demo"},
         {"beats", {{{"id", "one"}, {"duration_seconds", 1800.0}, {"target_event_id", "start"}}}}});
    REQUIRE(slice.total_duration_seconds == 1800.0);

    const auto script = jrpgmaker::domain::ParseEventScript(
        {{"schema", 1},
         {"events", {{{"id", "start"}, {"instructions", nlohmann::json::array()}}}}});
    REQUIRE_NOTHROW(jrpgmaker::domain::ValidateVerticalSliceTargets(slice, script));
    REQUIRE_THROWS(jrpgmaker::domain::ValidateVerticalSliceTargets(
        slice, jrpgmaker::domain::ParseEventScript({{"schema", 1}, {"events", {}}})));
}

TEST_CASE("vertical slice rejects short and duplicate beats", "[vertical_slice][p7]") {
    const nlohmann::json short_slice = {
        {"schema", 1},
        {"id", "demo"},
        {"beats", {{{"id", "one"}, {"duration_seconds", 1.0}, {"target_event_id", "start"}}}}};
    REQUIRE_THROWS(jrpgmaker::domain::ParseVerticalSliceDefinition(short_slice));

    const nlohmann::json duplicate_slice = {
        {"schema", 1},
        {"id", "demo"},
        {"beats",
         {{{"id", "one"}, {"duration_seconds", 900.0}, {"target_event_id", "start"}},
          {{"id", "one"}, {"duration_seconds", 900.0}, {"target_event_id", "start"}}}}};
    REQUIRE_THROWS(jrpgmaker::domain::ParseVerticalSliceDefinition(duplicate_slice));
}
