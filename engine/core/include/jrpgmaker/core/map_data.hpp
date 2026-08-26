#pragma once

#include <vector>

#include <nlohmann/json.hpp>

#include "jrpgmaker/core/camera_rig.hpp"
#include "jrpgmaker/core/character_controller.hpp"
#include "jrpgmaker/core/pathfinding.hpp"

namespace jrpgmaker::core {

NavigationGrid ParseNavigationGrid(const nlohmann::json& document);
std::vector<Aabb> ParseCollisionAabbs(const nlohmann::json& document);

struct CameraRigData {
    ThirdPersonCameraConfig third_person;
    std::vector<FixedCameraRegion> fixed_regions;
};

CameraRigData ParseCameraRigData(const nlohmann::json& document);

} // namespace jrpgmaker::core
