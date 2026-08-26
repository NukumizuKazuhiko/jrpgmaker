#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/core/pathfinding.hpp"

using jrpgmaker::core::GridCell;
using jrpgmaker::core::NavigationGrid;

TEST_CASE("navigation grid finds a deterministic four-way path", "[core][pathfinding]") {
    NavigationGrid grid(3, 3, {0.0f, 0.0f}, 2.0f,
                        {true, true, true, true, false, true, true, true, true});

    const auto result = grid.FindPath({0, 1}, {2, 1});

    REQUIRE(result.succeeded());
    REQUIRE(result.cells == std::vector<GridCell>{{0, 1}, {0, 0}, {1, 0}, {2, 0}, {2, 1}});
    REQUIRE(grid.CellToWorld({1, 2}) == glm::vec3{3.0f, 0.0f, 5.0f});
    REQUIRE(grid.WorldToCell({3.9f, 0.0f, 4.1f}) == GridCell{1, 2});
}

TEST_CASE("navigation grid returns no path for blocked endpoints or unreachable goals",
          "[core][pathfinding]") {
    NavigationGrid grid(2, 2, {0.0f, 0.0f}, 1.0f, {true, false, false, true});

    REQUIRE(grid.FindPath({0, 0}, {1, 1}).failure == jrpgmaker::core::PathFailure::kUnreachable);
    REQUIRE(grid.FindPath({1, 0}, {1, 1}).failure == jrpgmaker::core::PathFailure::kStartBlocked);
}

TEST_CASE("navigation grid reports distinct endpoint and search failures", "[core][pathfinding]") {
    NavigationGrid grid(2, 1, {0.0f, 0.0f}, 1.0f, {true, true});

    REQUIRE(grid.FindPath({-1, 0}, {1, 0}).failure ==
            jrpgmaker::core::PathFailure::kStartOutOfBounds);
    REQUIRE(grid.FindPath({0, 0}, {2, 0}).failure ==
            jrpgmaker::core::PathFailure::kGoalOutOfBounds);
    REQUIRE(grid.FindPath({0, 0}, {1, 0}, 1).failure == jrpgmaker::core::PathFailure::kSearchLimit);
    REQUIRE(grid.FindPath({1, 0}, {1, 0}).cells == std::vector<GridCell>{{1, 0}});
}
