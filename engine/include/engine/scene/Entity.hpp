#pragma once

#include <list>
#include <memory>
#include <utility>

#include <glm/glm.hpp>

#include "engine/assets/Model.hpp"
#include "engine/scene/Frustum.hpp"
#include "engine/scene/Transform.hpp"

namespace engine {

// Axis-aligned box in the (center, half-extents) form.
struct Aabb {
    glm::vec3 center{0.0F};
    glm::vec3 halfExtents{0.0F};
};

// Union of all mesh AABBs of a model (mesh AABBs are computed at load time).
[[nodiscard]] inline Aabb generateAabb(const Model& model)
{
    Aabb out;
    bool first = true;
    for (const auto& mesh : model.meshes()) {
        const glm::vec3 center = (mesh.aabb.min + mesh.aabb.max) * 0.5F;
        const glm::vec3 half = (mesh.aabb.max - mesh.aabb.min) * 0.5F;
        if (first) {
            out.center = center;
            out.halfExtents = half;
            first = false;
        } else {
            const glm::vec3 minCorner = glm::min(out.center - out.halfExtents, center - half);
            const glm::vec3 maxCorner = glm::max(out.center + out.halfExtents, center + half);
            out.center = (minCorner + maxCorner) * 0.5F;
            out.halfExtents = (maxCorner - minCorner) * 0.5F;
        }
    }
    return out;
}

// Scene graph node (clean port of the 2017 Entity): transform hierarchy +
// optional model reference + bounding volume for frustum culling.
class Entity {
public:
    std::list<std::unique_ptr<Entity>> children;
    Entity* parent = nullptr;

    Transform transform;
    const Model* model = nullptr; // may be null for pure grouping nodes
    std::unique_ptr<BoundingVolume> boundingVolume;

    Entity() = default;

    explicit Entity(const Model& inModel)
        : model(&inModel)
    {
        const Aabb aabb = generateAabb(inModel);
        boundingVolume = std::make_unique<SphereBV>(aabb.center, glm::length(aabb.halfExtents));
    }

    // Adds a child; returns a reference to it.
    template <typename... Args>
    Entity& addChild(Args&&... args)
    {
        children.emplace_back(std::make_unique<Entity>(std::forward<Args>(args)...));
        children.back()->parent = this;
        return *children.back();
    }

    // Recomputes world matrices when anything is dirty.
    void updateSelfAndChild()
    {
        if (!transform.isDirty() && parent != nullptr && !parent->transform.isDirty()) {
            return;
        }
        forceUpdateSelfAndChild();
    }

    void forceUpdateSelfAndChild()
    {
        if (parent != nullptr) {
            transform.computeModelMatrix(parent->transform.modelMatrix());
        } else {
            transform.computeModelMatrix();
        }
        for (auto&& child : children) {
            child->forceUpdateSelfAndChild();
        }
    }

    [[nodiscard]] bool isOnFrustum(const Frustum& frustum) const
    {
        return boundingVolume == nullptr || boundingVolume->isOnFrustum(frustum, transform.modelMatrix());
    }
};

} // namespace engine
