#pragma once

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletDynamics/Character/btKinematicCharacterController.h>
#include <glm/glm.hpp>

#include <memory>

namespace engine {

class Input;
class Model; // assets (skeleton-based capsule fit)
class PhysicsWorld;
struct Aabb;

// Fitted Y-aligned capsule: `height` is the cylinder part (total capsule
// height = height + 2*radius). The capsule is FEET-anchored — the spawn
// position is the feet point; the center sits half the total height above it
// (≈ hip level for a humanoid fit).
struct CapsuleFit {
    float radius = 0.4F;
    float height = 1.0F; // cylinder part; total capsule height = height + 2*radius
};

// Carves a Y-aligned capsule out of an axis-aligned box: the radius is
// clamped to half of the smallest box dimension, the cylinder segment spans
// the remaining vertical space and stays centered, so the capsule touches the
// box bounds exactly. Kept as the fallback for when the skeleton-based fit
// (capsuleFitFromRig) cannot identify the rig's bones.
[[nodiscard]] CapsuleFit fitCapsuleY(const Aabb& box, float maxRadius);

// Fits the capsule to the avatar's SKELETON (bind pose) instead of its mesh
// AABB, so silhouette outliers like spread T-pose arms cannot inflate the
// collider:
//   height = Head.y - Foot.y              (character height)
//   radius = 0.5 * distance(Hips, Foot)   (half leg length as a body proxy)
// clamped by `maxRadius` and half the height. Bone positions come from the
// inverse bind matrices. Falls back to fitCapsuleY on the model's AABB when
// Head/Hips/Foot bones cannot be identified. `scale` converts model units to
// world units.
[[nodiscard]] CapsuleFit capsuleFitFromRig(const Model& model, float scale, float maxRadius);

// First-person-ish character controller over Bullet's kinematic character
// (replaces the 2017 `Player`). Movement is resolved by the physics world;
// the camera simply follows `position()`.
class PlayerController final {
public:
    // The capsule is fitted to the avatar beforehand (capsuleFitFromRig /
    // fitCapsuleY — never a hardcoded size). `spawnPosition` is the FEET
    // point: the capsule center lands half the total height above it.
    PlayerController(PhysicsWorld& world, glm::vec3 spawnPosition, const CapsuleFit& fit);
    ~PlayerController();

    PlayerController(const PlayerController&) = delete;
    PlayerController& operator=(const PlayerController&) = delete;

    // Feeds movement from input; forward/right define the horizontal basis
    // (already rotated by the camera yaw, y components ignored).
    void update(const Input& input, float deltaSeconds, glm::vec3 forward, glm::vec3 right);

    [[nodiscard]] glm::vec3 position() const;
    [[nodiscard]] glm::quat rotation() const;
    // Distance from the transform origin (capsule center) DOWN to the feet:
    // radius + half of the cylinder segment. The renderer parks the avatar's
    // feet at `position() - halfHeight()`, so the visual stands exactly where
    // the capsule rests — never hovering, never sunken.
    [[nodiscard]] float halfHeight() const;
    void teleport(glm::vec3 position);

    // Parks the character controller (ragdoll mode): removes the controller's
    // action from the physics world so it stops stepping the capsule
    // (gravity + residual walk direction) while the avatar is simulated by
    // the ragdoll instead. The ghost object stays registered — position()
    // keeps working and the world stays consistent; setEnabled(true) re-adds
    // the action (same registration as the constructor).
    void setEnabled(bool enabled);

    float walkSpeed = 8.0F;
    float jumpSpeed = 7.0F; // with the controller's -30 gravity this is a ~0.8 high jump

private:
    PhysicsWorld& m_world; // for unregistering the ghost on destruction
    btPairCachingGhostObject m_ghost;
    CapsuleFit m_fit; // fitted capsule dimensions (initializes m_capsule)
    btCapsuleShape m_capsule;
    std::unique_ptr<btKinematicCharacterController> m_controller;
    bool m_enabled = true;
};

} // namespace engine
