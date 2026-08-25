#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "jrpgmaker/domain/flag_store.hpp"
#include "jrpgmaker/domain/lua_binding.hpp"

using jrpgmaker::domain::FlagStore;
using jrpgmaker::domain::LuaScriptEngine;

TEST_CASE("lua binding reads and writes flags", "[domain][lua]") {
    FlagStore flags;
    flags.Set("starting", true);
    std::vector<std::string> triggered;
    LuaScriptEngine engine(flags, [&](const std::string& id) {
        triggered.push_back(id);
        return true;
    });

    REQUIRE(engine.Run("assert(flags.get('starting') == true)\n"
                       "flags.set('from_lua', true)\n"
                       "assert(flags.get('from_lua') == true)\n"));
    REQUIRE(engine.last_error().empty());
    REQUIRE(flags.Get("from_lua"));
}

TEST_CASE("lua binding triggers events via the host callback", "[domain][lua]") {
    FlagStore flags;
    std::vector<std::string> triggered;
    LuaScriptEngine engine(flags, [&](const std::string& id) {
        triggered.push_back(id);
        return true;
    });

    REQUIRE(engine.Run("local ok = events.run('boss_battle')\nassert(ok == true)\n"));
    REQUIRE(triggered == std::vector<std::string>{"boss_battle"});
}

TEST_CASE("lua binding propagates a declined event trigger as false", "[domain][lua]") {
    FlagStore flags;
    LuaScriptEngine engine(flags, [&](const std::string&) { return false; });

    REQUIRE(engine.Run("assert(events.run('busy') == false)\n"));
}

TEST_CASE("lua binding reports script errors", "[domain][lua]") {
    FlagStore flags;
    LuaScriptEngine engine(flags, [&](const std::string&) { return true; });

    REQUIRE_FALSE(engine.Run("this is not valid lua"));
    REQUIRE_FALSE(engine.last_error().empty());
}

TEST_CASE("lua binding calls a defined global function", "[domain][lua]") {
    FlagStore flags;
    LuaScriptEngine engine(flags, [&](const std::string&) { return true; });

    REQUIRE(engine.Run("function act()\n  flags.set('acted', true)\nend\n"));
    REQUIRE(engine.Call("act"));
    REQUIRE(flags.Get("acted"));
}

TEST_CASE("lua binding reports a missing function", "[domain][lua]") {
    FlagStore flags;
    LuaScriptEngine engine(flags, [&](const std::string&) { return true; });

    REQUIRE_FALSE(engine.Call("does_not_exist"));
    REQUIRE_FALSE(engine.last_error().empty());
}

TEST_CASE("lua binding does not expose the event instruction set (escape hatch only)",
          "[domain][lua]") {
    FlagStore flags;
    LuaScriptEngine engine(flags, [&](const std::string&) { return true; });

    // The restricted surface is `flags` + `events` + `log`; there must be no
    // way to reach instruction parsing/execution from Lua.
    REQUIRE_FALSE(engine.Run("assert(instructions ~= nil)"));
    REQUIRE_FALSE(engine.Run("assert(schema ~= nil)"));
    REQUIRE_FALSE(engine.Run("assert(event_runner ~= nil)"));
    REQUIRE_FALSE(engine.Run("assert(parse ~= nil)"));
    // And the two public tables expose only their intended functions.
    REQUIRE(engine.Run("assert(type(flags.get) == 'function')\n"
                       "assert(type(flags.set) == 'function')\n"
                       "assert(type(events.run) == 'function')\n"
                       "assert(type(log) == 'function')\n"
                       "assert(flags.parse == nil and flags.execute == nil)\n"
                       "assert(events.parse == nil and events.start == nil)\n"));
}