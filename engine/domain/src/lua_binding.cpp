#include "jrpgmaker/domain/lua_binding.hpp"

#include <string>
#include <utility>

#include <sol/sol.hpp>

#include "jrpgmaker/domain/event_runner.hpp"

namespace jrpgmaker::domain {

struct LuaScriptEngine::Impl {
    FlagStore& flags;
    core::EventBus& bus;
    EventTrigger event_trigger;
    sol::state lua;
    std::string error;

    Impl(FlagStore& flags_in, core::EventBus& bus_in, EventTrigger trigger)
        : flags(flags_in), bus(bus_in), event_trigger(std::move(trigger)) {
        lua.open_libraries(sol::lib::base);

        // Restricted API surface (docs/01: escape hatch only, no bypassing the
        // event instruction set).
        sol::table flags_api = lua.create_table();
        flags_api["get"] = [this](const std::string& name) { return flags.Get(name); };
        flags_api["set"] = [this](const std::string& name, bool value) {
            flags.Set(name, value);
            bus.Publish(FlagChanged{.flag = name, .value = value});
        };
        lua["flags"] = flags_api;

        sol::table events_api = lua.create_table();
        events_api["run"] = [this](const std::string& event_id) { return event_trigger(event_id); };
        lua["events"] = events_api;

        lua["log"] = [this](const std::string& message) { (void) message; };
    }
};

LuaScriptEngine::LuaScriptEngine(FlagStore& flags, core::EventBus& bus, EventTrigger event_trigger)
    : impl_(std::make_unique<Impl>(flags, bus, std::move(event_trigger))) {}

LuaScriptEngine::~LuaScriptEngine() = default;
LuaScriptEngine::LuaScriptEngine(LuaScriptEngine&&) noexcept = default;
LuaScriptEngine& LuaScriptEngine::operator=(LuaScriptEngine&&) noexcept = default;

bool LuaScriptEngine::Run(const std::string& source) {
    impl_->error.clear();
    try {
        const sol::protected_function_result result = impl_->lua.safe_script(source);
        if (!result.valid()) {
            const sol::error error = result;
            impl_->error = error.what();
            return false;
        }
        return true;
    } catch (const sol::error& error) {
        impl_->error = error.what();
        return false;
    }
}

bool LuaScriptEngine::Call(const std::string& function_name) {
    impl_->error.clear();
    try {
        const sol::protected_function function = impl_->lua[function_name];
        if (!function.valid()) {
            impl_->error = "Lua function '" + function_name + "' not found";
            return false;
        }
        const sol::protected_function_result result = function();
        if (!result.valid()) {
            const sol::error error = result;
            impl_->error = error.what();
            return false;
        }
        return true;
    } catch (const sol::error& error) {
        impl_->error = error.what();
        return false;
    }
}

const std::string& LuaScriptEngine::last_error() const {
    return impl_->error;
}

FlagStore& LuaScriptEngine::flags() {
    return impl_->flags;
}

} // namespace jrpgmaker::domain