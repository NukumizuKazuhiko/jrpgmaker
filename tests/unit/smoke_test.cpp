#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "jrpgmaker/core/version.hpp"

TEST_CASE("core reports its version", "[smoke]") {
    const std::string_view v = jrpgmaker::core::version();
    REQUIRE_FALSE(v.empty());
    REQUIRE(v == "0.0.1");
}

TEST_CASE("core version matches project version", "[smoke]") {
    constexpr std::string_view kExpected = "0.0.1";
    REQUIRE(jrpgmaker::core::version().size() == kExpected.size());
}
