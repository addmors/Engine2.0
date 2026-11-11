#pragma once

#include <glm/glm.hpp>

#include "engine/assets/Mesh.hpp"

namespace engine {

// Procedural unit primitives for physics bodies (replaces the 2017
// Sphere/Cylinder/PrimShape generators). Vertex layout matches engine::Vertex.

[[nodiscard]] Mesh makeBoxMesh(const glm::vec3& halfExtents);
[[nodiscard]] Mesh makeSphereMesh(float radius, int segments = 32, int rings = 16);
[[nodiscard]] Mesh makeCylinderMesh(float radius, float halfHeight, int segments = 32);

} // namespace engine
