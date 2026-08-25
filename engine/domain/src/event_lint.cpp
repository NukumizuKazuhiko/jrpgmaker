#include "jrpgmaker/domain/event_lint.hpp"

#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace jrpgmaker::domain {

namespace {

// Recursively visits every instruction in a sequence, including those nested
// inside branch arms and choice options, invoking `visit` on each.
void WalkInstructions(const std::vector<Instruction>& instructions,
                      const std::function<void(const Instruction&)>& visit) {
    for (const Instruction& instruction : instructions) {
        visit(instruction);
        if (const auto* branch = std::get_if<BranchInstruction>(&instruction.op)) {
            WalkInstructions(branch->if_set, visit);
            WalkInstructions(branch->if_not_set, visit);
        } else if (const auto* choice = std::get_if<ChoiceInstruction>(&instruction.op)) {
            for (const DialogOption& option : choice->options) {
                WalkInstructions(option.instructions, visit);
            }
        }
    }
}

bool IsBlocking(const Instruction& instruction) {
    return std::holds_alternative<DialogInstruction>(instruction.op) ||
           std::holds_alternative<ChoiceInstruction>(instruction.op) ||
           std::holds_alternative<WaitInstruction>(instruction.op);
}

bool ContainsBlocking(const std::vector<Instruction>& sequence) {
    bool found = false;
    WalkInstructions(sequence, [&](const Instruction& instruction) {
        if (IsBlocking(instruction)) {
            found = true;
        }
    });
    return found;
}

} // namespace

std::vector<LintIssue> LintEventScript(const EventScript& script) {
    std::vector<LintIssue> issues;
    const auto add_error = [&](const std::string& event_id, std::string message) {
        issues.push_back(LintIssue{
            .severity = LintSeverity::kError, .event_id = event_id, .message = std::move(message)});
    };
    const auto add_warning = [&](const std::string& event_id, std::string message) {
        issues.push_back(LintIssue{.severity = LintSeverity::kWarning,
                                   .event_id = event_id,
                                   .message = std::move(message)});
    };

    // Duplicate / empty event ids.
    std::set<std::string> seen_ids;
    for (const Event& event : script.events) {
        if (event.id.empty()) {
            add_error("", "event has an empty id");
        } else if (!seen_ids.insert(event.id).second) {
            add_error(event.id, "duplicate event id '" + event.id + "'");
        }
    }

    // Every flag written anywhere in the script (set_flag/clear_flag), for the
    // unwritten-branch-flag check.
    std::set<std::string> written_flags;
    for (const Event& event : script.events) {
        WalkInstructions(event.instructions, [&](const Instruction& instruction) {
            if (const auto* set_flag = std::get_if<SetFlagInstruction>(&instruction.op)) {
                written_flags.insert(set_flag->flag);
            }
        });
    }

    for (const Event& event : script.events) {
        WalkInstructions(event.instructions, [&](const Instruction& instruction) {
            if (const auto* branch = std::get_if<BranchInstruction>(&instruction.op)) {
                if (branch->flag.empty()) {
                    add_error(event.id, "branch has an empty flag name");
                } else if (!written_flags.contains(branch->flag)) {
                    add_warning(event.id, "branch reads flag '" + branch->flag +
                                              "' which is never written by any event; "
                                              "verify the spelling or that the host injects it");
                }
            } else if (const auto* set_flag = std::get_if<SetFlagInstruction>(&instruction.op)) {
                if (set_flag->flag.empty()) {
                    add_error(event.id, "set_flag/clear_flag has an empty flag name");
                }
            } else if (const auto* dialog = std::get_if<DialogInstruction>(&instruction.op)) {
                if (dialog->speaker.empty()) {
                    add_error(event.id, "dialog has an empty speaker");
                }
                if (dialog->text_key.empty()) {
                    add_error(event.id, "dialog has an empty text_key");
                }
            } else if (const auto* choice = std::get_if<ChoiceInstruction>(&instruction.op)) {
                if (choice->prompt_text_key.empty()) {
                    add_error(event.id, "choice has an empty prompt_text_key");
                }
                for (const DialogOption& option : choice->options) {
                    if (option.text_key.empty()) {
                        add_error(event.id, "choice has an option with an empty text_key");
                    }
                }
            }
        });

        // Blocking instructions are top-level only (docs/01 schema v1): a
        // dialog/choice/wait inside a branch arm or choice option would break
        // the linear runner. The runner throws std::logic_error at runtime;
        // surface the same contract violation here, at authoring time.
        for (const Instruction& instruction : event.instructions) {
            if (const auto* branch = std::get_if<BranchInstruction>(&instruction.op)) {
                if (ContainsBlocking(branch->if_set)) {
                    add_error(event.id, "branch if_set contains a blocking instruction "
                                        "(dialog/choice/wait are top-level only)");
                }
                if (ContainsBlocking(branch->if_not_set)) {
                    add_error(event.id, "branch if_not_set contains a blocking instruction "
                                        "(dialog/choice/wait are top-level only)");
                }
            } else if (const auto* choice = std::get_if<ChoiceInstruction>(&instruction.op)) {
                for (const DialogOption& option : choice->options) {
                    if (ContainsBlocking(option.instructions)) {
                        add_error(event.id, "choice option '" + option.text_key +
                                                "' contains a blocking instruction "
                                                "(dialog/choice/wait are top-level only)");
                    }
                }
            }
        }
    }

    return issues;
}

} // namespace jrpgmaker::domain