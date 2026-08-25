#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/core/event_bus.hpp"
#include "jrpgmaker/domain/event_runner.hpp"

namespace jrpgmaker::domain {

inline constexpr int kFlagTriggerSchemaVersion = 1;

// A single flag-trigger binding (P3 event trigger wiring): when `flag`
// transitions to true, the target event should be started. Triggers are pure
// data (docs/01: data files express events); the trigger table ships as JSON
// and is parsed into this struct.
struct FlagTrigger {
    std::string flag;
    std::string target_event_id;
};

// A parsed trigger table (one JSON document). The schema field must equal
// kFlagTriggerSchemaVersion; unknown fields are rejected at parse time.
struct FlagTriggerTable {
    int schema = kFlagTriggerSchemaVersion;
    std::vector<FlagTrigger> triggers;
};

// Parses a trigger table JSON document. Throws nlohmann::json::exception /
// std::invalid_argument on malformed input, unknown schema version, empty flag
// or target event id, or a duplicate flag binding (two triggers on the same
// flag are ambiguous).
FlagTriggerTable ParseFlagTriggers(const nlohmann::json& document);

// Consumes FlagChanged projections and fires edge-triggered (false -> true)
// bindings. On a rising edge the registered callback is invoked with the target
// event id so the host can start it on the EventRunner.
//
// Edge semantics: repeated set_flag(true) on an already-true flag does not
// re-fire; the binding re-arms only when the flag returns to false. A flag that
// becomes false never fires. Unknown flags (no binding) are ignored.
//
// WIRING CONSTRAINT: the callback runs synchronously inside the EventRunner's
// Tick (FlagChanged is published while the source event is still completing),
// so the host MUST NOT call EventRunner::Start from the callback - it would
// throw "Start while an event is already active". The host queues the target
// event id and starts it at the next event boundary (standard game-loop
// pattern; see flag_trigger_test end-to-end wiring case).
class FlagTriggerSystem {
public:
    // `callback` receives the target event id of a fired trigger. Called on the
    // publisher's thread during FlagChanged delivery; must not re-enter the bus.
    using TriggerCallback = std::function<void(const std::string&)>;

    FlagTriggerSystem(const FlagTriggerTable& table, core::EventBus& bus, TriggerCallback callback)
        : table_(table), bus_(bus), callback_(std::move(callback)) {
        bus_.Subscribe<FlagChanged>([this](const FlagChanged& change) { OnFlagChanged(change); });
    }

    FlagTriggerSystem(const FlagTriggerSystem&) = delete;
    FlagTriggerSystem& operator=(const FlagTriggerSystem&) = delete;

private:
    void OnFlagChanged(const FlagChanged& change);

    const FlagTriggerTable& table_;
    core::EventBus& bus_;
    TriggerCallback callback_;
    // Flags currently holding true (rising edge already consumed).
    std::unordered_map<std::string, bool> armed_;
};

} // namespace jrpgmaker::domain