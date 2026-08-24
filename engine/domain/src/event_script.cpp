#include "jrpgmaker/domain/event_script.hpp"

#include <stdexcept>
#include <string>

namespace jrpgmaker::domain {

namespace {

[[noreturn]] void RaiseParseError(const std::string& message) {
    throw std::invalid_argument("event script parse error: " + message);
}

std::string RequireString(const nlohmann::json& object, const char* key, const char* context) {
    if (!object.contains(key)) {
        RaiseParseError(std::string("missing '") + key + "' in " + context);
    }
    if (!object[key].is_string()) {
        RaiseParseError(std::string("'") + key + "' must be a string in " + context);
    }
    return object[key].get<std::string>();
}

std::vector<Instruction> ParseSequence(const nlohmann::json& node, const char* key,
                                       const char* context);

Instruction ParseInstruction(const nlohmann::json& node) {
    if (!node.is_object() || !node.contains("op") || !node["op"].is_string()) {
        RaiseParseError("each instruction must be an object with a string 'op'");
    }
    const std::string op = node["op"].get<std::string>();

    if (op == "set_flag") {
        return Instruction{SetFlagInstruction{.flag = RequireString(node, "flag", op.c_str()),
                                              .value = node.value("value", true)}};
    }
    if (op == "clear_flag") {
        return Instruction{
            SetFlagInstruction{.flag = RequireString(node, "flag", op.c_str()), .value = false}};
    }
    if (op == "branch") {
        const std::string flag = RequireString(node, "flag", op.c_str());
        const std::vector<Instruction> if_set = ParseSequence(node, "if_set", op.c_str());
        const std::vector<Instruction> if_not_set = ParseSequence(node, "if_not_set", op.c_str());
        return Instruction{
            BranchInstruction{.flag = flag, .if_set = if_set, .if_not_set = if_not_set}};
    }
    if (op == "dialog") {
        return Instruction{
            DialogInstruction{.speaker = RequireString(node, "speaker", op.c_str()),
                              .text_key = RequireString(node, "text_key", op.c_str())}};
    }
    if (op == "wait") {
        const double seconds = node.value("seconds", 0.0);
        if (seconds < 0.0) {
            RaiseParseError("'wait' seconds must be non-negative");
        }
        return Instruction{WaitInstruction{.seconds = seconds}};
    }

    RaiseParseError("unknown op '" + op + "'");
}

std::vector<Instruction> ParseSequence(const nlohmann::json& node, const char* key,
                                       const char* context) {
    if (!node.contains(key)) {
        RaiseParseError(std::string("missing '") + key + "' in " + context);
    }
    if (!node[key].is_array()) {
        RaiseParseError(std::string("'") + key + "' must be an array in " + context);
    }
    std::vector<Instruction> instructions;
    instructions.reserve(node[key].size());
    for (const auto& child : node[key]) {
        instructions.push_back(ParseInstruction(child));
    }
    return instructions;
}

} // namespace

EventScript ParseEventScript(const nlohmann::json& document) {
    if (!document.is_object()) {
        RaiseParseError("document must be a JSON object");
    }

    const int schema = document.value("schema", 0);
    if (schema != kEventSchemaVersion) {
        RaiseParseError("unsupported schema version " + std::to_string(schema) + " (expected " +
                        std::to_string(kEventSchemaVersion) + ")");
    }

    if (!document.contains("events") || !document["events"].is_array()) {
        RaiseParseError("'events' must be an array");
    }

    EventScript script;
    script.schema = schema;
    script.events.reserve(document["events"].size());
    for (const auto& event_node : document["events"]) {
        Event event;
        event.id = RequireString(event_node, "id", "event");
        if (!event_node.contains("instructions") || !event_node["instructions"].is_array()) {
            RaiseParseError("event '" + event.id + "' must have an 'instructions' array");
        }
        event.instructions.reserve(event_node["instructions"].size());
        for (const auto& instruction_node : event_node["instructions"]) {
            event.instructions.push_back(ParseInstruction(instruction_node));
        }
        script.events.push_back(std::move(event));
    }
    return script;
}

} // namespace jrpgmaker::domain