#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/core/calendar.hpp"
#include "jrpgmaker/domain/flag_store.hpp"

namespace jrpgmaker::domain {

inline constexpr int kSaveSchemaVersion = 1;

struct SaveState {
    std::string calendar_id;
    core::GameDate date;
    std::int32_t minute_of_day = 0;
    std::vector<std::string> true_flags;
};

struct SaveParseResult {
    bool ok = false;
    SaveState state;
    std::string error;
};

[[nodiscard]] nlohmann::json SerializeSave(const SaveState& state);
[[nodiscard]] SaveParseResult ParseSave(const nlohmann::json& document);
using SaveMigration = std::function<nlohmann::json(const nlohmann::json&)>;

[[nodiscard]] bool WriteSaveFile(const std::filesystem::path& path, const SaveState& state,
                                 std::string& error);

// Applies explicit schema steps until kSaveSchemaVersion. Missing steps and
// non-advancing migrations fail instead of silently changing save meaning.
[[nodiscard]] SaveParseResult
MigrateAndParseSave(const nlohmann::json& document,
                    const std::map<int, SaveMigration>& migrations = {});

[[nodiscard]] SaveParseResult ReadSaveFile(const std::filesystem::path& path,
                                           const std::map<int, SaveMigration>& migrations = {});

[[nodiscard]] SaveState CaptureSave(const core::GameClock& clock, const FlagStore& flags);
void RestoreSave(const SaveState& state, core::GameClock& clock, FlagStore& flags);

} // namespace jrpgmaker::domain
