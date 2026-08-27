// eventlint CLI: validates event script JSON files against the schema v1
// contract plus cross-event consistency checks (duplicate ids, unwritten
// branch flags, blocking instructions nested in branch/option).
//
// Usage:
//   eventlint <file.json> [file2.json ...]
//   eventlint --check-triggers <events.json> <triggers.json>
//   eventlint --check-map <navigation.json> <collision.json> <interaction.json>
//                     <camera.json> <events.json>
//
// Exit code:
//   0  all files clean (schema parses, no lint errors; warnings allowed)
//   1  at least one file has a parse error or lint error
//   2  usage error

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/core/map_data.hpp"
#include "jrpgmaker/domain/encounter.hpp"
#include "jrpgmaker/domain/event_lint.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/flag_trigger.hpp"
#include "jrpgmaker/domain/interaction.hpp"
#include "jrpgmaker/domain/schedule.hpp"
#include "jrpgmaker/domain/vertical_slice.hpp"

namespace {

// Loads and parses a JSON file. Returns an empty document on failure (the
// error is reported to stderr).
nlohmann::json LoadJson(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << path.string() << ": cannot open file\n";
        return {};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    try {
        return nlohmann::json::parse(buffer.str());
    } catch (const nlohmann::json::exception& error) {
        std::cerr << path.string() << ": JSON parse error: " << error.what() << '\n';
        return {};
    }
}

// Lints a parsed script. Warnings are printed but do not fail the file.
bool LintParsedScript(const std::filesystem::path& path,
                      const jrpgmaker::domain::EventScript& script) {
    const std::vector<jrpgmaker::domain::LintIssue> issues =
        jrpgmaker::domain::LintEventScript(script);
    if (issues.empty()) {
        std::cout << path.string() << ": clean\n";
        return true;
    }

    bool ok = true;
    for (const auto& issue : issues) {
        const std::string location =
            issue.event_id.empty() ? "(file)" : "event '" + issue.event_id + "'";
        std::cout << path.string() << ": " << location << ": "
                  << jrpgmaker::domain::ToString(issue.severity) << ": " << issue.message << '\n';
        if (issue.severity == jrpgmaker::domain::LintSeverity::kError) {
            ok = false;
        }
    }
    return ok;
}

// Lints a single file. Returns false if the file fails to parse or has at
// least one lint error.
bool LintFile(const std::filesystem::path& path) {
    const nlohmann::json document = LoadJson(path);
    if (document.is_null()) {
        return false;
    }

    try {
        return LintParsedScript(path, jrpgmaker::domain::ParseEventScript(document));
    } catch (const std::invalid_argument& error) {
        std::cerr << path.string() << ": " << error.what() << '\n';
        return false;
    }
}

// Cross-checks a trigger table against an event script: every trigger's target
// event id must exist in the script, otherwise firing the trigger silently
// no-ops (EventRunner::Start returns false). Returns false on any failure.
bool CheckTriggerReferences(const std::filesystem::path& events_path,
                            const std::filesystem::path& triggers_path) {
    const nlohmann::json events_document = LoadJson(events_path);
    if (events_document.is_null()) {
        return false;
    }
    jrpgmaker::domain::EventScript script;
    try {
        script = jrpgmaker::domain::ParseEventScript(events_document);
        if (!LintParsedScript(events_path, script))
            return false;
    } catch (const std::invalid_argument& error) {
        std::cerr << events_path.string() << ": " << error.what() << '\n';
        return false;
    }

    const nlohmann::json triggers_document = LoadJson(triggers_path);
    if (triggers_document.is_null()) {
        return false;
    }
    jrpgmaker::domain::FlagTriggerTable table;
    try {
        table = jrpgmaker::domain::ParseFlagTriggers(triggers_document);
    } catch (const std::invalid_argument& error) {
        std::cerr << triggers_path.string() << ": " << error.what() << '\n';
        return false;
    }

    bool ok = true;
    for (const jrpgmaker::domain::FlagTrigger& trigger : table.triggers) {
        const bool exists = std::any_of(script.events.begin(), script.events.end(),
                                        [&](const jrpgmaker::domain::Event& event) {
                                            return event.id == trigger.target_event_id;
                                        });
        if (!exists) {
            std::cerr << triggers_path.string() << ": trigger on flag '" << trigger.flag
                      << "' targets unknown event '" << trigger.target_event_id << "' (not in "
                      << events_path.string() << "); firing would silently no-op\n";
            ok = false;
        }
    }
    if (ok) {
        std::cout << triggers_path.string() << ": all trigger targets resolve in "
                  << events_path.string() << '\n';
    }
    return ok;
}

bool CheckMapReferences(const std::filesystem::path& navigation_path,
                        const std::filesystem::path& collision_path,
                        const std::filesystem::path& interaction_path,
                        const std::filesystem::path& camera_path,
                        const std::filesystem::path& events_path) {
    const nlohmann::json navigation_document = LoadJson(navigation_path);
    const nlohmann::json collision_document = LoadJson(collision_path);
    const nlohmann::json interaction_document = LoadJson(interaction_path);
    const nlohmann::json camera_document = LoadJson(camera_path);
    const nlohmann::json events_document = LoadJson(events_path);
    if (navigation_document.is_null() || collision_document.is_null() ||
        interaction_document.is_null() || camera_document.is_null() || events_document.is_null()) {
        return false;
    }

    try {
        const auto navigation = jrpgmaker::core::ParseNavigationGrid(navigation_document);
        const auto collision = jrpgmaker::core::ParseCollisionAabbs(collision_document);
        const auto interactions = jrpgmaker::domain::ParseInteractionPoints(interaction_document);
        const auto camera = jrpgmaker::core::ParseCameraRigData(camera_document);
        jrpgmaker::domain::EventScript script =
            jrpgmaker::domain::ParseEventScript(events_document);
        if (!LintParsedScript(events_path, script))
            return false;
        jrpgmaker::domain::ValidateInteractionTargets(interactions, script);
        std::cout << "map data clean: " << navigation.width() << "x" << navigation.height() << ", "
                  << collision.size() << " collision boxes, " << interactions.size()
                  << " interactions, " << camera.fixed_regions.size() << " camera regions\n";
        return true;
    } catch (const std::exception& error) {
        std::cerr << "map data lint error: " << error.what() << '\n';
        return false;
    }
}

bool CheckScheduleReferences(const std::filesystem::path& calendar_path,
                             const std::filesystem::path& schedule_path,
                             const std::filesystem::path& events_path) {
    const nlohmann::json calendar_document = LoadJson(calendar_path);
    const nlohmann::json schedule_document = LoadJson(schedule_path);
    const nlohmann::json events_document = LoadJson(events_path);
    if (calendar_document.is_null() || schedule_document.is_null() || events_document.is_null()) {
        return false;
    }
    try {
        const auto calendar_result = jrpgmaker::core::ParseCalendarDefinition(calendar_document);
        if (!calendar_result.ok) {
            throw std::invalid_argument(calendar_result.error);
        }
        const auto schedule =
            jrpgmaker::domain::ParseScheduleTable(schedule_document, calendar_result.calendar);
        const auto script = jrpgmaker::domain::ParseEventScript(events_document);
        if (!LintParsedScript(events_path, script))
            return false;
        jrpgmaker::domain::ValidateScheduleTargets(schedule, script);
        std::cout << "schedule data clean: " << schedule.entries.size() << " entries for "
                  << calendar_result.calendar.id << '\n';
        return true;
    } catch (const std::exception& error) {
        std::cerr << "schedule data lint error: " << error.what() << '\n';
        return false;
    }
}

bool CheckCutsceneReferences(const std::filesystem::path& cutscene_path,
                             const std::filesystem::path& events_path) {
    const nlohmann::json cutscene_document = LoadJson(cutscene_path);
    const nlohmann::json events_document = LoadJson(events_path);
    if (cutscene_document.is_null() || events_document.is_null())
        return false;
    try {
        const auto timeline = jrpgmaker::core::ParseCutsceneTimeline(cutscene_document);
        const auto script = jrpgmaker::domain::ParseEventScript(events_document);
        if (!LintParsedScript(events_path, script))
            return false;
        jrpgmaker::domain::ValidateCutsceneTargets(timeline, script);
        std::cout << "cutscene data clean: " << timeline.cues.size() << " cues\n";
        return true;
    } catch (const std::exception& error) {
        std::cerr << "cutscene data lint error: " << error.what() << '\n';
        return false;
    }
}

bool CheckVerticalSliceReferences(const std::filesystem::path& slice_path,
                                  const std::filesystem::path& events_path) {
    const nlohmann::json slice_document = LoadJson(slice_path);
    const nlohmann::json events_document = LoadJson(events_path);
    if (slice_document.is_null() || events_document.is_null())
        return false;
    try {
        const auto slice = jrpgmaker::domain::ParseVerticalSliceDefinition(slice_document);
        const auto script = jrpgmaker::domain::ParseEventScript(events_document);
        if (!LintParsedScript(events_path, script))
            return false;
        jrpgmaker::domain::ValidateVerticalSliceTargets(slice, script);
        std::cout << "vertical slice data clean: " << slice.beats.size() << " beats, "
                  << slice.total_duration_seconds << " seconds\n";
        return true;
    } catch (const std::exception& error) {
        std::cerr << "vertical slice data lint error: " << error.what() << '\n';
        return false;
    }
}

bool CheckEncounterReferences(const std::filesystem::path& encounter_path,
                              const std::filesystem::path& events_path) {
    const nlohmann::json encounter_document = LoadJson(encounter_path);
    const nlohmann::json events_document = LoadJson(events_path);
    if (encounter_document.is_null() || events_document.is_null())
        return false;
    try {
        const auto encounters = jrpgmaker::domain::ParseEncounterPoints(encounter_document);
        const auto script = jrpgmaker::domain::ParseEventScript(events_document);
        if (!LintParsedScript(events_path, script))
            return false;
        jrpgmaker::domain::ValidateEncounterTargets(encounters, script);
        std::cout << "encounter data clean: " << encounters.size() << " points\n";
        return true;
    } catch (const std::exception& error) {
        std::cerr << "encounter data lint error: " << error.what() << '\n';
        return false;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr
            << "usage: eventlint <file.json> [file2.json ...]\n"
            << "       eventlint --check-triggers <events.json> <triggers.json>\n"
            << "       eventlint --check-schedule <calendar.json> <schedule.json> <events.json>\n"
            << "       eventlint --check-cutscene <cutscene.json> <events.json>\n"
            << "       eventlint --check-encounter <encounter.json> <events.json>\n"
            << "       eventlint --check-vertical-slice <slice.json> <events.json>\n";
        return 2;
    }

    if (std::string(argv[1]) == "--check-triggers") {
        if (argc != 4) {
            std::cerr << "eventlint --check-triggers requires <events.json> <triggers.json>\n";
            return 2;
        }
        return CheckTriggerReferences(argv[2], argv[3]) ? 0 : 1;
    }

    if (std::string(argv[1]) == "--check-schedule") {
        if (argc != 5) {
            std::cerr << "eventlint --check-schedule requires <calendar.json> <schedule.json> "
                         "<events.json>\n";
            return 2;
        }
        return CheckScheduleReferences(argv[2], argv[3], argv[4]) ? 0 : 1;
    }

    if (std::string(argv[1]) == "--check-cutscene") {
        if (argc != 4) {
            std::cerr << "eventlint --check-cutscene requires <cutscene.json> <events.json>\n";
            return 2;
        }
        return CheckCutsceneReferences(argv[2], argv[3]) ? 0 : 1;
    }

    if (std::string(argv[1]) == "--check-vertical-slice") {
        if (argc != 4) {
            std::cerr << "eventlint --check-vertical-slice requires <slice.json> <events.json>\n";
            return 2;
        }
        return CheckVerticalSliceReferences(argv[2], argv[3]) ? 0 : 1;
    }

    if (std::string(argv[1]) == "--check-encounter") {
        if (argc != 4) {
            std::cerr << "eventlint --check-encounter requires <encounter.json> <events.json>\n";
            return 2;
        }
        return CheckEncounterReferences(argv[2], argv[3]) ? 0 : 1;
    }

    if (std::string(argv[1]) == "--check-map") {
        if (argc != 7) {
            std::cerr << "eventlint --check-map requires <navigation.json> <collision.json> "
                         "<interaction.json> <camera.json> <events.json>\n";
            return 2;
        }
        return CheckMapReferences(argv[2], argv[3], argv[4], argv[5], argv[6]) ? 0 : 1;
    }

    bool all_ok = true;
    for (int i = 1; i < argc; ++i) {
        if (!LintFile(argv[i])) {
            all_ok = false;
        }
    }
    return all_ok ? 0 : 1;
}
