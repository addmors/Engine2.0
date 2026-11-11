#pragma once

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {

// Orbit camera that follows a target (the player) — 2017-style mouse look,
// modernized: no globals, no GLFW coupling, pure math.
class Camera final {
public:
    glm::vec3 target{0.0F};
    float yaw = 45.0F;   // degrees, around the Y axis
    float pitch = 25.0F; // degrees above the horizon
    float distance = 8.0F;
    float fov = 45.0F;

    [[nodiscard]] glm::vec3 position() const
    {
        const float yawRad = glm::radians(yaw);
        const float pitchRad = glm::radians(pitch);
        const float horizontal = distance * std::cos(pitchRad);
        return target + glm::vec3{
            horizontal * std::sin(yawRad),
            distance * std::sin(pitchRad),
            horizontal * std::cos(yawRad),
        };
    }

    // Horizontal look direction (target - camera, flattened) for player movement.
    [[nodiscard]] glm::vec3 forward() const
    {
        const float yawRad = glm::radians(yaw);
        return glm::normalize(glm::vec3{-std::sin(yawRad), 0.0F, -std::cos(yawRad)});
    }

    [[nodiscard]] glm::vec3 right() const
    {
        const float yawRad = glm::radians(yaw);
        return glm::normalize(glm::vec3{std::cos(yawRad), 0.0F, -std::sin(yawRad)});
    }

    [[nodiscard]] glm::mat4 viewMatrix() const
    {
        return glm::lookAt(position(), target, glm::vec3{0.0F, 1.0F, 0.0F});
    }

    [[nodiscard]] glm::mat4 projectionMatrix(float aspectRatio) const
    {
        return glm::perspective(glm::radians(fov), aspectRatio, 0.1F, 1000.0F);
    }

    void orbit(float deltaX, float deltaY, float sensitivity = 0.2F)
    {
        yaw -= deltaX * sensitivity;
        pitch += deltaY * sensitivity;
        yaw = std::fmod(yaw + 360.0F, 360.0F);
        pitch = glm::clamp(pitch, 1.0F, 80.0F);
    }

    void zoom(float wheelDelta)
    {
        distance = glm::clamp(distance - wheelDelta * 1.5F, 2.5F, 40.0F);
    }
};

} // namespace engine
