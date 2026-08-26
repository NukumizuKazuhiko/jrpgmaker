#include "jrpgmaker/core/map_data.hpp"

#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace jrpgmaker::core {

namespace {

[[noreturn]] void ParseError(const char* message) {
    throw std::invalid_argument(std::string("map parse error: ") + message);
}

glm::vec3 ParseVec3(const nlohmann::json& node) {
    if (!node.is_array() || node.size() != 3 || !node[0].is_number() || !node[1].is_number() ||
        !node[2].is_number()) {
        ParseError("expected vec3 array");
    }
    return {node[0].get<float>(), node[1].get<float>(), node[2].get<float>()};
}

} // namespace

NavigationGrid ParseNavigationGrid(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != 1 || !document.contains("width") ||
        !document["width"].is_number_integer() || !document.contains("height") ||
        !document["height"].is_number_integer() || !document.contains("cell_size") ||
        !document["cell_size"].is_number() || !document.contains("origin") ||
        !document.contains("walkable") || !document["walkable"].is_array()) {
        ParseError("navigation requires schema 1, dimensions, origin, cell_size and walkable");
    }
    std::vector<bool> walkable;
    for (const auto& value : document["walkable"]) {
        if (!value.is_boolean()) {
            ParseError("navigation walkable entries must be boolean");
        }
        walkable.push_back(value.get<bool>());
    }
    const auto origin = ParseVec3(document["origin"]);
    return NavigationGrid(document["width"].get<int>(), document["height"].get<int>(),
                          {origin.x, origin.z}, document["cell_size"].get<float>(),
                          std::move(walkable));
}

std::vector<Aabb> ParseCollisionAabbs(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != 1 ||
        !document.contains("obstacles") || !document["obstacles"].is_array()) {
        ParseError("collision requires schema 1 and obstacles");
    }
    std::vector<Aabb> obstacles;
    for (const auto& node : document["obstacles"]) {
        if (!node.is_object() || !node.contains("min") || !node.contains("max")) {
            ParseError("collision obstacle requires min and max");
        }
        const Aabb obstacle{ParseVec3(node["min"]), ParseVec3(node["max"])};
        if (obstacle.min.x >= obstacle.max.x || obstacle.min.y >= obstacle.max.y ||
            obstacle.min.z >= obstacle.max.z) {
            ParseError("collision obstacle bounds must have positive size");
        }
        obstacles.push_back(obstacle);
    }
    return obstacles;
}

CameraRigData ParseCameraRigData(const nlohmann::json& document) {
    if (!document.is_object() || document.value("schema", 0) != 1 ||
        !document.contains("third_person") || !document["third_person"].is_object() ||
        !document.contains("fixed_regions") || !document["fixed_regions"].is_array()) {
        ParseError("camera requires schema 1, third_person and fixed_regions");
    }
    const auto& third = document["third_person"];
    constexpr const char* required_fields[] = {"distance", "height", "pitch_degrees",
                                               "smoothing_seconds"};
    for (const char* field : required_fields) {
        if (!third.contains(field) || !third[field].is_number()) {
            ParseError("camera third_person requires numeric distance, height, pitch_degrees and "
                       "smoothing_seconds");
        }
    }
    CameraRigData data;
    data.third_person.distance = third["distance"].get<float>();
    data.third_person.height = third["height"].get<float>();
    data.third_person.pitch_degrees = third["pitch_degrees"].get<float>();
    data.third_person.smoothing_seconds = third["smoothing_seconds"].get<float>();
    if (data.third_person.distance <= 0.0f || data.third_person.height < 0.0f ||
        data.third_person.smoothing_seconds < 0.0f) {
        ParseError("camera third_person values are out of range");
    }
    for (const auto& node : document["fixed_regions"]) {
        if (!node.is_object() || !node.contains("id") || !node["id"].is_string() ||
            !node.contains("min") || !node.contains("max") || !node.contains("priority") ||
            !node.contains("eye") || !node.contains("target")) {
            ParseError("fixed camera region requires id, bounds, priority, eye and target");
        }
        FixedCameraRegion region;
        if (!node["priority"].is_number_integer()) {
            ParseError("fixed camera region priority must be an integer");
        }
        region.id = node["id"].get<std::string>();
        region.bounds = {ParseVec3(node["min"]), ParseVec3(node["max"])};
        region.priority = node["priority"].get<int>();
        region.camera.eye = ParseVec3(node["eye"]);
        region.camera.target = ParseVec3(node["target"]);
        if (region.id.empty() || region.bounds.min.x >= region.bounds.max.x ||
            region.bounds.min.y >= region.bounds.max.y ||
            region.bounds.min.z >= region.bounds.max.z) {
            ParseError("fixed camera region has invalid id or bounds");
        }
        data.fixed_regions.push_back(std::move(region));
    }
    std::unordered_set<std::string> region_ids;
    for (const FixedCameraRegion& region : data.fixed_regions) {
        if (!region_ids.insert(region.id).second) {
            ParseError("duplicate fixed camera region id");
        }
    }
    return data;
}

} // namespace jrpgmaker::core
