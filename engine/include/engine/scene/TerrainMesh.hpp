#pragma once

#include "engine/assets/Mesh.hpp"
#include "engine/scene/Terrain.hpp"

namespace engine {

// Builds the renderable grid mesh for a terrain: world-space positions
// (centered), smooth normals, 0..1 UVs. 256x256 default -> ~65k vertices.
[[nodiscard]] Mesh generateTerrainMesh(const Terrain& terrain);

} // namespace engine
