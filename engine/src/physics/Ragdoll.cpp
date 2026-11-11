#include "engine/physics/Ragdoll.hpp"

#include <array>
#include <cmath>

#include "engine/core/Log.hpp"
#include "engine/physics/PhysicsWorld.hpp"

namespace engine {
namespace {

// One capsule per part; the segment = [bone joint .. endBone joint] of the
// rig, so bodies always match wherever the clips (or the seed pose) put the
// skeleton. Masses sum to ~79 kg for the 1.8 m avatar.
struct PartDef {
    const char* bone;
    const char* endBone;
    float radius; // world units
    float mass;   // kg
};

// Order = parents before children (indices referenced by the joint table).
constexpr std::array<PartDef, 15> kPartDefs{{
    {"Hips", "Spine", 0.11F, 14.0F},                 // 0 pelvis
    {"Spine", "Head", 0.12F, 22.0F},                 // 1 torso (Spine..Spine2 rigid)
    {"Head", "HeadTop_End", 0.085F, 5.0F},           // 2 head
    {"LeftUpLeg", "LeftLeg", 0.075F, 9.0F},          // 3 left thigh
    {"LeftLeg", "LeftFoot", 0.06F, 5.0F},            // 4 left shin
    {"LeftFoot", "LeftToeBase", 0.045F, 1.2F},       // 5 left foot
    {"RightUpLeg", "RightLeg", 0.075F, 9.0F},        // 6 right thigh
    {"RightLeg", "RightFoot", 0.06F, 5.0F},          // 7 right shin
    {"RightFoot", "RightToeBase", 0.045F, 1.2F},     // 8 right foot
    {"LeftArm", "LeftForeArm", 0.05F, 2.2F},         // 9 left upper arm
    {"LeftForeArm", "LeftHand", 0.045F, 1.4F},       // 10 left forearm
    {"LeftHand", "LeftHandMiddle1", 0.035F, 0.4F},   // 11 left hand (fingers stay posed)
    {"RightArm", "RightForeArm", 0.05F, 2.2F},       // 12 right upper arm
    {"RightForeArm", "RightHand", 0.045F, 1.4F},     // 13 right forearm
    {"RightHand", "RightHandMiddle1", 0.035F, 0.4F}, // 14 right hand (fingers stay posed)
}};

// btMatrix3x3 stores rows, glm stores columns.
glm::mat4 toGlm(const btTransform& t)
{
    const btMatrix3x3& basis = t.getBasis();
    const btVector3& origin = t.getOrigin();
    glm::mat4 m{1.0F};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            m[column][row] = basis[row][column];
        }
    }
    m[3] = glm::vec4{origin.x(), origin.y(), origin.z(), 1.0F};
    return m;
}

btVector3 toBt(const glm::vec3& v)
{
    return btVector3{v.x, v.y, v.z};
}

// Quaternion rotating +Y onto `dir` (short arc): capsule bodies use +Y as
// their axis, so this orients the collider along the bone segment.
glm::quat yRotationTo(const glm::vec3& dir)
{
    const glm::vec3 up{0.0F, 1.0F, 0.0F};
    const float d = glm::dot(up, dir);
    if (d > 0.9995F) {
        return glm::quat{1.0F, 0.0F, 0.0F, 0.0F};
    }
    if (d < -0.9995F) { // 180 degrees: any axis perpendicular to +Y
        return glm::angleAxis(glm::pi<float>(), glm::vec3{0.0F, 0.0F, 1.0F});
    }
    const glm::vec3 axis = glm::cross(up, dir);
    return glm::normalize(glm::quat{1.0F + d, axis.x, axis.y, axis.z});
}

// World-space frame whose X axis = `dir`. btConeTwistConstraint twists around
// its frame's X and swings around the perpendicular axes, so with X aligned
// to the bone direction the limits mean: swing = bend away from the bone,
// twist = roll around the bone — for ANY animated pose. The frame is then
// pulled into each body's local space, which keeps the seed pose exactly on
// zero deviation.
btTransform worldTwistFrame(const glm::vec3& dir, const glm::vec3& pivot)
{
    const glm::vec3 x = dir;
    glm::vec3 helper{0.0F, 1.0F, 0.0F};
    if (std::abs(glm::dot(x, helper)) > 0.95F) {
        helper = {0.0F, 0.0F, 1.0F};
    }
    const glm::vec3 y = glm::normalize(glm::cross(helper, x));
    const glm::vec3 z = glm::normalize(glm::cross(x, y));
    btTransform frame;
    frame.setBasis(btMatrix3x3{
        x.x, y.x, z.x, // row 0 = X components of the three basis columns
        x.y, y.y, z.y, //
        x.z, y.z, z.z});
    frame.setOrigin(toBt(pivot));
    return frame;
}

// `v` projected onto the plane perpendicular to `ref` and normalized; when
// `v` is (nearly) parallel to `ref`, any perpendicular to `ref` is returned.
glm::vec3 orthogonalized(const glm::vec3& v, const glm::vec3& ref)
{
    glm::vec3 out = v - ref * glm::dot(v, ref);
    if (glm::length(out) < 1e-5F) {
        const glm::vec3 helper = std::abs(ref.y) < 0.95F ? glm::vec3{0.0F, 1.0F, 0.0F}
                                                         : glm::vec3{1.0F, 0.0F, 0.0F};
        out = helper - ref * glm::dot(helper, ref);
    }
    return glm::normalize(out);
}

// World-space hinge frame at `pivot`: Z = hinge axis (btHingeConstraint
// rotates around its frame's Z), X = `refDir` orthogonalized in-plane as the
// angle reference. Both bodies receive this SAME world frame, so the seed
// pose sits at exactly 0 rad and setLimit() ranges are seed-relative.
btTransform worldHingeFrame(const glm::vec3& pivot, const glm::vec3& axis,
    const glm::vec3& refDir)
{
    const glm::vec3 z = glm::normalize(axis);
    const glm::vec3 x = orthogonalized(refDir, z);
    const glm::vec3 y = glm::normalize(glm::cross(z, x));
    btTransform frame;
    frame.setBasis(btMatrix3x3{
        x.x, y.x, z.x, // row 0 = X components of the three basis columns
        x.y, y.y, z.y, //
        x.z, y.z, z.z});
    frame.setOrigin(toBt(pivot));
    return frame;
}

} // namespace

Ragdoll::Ragdoll(PhysicsWorld& world, Animator& animator, const glm::mat4& modelToWorld,
    bool enableSelfCollision)
    : m_world(world)
    , m_animator(animator)
    , m_selfCollide(enableSelfCollision)
{
    // Seed pose: everything the ragdoll does not drive stays on this
    // snapshot (neck, shoulders, fingers...), and the node globals resolve
    // the bone joints the bodies are seeded from.
    m_seedLocals = animator.snapshotPose();
    m_snapshotGlobals.resize(static_cast<size_t>(animator.nodeCount()));
    for (size_t i = 0; i < m_snapshotGlobals.size(); ++i) {
        m_snapshotGlobals[i] = animator.nodeGlobalTransform(static_cast<int>(i));
    }
    m_backRoot = glm::scale(glm::mat4{1.0F}, glm::vec3{0.01F}) * glm::inverse(modelToWorld);

    for (const PartDef& def : kPartDefs) {
        Part& part = m_parts.emplace_back();
        part.bone = def.bone;
        part.endBone = def.endBone;
        part.radius = def.radius;
        part.mass = def.mass;
        if (!buildPart(part, modelToWorld)) {
            return; // m_valid stays false; the destructor detaches what exists
        }
    }

    for (Part& part : m_parts) {
        resolveParent(part);
    }

    // Medio-lateral body axis from the shoulder line (left shoulder joint ->
    // right shoulder joint, toward the character's right): the knee/elbow
    // hinges flex around it, human-style, in whatever pose the seed is.
    if (m_parts.size() > 12) {
        const glm::vec3 leftToRight = m_parts[12].startJoint - m_parts[9].startJoint;
        if (glm::length(leftToRight) > 1e-5F) {
            m_characterRight = glm::normalize(leftToRight);
        }
    }

    buildJoints();

    m_valid = true;
    Log::info("ragdoll built: {} bodies, {} joints ({} ball, {} hinge), self-collision: {}",
        m_parts.size(), m_joints.size(), m_joints.size() - m_hinges.size(), m_hinges.size(),
        m_selfCollide ? "on" : "off");
}

Ragdoll::~Ragdoll()
{
    // Mirror of Bullet's ragdoll teardown order: constraints first, then
    // bodies, then motion states/shapes (dropped with m_parts).
    for (const auto& joint : m_joints) {
        if (joint != nullptr) {
            m_world.world()->removeConstraint(joint.get());
        }
    }
    m_joints.clear();
    for (Part& part : m_parts) {
        if (part.body != nullptr) {
            m_world.world()->removeRigidBody(part.body.get());
        }
    }
    m_parts.clear();
}

bool Ragdoll::buildPart(Part& part, const glm::mat4& modelToWorld)
{
    part.nodeIndex = m_animator.nodeIndexForBone(part.bone);
    if (part.nodeIndex < 0) {
        Log::error("Ragdoll: bone '{}' not found in the rig", part.bone);
        return false;
    }
    part.endNodeIndex = part.endBone != nullptr ? m_animator.nodeIndexForBone(part.endBone) : -1;

    // S(100): assimp wraps the node tree in a 0.01 root scale (see
    // Animator::composePalette) — bone globals must land in the rig's
    // centimeter space before modelToWorld, the matrix drawSkinned uses.
    static const glm::mat4 toCentimeters = glm::scale(glm::mat4{1.0F}, glm::vec3{100.0F});
    part.boneWorldSeed
        = modelToWorld * toCentimeters * m_animator.nodeGlobalTransform(part.nodeIndex);
    part.startJoint = glm::vec3{part.boneWorldSeed[3]}; // a bone's origin IS its joint

    // Segment end = the child joint; defines the capsule's extent.
    glm::vec3 endJoint = part.startJoint;
    if (part.endNodeIndex >= 0) {
        const glm::mat4 endWorld
            = modelToWorld * toCentimeters * m_animator.nodeGlobalTransform(part.endNodeIndex);
        endJoint = glm::vec3{endWorld[3]};
    } else {
        // Missing end bone: short stub along the bone's world X axis.
        endJoint += glm::mat3(part.boneWorldSeed) * glm::vec3{0.06F, 0.0F, 0.0F};
    }
    const glm::vec3 segment = endJoint - part.startJoint;
    const float length = glm::length(segment);
    part.dirSeed = length > 1e-5F
        ? segment / length
        : glm::normalize(glm::mat3(part.boneWorldSeed) * glm::vec3{1.0F, 0.0F, 0.0F});
    const float cylinder = glm::max(length - 2.0F * part.radius, 0.01F);

    part.shape = std::make_unique<btCapsuleShape>(btScalar(part.radius), btScalar(cylinder));
    part.bodySeed.setIdentity();
    const glm::quat axisRotation = yRotationTo(part.dirSeed);
    part.bodySeed.setRotation(btQuaternion(axisRotation.x, axisRotation.y, axisRotation.z, axisRotation.w));
    part.bodySeed.setOrigin(toBt(0.5F * (part.startJoint + endJoint)));
    part.invBodySeed = glm::inverse(toGlm(part.bodySeed));

    btVector3 localInertia(0.0F, 0.0F, 0.0F);
    part.shape->calculateLocalInertia(btScalar(part.mass), localInertia);
    part.motionState = std::make_unique<btDefaultMotionState>(part.bodySeed);
    btRigidBody::btRigidBodyConstructionInfo info(
        btScalar(part.mass), part.motionState.get(), part.shape.get(), localInertia);
    part.body = std::make_unique<btRigidBody>(info);

    // The canonical Bullet ragdoll feel: heavy angular damping (capsules
    // otherwise roll forever) and soft sleep thresholds so the pile settles
    // instead of jittering.
    part.body->setDamping(btScalar(0.05F), btScalar(0.85F));
    part.body->setSleepingThresholds(btScalar(0.1F), btScalar(0.1F));
    part.body->setFriction(btScalar(0.8F));
    part.body->setRestitution(btScalar(0.05F));
    // CCD for thin fast limbs vs. the heightfield's large flat triangles.
    part.body->setCcdMotionThreshold(btScalar(0.2F));
    part.body->setCcdSweptSphereRadius(btScalar(part.radius * 0.5F));

    // DebrisFilter group keeps the ragdoll OFF the player capsule's ghost
    // (CharacterFilter): the bodies are seeded exactly on that capsule, and
    // any ghost pair would shove them away on the first step. Terrain
    // (StaticFilter), spawned props (DefaultFilter) and other ragdoll parts
    // still collide; linked pairs are excluded per joint below (unless
    // self-collision was requested).
    m_world.world()->addRigidBody(part.body.get(),
        btBroadphaseProxy::DebrisFilter,
        btBroadphaseProxy::DefaultFilter | btBroadphaseProxy::StaticFilter
            | btBroadphaseProxy::DebrisFilter);
    return true;
}

void Ragdoll::resolveParent(Part& part)
{
    part.parentNode = m_animator.nodeParentIndex(part.nodeIndex);
    if (part.parentNode < 0) {
        return; // root chain: parent global is identity
    }

    // Direct part parent (e.g. Leg -> UpLeg)?
    for (size_t i = 0; i < m_parts.size(); ++i) {
        if (m_parts[i].nodeIndex == part.parentNode) {
            part.parentIsPart = true;
            part.parentPart = static_cast<int>(i);
            return;
        }
    }

    // Nearest driven ancestor (e.g. Head -> Neck -> ... -> Spine[torso part]):
    // its frozen snapshot offset composes the parent's current global.
    int ancestor = m_animator.nodeParentIndex(part.parentNode);
    while (ancestor >= 0) {
        for (size_t i = 0; i < m_parts.size(); ++i) {
            if (m_parts[i].nodeIndex == ancestor) {
                part.drivenAncestor = static_cast<int>(i);
                part.parentRel = glm::inverse(m_snapshotGlobals[static_cast<size_t>(ancestor)])
                    * m_snapshotGlobals[static_cast<size_t>(part.parentNode)];
                return;
            }
        }
        ancestor = m_animator.nodeParentIndex(ancestor);
    }
    // No driven ancestor (root chain above the hips): the parent global is
    // the constant snapshot global — nothing more to precompute.
}

void Ragdoll::buildJoints()
{
    // Human-like joint anatomy; every limit is a deviation from the seed pose
    // (0 rad = exactly the pose at activation):
    // - waist/neck: tight cones — the spine barely folds or twists, the head
    //   nods and turns but never spins;
    // - shoulders/hips: generous cones, small roll;
    // - wrists/ankles: small cones;
    // - knees/elbows: HINGES around the body's medio-lateral axis (the
    //   shoulder line) — the shin only folds heel-to-butt, the forearm only
    //   folds hand-to-shoulder; no hyperextension, no backwards bending, no
    //   forearm spin.
    // Hinge angle sign (Bullet: atan2 of the frames' Y axes, referenceSign=+1
    // for useReferenceFrameA=false): positive hinge angle = the child segment
    // rotates left-handed around the axis. With the axis pointing toward the
    // character's right, knee flexion (heel backwards) is positive and elbow
    // flexion (hand forwards) is negative.
    enum class Kind {
        Ball,  // cone-twist: swing cone + twist roll around the bone
        Hinge, // one axis with a signed range (knees, elbows)
    };
    struct JointSpec {
        int parent;
        int child;
        Kind kind;
        float swing1;
        float swing2;
        float twist;
        float low;
        float high;
    };
    constexpr std::array<JointSpec, 14> kSpecs{{
        {0, 1, Kind::Ball, 0.50F, 0.50F, 0.30F, 0.0F, 0.0F},    // waist
        {1, 2, Kind::Ball, 0.55F, 0.55F, 0.40F, 0.0F, 0.0F},    // neck
        {0, 3, Kind::Ball, 0.90F, 0.90F, 0.35F, 0.0F, 0.0F},    // left hip
        {3, 4, Kind::Hinge, 0.0F, 0.0F, 0.0F, -0.10F, 2.40F},   // left knee
        {4, 5, Kind::Ball, 0.45F, 0.45F, 0.25F, 0.0F, 0.0F},    // left ankle
        {0, 6, Kind::Ball, 0.90F, 0.90F, 0.35F, 0.0F, 0.0F},    // right hip
        {6, 7, Kind::Hinge, 0.0F, 0.0F, 0.0F, -0.10F, 2.40F},   // right knee
        {7, 8, Kind::Ball, 0.45F, 0.45F, 0.25F, 0.0F, 0.0F},    // right ankle
        {1, 9, Kind::Ball, 1.80F, 1.80F, 0.70F, 0.0F, 0.0F},    // left shoulder
        {9, 10, Kind::Hinge, 0.0F, 0.0F, 0.0F, -2.50F, 0.10F},  // left elbow
        {10, 11, Kind::Ball, 0.50F, 0.50F, 0.25F, 0.0F, 0.0F},  // left wrist
        {1, 12, Kind::Ball, 1.80F, 1.80F, 0.70F, 0.0F, 0.0F},   // right shoulder
        {12, 13, Kind::Hinge, 0.0F, 0.0F, 0.0F, -2.50F, 0.10F}, // right elbow
        {13, 14, Kind::Ball, 0.50F, 0.50F, 0.25F, 0.0F, 0.0F},  // right wrist
    }};

    m_joints.reserve(kSpecs.size());
    for (const JointSpec& spec : kSpecs) {
        const Part& parent = m_parts[static_cast<size_t>(spec.parent)];
        const Part& child = m_parts[static_cast<size_t>(spec.child)];

        // The joint pivots at the child part's start joint (its bone origin).
        const btVector3 pivot = toBt(child.startJoint);
        std::unique_ptr<btTypedConstraint> joint;
        if (spec.kind == Kind::Ball) {
            // Cone around the CHILD's bone direction: both frames map to the
            // same world frame at seed, so the seed sits at exactly zero
            // swing/twist. (Frames on parent.dir/child.dir separately would
            // put the seed pose itself far outside the cone whenever the
            // bones are not collinear — hips, shoulders, ankles.)
            const btTransform frameA
                = parent.bodySeed.inverse() * worldTwistFrame(child.dirSeed, child.startJoint);
            const btTransform frameB
                = child.bodySeed.inverse() * worldTwistFrame(child.dirSeed, child.startJoint);
            auto ball = std::make_unique<btConeTwistConstraint>(
                *parent.body, *child.body, frameA, frameB);
            ball->setLimit(btScalar(spec.swing1), btScalar(spec.swing2), btScalar(spec.twist));
            joint = std::move(ball);
        } else {
            // Hinge around the body's medio-lateral axis, orthogonalized
            // against the parent's bone direction; the shared world frame
            // puts the seed pose at exactly 0 rad. Flexion ranges (rad): the
            // knee folds up to +2.4, the elbow to -2.5; ±0.1 rad of
            // straightening margin so the seed pose is not a hard stop.
            const glm::vec3 axis = orthogonalized(m_characterRight, parent.dirSeed);
            const btTransform frameA
                = parent.bodySeed.inverse() * worldHingeFrame(child.startJoint, axis, parent.dirSeed);
            const btTransform frameB
                = child.bodySeed.inverse() * worldHingeFrame(child.startJoint, axis, parent.dirSeed);
            auto hinge
                = std::make_unique<btHingeConstraint>(*parent.body, *child.body, frameA, frameB);
            hinge->setLimit(btScalar(spec.low), btScalar(spec.high));
            m_hinges.push_back(hinge.get());
            joint = std::move(hinge);
        }

        // Diagnostics: both anchor points (co-located while the joint holds).
        m_jointProbes.push_back({parent.body.get(), child.body.get(),
            parent.bodySeed.inverse()(pivot), child.bodySeed.inverse()(pivot)});
        // Adjacent capsules overlap at the joints by design; contacts there
        // fight the constraints (plank-like bracing during the fall +
        // persistent convulsive jitter), so they are ALWAYS excluded from
        // collision, in both modes. m_selfCollide only decides whether the
        // NON-adjacent parts collide (hands resting on the chest, crossed
        // legs) — see below.
        m_world.world()->addConstraint(joint.get(), true);
        m_joints.push_back(std::move(joint));
    }

    if (!m_selfCollide) {
        // Smooth mode: no ragdoll-internal contacts at all. Every pair the
        // constraint refs did not already cover (all non-adjacent parts)
        // goes into the same ignore list. Limbs pass through each other,
        // but no contact ever fights a joint.
        // setIgnoreCollisionCheck asserts on duplicates, which is why the
        // adjacent pairs (already registered above) are skipped here.
        constexpr size_t kPartCount = kSpecs.size() + 1; // + the pelvis root
        static_assert(kPartCount == 15, "adjacency matrix assumes 15 parts");
        bool adjacent[kPartCount][kPartCount]{};
        for (const JointSpec& spec : kSpecs) {
            adjacent[spec.parent][spec.child] = true;
            adjacent[spec.child][spec.parent] = true;
        }
        for (size_t i = 0; i < m_parts.size(); ++i) {
            for (size_t j = i + 1; j < m_parts.size(); ++j) {
                if (!adjacent[i][j]) {
                    m_parts[i].body->setIgnoreCollisionCheck(m_parts[j].body.get(), true);
                }
            }
        }
    }
}

void Ragdoll::update()
{
    if (!m_valid) {
        return;
    }

    // 1) Body transforms -> driven node globals: the rigid delta between the
    // current and the seeded capsule transform, applied to the seeded bone
    // world global, mapped back into the node space composePalette works in.
    for (Part& part : m_parts) {
        btTransform bodyNow;
        part.body->getMotionState()->getWorldTransform(bodyNow);
        const glm::mat4 delta = toGlm(bodyNow) * part.invBodySeed;
        part.g_mCurrent = m_backRoot * (delta * part.boneWorldSeed);
    }

    // 2) Driven node locals = inverse(parent global) * own global; everything
    // else keeps the seed pose (fingers, neck, shoulders...). Then one
    // composePalette pass rebuilds the skinning matrices.
    std::vector<Animator::Trs> locals = m_seedLocals;
    for (Part& part : m_parts) {
        glm::mat4 parentGlobal{1.0F};
        if (part.parentIsPart) {
            parentGlobal = m_parts[static_cast<size_t>(part.parentPart)].g_mCurrent;
        } else if (part.drivenAncestor >= 0) {
            parentGlobal
                = m_parts[static_cast<size_t>(part.drivenAncestor)].g_mCurrent * part.parentRel;
        } else if (part.parentNode >= 0) {
            parentGlobal = m_snapshotGlobals[static_cast<size_t>(part.parentNode)];
        }
        locals[static_cast<size_t>(part.nodeIndex)]
            = Animator::decomposeTrs(glm::inverse(parentGlobal) * part.g_mCurrent);
    }
    m_animator.overridePose(locals);
}

glm::vec3 Ragdoll::hipsPosition() const
{
    if (m_parts.empty() || m_parts.front().body == nullptr) {
        return glm::vec3{0.0F};
    }
    btTransform transform;
    m_parts.front().body->getMotionState()->getWorldTransform(transform);
    const btVector3& origin = transform.getOrigin();
    return {origin.x(), origin.y(), origin.z()};
}

float Ragdoll::maxJointStretch() const
{
    float worst = 0.0F;
    for (const JointProbe& probe : m_jointProbes) {
        // World gap between the two anchor points of one joint: 0.0 while
        // the joint holds exactly, growing when the solver lets the pair
        // drift apart (contacts fighting the constraint).
        const btVector3 pivotA = probe.bodyA->getWorldTransform()(probe.pivotInA);
        const btVector3 pivotB = probe.bodyB->getWorldTransform()(probe.pivotInB);
        worst = glm::max(worst, (pivotA - pivotB).length());
    }
    return worst;
}

float Ragdoll::maxBodySpeed() const
{
    float worst = 0.0F;
    for (const Part& part : m_parts) {
        if (part.body != nullptr) {
            worst = glm::max(worst, part.body->getLinearVelocity().length());
        }
    }
    return worst;
}

std::vector<float> Ragdoll::hingeAngles()
{
    std::vector<float> angles;
    angles.reserve(m_hinges.size());
    for (btHingeConstraint* hinge : m_hinges) {
        angles.push_back(static_cast<float>(hinge->getHingeAngle()));
    }
    return angles;
}

} // namespace engine