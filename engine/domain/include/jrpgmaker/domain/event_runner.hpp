#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "jrpgmaker/core/event_bus.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/flag_store.hpp"

namespace jrpgmaker::domain {

// A dialog line or prompt awaiting host acknowledgement (P3 dialog model).
// Published on the event bus when the runner reaches a dialog/choice
// instruction; the runner then blocks until AdvanceDialog is called.
//
// For a plain dialog (no choice) options is empty and the host calls
// AdvanceDialog() with no index. For a choice, options lists the selectable
// branches; the host calls AdvanceDialog(index) with the picked option.
struct DialogRequested {
    std::string event_id;
    std::string speaker;
    std::string text_key;
    std::vector<DialogOption> options;
};

// Structured dialog state projection emitted after each handshake advance.
// Presentation consumes this (docs/01 owner map: domain projects, ui renders).
// Typewriter progress / portrait slots / i18n text tables are presentation
// concerns deferred to the UI / HarfBuzz subtasks (docs/02 P3).
struct DialogState {
    std::string event_id;
    std::string speaker;
    std::string text_key;
    std::vector<DialogOption> options;
};

// Executes events from a parsed EventScript against a FlagStore (P3 contract).
// Driven by the host at a fixed time step: Start(event_id) begins an event,
// Tick(delta) advances it. Set_flag/clear_flag mutate the store; branch picks
// a sub-sequence from flag state; wait blocks until its seconds elapse; dialog
// and choice block on the host: the runner publishes DialogRequested and pauses
// until AdvanceDialog (plain) or AdvanceDialog(index) (choice) resolves it.
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
    // (no-op). May complete the event (IsFinished() becomes true). While a
    // dialog is awaiting acknowledgement, Tick is a no-op (the runner blocks
    // on the host, not on wall-clock).
    void Tick(double delta_seconds);

    // Acknowledges the current blocking dialog (plain line, no options),
    // allowing the runner to continue. Calling when no dialog is pending is
    // an error.
    void AdvanceDialog();

    // Resolves the current blocking choice with the picked option index,
    // executing that option's instruction sequence. Out-of-range index is an
    // error.
    void AdvanceDialog(std::size_t option_index);

    // True while a dialog/choice awaits host acknowledgement.
    bool IsDialogPending() const { return dialog_pending_; }

    // True between Start() and event completion (an active, unfinished event).
    bool IsActive() const { return active_.has_value() && !finished_; }

    // True once the current event has run to completion.
    bool IsFinished() const { return active_.has_value() && finished_; }

    // The event currently executing, if any.
    const std::string& active_event_id() const;

private:
    bool AdvanceOne(double delta_seconds);
    void RunSequence(const std::vector<Instruction>& sequence, std::size_t& index);
    void BeginDialog(std::string speaker, std::string text_key, std::vector<DialogOption> options);

    const EventScript& script_;
    FlagStore& flags_;
    core::EventBus& bus_;

    std::optional<Event> active_;
    std::size_t index_ = 0;
    bool finished_ = false;
    double wait_remaining_ = 0.0;
    bool dialog_pending_ = false;
    std::vector<DialogOption> pending_options_;
};

} // namespace jrpgmaker::domain