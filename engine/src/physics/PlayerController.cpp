#include "engine/physics/PlayerController.hpp"

#include <GLFW/glfw3.h> // key constants (GLFW_INCLUDE_NONE is set engine-wide)

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "engine/assets/Model.hpp"
#include "engine/core/Input.hpp"
#include "engine/core/Log.hpp"
#include "engine/physics/PhysicsWorld.hpp"
#include "engine/scene/Entity.hpp" // Aabb + generateAabb (fallback fit)

namespace engine {
namespace {

// Convex-cast filter for the grounding guard: ignore the capsule itself and
// everything that is not static or kinematic, so the guard re-grounds the
// character on terrain only, never on dynamic props.
class GroundSnapCallback final : public btCollisionWorld::ClosestConvexResultCallback {
public:
    GroundSnapCallback(const btCollisionObject* self, const btVector3& from, const btVector3& to)
        : btCollisionWorld::ClosestConvexResultCallback(from, to)
        , m_self(self)
    {
    }

    btScalar addSingleResult(btCollisionWorld::LocalConvexResult& convexResult, bool normalInWorldSpace) override
    {
        const btCollisionObject* object = convexResult.m_hitCollisionObject;
        if (object == m_self || !object->isStaticOrKinematicObject()) {
            return 0.0F; // keep searching
        }
        return ClosestConvexResultCallback::addSingleResult(convexResult, normalInWorldSpace);
    }

private:
    const btCollisionObject* m_self;
};

// The vendored btKinematicCharacterController keeps `bounce_fix` — its own
// remedy for unreliable convex-cast closestHitFractions over large flat
// polygons (exactly our terrain heightfield: the landing sweep either misses
// entirely or reports garbage fractions, teleporting the capsule through the
// floor) — in the protected section with no public setter. Enable it via a
// minimal subclass, recover from penetrations deeper than 5 cm (the default
// 0.2 m let a bad landing bury the capsule halfway before recovery ever
// fired), and add a guaranteed post-step grounding guard (see below).
class CandyKinematicController final : public btKinematicCharacterController {
public:
    // Keep the base's 16-byte allocation contract (ATTRIBUTE_ALIGNED16 SIMD
    // members: btVector3/btQuaternion inside the controller state).
    BT_DECLARE_ALIGNED_ALLOCATOR();

    CandyKinematicController(btPairCachingGhostObject* ghostObject, btConvexShape* convexShape,
        btScalar stepHeight, const btVector3& up)
        : btKinematicCharacterController(ghostObject, convexShape, stepHeight, up)
    {
        bounce_fix = true;
        setMaxPenetrationDepth(0.05F);
    }

    // The vendored bounce_fix path evaluates needsCollision(m_ghostObject,
    // callback.m_hitCollisionObject) even when only the double-fall callback
    // hit and the primary one missed — passing a NULL object that the base
    // dereferences unconditionally (access violation on every landing where
    // the primary sweep flakes). Null-guard it.
    bool needsCollision(const btCollisionObject* body0, const btCollisionObject* body1) override
    {
        if (body1 == nullptr) {
            return false;
        }
        return btKinematicCharacterController::needsCollision(body0, body1);
    }

    void updateAction(btCollisionWorld* collisionWorld, btScalar deltaTime) override
    {
        preStep(collisionWorld);
        playerStep(collisionWorld, deltaTime);
        reconcileGround(collisionWorld, deltaTime);
    }

    // Post-step grounding guard. The controller resolves all vertical motion
    // with convex casts, which are unreliable against large flat polygons
    // (heightfield cells): a fast fall or a flaky stepDown sweep teleports
    // the capsule THROUGH the floor. Worse, the heightfield's contact
    // generation stops reporting usable contacts a couple of centimeters
    // deep (empirically verified: manifolds go empty past ~2.5 cm while the
    // capsule sits half a meter inside), so the controller's own penetration
    // recovery — and any manifold-based fix — cannot even see the burial.
    // The one reliable probe is a convex cast STARTED NON-OVERLAPPING. So
    // after every controller step, cast the capsule down from above the
    // deepest burial a single controller step can produce — bounded by the
    // controller's own configuration (fallSpeed * dt, or one stepHeight) —
    // and, if a standable static surface sits above the capsule's feet, park
    // the feet exactly on it. No tuned constants: the cast window is derived
    // from the controller's own limits and the actual step time.
    void reconcileGround(btCollisionWorld* collisionWorld, btScalar deltaTime)
    {
        if (m_convexShape->getShapeType() != CAPSULE_SHAPE_PROXYTYPE) {
            return;
        }
        const btCapsuleShape* capsule = static_cast<const btCapsuleShape*>(m_convexShape);
        const btScalar totalHalf = capsule->getRadius() + capsule->getHalfHeight();

        // Deepest burial one controller step can create: a full-speed fall
        // teleport or a stepDown step, whichever is larger.
        const btScalar burialBound = btMax(m_fallSpeed * deltaTime, m_stepHeight);

        const btVector3& origin = m_ghostObject->getWorldTransform().getOrigin();
        // Lift the capsule so its bottom starts at the deepest possible
        // burial point — clear of any surface it could be inside of — then
        // sweep down to the current position, covering the whole window.
        const btScalar lift = burialBound + totalHalf;

        btTransform start, end;
        start.setIdentity();
        end.setIdentity();
        start.setOrigin(origin + m_up * lift);
        end.setOrigin(origin);

        GroundSnapCallback callback(m_ghostObject, start.getOrigin(), end.getOrigin());
        callback.m_collisionFilterGroup = m_ghostObject->getBroadphaseHandle()->m_collisionFilterGroup;
        callback.m_collisionFilterMask = m_ghostObject->getBroadphaseHandle()->m_collisionFilterMask;
        collisionWorld->convexSweepTest(capsule, start, end, callback, 0.0F);

        if (!callback.hasHit()) {
            return;
        }
        if (callback.m_hitNormalWorld.dot(m_up) < m_maxSlopeCosine) {
            return; // wall or ceiling — not a standable surface
        }
        const btScalar feet = origin.getY() - totalHalf; // capsule is Y-up aligned
        const btScalar rise = callback.m_hitPointWorld.getY() - feet;
        if (rise <= 0.0F) {
            return; // surface below the feet — airborne, not buried
        }

        // Park the capsule exactly on the surface and absorb the fall.
        btTransform transform = m_ghostObject->getWorldTransform();
        transform.setOrigin(origin + m_up * rise);
        m_ghostObject->setWorldTransform(transform);
        m_verticalVelocity = 0.0F;
        m_verticalOffset = 0.0F;
        m_wasJumping = false;
    }
};

std::string lowercased(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

// Bone-role name heuristics (rig naming conventions vary wildly).
bool isHeadBone(std::string_view name)
{
    return contains(name, "head") && !contains(name, "top") && !contains(name, "end")
        && !contains(name, "tip") && !contains(name, "hat");
}

bool isHipsBone(std::string_view name)
{
    return contains(name, "hips") || contains(name, "pelvis");
}

bool isFootBone(std::string_view name)
{
    return contains(name, "foot") || contains(name, "ankle");
}

} // namespace

CapsuleFit fitCapsuleY(const Aabb& box, float maxRadius)
{
    const glm::vec3 size = box.halfExtents * 2.0F;
    const float rMax = glm::min(size.x, glm::min(size.z, size.y)) * 0.5F;
    const float radius = glm::min(maxRadius, rMax);
    const float length = glm::max(0.0F, size.y - 2.0F * radius);
    return {radius, length};
}

CapsuleFit capsuleFitFromRig(const Model& model, float scale, float maxRadius)
{
    // Bind-pose bone positions: offsetMatrix maps mesh space -> bone space,
    // so its inverse's translation column is the bone's origin in mesh space.
    // Only the skeleton drives the fit — mesh silhouette outliers (spread
    // T-pose arms) cannot inflate the capsule.
    std::optional<glm::vec3> head, hips, foot;
    std::string headBone, hipsBone, footBone;
    for (const auto& [name, info] : model.bones()) {
        const glm::vec3 pos = glm::vec3(glm::inverse(info.offsetMatrix)[3]) * scale;
        const std::string lower = lowercased(name);
        if (!head && isHeadBone(lower)) {
            head = pos;
            headBone = name;
        }
        if (!hips && isHipsBone(lower)) {
            hips = pos;
            hipsBone = name;
        }
        // Two feet: take the lower one as the ground contact.
        if (isFootBone(lower) && (!foot || pos.y < foot->y)) {
            foot = pos;
            footBone = name;
        }
    }
    engine::Log::info(
        "capsuleFitFromRig: head '{}' ({:.2f}, {:.2f}, {:.2f}), hips '{}' ({:.2f}, {:.2f}, {:.2f}), foot '{}' ({:.2f}, {:.2f}, {:.2f})",
        headBone, head ? head->x : 0.0F, head ? head->y : 0.0F, head ? head->z : 0.0F,
        hipsBone, hips ? hips->x : 0.0F, hips ? hips->y : 0.0F, hips ? hips->z : 0.0F,
        footBone, foot ? foot->x : 0.0F, foot ? foot->y : 0.0F, foot ? foot->z : 0.0F);

    if (head && foot && head->y > foot->y) {
        CapsuleFit fit;
        if (hips) {
            // Half the leg length (hips -> foot) as a body-radius proxy.
            fit.radius = glm::length(*hips - *foot) * 0.5F;
        } else {
            // No hips/pelvis identified: quarter of the height as a stand-in.
            fit.radius = (head->y - foot->y) * 0.25F;
        }
        const float bodyHeight = head->y - foot->y; // feet at y=0
        fit.radius = glm::clamp(fit.radius, 0.02F, glm::min(maxRadius, bodyHeight * 0.5F));
        fit.height = glm::max(0.0F, bodyHeight - 2.0F * fit.radius); // cylinder part
        engine::Log::info("capsuleFitFromRig: radius {:.3f}, cylinder {:.3f} (total {:.3f})",
            fit.radius, fit.height, fit.height + 2.0F * fit.radius);
        return fit;
    }

    // Rig without identifiable Head/Foot bones: fall back to the mesh AABB.
    engine::Log::warn("capsuleFitFromRig: skeleton roles not identified, falling back to the AABB fit");
    const Aabb box = generateAabb(model);
    const Aabb scaled{box.center * scale, box.halfExtents * scale};
    return fitCapsuleY(scaled, maxRadius);
}

PlayerController::PlayerController(PhysicsWorld& world, glm::vec3 spawnPosition, const CapsuleFit& fit)
    : m_world(world)
    , m_ghost()
    , m_fit(fit)
    , m_capsule(m_fit.radius, m_fit.height) // radius, cylinder part
{
    btTransform start;
    start.setIdentity();
    start.setOrigin(btVector3(spawnPosition.x, spawnPosition.y, spawnPosition.z));

    m_ghost.setWorldTransform(start);
    m_ghost.setCollisionShape(&m_capsule);
    m_ghost.setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);

    m_controller = std::make_unique<CandyKinematicController>(
        &m_ghost, &m_capsule, 0.35F /* step height */, btVector3(0.0F, 1.0F, 0.0F) /* up axis */);
    // Sweep against the world's broadphase instead of the ghost's overlapping
    // pair cache: while the capsule is nearly touching the floor the cache
    // holds no pairs for the triangles BELOW the swept path, so landing sweeps
    // flake (miss or report garbage fractions) and the controller teleports
    // the capsule through the floor. The world cast sees those triangles.
    m_controller->setUseGhostSweepTest(false);
    // Stronger-than-world gravity: btKinematicCharacterController descends
    // ballistically, so on this steep heightmap (~45 deg slopes) with walk
    // speed 8 the default -9.81 cannot keep up and the avatar visibly GLIDES
    // above the ground when walking downhill. -30 tracks the slope tightly
    // (residual float < ~1 unit); jumpSpeed is raised to keep jumps ~0.8 high.
    m_controller->setGravity(btVector3(0.0F, -30.0F, 0.0F));

    // CharacterFilter group / StaticFilter|DefaultFilter mask — same as
    // Bullet's CharacterDemo. CRITICAL: the mask must include StaticFilter,
    // because btDiscreteDynamicsWorld::addRigidBody assigns static geometry
    // (the terrain heightfield) the StaticFilter GROUP; with a DefaultFilter
    // only mask the pair is never created and the character falls through.
    world.world()->addCollisionObject(&m_ghost,
        btBroadphaseProxy::CharacterFilter,
        btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter);
    world.world()->addAction(m_controller.get());
}

PlayerController::~PlayerController()
{
    // Unregister while everything is still alive: the world's pair cache
    // keeps pairs (and narrowphase algorithms) referencing this ghost's
    // proxy, so leaving them behind is a use-after-free at world teardown.
    m_world.world()->removeAction(m_controller.get());
    m_world.world()->removeCollisionObject(&m_ghost);
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
    // NOTE: bullet's kinematic character applies walkDirection as displacement
    // PER SIMULATION SUBSTEP (updateAction runs once per fixed step inside
    // stepSimulation), so it must be scaled by the fixed substep time — using
    // the frame's deltaSeconds would make the walk speed frame-rate dependent.
    m_controller->setWalkDirection(
        btVector3(move.x, move.y, move.z) * walkSpeed * PhysicsWorld::fixedStepTime());

    if (input.pressed(GLFW_KEY_SPACE) && m_controller->onGround()) {
        m_controller->setJumpSpeed(jumpSpeed);
        m_controller->jump();
    }
}

float PlayerController::halfHeight() const
{
    return m_capsule.getRadius() + m_capsule.getHalfHeight();
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
