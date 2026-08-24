#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "jrpgmaker/domain/flag_store.hpp"

TEST_CASE("flag store defaults to false", "[domain][flag_store]") {
    jrpgmaker::domain::FlagStore flags;
    REQUIRE_FALSE(flags.Get("any.flag"));
    REQUIRE(flags.live_count() == 0);
}

TEST_CASE("flag store sets and clears", "[domain][flag_store]") {
    jrpgmaker::domain::FlagStore flags;
    flags.Set("quest.act1.done", true);
    REQUIRE(flags.Get("quest.act1.done"));
    REQUIRE(flags.live_count() == 1);

    flags.Set("quest.act1.done", false);
    REQUIRE_FALSE(flags.Get("quest.act1.done"));
    REQUIRE(flags.live_count() == 0);
}

TEST_CASE("flag store keeps distinct flags independent", "[domain][flag_store]") {
    jrpgmaker::domain::FlagStore flags;
    flags.Set("a", true);
    flags.Set("b", true);
    flags.Set("a", false);
    REQUIRE_FALSE(flags.Get("a"));
    REQUIRE(flags.Get("b"));
    REQUIRE(flags.live_count() == 1);
}

TEST_CASE("flag store rejects empty names", "[domain][flag_store]") {
    jrpgmaker::domain::FlagStore flags;
    REQUIRE_THROWS_AS(flags.Set("", true), std::invalid_argument);
}