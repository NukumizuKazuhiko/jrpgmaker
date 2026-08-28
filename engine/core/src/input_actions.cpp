#include "jrpgmaker/core/input_actions.hpp"

#include <unordered_set>
#include <utility>

namespace jrpgmaker::core {

const InputAction* InputActionMap::Find(std::string_view id) const {
    for (const InputAction& action : actions) {
        if (action.id == id)
            return &action;
    }
    return nullptr;
}

InputActionMapParseResult ParseInputActionMap(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != kInputActionSchemaVersion) {
        return {.map = std::nullopt, .error = "input action map requires schema 1"};
    }
    if (!document.contains("actions") || !document["actions"].is_array() ||
        document["actions"].empty()) {
        return {.map = std::nullopt,
                .error = "input action map requires a non-empty actions array"};
    }
    if (document["actions"].size() > kMaxInputActions) {
        return {.map = std::nullopt, .error = "input action map exceeds the action budget"};
    }

    InputActionMap map;
    std::unordered_set<std::string> ids;
    map.actions.reserve(document["actions"].size());
    for (const auto& value : document["actions"]) {
        if (!value.is_object() || !value.contains("id") || !value["id"].is_string() ||
            value["id"].get<std::string>().empty() || !value.contains("keys") ||
            !value["keys"].is_array()) {
            return {.map = std::nullopt,
                    .error = "each input action requires a unique id and keys array"};
        }
        InputAction action{.id = value["id"].get<std::string>(), .keys = {}};
        if (!ids.insert(action.id).second) {
            return {.map = std::nullopt, .error = "input action IDs must be unique"};
        }
        if (value["keys"].empty() || value["keys"].size() > kMaxKeysPerInputAction) {
            return {.map = std::nullopt, .error = "input action keys exceed the per-action budget"};
        }
        for (const auto& key : value["keys"]) {
            if (!key.is_string() || key.get<std::string>().empty()) {
                return {.map = std::nullopt,
                        .error = "input action keys must be non-empty strings"};
            }
            action.keys.push_back(key.get<std::string>());
        }
        map.actions.push_back(std::move(action));
    }
    return {.map = std::move(map), .error = {}};
}

} // namespace jrpgmaker::core
