#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/vec4.hpp>
#include <nlohmann/json.hpp>

namespace jrpgmaker::ui {

struct Theme {
    std::string id;
    glm::vec4 accent{1.0f};
    std::uint32_t text_pixel_height = 16;
    std::vector<std::string> font_paths;
};

struct ThemeParseResult {
    std::optional<Theme> theme;
    std::string error;
    explicit operator bool() const { return theme.has_value(); }
};

inline ThemeParseResult ParseTheme(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != 1 || !document.contains("id") ||
        !document["id"].is_string() || document["id"].get<std::string>().empty() ||
        !document.contains("accent") || !document["accent"].is_array() ||
        document["accent"].size() != 4 || !document.contains("text_pixel_height") ||
        !document.contains("font_paths") || !document["font_paths"].is_array() ||
        document["font_paths"].empty() || document["font_paths"].size() > 16u ||
        (!document["text_pixel_height"].is_number_unsigned() &&
         !document["text_pixel_height"].is_number_integer())) {
        return {
            .theme = std::nullopt,
            .error =
                "theme requires schema 1, id, accent[4], text_pixel_height, and 1-16 font_paths"};
    }
    glm::vec4 accent;
    for (std::size_t i = 0; i < 4; ++i) {
        if (!document["accent"][i].is_number() || document["accent"][i].get<float>() < 0.0f ||
            document["accent"][i].get<float>() > 1.0f) {
            return {.theme = std::nullopt, .error = "theme accent components must be in [0,1]"};
        }
        accent[static_cast<glm::vec4::length_type>(i)] = document["accent"][i].get<float>();
    }
    std::uint64_t height = 0;
    if (document["text_pixel_height"].is_number_unsigned()) {
        height = document["text_pixel_height"].get<std::uint64_t>();
    } else if (document["text_pixel_height"].get<std::int64_t>() > 0) {
        height = static_cast<std::uint64_t>(document["text_pixel_height"].get<std::int64_t>());
    }
    if (height == 0 || height > 256) {
        return {.theme = std::nullopt, .error = "theme text_pixel_height must be in [1,256]"};
    }
    std::vector<std::string> font_paths;
    std::unordered_set<std::string> unique_font_paths;
    font_paths.reserve(document["font_paths"].size());
    for (const auto& value : document["font_paths"]) {
        if (!value.is_string()) {
            return {.theme = std::nullopt, .error = "theme font_paths must contain strings"};
        }
        const auto path = value.get<std::string>();
        const std::filesystem::path parsed(path);
        const bool has_parent = std::any_of(parsed.begin(), parsed.end(),
                                            [](const auto& part) { return part == ".."; });
        if (path.empty() || path.size() > 4096u || parsed.is_absolute() || parsed.has_root_name() ||
            path.front() == '/' || path.front() == '\\' || path.find(':') != std::string::npos ||
            has_parent || !unique_font_paths.insert(path).second) {
            return {.theme = std::nullopt,
                    .error = "theme font_paths must be unique safe project-relative paths"};
        }
        font_paths.push_back(path);
    }
    return {.theme = Theme{.id = document["id"].get<std::string>(),
                           .accent = accent,
                           .text_pixel_height = static_cast<std::uint32_t>(height),
                           .font_paths = std::move(font_paths)},
            .error = std::string{}};
}

struct AnimationValue {
    float value = 0.0f;
    float velocity = 0.0f;
};

inline void AdvanceAnimation(AnimationValue& animation, float target, float delta_seconds,
                             float stiffness = 120.0f, float damping = 20.0f) {
    animation.velocity += (target - animation.value) * stiffness * delta_seconds;
    animation.velocity *= 1.0f / (1.0f + damping * delta_seconds);
    animation.value += animation.velocity * delta_seconds;
}

} // namespace jrpgmaker::ui
