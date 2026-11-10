#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine {

// Interleaved vertex layout used by all renderers (RHI uploads this as-is).
struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};
    glm::vec3 tangent{};
    std::array<int, 4> boneIds{-1, -1, -1, -1};
    std::array<float, 4> boneWeights{};
};

struct MeshAabb {
    glm::vec3 min{};
    glm::vec3 max{};
};

// Geometry of a single submesh. Pure CPU data; the renderer uploads it later.
struct Mesh {
    std::string name;
    unsigned materialIndex = 0;

    std::vector<Vertex> vertices;
    std::vector<unsigned> indices;

    MeshAabb aabb{};

    void computeAabb()
    {
        constexpr float big = std::numeric_limits<float>::max();
        aabb = {glm::vec3{big}, glm::vec3{-big}};
        for (const auto& v : vertices) {
            aabb.min = glm::min(aabb.min, v.position);
            aabb.max = glm::max(aabb.max, v.position);
        }
    }
};

} // namespace engine
