#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/core/map_data.hpp"

TEST_CASE("map data parsers load versioned navigation and collision data", "[core][map]") {
    const auto grid =
        jrpgmaker::core::ParseNavigationGrid(nlohmann::json{{"schema", 1},
                                                            {"width", 2},
                                                            {"height", 1},
                                                            {"origin", {0, 0, 0}},
                                                            {"cell_size", 1.0},
                                                            {"walkable", {true, false}}});
    REQUIRE(grid.IsWalkable({0, 0}));
    REQUIRE_FALSE(grid.IsWalkable({1, 0}));

    const auto obstacles = jrpgmaker::core::ParseCollisionAabbs(
        nlohmann::json{{"schema", 1}, {"obstacles", {{{"min", {0, 0, 0}}, {"max", {1, 2, 1}}}}}});
    REQUIRE(obstacles.size() == 1);

    const auto camera = jrpgmaker::core::ParseCameraRigData(nlohmann::json{
        {"schema", 1},
        {"third_person",
         {{"distance", 4.0}, {"height", 2.0}, {"pitch_degrees", 15.0}, {"smoothing_seconds", 0.1}}},
        {"fixed_regions",
         {{{"id", "west"},
           {"min", {-1, -1, -1}},
           {"max", {1, 1, 1}},
           {"priority", 1},
           {"eye", {3, 3, 3}},
           {"target", {0, 0, 0}}}}}});
    REQUIRE(camera.fixed_regions.size() == 1);
}

TEST_CASE("map data parsers reject malformed geometry", "[core][map]") {
    REQUIRE_THROWS(jrpgmaker::core::ParseCollisionAabbs(
        nlohmann::json{{"schema", 1}, {"obstacles", {{{"min", {1, 0, 0}}, {"max", {0, 2, 1}}}}}}));
    REQUIRE_THROWS(jrpgmaker::core::ParseCameraRigData(nlohmann::json{{"schema", 1}}));
    REQUIRE_THROWS(jrpgmaker::core::ParseCameraRigData(
        nlohmann::json{{"schema", 1},
                       {"third_person", {{"distance", 4.0}, {"height", 2.0}}},
                       {"fixed_regions", nlohmann::json::array()}}));
}
