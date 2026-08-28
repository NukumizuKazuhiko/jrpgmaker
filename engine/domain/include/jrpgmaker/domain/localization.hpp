#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/domain/event_script.hpp"

namespace jrpgmaker::domain {

inline constexpr int kLocalizationSchemaVersion = 1;
inline constexpr std::size_t kMaxLocalizationEntries = 4096;
inline constexpr std::size_t kMaxLocalizationTextBytes = 16 * 1024;

struct LocalizationTable {
    int schema = kLocalizationSchemaVersion;
    std::string locale;
    std::map<std::string, std::string> entries;
};

struct LocalizationParseResult {
    std::optional<LocalizationTable> table;
    std::string error;
    explicit operator bool() const { return table.has_value(); }
};

struct LocalizationIssue {
    std::string key;
    std::string message;
};

[[nodiscard]] LocalizationParseResult ParseLocalizationTable(const nlohmann::json& document);
[[nodiscard]] std::vector<LocalizationIssue>
ValidateLocalizationCoverage(const EventScript& script, const LocalizationTable& table);

} // namespace jrpgmaker::domain
