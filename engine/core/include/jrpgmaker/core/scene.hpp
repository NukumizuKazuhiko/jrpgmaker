#pragma once

#include <cstdint>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace jrpgmaker::core {

// EnTT entity handle. ADR-001: EnTT ECS is the sole runtime object model.
using Entity = entt::entity;
inline constexpr Entity kNullEntity = entt::null;

// Local transform of a scene entity (TRS). Layout matches glTF node TRS.
struct Transform {
    glm::vec3 translation{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

// Parent link for hierarchy. A root entity carries no Parent component.
struct Parent {
    Entity parent{kNullEntity};
};

// Runtime scene: owns the EnTT registry and exposes entity + hierarchy ops.
// `Entity` is EnTT's native handle (K is `entt::null`). World transforms are
// computed on demand by walking the parent chain (P2: glTF node hierarchy).
class Scene {
public:
    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    Entity CreateEntity() { return registry_.create(); }
    void DestroyEntity(Entity entity) { registry_.destroy(entity); }

    entt::registry& Registry() { return registry_; }
    const entt::registry& Registry() const { return registry_; }

    // Links `child` under `parent`. Replaces any previous parent of `child`.
    void SetParent(Entity child, Entity parent) {
        registry_.emplace_or_replace<Parent>(child, Parent{parent});
    }
    // Removes the parent link, making `entity` a root.
    void Detach(Entity entity) { registry_.remove<Parent>(entity); }

    // Composes the TRS components along the parent chain into a world matrix
    // (column-major, GLM). Entities without a Transform are identity; a missing
    // parent ends the chain. Cyclic parent links are rejected by assertion.
    glm::mat4 WorldMatrix(Entity entity) const;

    // Direct children of `entity` (parent chain scan; O(scene) in v0).
    std::vector<Entity> ChildrenOf(Entity entity) const;

private:
    entt::registry registry_;
};

} // namespace jrpgmaker::core