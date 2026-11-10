#include "engine/physics/PlayerController.hpp"

#include <GLFW/glfw3.h> // key constants (GLFW_INCLUDE_NONE is set engine-wide)

#include "engine/core/Input.hpp"
#include "engine/physics/PhysicsWorld.hpp"

namespace engine {

PlayerController::PlayerController(PhysicsWorld& world, glm::vec3 spawnPosition)
    : m_ghost()
    , m_capsule(0.5F, 1.2F) // radius, cylinder part height; total height = 1.2 + 2*0.5
{
    btTransform start;
    start.setIdentity();
    start.setOrigin(btVector3(spawnPosition.x, spawnPosition.y, spawnPosition.z));

    m_ghost.setWorldTransform(start);
    m_ghost.setCollisionShape(&m_capsule);
    m_ghost.setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

    m_controller = std::make_unique<btKinematicCharacterController>(
        &m_ghost, &m_capsule, 0.35F /* step height */, btVector3(0.0F, 1.0F, 0.0F) /* up axis */);
    m_controller->setGravity(btVector3(0.0F, -9.81F, 0.0F));

    world.world()->addCollisionObject(&m_ghost,
        btBroadphaseProxy::DefaultFilter, btBroadphaseProxy::DefaultFilter);
    world.world()->addAction(m_controller.get());
}

void PlayerController::update(const Input& input, float deltaSeconds, glm::vec3 forward, glm::vec3 right)
{
    glm::vec3 move{0.0F};
    if (input.isDown(GLFW_KEY_W)) {
        move += forward;
    }
    if (input.isDown(GLFW_KEY_S)) {
        move -= forward;
    }
    if (input.isDown(GLFW_KEY_A)) {
        move -= right;
    }
    if (input.isDown(GLFW_KEY_D)) {
        move += right;
    }

    if (glm::dot(move, move) > 0.0F) {
        move = glm::normalize(move);
    }
    // NOTE: bullet's kinematic character treats walkDirection as displacement
    // per simulation step, so the speed must be scaled by deltaSeconds here.
    m_controller->setWalkDirection(btVector3(move.x, move.y, move.z) * walkSpeed * deltaSeconds);

    if (input.pressed(GLFW_KEY_SPACE) && m_controller->onGround()) {
        m_controller->setJumpSpeed(jumpSpeed);
        m_controller->jump();
    }
}

glm::vec3 PlayerController::position() const
{
    const btVector3& origin = m_ghost.getWorldTransform().getOrigin();
    return {origin.x(), origin.y(), origin.z()};
}

glm::quat PlayerController::rotation() const
{
    const btQuaternion& rotation = m_ghost.getWorldTransform().getRotation();
    return {rotation.x(), rotation.y(), rotation.z(), rotation.w()};
}

void PlayerController::teleport(glm::vec3 position)
{
    btTransform transform = m_ghost.getWorldTransform();
    transform.setOrigin(btVector3(position.x, position.y, position.z));
    m_ghost.setWorldTransform(transform);
}

} // namespace engine
