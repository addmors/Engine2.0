#pragma once

#include <btBulletDynamicsCommon.h>
#include <BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>
#include <BulletDynamics/ConstraintSolver/btHingeConstraint.h>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

#include "engine/assets/Animator.hpp" // Animator::Trs in member declarations

namespace engine {

class PhysicsWorld;

// Skeletal ragdoll for the player avatar. On construction it seeds one Bullet
// capsule per body part (a bone segment of the rig) at the animator's CURRENT
// animated pose — mapped through the renderer's model matrix, so the ragdoll
// appears exactly where the skinned avatar was drawn — and links the parts
// with human-like joints: cone-twist "ball" joints for the spine, shoulders,
// hips, wrists and ankles, plus true hinge joints for the knees and elbows,
// so limbs only flex the human way (the knee never hyperextends, the elbow
// never bends backwards, the spine barely folds or twists). update() then
// reads the simulated transforms back into the animator's pose, so the very
// same GPU skinning that played the clips now plays the physics. Dropping
// the object detaches everything again and clip animation takes over.
//
// Coordinate mapping (mirrors Animator::composePalette): the flat node tree
// lives in assimp's unit-wrapped space (g_m); the palette applies S(100) to
// reach the rig's centimeter vertex space; `modelToWorld` is the drawSkinned
// matrix converting centimeters into world meters. Therefore
//   world bone global = modelToWorld * S(100) * g_m(bone)
//   g_m(bone) back    = S(0.01) * inverse(modelToWorld) * world bone global
// Per frame the bone's world global is recovered as
//   W = B_now * B_seed^-1 * W_seed   (rigid delta of the capsule transform)
// and the driven node's local TRS = inverse(parent g_m) * g_m. The parent
// global resolves to the parent part's current global, or — through nodes the
// ragdoll does not simulate (neck, shoulders) — to a driven ancestor's
// current global composed with its frozen snapshot offset, or to the frozen
// snapshot global (the root chain above the hips). The identity
// g_m = S(0.01) * inv(M2W) * B_now * inv(B_seed) * M2W * S(100) * g_m_seed is
// exact, so the seed pose round-trips without drift or activation pop.
//
// The object must not outlive its PhysicsWorld or Animator (main owns both
// longer). Bodies/constraints attach directly through PhysicsWorld::world();
// they are NOT part of the spawn-snapshot bookkeeping.
class Ragdoll final {
public:
    // Seeds every body from the animator's CURRENT pose. Main must have run
    // Animator::update() this frame (the palette on screen is the seed).
    // With `enableSelfCollision` the joint-linked capsules are NOT excluded
    // from each other (the classic ragdoll filters them out): the solver then
    // also resolves their overlap at the joints as contacts. Off by default —
    // the capsule end-caps overlap at every joint by design.
    Ragdoll(PhysicsWorld& world, Animator& animator, const glm::mat4& modelToWorld,
        bool enableSelfCollision = false);
    ~Ragdoll();

    Ragdoll(const Ragdoll&) = delete;
    Ragdoll& operator=(const Ragdoll&) = delete;

    // Body transforms -> node-local TRS -> animator palette (same
    // composePalette path the clips use). Call once per frame, after the step.
    void update();

    // World position of the pelvis body — camera anchor while ragdolling.
    [[nodiscard]] glm::vec3 hipsPosition() const;

    // Experiment diagnostics: worst world gap between a joint's two anchor
    // points (0.0 when every joint holds exactly), the fastest body, and the
    // current hinge angles.
    [[nodiscard]] float maxJointStretch() const;
    [[nodiscard]] float maxBodySpeed() const;

    // Current angle of every hinge (rad, joint-table order: left knee, right
    // knee, left elbow, right elbow). 0 = the seed pose; knee flexion (heel
    // backwards) is positive, elbow flexion (hand forwards) is negative.
    // Non-const because Bullet's btHingeConstraint::getHingeAngle() is.
    [[nodiscard]] std::vector<float> hingeAngles();

    // False when the rig was missing a required bone (nothing was attached).
    [[nodiscard]] bool valid() const { return m_valid; }
    [[nodiscard]] size_t bodyCount() const { return m_parts.size(); }

private:
    // One simulated body covering one bone segment [bone .. endBone]: a
    // capsule aligned along the segment, centered on its midpoint.
    struct Part {
        const char* bone = nullptr;    // node this body drives (e.g. "LeftUpLeg")
        const char* endBone = nullptr; // node whose joint closes the segment
        float radius = 0.05F;          // collider radius, world units
        float mass = 1.0F;             // kg

        int nodeIndex = -1;    // resolved at build
        int endNodeIndex = -1; // -1 = synthesize a short stub segment

        // How the driven node's PARENT global resolves per frame (see class
        // comment): the parent node may be driven itself, hang off a driven
        // ancestor through frozen snapshot locals, or be constant.
        int parentNode = -1;
        int parentPart = -1;
        int drivenAncestor = -1;
        glm::mat4 parentRel{1.0F}; // parent global = ancestor global * parentRel
        bool parentIsPart = false;

        // Seed data (fixed at build):
        glm::mat4 boneWorldSeed{1.0F};       // world bone global at seed time
        btTransform bodySeed{};              // capsule world transform at seed time
        glm::mat4 invBodySeed{1.0F};         // inverse of bodySeed (glm form)
        glm::vec3 dirSeed{0.0F, 1.0F, 0.0F}; // segment direction, world
        glm::vec3 startJoint{0.0F};          // pivot where joints attach, world

        glm::mat4 g_mCurrent{1.0F}; // per-frame back-converted node global

        // Owned Bullet objects; detached from the world in ~Ragdoll.
        std::unique_ptr<btCapsuleShape> shape;
        std::unique_ptr<btDefaultMotionState> motionState;
        std::unique_ptr<btRigidBody> body;
    };

    // Joint between a child part and its parent part, pivoting at the
    // child's start joint. Both constraint frames map to the SAME world
    // frame at seed time (ball: X = the child's bone direction; hinge:
    // Z = the body's medio-lateral axis), so every limit is a deviation FROM
    // THE SEED POSE — the pose at activation is always exactly inside its
    // limits, no startup jerk, whatever the clips were doing.
    enum class JointKind {
        Ball,  // cone-twist: swing cone + twist roll around the bone
        Hinge, // one axis with a signed range (knees, elbows)
    };
    struct JointSpec {
        int parentPart = 0;
        int childPart = 0;
        JointKind kind = JointKind::Ball;
        // Ball: cone semi-axes / roll around the bone (rad from seed).
        float swing1 = 0.4F;
        float swing2 = 0.4F;
        float twist = 0.3F;
        // Hinge: allowed angle range around the axis (rad, 0 = seed pose).
        // Axis = the body's right; knee flexion (heel backwards) is positive,
        // elbow flexion (hand forwards) is negative.
        float low = 0.0F;
        float high = 0.0F;
    };

    bool buildPart(Part& part, const glm::mat4& modelToWorld);
    void resolveParent(Part& part);
    void buildJoints();

    PhysicsWorld& m_world;
    Animator& m_animator;
    // Joint-linked (adjacent) capsules are ALWAYS excluded from collision —
    // they overlap at the joints by design, and their contacts fight the
    // constraints (plank-like bracing + convulsive jitter). When true, the
    // remaining (non-adjacent) ragdoll parts also collide with each other.
    bool m_selfCollide = false;

    // Diagnostics probe: the two anchor points of one joint (co-located by
    // the solver) plus the bodies that carry them — works for every joint
    // type, so maxJointStretch() needs no per-type casts.
    struct JointProbe {
        const btRigidBody* bodyA = nullptr;
        const btRigidBody* bodyB = nullptr;
        btVector3 pivotInA{};
        btVector3 pivotInB{};
    };

    std::vector<Part> m_parts;
    std::vector<std::unique_ptr<btTypedConstraint>> m_joints;
    std::vector<btHingeConstraint*> m_hinges; // non-owning, for hingeAngles()
    std::vector<JointProbe> m_jointProbes;    // non-owning, for maxJointStretch()

    // The body's medio-lateral axis (left shoulder joint -> right shoulder
    // joint, points toward the character's RIGHT): orients the knee/elbow
    // hinge axes so flexion goes the human way in any seed pose.
    glm::vec3 m_characterRight{1.0F, 0.0F, 0.0F};

    // Seed pose: frozen snapshot locals + the node globals they compose to.
    std::vector<Animator::Trs> m_seedLocals;
    std::vector<glm::mat4> m_snapshotGlobals;

    glm::mat4 m_backRoot{1.0F}; // S(0.01) * inverse(modelToWorld)
    bool m_valid = false;
};

} // namespace engine