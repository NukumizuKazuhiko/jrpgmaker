#include "jrpgmaker/domain/event_runner.hpp"

#include <algorithm>
#include <stdexcept>

namespace jrpgmaker::domain {

bool EventRunner::Start(const std::string& event_id) {
    if (IsActive()) {
        throw std::logic_error("EventRunner::Start while an event is already active");
    }
    const auto it = std::find_if(script_.events.begin(), script_.events.end(),
                                 [&](const Event& event) { return event.id == event_id; });
    if (it == script_.events.end()) {
        return false;
    }
    active_ = *it;
    index_ = 0;
    finished_ = false;
    wait_remaining_ = 0.0;
    dialog_pending_ = false;
    pending_options_.clear();
    bus_.Publish(EventStarted{.event_id = event_id});
    return true;
}

void EventRunner::Tick(double delta_seconds) {
    if (!active_.has_value() || finished_ || dialog_pending_) {
        return;
    }
    if (delta_seconds < 0.0) {
        throw std::invalid_argument("EventRunner::Tick delta must be non-negative");
    }

    // A wait with remaining time blocks the runner; consume delta. When it
    // elapses mid-delta, the leftover time carries into the next instruction so
    // wall-clock time is not double-spent (docs/01: fixed-step runner).
    if (wait_remaining_ > 0.0) {
        wait_remaining_ -= delta_seconds;
        if (wait_remaining_ <= 0.0) {
            double spillover = -wait_remaining_;
            wait_remaining_ = 0.0;
            ++index_; // leave the wait instruction
            AdvanceOne(spillover);
        }
        return;
    }

    AdvanceOne(delta_seconds);
}

void EventRunner::AdvanceDialog() {
    if (!dialog_pending_ || !pending_options_.empty()) {
        throw std::logic_error("EventRunner::AdvanceDialog called with no pending plain dialog");
    }
    dialog_pending_ = false;
    ++index_; // leave the dialog instruction
    double zero = 0.0;
    AdvanceOne(zero);
}

void EventRunner::AdvanceDialog(std::size_t option_index) {
    if (!dialog_pending_ || pending_options_.empty()) {
        throw std::logic_error("EventRunner::AdvanceDialog(index) called with no pending choice");
    }
    if (option_index >= pending_options_.size()) {
        throw std::out_of_range("EventRunner::AdvanceDialog index out of range");
    }
    const DialogOption chosen = std::move(pending_options_[option_index]);
    dialog_pending_ = false;
    pending_options_.clear();
    ++index_; // leave the choice instruction
    std::size_t option_index_local = 0;
    RunSequence(chosen.instructions, option_index_local);
    double zero = 0.0;
    AdvanceOne(zero);
}

void EventRunner::BeginDialog(std::string speaker, std::string text_key,
                              std::vector<DialogOption> options) {
    dialog_pending_ = true;
    pending_options_ = std::move(options);
    bus_.Publish(DialogRequested{
        .event_id = active_->id,
        .speaker = std::move(speaker),
        .text_key = std::move(text_key),
        .options = pending_options_,
    });
}

bool EventRunner::AdvanceOne(double& delta_seconds) {
    if (dialog_pending_) {
        return false;
    }
    Event& event = *active_;
    // Run until we either block (wait / dialog) or exhaust the sequence.
    while (index_ < event.instructions.size()) {
        const Instruction& instruction = event.instructions[index_];
        if (const auto* set_flag = std::get_if<SetFlagInstruction>(&instruction.op)) {
            flags_.Set(set_flag->flag, set_flag->value);
            bus_.Publish(FlagChanged{.flag = set_flag->flag, .value = set_flag->value});
            ++index_;
            continue;
        }
        if (const auto* branch = std::get_if<BranchInstruction>(&instruction.op)) {
            const bool set = flags_.Get(branch->flag);
            const std::vector<Instruction>& chosen = set ? branch->if_set : branch->if_not_set;
            std::size_t nested_index = 0;
            RunSequence(chosen, nested_index);
            ++index_;
            continue;
        }
        if (const auto* dialog = std::get_if<DialogInstruction>(&instruction.op)) {
            BeginDialog(dialog->speaker, dialog->text_key, {});
            return false;
        }
        if (const auto* choice = std::get_if<ChoiceInstruction>(&instruction.op)) {
            BeginDialog(/*speaker=*/std::string{}, choice->prompt_text_key, choice->options);
            return false;
        }
        if (const auto* wait = std::get_if<WaitInstruction>(&instruction.op)) {
            if (wait->seconds > 0.0) {
                if (delta_seconds >= wait->seconds) {
                    // The wait elapses within this tick; consume only its share
                    // and let the leftover carry into the next instruction.
                    delta_seconds -= wait->seconds;
                    ++index_;
                    continue;
                }
                // Blocked: hold the remaining time; Tick subtracts it later.
                wait_remaining_ = wait->seconds - delta_seconds;
                return false;
            }
            ++index_;
            continue;
        }
        // Unknown instruction kinds are a contract violation (schema v1 is closed).
        throw std::logic_error("EventRunner: unknown instruction kind");
    }
    finished_ = true;
    bus_.Publish(EventFinished{.event_id = active_->id});
    return false;
}

void EventRunner::RunSequence(const std::vector<Instruction>& sequence, std::size_t& index) {
    while (index < sequence.size()) {
        const Instruction& instruction = sequence[index];
        if (const auto* set_flag = std::get_if<SetFlagInstruction>(&instruction.op)) {
            flags_.Set(set_flag->flag, set_flag->value);
            bus_.Publish(FlagChanged{.flag = set_flag->flag, .value = set_flag->value});
            ++index;
            continue;
        }
        if (const auto* branch = std::get_if<BranchInstruction>(&instruction.op)) {
            const bool set = flags_.Get(branch->flag);
            const std::vector<Instruction>& chosen = set ? branch->if_set : branch->if_not_set;
            std::size_t nested_index = 0;
            RunSequence(chosen, nested_index);
            ++index;
            continue;
        }
        if (std::get_if<DialogInstruction>(&instruction.op) != nullptr) {
            // Blocking instructions are top-level only in schema v1: a dialog
            // inside a branch/option would break the linear runner. Reject
            // loudly rather than silently eliding script semantics.
            throw std::logic_error(
                "EventRunner: dialog inside a branch/option is not supported in schema v1");
        }
        if (std::get_if<ChoiceInstruction>(&instruction.op) != nullptr) {
            throw std::logic_error(
                "EventRunner: choice inside a branch/option is not supported in schema v1");
        }
        if (std::get_if<WaitInstruction>(&instruction.op) != nullptr) {
            // Nested waits are not supported in schema v1 (blocking inside a
            // branch would break the linear runner). Reject loudly.
            throw std::logic_error(
                "EventRunner: wait inside a branch is not supported in schema v1");
        }
        throw std::logic_error("EventRunner: unknown instruction kind");
    }
}

const std::string& EventRunner::active_event_id() const {
    static const std::string kEmpty;
    return active_.has_value() ? active_->id : kEmpty;
}

} // namespace jrpgmaker::domain