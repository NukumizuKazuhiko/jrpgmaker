#include "jrpgmaker/domain/localization.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <variant>

namespace jrpgmaker::domain {
namespace {

bool IsValidUtf8(const std::string& text) {
    std::size_t index = 0;
    while (index < text.size()) {
        const auto byte = static_cast<std::uint8_t>(text[index]);
        std::size_t width = 0;
        if (byte <= 0x7fu)
            width = 1;
        else if ((byte & 0xe0u) == 0xc0u)
            width = 2;
        else if ((byte & 0xf0u) == 0xe0u)
            width = 3;
        else if ((byte & 0xf8u) == 0xf0u)
            width = 4;
        else
            return false;
        if (index + width > text.size())
            return false;
        for (std::size_t offset = 1; offset < width; ++offset) {
            if ((static_cast<std::uint8_t>(text[index + offset]) & 0xc0u) != 0x80u)
                return false;
        }
        index += width;
    }
    return true;
}

void CollectTextKeys(const std::vector<Instruction>& instructions, std::vector<std::string>& keys) {
    for (const Instruction& instruction : instructions) {
        if (const auto* dialog = std::get_if<DialogInstruction>(&instruction.op)) {
            keys.push_back(dialog->text_key);
        } else if (const auto* choice = std::get_if<ChoiceInstruction>(&instruction.op)) {
            keys.push_back(choice->prompt_text_key);
            for (const DialogOption& option : choice->options) {
                keys.push_back(option.text_key);
                CollectTextKeys(option.instructions, keys);
            }
        } else if (const auto* branch = std::get_if<BranchInstruction>(&instruction.op)) {
            CollectTextKeys(branch->if_set, keys);
            CollectTextKeys(branch->if_not_set, keys);
        }
    }
}

} // namespace

LocalizationParseResult ParseLocalizationTable(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != kLocalizationSchemaVersion ||
        !document.contains("locale") || !document["locale"].is_string() ||
        document["locale"].get<std::string>().empty() || !document.contains("strings") ||
        !document["strings"].is_object()) {
        return {.table = std::nullopt,
                .error = "localization table requires schema 1, locale and strings"};
    }
    if (document["strings"].size() > kMaxLocalizationEntries) {
        return {.table = std::nullopt, .error = "localization table exceeds entry budget"};
    }

    LocalizationTable table{.schema = kLocalizationSchemaVersion,
                            .locale = document["locale"].get<std::string>(),
                            .entries = {}};
    for (auto it = document["strings"].begin(); it != document["strings"].end(); ++it) {
        const std::string value = it.value().is_string() ? it.value().get<std::string>() : "";
        if (it.key().empty() || value.empty() || value.size() > kMaxLocalizationTextBytes ||
            !IsValidUtf8(value)) {
            return {.table = std::nullopt,
                    .error = "localization keys require non-empty valid UTF-8 text within budget"};
        }
        table.entries.emplace(it.key(), value);
    }
    return {.table = std::move(table), .error = {}};
}

std::vector<LocalizationIssue> ValidateLocalizationCoverage(const EventScript& script,
                                                            const LocalizationTable& table) {
    std::vector<std::string> keys;
    for (const Event& event : script.events)
        CollectTextKeys(event.instructions, keys);
    std::vector<LocalizationIssue> issues;
    for (const std::string& key : keys) {
        if (!table.entries.contains(key))
            issues.push_back(
                {.key = key, .message = "text key is missing from localization table"});
    }
    return issues;
}

} // namespace jrpgmaker::domain
