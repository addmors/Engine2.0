#pragma once

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>
#include <glm/glm.hpp>

#include <memory>

namespace engine {

class Input;
class PhysicsWorld;

// First-person-ish character controller over Bullet's kinematic character
// (replaces the 2017 `Player`). Movement is resolved by the physics world;
// the camera simply follows `position()`.
class PlayerController final {
public:
    PlayerController(PhysicsWorld& world, glm::vec3 spawnPosition);

    // Feeds movement from input; forward/right define the horizontal basis
    // (already rotated by the camera yaw, y components ignored).
    void update(const Input& input, float deltaSeconds, glm::vec3 forward, glm::vec3 right);

    [[nodiscard]] glm::vec3 position() const;
    [[nodiscard]] glm::quat rotation() const;
    void teleport(glm::vec3 position);

    float walkSpeed = 8.0F;
    float jumpSpeed = 5.0F;

private:
    btPairCachingGhostObject m_ghost;
    btCapsuleShape m_capsule;
    std::unique_ptr<btKinematicCharacterController> m_controller;
};

} // namespace engine
