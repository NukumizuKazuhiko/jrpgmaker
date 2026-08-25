#include "jrpgmaker/domain/flag_trigger.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace jrpgmaker::domain {

namespace {

[[noreturn]] void RaiseParseError(const std::string& message) {
    throw std::invalid_argument("flag trigger parse error: " + message);
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

} // namespace

FlagTriggerTable ParseFlagTriggers(const nlohmann::json& document) {
    if (!document.is_object()) {
        RaiseParseError("document must be a JSON object");
    }

    const int schema = document.value("schema", 0);
    if (schema != kFlagTriggerSchemaVersion) {
        RaiseParseError("unsupported schema version " + std::to_string(schema) + " (expected " +
                        std::to_string(kFlagTriggerSchemaVersion) + ")");
    }

    if (!document.contains("triggers") || !document["triggers"].is_array()) {
        RaiseParseError("'triggers' must be an array");
    }

    FlagTriggerTable table;
    table.schema = schema;
    table.triggers.reserve(document["triggers"].size());
    std::unordered_map<std::string, std::string> seen_flags;
    for (const auto& trigger_node : document["triggers"]) {
        FlagTrigger trigger;
        trigger.flag = RequireString(trigger_node, "flag", "trigger");
        trigger.target_event_id = RequireString(trigger_node, "target_event_id", "trigger");
        if (trigger.flag.empty()) {
            RaiseParseError("trigger has an empty flag name");
        }
        if (trigger.target_event_id.empty()) {
            RaiseParseError("trigger for flag '" + trigger.flag + "' has an empty target_event_id");
        }
        const auto [it, inserted] = seen_flags.emplace(trigger.flag, trigger.target_event_id);
        if (!inserted) {
            RaiseParseError("duplicate trigger for flag '" + trigger.flag +
                            "' (already bound to event '" + it->second + "')");
        }
        table.triggers.push_back(std::move(trigger));
    }
    return table;
}

void FlagTriggerSystem::OnFlagChanged(const FlagChanged& change) {
    // Look up a binding for this flag.
    const FlagTrigger* binding = nullptr;
    for (const FlagTrigger& trigger : table_.triggers) {
        if (trigger.flag == change.flag) {
            binding = &trigger;
            break;
        }
    }
    if (binding == nullptr) {
        return;
    }

    if (!change.value) {
        // Falling edge: re-arm the binding.
        armed_[change.flag] = false;
        return;
    }

    // Rising edge: fire only if not already armed (repeated set true no-op).
    if (armed_[change.flag]) {
        return;
    }
    armed_[change.flag] = true;
    callback_(binding->target_event_id);
}

} // namespace jrpgmaker::domain