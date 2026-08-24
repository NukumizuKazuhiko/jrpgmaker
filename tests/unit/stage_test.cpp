#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/core/stage.hpp"

TEST_CASE("stage runner executes systems in stage order", "[core][stage]") {
    jrpgmaker::core::StageRunner stages;

    std::vector<std::string> visited;
    stages.RegisterSystem(jrpgmaker::core::Stage::kInput,
                          {jrpgmaker::core::Stage::kInput, 0},
                          [&visited](double) { visited.push_back("input"); });
    stages.RegisterSystem(jrpgmaker::core::Stage::kRenderSubmit,
                          {jrpgmaker::core::Stage::kRenderSubmit, 0},
                          [&visited](double) { visited.push_back("render"); });
    stages.RegisterSystem(jrpgmaker::core::Stage::kDomainSim,
                          {jrpgmaker::core::Stage::kDomainSim, 0},
                          [&visited](double) { visited.push_back("sim"); });

    stages.Tick(1.0 / 60.0);

    REQUIRE(visited.size() == 3);
    REQUIRE(visited[0] == "input");
    REQUIRE(visited[1] == "sim");
    REQUIRE(visited[2] == "render");
}

TEST_CASE("stage runner executes within-stage systems in ascending order", "[core][stage]") {
    jrpgmaker::core::StageRunner stages;

    std::vector<std::string> visited;
    stages.RegisterSystem(jrpgmaker::core::Stage::kDomainSim,
                          {jrpgmaker::core::Stage::kDomainSim, 1},
                          [&visited](double) { visited.push_back("late"); });
    stages.RegisterSystem(jrpgmaker::core::Stage::kDomainSim,
                          {jrpgmaker::core::Stage::kDomainSim, 0},
                          [&visited](double) { visited.push_back("early"); });

    stages.Tick(1.0 / 60.0);

    REQUIRE(visited.size() == 2);
    REQUIRE(visited[0] == "early");
    REQUIRE(visited[1] == "late");
}

TEST_CASE("stage runner forwards delta seconds to systems", "[core][stage]") {
    jrpgmaker::core::StageRunner stages;

    double received = 0.0;
    stages.RegisterSystem(jrpgmaker::core::Stage::kAnimation,
                          {jrpgmaker::core::Stage::kAnimation, 0},
                          [&received](double delta) { received = delta; });

    stages.Tick(0.016);

    REQUIRE(received == Catch::Approx(0.016));
}

TEST_CASE("stage runner rejects duplicate order within a stage", "[core][stage]") {
    jrpgmaker::core::StageRunner stages;

    stages.RegisterSystem(jrpgmaker::core::Stage::kInput,
                          {jrpgmaker::core::Stage::kInput, 0}, [](double) {});
    REQUIRE_THROWS_AS(stages.RegisterSystem(jrpgmaker::core::Stage::kInput,
                                            {jrpgmaker::core::Stage::kInput, 0}, [](double) {}),
                      std::runtime_error);
}