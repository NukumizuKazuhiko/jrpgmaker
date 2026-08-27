#pragma once

#include <string>
#include <vector>

#include "jrpgmaker/core/cutscene.hpp"
#include "jrpgmaker/domain/event_script.hpp"

namespace jrpgmaker::domain {

// Severity of a lint finding. Errors must be fixed; warnings flag likely
// mistakes that may still be legitimate at runtime (e.g. a flag written by
// the host rather than by the script itself).
enum class LintSeverity { kError, kWarning };

// A single lint finding. `event_id` is empty for file-level findings.
struct LintIssue {
    LintSeverity severity = LintSeverity::kError;
    std::string event_id;
    std::string message;
};

inline constexpr const char* ToString(LintSeverity severity) {
    return severity == LintSeverity::kError ? "error" : "warning";
}

// Runs consistency checks over a parsed event script that the single-file
// parser cannot express (they span events or re-check nesting rules the
// interpreter rejects at runtime):
//   - duplicate event ids (error)
//   - empty event id / flag name / speaker / text key (error)
//   - blocking instructions (dialog/choice/wait) nested inside a branch or
//     choice option (error; the runner throws std::logic_error at runtime, so
//     lint surfaces the same contract violation at authoring time)
//   - a branch flag that is never written anywhere in the script (warning:
//     may be injected by the host, but an unwritten flag is also a classic
//     misspelling / dead-branch symptom)
// Reference resolution against i18n text tables is out of scope for v1
// (docs/01: the text table schema lands with the i18n subtask).
std::vector<LintIssue> LintEventScript(const EventScript& script);

// Validates cutscene cue event IDs against the domain event script at the data
// boundary; core owns cue timing, domain owns event identity.
void ValidateCutsceneTargets(const core::CutsceneTimeline& timeline,
                             const EventScript& event_script);

} // namespace jrpgmaker::domain
