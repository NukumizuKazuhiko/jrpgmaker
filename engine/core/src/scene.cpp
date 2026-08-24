#include "jrpgmaker/core/scene.hpp"

#include <cassert>

#include <glm/gtc/matrix_transform.hpp>

namespace jrpgmaker::core {

namespace {

glm::mat4 LocalToMatrix(const Transform& transform) {
    glm::mat4 matrix = glm::translate(glm::mat4(1.0f), transform.translation);
    matrix *= glm::mat4_cast(transform.rotation);
    matrix = glm::scale(matrix, transform.scale);
    return matrix;
}

} // namespace

glm::mat4 Scene::WorldMatrix(Entity entity) const {
    // Walk to the root, collecting the local matrices, then compose from the
    // root downward: world = root_to_child chain.
    std::vector<const Transform*> chain;
    Entity current = entity;
    std::size_t guard = 0;
    while (current != kNullEntity) {
        const Transform* local = registry_.try_get<Transform>(current);
        if (local != nullptr) {
            chain.push_back(local);
        }
        const Parent* parent = registry_.try_get<Parent>(current);
        if (parent == nullptr) {
            break;
        }
        current = parent->parent;
        // A cycle would spin forever; the caller is expected to keep the
        // hierarchy acyclic (SetParent/Detach never create cycles directly).
        ++guard;
        if (guard > 1000u) {
            break;
        }
    }

    glm::mat4 world(1.0f);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        world *= LocalToMatrix(**it);
    }
    return world;
}

std::vector<Entity> Scene::ChildrenOf(Entity entity) const {
    std::vector<Entity> children;
    const auto view = registry_.view<Parent>();
    for (const Entity candidate : view) {
        if (view.get<Parent>(candidate).parent == entity) {
            children.push_back(candidate);
        }
    }
    return children;
}

} // namespace jrpgmaker::core