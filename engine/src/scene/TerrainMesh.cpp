#include "engine/scene/TerrainMesh.hpp"

namespace engine {

Mesh generateTerrainMesh(const Terrain& terrain)
{
    Mesh mesh;
    mesh.name = "terrain";

    const size_t resolution = terrain.resolution();
    const float half = terrain.worldSize() / 2.0F;
    const float step = terrain.worldSize() / static_cast<float>(resolution - 1);
    const auto heights = terrain.heights2d();

    mesh.vertices.resize(resolution * resolution);
    for (size_t ix = 0; ix < resolution; ++ix) {
        for (size_t iz = 0; iz < resolution; ++iz) {
            const float x = static_cast<float>(ix) * step - half;
            const float z = static_cast<float>(iz) * step - half;

            Vertex& v = mesh.vertices[ix * resolution + iz];
            v.position = {x, heights[ix, iz], z};
            v.normal = terrain.normalAt(x, z);
            v.uv = {static_cast<float>(ix) / static_cast<float>(resolution - 1),
                static_cast<float>(iz) / static_cast<float>(resolution - 1)};
        }
    }

    mesh.indices.reserve((resolution - 1) * (resolution - 1) * 6);
    for (size_t ix = 0; ix + 1 < resolution; ++ix) {
        for (size_t iz = 0; iz + 1 < resolution; ++iz) {
            const unsigned a = static_cast<unsigned>(ix * resolution + iz);
            const unsigned b = static_cast<unsigned>((ix + 1) * resolution + iz);
            const unsigned c = a + 1;
            const unsigned d = b + 1;
            // CCW from above (Y-up): (+Z, +X) winding keeps the normals up.
            mesh.indices.insert(mesh.indices.end(), {a, c, d, a, d, b});
        }
    }

    mesh.computeAabb();
    return mesh;
}

} // namespace engine
