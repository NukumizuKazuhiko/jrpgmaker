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
#include "jrpgmaker/domain/event_lint.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/flag_trigger.hpp"
#include "jrpgmaker/domain/interaction.hpp"

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

// Lints a single file. Returns false if the file fails to parse or has at
// least one lint error. Warnings are printed but do not fail the file.
bool LintFile(const std::filesystem::path& path) {
    const nlohmann::json document = LoadJson(path);
    if (document.is_null()) {
        return false;
    }

    jrpgmaker::domain::EventScript script;
    try {
        script = jrpgmaker::domain::ParseEventScript(document);
    } catch (const std::invalid_argument& error) {
        std::cerr << path.string() << ": " << error.what() << '\n';
        return false;
    }

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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: eventlint <file.json> [file2.json ...]\n"
                  << "       eventlint --check-triggers <events.json> <triggers.json>\n";
        return 2;
    }

    if (std::string(argv[1]) == "--check-triggers") {
        if (argc != 4) {
            std::cerr << "eventlint --check-triggers requires <events.json> <triggers.json>\n";
            return 2;
        }
        return CheckTriggerReferences(argv[2], argv[3]) ? 0 : 1;
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
