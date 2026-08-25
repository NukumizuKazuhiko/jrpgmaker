#pragma once

#include <functional>
#include <memory>
#include <string>

#include "jrpgmaker/domain/flag_store.hpp"

namespace jrpgmaker::domain {

// Lua script engine (P3 escape hatch, docs/01: "Lua(sol2) is only an escape
// hatch for complex logic; Lua entry still passes schema/lint, and must not
// bypass the instruction set to privately script flows").
//
// The engine exposes a deliberately restricted API surface to Lua - it is NOT
// a general scripting host:
//   - flags.get(name)      -> bool   (read FlagStore)
//   - flags.set(name,val)  -> void   (write FlagStore; the host owns projecting
//                                     host-injected flag changes per docs/01)
//   - events.run(id)       -> bool   (asks the host to start an event on the
//                                     EventRunner; returns false when the host
//                                     declines, e.g. an event is already active)
//   - log(message)         -> void   (diagnostic)
// No instruction parsing/execution, no schema access, no EventRunner internals
// are reachable from Lua - business flows stay in data files / EventRunner.
class LuaScriptEngine {
public:
    // Host callback used to start an event from Lua (events.run). Returns true
    // if the event was started. The host decides when starting is legal
    // (EventRunner throws if an event is already active).
    using EventTrigger = std::function<bool(const std::string&)>;

    LuaScriptEngine(FlagStore& flags, EventTrigger event_trigger);
    ~LuaScriptEngine();

    LuaScriptEngine(const LuaScriptEngine&) = delete;
    LuaScriptEngine& operator=(const LuaScriptEngine&) = delete;
    LuaScriptEngine(LuaScriptEngine&&) noexcept;
    LuaScriptEngine& operator=(LuaScriptEngine&&) noexcept;

    // Compiles and runs `source`. Returns false on a Lua error (the error
    // message is reported via the log callback / stored in last_error()).
    bool Run(const std::string& source);

    // Executes a previously defined global Lua function by name. Returns false
    // if the function is missing or errors.
    bool Call(const std::string& function_name);

    // Last error message (empty on success), useful for diagnostics.
    const std::string& last_error() const;

    // FlagStore the engine reads/writes (for tests / host wiring).
    FlagStore& flags();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace jrpgmaker::domain