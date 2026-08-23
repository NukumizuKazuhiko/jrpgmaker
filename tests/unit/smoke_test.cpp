#include <regex>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/core/version.hpp"

TEST_CASE("core reports its version", "[smoke]") {
    const std::string_view v = jrpgmaker::core::version();
    REQUIRE_FALSE(v.empty());
    REQUIRE(v == "0.0.1");
}

TEST_CASE("core version follows semantic versioning", "[smoke]") {
    const std::string_view v = jrpgmaker::core::version();
    const std::regex semver(R"(\d+\.\d+\.\d+)");
    REQUIRE(std::regex_match(v.begin(), v.end(), semver));
}
