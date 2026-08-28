#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace jrpgmaker::core {

inline constexpr int kInputActionSchemaVersion = 1;
inline constexpr std::size_t kMaxInputActions = 64;
inline constexpr std::size_t kMaxKeysPerInputAction = 8;

struct InputAction {
    std::string id;
    std::vector<std::string> keys;
};

struct InputActionMap {
    int schema = kInputActionSchemaVersion;
    std::vector<InputAction> actions;

    [[nodiscard]] const InputAction* Find(std::string_view id) const;
};

struct InputActionMapParseResult {
    std::optional<InputActionMap> map;
    std::string error;
    explicit operator bool() const { return map.has_value(); }
};

[[nodiscard]] InputActionMapParseResult ParseInputActionMap(const nlohmann::json& document);

} // namespace jrpgmaker::core
