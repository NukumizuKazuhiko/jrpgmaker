#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "jrpgmaker/core/event_bus.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/flag_store.hpp"

namespace jrpgmaker::domain {

// Structured command emitted when a dialog beat is reached. Published to the
// event bus; presentation (future dialog model / UI) consumes it. The runner
// does not block on dialog in v0 (no UI acknowledgement exists yet) - it
// publishes the beat and continues. Blocking dialog awaits the P3 dialog
// model subtask (docs/02).
struct DialogRequested {
    std::string event_id;
    std::string speaker;
    std::string text_key;
};

// Executes events from a parsed EventScript against a FlagStore (P3 contract).
// Driven by the host at a fixed time step: Start(event_id) begins an event,
// Tick(delta) advances it. Set_flag/clear_flag mutate the store; branch picks
// a sub-sequence from flag state; wait blocks until its seconds elapse; dialog
// publishes DialogRequested to the bus and continues.
//
// Events execute sequentially with an explicit program counter; nesting beyond
// branch's two sub-sequences is not in schema v1. The runner holds no state
// that outlives a single event.
class EventRunner {
public:
    EventRunner(const EventScript& script, FlagStore& flags, core::EventBus& bus)
        : script_(script), flags_(flags), bus_(bus) {}

    EventRunner(const EventRunner&) = delete;
    EventRunner& operator=(const EventRunner&) = delete;

    // Starts executing the named event. Returns false if the event id is not in
    // the script (no state changes). Starting while an event is active is an
    // error: the host must wait for the active event to finish first.
    bool Start(const std::string& event_id);

    // Advances the active event by delta seconds. Safe to call when idle
    // (no-op). May complete the event (IsFinished() becomes true).
    void Tick(double delta_seconds);

    // True between Start() and event completion (an active, unfinished event).
    bool IsActive() const { return active_.has_value() && !finished_; }

    // True once the current event has run to completion.
    bool IsFinished() const { return active_.has_value() && finished_; }

    // The event currently executing, if any.
    const std::string& active_event_id() const;

private:
    bool AdvanceOne(double delta_seconds);
    void RunSequence(const std::vector<Instruction>& sequence, std::size_t& index,
                     std::string& event_id);

    const EventScript& script_;
    FlagStore& flags_;
    core::EventBus& bus_;

    std::optional<Event> active_;
    std::size_t index_ = 0;
    bool finished_ = false;
    double wait_remaining_ = 0.0;
};

} // namespace jrpgmaker::domain