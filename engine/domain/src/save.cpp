#include "jrpgmaker/domain/save.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace jrpgmaker::domain {

namespace {
constexpr std::uintmax_t kMaxSaveFileBytes = 4U * 1024U * 1024U;
}

nlohmann::json SerializeSave(const SaveState& state) {
    return {
        {"schema", kSaveSchemaVersion},
        {"calendar_id", state.calendar_id},
        {"date", {{"year", state.date.year}, {"month", state.date.month}, {"day", state.date.day}}},
        {"minute_of_day", state.minute_of_day},
        {"true_flags", state.true_flags}};
}

SaveParseResult ParseSave(const nlohmann::json& document) {
    try {
        if (!document.is_object() || document.value("schema", 0) != kSaveSchemaVersion ||
            !document.contains("calendar_id") || !document["calendar_id"].is_string() ||
            document["calendar_id"].get<std::string>().empty() || !document.contains("date") ||
            !document["date"].is_object() || !document.contains("minute_of_day") ||
            !document["minute_of_day"].is_number_integer() || !document.contains("true_flags") ||
            !document["true_flags"].is_array()) {
            return {
                .ok = false, .state = {}, .error = "save requires schema 1 and complete fields"};
        }
        const auto& date = document["date"];
        if (!date.contains("year") || !date.contains("month") || !date.contains("day") ||
            !date["year"].is_number_integer() || !date["month"].is_number_integer() ||
            !date["day"].is_number_integer()) {
            return {.ok = false, .state = {}, .error = "save date is incomplete"};
        }
        SaveState state{.calendar_id = document["calendar_id"].get<std::string>(),
                        .date = {.year = date["year"].get<std::int32_t>(),
                                 .month = date["month"].get<std::int32_t>(),
                                 .day = date["day"].get<std::int32_t>()},
                        .minute_of_day = document["minute_of_day"].get<std::int32_t>(),
                        .true_flags = {}};
        if (document["true_flags"].size() > 4096) {
            return {.ok = false, .state = {}, .error = "save contains too many flags"};
        }
        for (const auto& flag : document["true_flags"]) {
            if (!flag.is_string() || flag.get<std::string>().empty()) {
                return {.ok = false, .state = {}, .error = "save flags must be non-empty strings"};
            }
            state.true_flags.push_back(flag.get<std::string>());
        }
        std::sort(state.true_flags.begin(), state.true_flags.end());
        if (std::adjacent_find(state.true_flags.begin(), state.true_flags.end()) !=
            state.true_flags.end()) {
            return {.ok = false, .state = {}, .error = "save flags must be unique"};
        }
        if (state.date.year < 1 || state.date.month < 1 || state.date.day < 1 ||
            state.minute_of_day < 0 || state.minute_of_day >= 24 * 60) {
            return {.ok = false, .state = {}, .error = "save date is outside basic bounds"};
        }
        return {.ok = true, .state = std::move(state), .error = {}};
    } catch (const nlohmann::json::exception&) {
        return {.ok = false, .state = {}, .error = "save contains an invalid value type"};
    }
}

bool WriteSaveFile(const std::filesystem::path& path, const SaveState& state, std::string& error) {
    std::ofstream file(path);
    if (!file.is_open()) {
        error = "cannot open save file for writing";
        return false;
    }
    file << SerializeSave(state).dump(2) << '\n';
    if (!file.good()) {
        error = "failed while writing save file";
        return false;
    }
    error.clear();
    return true;
}

SaveParseResult ReadSaveFile(const std::filesystem::path& path,
                             const std::map<int, SaveMigration>& migrations) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return {.ok = false, .state = {}, .error = "cannot open save file for reading"};
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > kMaxSaveFileBytes) {
        return {.ok = false, .state = {}, .error = "save file exceeds the 4 MiB limit"};
    }
    file.seekg(0, std::ios::beg);
    std::stringstream buffer;
    buffer << file.rdbuf();
    try {
        return MigrateAndParseSave(nlohmann::json::parse(buffer.str()), migrations);
    } catch (const nlohmann::json::exception&) {
        return {.ok = false, .state = {}, .error = "save file contains invalid JSON"};
    }
}

SaveParseResult MigrateAndParseSave(const nlohmann::json& document,
                                    const std::map<int, SaveMigration>& migrations) {
    if (!document.is_object() || !document.contains("schema") ||
        !document["schema"].is_number_integer()) {
        return {.ok = false, .state = {}, .error = "save schema is missing"};
    }
    nlohmann::json current = document;
    int version = current["schema"].get<int>();
    if (version < 0) {
        return {.ok = false, .state = {}, .error = "save schema cannot be negative"};
    }
    while (version < kSaveSchemaVersion) {
        const auto it = migrations.find(version);
        if (it == migrations.end()) {
            return {.ok = false, .state = {}, .error = "save migration step is missing"};
        }
        current = it->second(current);
        if (!current.is_object() || !current.contains("schema") ||
            !current["schema"].is_number_integer() || current["schema"].get<int>() <= version) {
            return {.ok = false, .state = {}, .error = "save migration did not advance schema"};
        }
        version = current["schema"].get<int>();
    }
    if (version != kSaveSchemaVersion) {
        return {.ok = false, .state = {}, .error = "save schema is newer than this runtime"};
    }
    return ParseSave(current);
}

SaveState CaptureSave(const core::GameClock& clock, const FlagStore& flags) {
    return {.calendar_id = clock.calendar().id,
            .date = clock.date(),
            .minute_of_day = clock.minute_of_day(),
            .true_flags = flags.Snapshot()};
}

void RestoreSave(const SaveState& state, core::GameClock& clock, FlagStore& flags) {
    if (state.calendar_id != clock.calendar().id) {
        throw std::invalid_argument("save calendar does not match runtime calendar");
    }
    clock.Set(state.date, state.minute_of_day);
    flags.Restore(state.true_flags);
}

} // namespace jrpgmaker::domain
