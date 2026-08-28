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
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/core/input_actions.hpp"
#include "jrpgmaker/core/map_data.hpp"
#include "jrpgmaker/domain/encounter.hpp"
#include "jrpgmaker/domain/event_lint.hpp"
#include "jrpgmaker/domain/event_script.hpp"
#include "jrpgmaker/domain/flag_trigger.hpp"
#include "jrpgmaker/domain/interaction.hpp"
#include "jrpgmaker/domain/localization.hpp"
#include "jrpgmaker/domain/schedule.hpp"
#include "jrpgmaker/domain/vertical_slice.hpp"
#include "jrpgmaker/plugin/plugin.hpp"
#include "jrpgmaker/plugins/register.hpp"

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

bool IsSafeResourcePath(const std::string& path) {
    return !path.empty() && path.find("..") == std::string::npos && path.front() != '/' &&
           path.front() != '\\';
}

std::optional<std::uint64_t> HashFile(const std::filesystem::path& path, std::uintmax_t size) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
        return std::nullopt;
    std::uint64_t hash = 1469598103934665603ull;
    std::array<char, 64 * 1024> buffer{};
    std::uintmax_t remaining = size;
    while (remaining > 0) {
        const auto requested =
            static_cast<std::streamsize>(std::min<std::uintmax_t>(remaining, buffer.size()));
        file.read(buffer.data(), requested);
        const auto read = file.gcount();
        if (read != requested)
            return std::nullopt;
        for (std::streamsize index = 0; index < read; ++index) {
            hash ^= static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(index)]);
            hash *= 1099511628211ull;
        }
        remaining -= static_cast<std::uintmax_t>(read);
    }
    return hash;
}

bool CheckResourceUris(const nlohmann::json& node, const std::filesystem::path& resource_path) {
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (it.key() == "uri" && it.value().is_string()) {
                const std::string uri = it.value().get<std::string>();
                if (uri.starts_with("data:"))
                    continue;
                if (!IsSafeResourcePath(uri) ||
                    !std::filesystem::is_regular_file(resource_path.parent_path() / uri)) {
                    std::cerr << resource_path.string()
                              << ": resource URI is missing or unsafe: " << uri << '\n';
                    return false;
                }
            }
            if (!CheckResourceUris(it.value(), resource_path))
                return false;
        }
    } else if (node.is_array()) {
        for (const auto& value : node) {
            if (!CheckResourceUris(value, resource_path))
                return false;
        }
    }
    return true;
}

bool CheckResourceManifest(const std::filesystem::path& manifest_path,
                           const std::filesystem::path& project_root) {
    const nlohmann::json document = LoadJson(manifest_path);
    if (document.is_null())
        return false;
    if (!document.is_object() || document.value("schema", 0) != 1 ||
        !document.contains("resources") || !document["resources"].is_array() ||
        document["resources"].empty() || document["resources"].size() > 4096) {
        std::cerr << manifest_path.string()
                  << ": resource manifest requires schema 1 and 1..4096 resources\n";
        return false;
    }
    std::unordered_set<std::string> ids;
    std::vector<std::tuple<std::string, std::uintmax_t, std::uint64_t>> summary;
    bool ok = true;
    for (const auto& resource : document["resources"]) {
        if (!resource.is_object() || !resource.contains("owner") ||
            !resource["owner"].is_string() || resource["owner"].get<std::string>().empty() ||
            !resource.contains("version") || !resource["version"].is_number_unsigned() ||
            resource["version"].get<std::uint64_t>() == 0 || !resource.contains("id") ||
            !resource["id"].is_string() || resource["id"].get<std::string>().empty() ||
            !resource.contains("kind") || !resource["kind"].is_string() ||
            resource["kind"].get<std::string>().empty() || !resource.contains("path") ||
            !resource["path"].is_string() ||
            !IsSafeResourcePath(resource["path"].get<std::string>()) ||
            !resource.contains("max_bytes") || !resource["max_bytes"].is_number_unsigned() ||
            resource["max_bytes"].get<std::uint64_t>() == 0 ||
            resource["max_bytes"].get<std::uint64_t>() > 64u * 1024u * 1024u ||
            !ids.insert(resource["id"].get<std::string>()).second) {
            std::cerr << manifest_path.string() << ": invalid or duplicate resource entry\n";
            ok = false;
            continue;
        }
        const auto path = project_root / resource["path"].get<std::string>();
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > resource["max_bytes"].get<std::uint64_t>()) {
            std::cerr << manifest_path.string() << ": resource exceeds budget or is missing: "
                      << resource["path"].get<std::string>() << '\n';
            ok = false;
            continue;
        }
        const auto hash = HashFile(path, size);
        if (!hash.has_value()) {
            std::cerr << manifest_path.string()
                      << ": resource cannot be hashed: " << resource["path"].get<std::string>()
                      << '\n';
            ok = false;
            continue;
        }
        summary.emplace_back(resource["id"].get<std::string>(), size, *hash);
        if (resource["kind"] == "gltf") {
            const auto gltf = LoadJson(path);
            if (gltf.is_null() || !CheckResourceUris(gltf, path))
                ok = false;
        }
    }
    std::sort(summary.begin(), summary.end(), [](const auto& left, const auto& right) {
        return std::get<0>(left) < std::get<0>(right);
    });
    for (const auto& [id, size, hash] : summary) {
        std::ostringstream formatted;
        formatted << std::hex << std::setfill('0') << std::setw(16) << hash;
        std::cout << "resource: " << id << " bytes=" << size << " hash=" << formatted.str() << '\n';
    }
    return ok;
}

bool BuildResourcePackage(const std::filesystem::path& manifest_path,
                          const std::filesystem::path& project_root,
                          const std::filesystem::path& output_path) {
    if (!CheckResourceManifest(manifest_path, project_root))
        return false;
    const auto document = LoadJson(manifest_path);
    if (document.is_null())
        return false;

    std::vector<nlohmann::json> resources;
    for (const auto& resource : document["resources"]) {
        const std::string id = resource["id"].get<std::string>();
        const std::string relative_path = resource["path"].get<std::string>();
        const auto path = project_root / relative_path;
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        const auto hash = HashFile(path, size);
        if (error || !hash.has_value())
            return false;
        resources.push_back({{"owner", resource["owner"]},
                             {"version", resource["version"]},
                             {"id", id},
                             {"kind", resource["kind"]},
                             {"path", relative_path},
                             {"bytes", size},
                             {"hash", [&hash] {
                                  std::ostringstream value;
                                  value << std::hex << std::setfill('0') << std::setw(16) << *hash;
                                  return value.str();
                              }()}});
    }
    std::sort(resources.begin(), resources.end(),
              [](const nlohmann::json& left, const nlohmann::json& right) {
                  return left["id"].get<std::string>() < right["id"].get<std::string>();
              });
    std::uint64_t cache_hash = 1469598103934665603ull;
    for (const auto& resource : resources) {
        const std::string key_material = resource["owner"].get<std::string>() + "\n" +
                                         std::to_string(resource["version"].get<std::uint64_t>()) +
                                         "\n" + resource["id"].get<std::string>() + "\n" +
                                         resource["path"].get<std::string>() + "\n" +
                                         std::to_string(resource["bytes"].get<std::uintmax_t>()) +
                                         "\n" + resource["hash"].get<std::string>() + "\n";
        for (const unsigned char value : key_material) {
            cache_hash ^= value;
            cache_hash *= 1099511628211ull;
        }
    }
    std::ostringstream cache_key;
    cache_key << std::hex << std::setfill('0') << std::setw(16) << cache_hash;
    const nlohmann::json package = {
        {"schema", 1}, {"cache_key", cache_key.str()}, {"resources", resources}};
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        std::cerr << output_path.string() << ": cannot create resource package manifest\n";
        return false;
    }
    output << package.dump(2) << '\n';
    return static_cast<bool>(output);
}

bool CheckProject(const std::filesystem::path& project_path,
                  const std::filesystem::path& project_root) {
    const nlohmann::json project_document = LoadJson(project_path);
    if (project_document.is_null())
        return false;

    const auto project_result = jrpgmaker::plugin::ParseProjectManifest(project_document);
    if (!project_result) {
        std::cerr << project_path.string() << ": " << project_result.error->message << '\n';
        return false;
    }

    const auto read_manifest = [&project_root](const char* directory) {
        const auto path = project_root / "plugins" / directory / "plugin.json";
        const auto result = jrpgmaker::plugin::ParseManifest(LoadJson(path));
        if (!result)
            throw std::invalid_argument(path.string() + ": " + result.error->message);
        return *result.manifest;
    };

    jrpgmaker::plugin::PluginRegistry registry;
    try {
        const auto unlit = read_manifest("sample_unlit");
        const auto style = read_manifest("sample_style");
        const auto instant = read_manifest("sample_instant");
        const auto turn_based = read_manifest("sample_turn_based");
        if (const auto error = jrpgmaker::plugins::RegisterSamplePlugins(registry, unlit, style);
            error.has_value()) {
            std::cerr << "plugin registration error: " << error->message << '\n';
            return false;
        }
        if (const auto error =
                jrpgmaker::plugins::RegisterSampleBattlePlugins(registry, instant, turn_based);
            error.has_value()) {
            std::cerr << "battle plugin registration error: " << error->message << '\n';
            return false;
        }
    } catch (const std::exception& error) {
        std::cerr << "plugin manifest error: " << error.what() << '\n';
        return false;
    }

    bool ok = true;
    for (const auto& issue : jrpgmaker::plugin::ValidateProjectPluginData(*project_result.manifest,
                                                                          registry, project_root)) {
        std::cerr << project_path.string() << ": " << issue.code << ": " << issue.path << ": "
                  << issue.message << '\n';
        ok = false;
    }

    const auto input_path = project_root / project_result.manifest->input_actions;
    const auto input_document = LoadJson(input_path);
    if (input_document.is_null()) {
        ok = false;
    } else {
        const auto input_result = jrpgmaker::core::ParseInputActionMap(input_document);
        if (!input_result) {
            std::cerr << input_path.string() << ": input actions: " << input_result.error << '\n';
            ok = false;
        }
    }

    const auto events_path = project_root / project_result.manifest->event_script;
    const auto events_document = LoadJson(events_path);
    const auto localization_path = project_root / project_result.manifest->localization;
    const auto localization_document = LoadJson(localization_path);
    if (events_document.is_null() || localization_document.is_null()) {
        ok = false;
    } else {
        try {
            const auto script = jrpgmaker::domain::ParseEventScript(events_document);
            if (!LintParsedScript(events_path, script))
                ok = false;
            const auto table_result =
                jrpgmaker::domain::ParseLocalizationTable(localization_document);
            if (!table_result) {
                std::cerr << localization_path.string() << ": localization: " << table_result.error
                          << '\n';
                ok = false;
            } else {
                for (const auto& issue :
                     jrpgmaker::domain::ValidateLocalizationCoverage(script, *table_result.table)) {
                    std::cerr << localization_path.string() << ": localization: " << issue.key
                              << ": " << issue.message << '\n';
                    ok = false;
                }
            }
        } catch (const std::exception& error) {
            std::cerr << events_path.string()
                      << ": project event/localization lint error: " << error.what() << '\n';
            ok = false;
        }
    }

    const auto resource_path = project_root / project_result.manifest->resource_manifest;
    if (!CheckResourceManifest(resource_path, project_root))
        ok = false;
    const auto material_path = project_root / project_result.manifest->material_document;
    const auto material_document = LoadJson(material_path);
    if (!material_document.is_object() ||
        material_document.value("style_plugin_id", std::string{}) !=
            project_result.manifest->render_style) {
        std::cerr << material_path.string() << ": style_plugin_id must match render_style '"
                  << project_result.manifest->render_style << "'\n";
        ok = false;
    }
    if (ok)
        std::cout << project_path.string() << ": project data clean\n";
    return ok;
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
            << "       eventlint --check-vertical-slice <slice.json> <events.json>\n"
            << "       eventlint --check-project <project.json> <project-root>\n"
            << "       eventlint --build-resource-package <resources.json> <project-root> "
               "<output.json>\n";
        return 2;
    }

    if (std::string(argv[1]) == "--check-triggers") {
        if (argc != 4) {
            std::cerr << "eventlint --check-triggers requires <events.json> <triggers.json>\n";
            return 2;
        }
        return CheckTriggerReferences(argv[2], argv[3]) ? 0 : 1;
    }

    if (std::string(argv[1]) == "--check-project") {
        if (argc != 4) {
            std::cerr << "eventlint --check-project requires <project.json> <project-root>\n";
            return 2;
        }
        return CheckProject(argv[2], argv[3]) ? 0 : 1;
    }

    if (std::string(argv[1]) == "--build-resource-package") {
        if (argc != 5) {
            std::cerr
                << "eventlint --build-resource-package requires <resources.json> <project-root> "
                   "<output.json>\n";
            return 2;
        }
        return BuildResourcePackage(argv[2], argv[3], argv[4]) ? 0 : 1;
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
