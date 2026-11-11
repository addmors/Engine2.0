#include "engine/scene/Primitives.hpp"

#include <cmath>
#include <iterator>
#include <numbers>

namespace engine {

Mesh makeBoxMesh(const glm::vec3& h)
{
    Mesh mesh;
    mesh.name = "box";

    // Classic cube face table: unit position, normal, uv — per face corner.
    // WINDING: every face's corners are CCW viewed from OUTSIDE (GL front face
    // is CCW and the renderer culls GL_BACK; wrong winding makes the rasterizer
    // clip the front faces and the box renders inside-out).
    struct FaceVertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
    };
    constexpr FaceVertex faceData[24] = {
        // +Z
        {{-1, -1, 1}, {0, 0, 1}, {0, 0}}, {{1, -1, 1}, {0, 0, 1}, {1, 0}},
        {{1, 1, 1}, {0, 0, 1}, {1, 1}}, {{-1, 1, 1}, {0, 0, 1}, {0, 1}},
        // -Z
        {{-1, -1, -1}, {0, 0, -1}, {0, 0}}, {{-1, 1, -1}, {0, 0, -1}, {0, 1}},
        {{1, 1, -1}, {0, 0, -1}, {1, 1}}, {{1, -1, -1}, {0, 0, -1}, {1, 0}},
        // +X
        {{1, -1, 1}, {1, 0, 0}, {0, 0}}, {{1, -1, -1}, {1, 0, 0}, {1, 0}},
        {{1, 1, -1}, {1, 0, 0}, {1, 1}}, {{1, 1, 1}, {1, 0, 0}, {0, 1}},
        // -X
        {{-1, -1, -1}, {-1, 0, 0}, {0, 0}}, {{-1, -1, 1}, {-1, 0, 0}, {1, 0}},
        {{-1, 1, 1}, {-1, 0, 0}, {1, 1}}, {{-1, 1, -1}, {-1, 0, 0}, {0, 1}},
        // +Y
        {{-1, 1, 1}, {0, 1, 0}, {0, 1}}, {{1, 1, 1}, {0, 1, 0}, {1, 1}},
        {{1, 1, -1}, {0, 1, 0}, {1, 0}}, {{-1, 1, -1}, {0, 1, 0}, {0, 0}},
        // -Y
        {{-1, -1, -1}, {0, -1, 0}, {0, 0}}, {{1, -1, -1}, {0, -1, 0}, {1, 0}},
        {{1, -1, 1}, {0, -1, 0}, {1, 1}}, {{-1, -1, 1}, {0, -1, 0}, {0, 1}},
    };
    // Uniform per-face pattern: (v0,v1,v2) + (v0,v2,v3), CCW from outside.
    constexpr unsigned faceIndices[36] = {
        0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
    };

    mesh.vertices.reserve(24);
    for (const auto& f : faceData) {
        Vertex v;
        v.position = f.position * h; // componentwise scale of the unit cube
        v.normal = f.normal;
        v.uv = f.uv;
        mesh.vertices.push_back(v);
    }
    mesh.indices.assign(std::begin(faceIndices), std::end(faceIndices));
    mesh.computeAabb();
    return mesh;
}

Mesh makeSphereMesh(float radius, int segments, int rings)
{
    Mesh mesh;
    mesh.name = "sphere";

    const int columns = segments + 1;
    mesh.vertices.reserve(static_cast<size_t>(rings + 1) * static_cast<size_t>(columns));
    for (int r = 0; r <= rings; ++r) {
        const float theta = std::numbers::pi_v<float> * static_cast<float>(r) / static_cast<float>(rings);
        for (int s = 0; s <= segments; ++s) {
            const float phi
                = 2.0F * std::numbers::pi_v<float> * static_cast<float>(s) / static_cast<float>(segments);
            Vertex v;
            v.normal = {std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            v.position = v.normal * radius;
            v.uv = {static_cast<float>(s) / static_cast<float>(segments),
                1.0F - static_cast<float>(r) / static_cast<float>(rings)};
            mesh.vertices.push_back(v);
        }
    }

    mesh.indices.reserve(static_cast<size_t>(rings) * static_cast<size_t>(segments) * 6);
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const unsigned a = static_cast<unsigned>(r * columns + s);
            const unsigned b = a + 1;
            const unsigned c = a + static_cast<unsigned>(columns);
            const unsigned d = c + 1;
            // CCW viewed from outside (matches the terrain's (+Z,+X) winding
            // and the GL_CCW front-face convention; the old (a,c,b) order was
            // CW and the rasterizer culled the outward faces).
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
    }
    mesh.computeAabb();
    return mesh;
}

Mesh makeCylinderMesh(float radius, float halfHeight, int segments)
{
    Mesh mesh;
    mesh.name = "cylinder";

    // Side quads.
    for (int s = 0; s <= segments; ++s) {
        const float phi = 2.0F * std::numbers::pi_v<float> * static_cast<float>(s) / static_cast<float>(segments);
        const glm::vec3 dir{std::cos(phi), 0.0F, std::sin(phi)};
        const glm::vec2 uv{static_cast<float>(s) / static_cast<float>(segments), 0.0F};

        Vertex bottom;
        bottom.position = dir * radius + glm::vec3{0.0F, -halfHeight, 0.0F};
        bottom.normal = dir;
        bottom.uv = uv;

        Vertex top = bottom;
        top.position = dir * radius + glm::vec3{0.0F, halfHeight, 0.0F};
        top.uv = {uv.x, 1.0F};

        mesh.vertices.push_back(bottom);
        mesh.vertices.push_back(top);
    }
    for (int s = 0; s < segments; ++s) {
        const unsigned a = static_cast<unsigned>(s * 2);
        mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 2, a + 1, a + 3, a + 2});
    }

    // Caps (triangle fans around a center vertex).
    const auto addCap = [&](float y, const glm::vec3& normal) {
        const unsigned center = static_cast<unsigned>(mesh.vertices.size());
        Vertex capCenter;
        capCenter.position = {0.0F, y, 0.0F};
        capCenter.normal = normal;
        mesh.vertices.push_back(capCenter);

        for (int s = 0; s <= segments; ++s) {
            const float phi
                = 2.0F * std::numbers::pi_v<float> * static_cast<float>(s) / static_cast<float>(segments);
            Vertex v;
            v.position = {radius * std::cos(phi), y, radius * std::sin(phi)};
            v.normal = normal;
            mesh.vertices.push_back(v);
        }
        return center;
    };

    const unsigned bottomCenter = addCap(-halfHeight, {0.0F, -1.0F, 0.0F});
    const unsigned topCenter = addCap(halfHeight, {0.0F, 1.0F, 0.0F});

    for (int s = 0; s < segments; ++s) {
        const unsigned first = bottomCenter + 1 + static_cast<unsigned>(s);
        // CCW viewed from the cap's outward side (see makeSphereMesh note):
        // bottom cap faces -Y, top cap faces +Y, hence mirrored orders.
        mesh.indices.insert(mesh.indices.end(), {bottomCenter, first, first + 1});
        const unsigned topFirst = topCenter + 1 + static_cast<unsigned>(s);
        mesh.indices.insert(mesh.indices.end(), {topCenter, topFirst + 1, topFirst});
    }

    mesh.computeAabb();
    return mesh;
}

} // namespace engine
