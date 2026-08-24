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
    return true;
}

void EventRunner::Tick(double delta_seconds) {
    if (!active_.has_value() || finished_) {
        return;
    }
    if (delta_seconds < 0.0) {
        throw std::invalid_argument("EventRunner::Tick delta must be non-negative");
    }

    // A wait with remaining time blocks the runner; consume delta and return.
    if (wait_remaining_ > 0.0) {
        wait_remaining_ -= delta_seconds;
        if (wait_remaining_ <= 0.0) {
            wait_remaining_ = 0.0;
            ++index_; // leave the wait instruction
        } else {
            return;
        }
    }

    AdvanceOne(delta_seconds);
}

bool EventRunner::AdvanceOne(double delta_seconds) {
    Event& event = *active_;
    // Run until we either block (wait) or exhaust the sequence.
    while (index_ < event.instructions.size()) {
        const Instruction& instruction = event.instructions[index_];
        if (const auto* set_flag = std::get_if<SetFlagInstruction>(&instruction.op)) {
            flags_.Set(set_flag->flag, set_flag->value);
            ++index_;
            continue;
        }
        if (const auto* branch = std::get_if<BranchInstruction>(&instruction.op)) {
            const bool set = flags_.Get(branch->flag);
            const std::vector<Instruction>& chosen = set ? branch->if_set : branch->if_not_set;
            std::size_t nested_index = 0;
            std::string event_id = event.id;
            RunSequence(chosen, nested_index, event_id);
            ++index_;
            continue;
        }
        if (const auto* dialog = std::get_if<DialogInstruction>(&instruction.op)) {
            bus_.Publish(DialogRequested{
                .event_id = event.id, .speaker = dialog->speaker, .text_key = dialog->text_key});
            ++index_;
            continue;
        }
        if (const auto* wait = std::get_if<WaitInstruction>(&instruction.op)) {
            if (wait->seconds > 0.0) {
                wait_remaining_ = wait->seconds - delta_seconds;
                if (wait_remaining_ <= 0.0) {
                    wait_remaining_ = 0.0;
                    ++index_;
                    continue;
                }
                // Blocked: do not advance index; Tick subtracts remaining delta.
                return false;
            }
            ++index_;
            continue;
        }
        // Unknown instruction kinds are a contract violation (schema v1 is closed).
        throw std::logic_error("EventRunner: unknown instruction kind");
    }
    finished_ = true;
    return false;
}

void EventRunner::RunSequence(const std::vector<Instruction>& sequence, std::size_t& index,
                              std::string& event_id) {
    while (index < sequence.size()) {
        const Instruction& instruction = sequence[index];
        if (const auto* set_flag = std::get_if<SetFlagInstruction>(&instruction.op)) {
            flags_.Set(set_flag->flag, set_flag->value);
            ++index;
            continue;
        }
        if (const auto* branch = std::get_if<BranchInstruction>(&instruction.op)) {
            const bool set = flags_.Get(branch->flag);
            const std::vector<Instruction>& chosen = set ? branch->if_set : branch->if_not_set;
            std::size_t nested_index = 0;
            RunSequence(chosen, nested_index, event_id);
            ++index;
            continue;
        }
        if (const auto* dialog = std::get_if<DialogInstruction>(&instruction.op)) {
            bus_.Publish(DialogRequested{
                .event_id = event_id, .speaker = dialog->speaker, .text_key = dialog->text_key});
            ++index;
            continue;
        }
        if (std::get_if<WaitInstruction>(&instruction.op) != nullptr) {
            // Nested waits are not supported in schema v1: a blocking wait
            // inside a branch would break the linear runner. Reject loudly
            // rather than silently eliding script semantics.
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