#pragma once

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace jrpgmaker::domain {

// Event script schema version. Bumping this is a breaking contract change;
// scripts must declare the version they were authored against.
inline constexpr int kEventSchemaVersion = 1;

// Flag value: event-script switches that steer branches. Named flags live in
// FlagStore; the name is a stable contract (docs/01 event instruction set).
struct SetFlagInstruction {
    std::string flag;
    bool value = true;
};

// Blocking dialog beat: presentation renders speaker + text_key; the event
// runner pauses until the UI acknowledges (future dialog model subtask).
struct DialogInstruction {
    std::string speaker;
    std::string text_key;
};

// Blocking pause for a fixed number of seconds.
struct WaitInstruction {
    double seconds = 0.0;
};

// One executable instruction in an event. Schema v1 op set:
// set_flag / branch / dialog / wait.
struct Instruction;

// Conditionally execute one of two instruction sequences based on a flag.
struct BranchInstruction {
    std::string flag;
    std::vector<Instruction> if_set;
    std::vector<Instruction> if_not_set;
};

struct Instruction {
    std::variant<SetFlagInstruction, BranchInstruction, DialogInstruction, WaitInstruction> op;
};

// A named event: a sequence of instructions, triggerable by the host (area /
// interact / flag transitions are host concerns, docs/02 P3).
struct Event {
    std::string id;
    std::vector<Instruction> instructions;
};

// A parsed event script (one JSON file). The schema field must equal
// kEventSchemaVersion; unknown ops are rejected at parse time.
struct EventScript {
    int schema = kEventSchemaVersion;
    std::vector<Event> events;
};

// Parses a script JSON document into an EventScript. Throws
// nlohmann::json::exception / std::invalid_argument on malformed input,
// unknown ops, or wrong schema version. The script is data-owned by domain
// (docs/01: pure data files express events).
EventScript ParseEventScript(const nlohmann::json& document);

// Convenience overload: parse a JSON document loaded from text.
inline EventScript ParseEventScriptText(const std::string& text) {
    return ParseEventScript(nlohmann::json::parse(text));
}

} // namespace jrpgmaker::domain