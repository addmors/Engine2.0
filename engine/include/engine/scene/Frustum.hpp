#pragma once

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

// Infinite plane (Gribb/Hartmann frustum extraction, ported from the 2017 Entity.h).
struct Plane {
    glm::vec3 normal{0.0F, 1.0F, 0.0F}; // unit vector
    float distance = 0.0F;              // distance from origin along the normal

    Plane() = default;
    Plane(const glm::vec3& point, const glm::vec3& unitNormal)
        : normal(glm::normalize(unitNormal))
        , distance(glm::dot(normal, point))
    {
    }

    [[nodiscard]] float signedDistance(const glm::vec3& point) const
    {
        return glm::dot(normal, point) - distance;
    }

    // Builds a plane from the homogeneous (n, d) form: dot(n, p) + d = 0.
    [[nodiscard]] static Plane fromHomogeneous(glm::vec3 n, float d)
    {
        const float len = glm::length(n);
        Plane plane;
        plane.normal = n / len;
        plane.distance = -d / len;
        return plane;
    }
};

struct Frustum {
    Plane topFace;
    Plane bottomFace;
    Plane rightFace;
    Plane leftFace;
    Plane farFace;
    Plane nearFace;

    // Extracts the 6 planes from a projection*view matrix.
    [[nodiscard]] static Frustum fromViewProjection(const glm::mat4& viewProjection)
    {
        // glm is column-major: m[col][row]; row i = (m[0][i], m[1][i], m[2][i], m[3][i]).
        const auto row = [&viewProjection](int i) {
            return glm::vec4{viewProjection[0][i], viewProjection[1][i], viewProjection[2][i], viewProjection[3][i]};
        };
        const glm::vec4 row0 = row(0);
        const glm::vec4 row1 = row(1);
        const glm::vec4 row2 = row(2);
        const glm::vec4 row3 = row(3);

        Frustum frustum;
        frustum.leftFace = Plane::fromHomogeneous(glm::vec3{row3} + glm::vec3{row0}, row3.w + row0.w);
        frustum.rightFace = Plane::fromHomogeneous(glm::vec3{row3} - glm::vec3{row0}, row3.w - row0.w);
        frustum.bottomFace = Plane::fromHomogeneous(glm::vec3{row3} + glm::vec3{row1}, row3.w + row1.w);
        frustum.topFace = Plane::fromHomogeneous(glm::vec3{row3} - glm::vec3{row1}, row3.w - row1.w);
        frustum.nearFace = Plane::fromHomogeneous(glm::vec3{row3} + glm::vec3{row2}, row3.w + row2.w);
        frustum.farFace = Plane::fromHomogeneous(glm::vec3{row3} - glm::vec3{row2}, row3.w - row2.w);
        return frustum;
    }
};

// Abstract culling volume (ported from the 2017 BoundingVolume).
struct BoundingVolume {
    virtual ~BoundingVolume() = default;
    [[nodiscard]] virtual bool isOnOrForwardPlane(const Plane& plane) const = 0;
    [[nodiscard]] virtual bool isOnFrustum(const Frustum& frustum, const glm::mat4& modelMatrix) const = 0;
};

// Bounding sphere (center/radius, scaled by the model matrix).
struct SphereBV final : BoundingVolume {
    glm::vec3 center{0.0F};
    float radius = 0.0F;

    SphereBV() = default;
    SphereBV(const glm::vec3& inCenter, float inRadius)
        : center(inCenter)
        , radius(inRadius)
    {
    }

    [[nodiscard]] bool isOnOrForwardPlane(const Plane& plane) const final
    {
        return plane.signedDistance(center) > -radius;
    }

    [[nodiscard]] bool isOnFrustum(const Frustum& frustum, const glm::mat4& modelMatrix) const final
    {
        const glm::vec3 scaledCenter = modelMatrix * glm::vec4{center, 1.0F};
        const float maxScale = std::max({glm::length(glm::vec3{modelMatrix[0]}),
            glm::length(glm::vec3{modelMatrix[1]}), glm::length(glm::vec3{modelMatrix[2]})});
        const SphereBV scaled{scaledCenter, radius * maxScale};
        return scaled.isOnOrForwardPlane(frustum.leftFace) && scaled.isOnOrForwardPlane(frustum.rightFace)
            && scaled.isOnOrForwardPlane(frustum.topFace) && scaled.isOnOrForwardPlane(frustum.bottomFace)
            && scaled.isOnOrForwardPlane(frustum.nearFace) && scaled.isOnOrForwardPlane(frustum.farFace);
    }
};

} // namespace engine
