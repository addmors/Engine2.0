#include "engine/physics/PhysicsWorld.hpp"

#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Log.hpp"
#include "engine/scene/Terrain.hpp"

namespace engine {

PhysicsWorld::PhysicsWorld()
    : m_collisionConfig()
    , m_dispatcher(&m_collisionConfig)
    , m_broadphase()
    , m_solver()
    , m_world(std::make_unique<btDiscreteDynamicsWorld>(&m_dispatcher, &m_broadphase, &m_solver, &m_collisionConfig))
{
    m_world->setGravity(btVector3(0.0F, -9.81F, 0.0F));
    Log::info("physics world initialized (Bullet 3.25)");
}

PhysicsWorld::~PhysicsWorld()
{
    // Explicit teardown in the order Bullet requires:
    // world first, then bodies, then their motion states/shapes/data.
    m_world.reset();
    m_bodies.clear();
    m_motionStates.clear();
    m_shapes.clear();
    m_heightfieldBuffers.clear();
}

void PhysicsWorld::setGravity(glm::vec3 gravity)
{
    m_world->setGravity(btVector3(gravity.x, gravity.y, gravity.z));
}

void PhysicsWorld::addStaticTerrain(const Terrain& terrain)
{
    const size_t resolution = terrain.resolution();
    const float spacing = terrain.worldSize() / static_cast<float>(resolution - 1);

    // Bullet requires a mutable raw pointer -> keep our own copy alive.
    m_heightfieldBuffers.emplace_back(terrain.heights().begin(), terrain.heights().end());
    std::vector<float>& heights = m_heightfieldBuffers.back();

    auto shape = std::make_unique<btHeightfieldTerrainShape>(
        static_cast<int>(resolution), static_cast<int>(resolution), heights.data(),
        1.0F, terrain.minHeight(), terrain.maxHeight(),
        1 /* up axis Y */, PHY_FLOAT, false);
    shape->setLocalScaling(btVector3(spacing, 1.0F, spacing));

    // Diagnostic: verify the collider occupies the expected world volume.
    {
        btVector3 aabbMin(0, 0, 0);
        btVector3 aabbMax(0, 0, 0);
        shape->getAabb(btTransform::getIdentity(), aabbMin, aabbMax);
        Log::info("heightfield aabb: [{:.1f} .. {:.1f}] x [{:.1f} .. {:.1f}] x [{:.1f} .. {:.1f}]",
            aabbMin.x(), aabbMax.x(), aabbMin.y(), aabbMax.y(), aabbMin.z(), aabbMax.z());
    }

    btTransform groundTransform;
    groundTransform.setIdentity();
    groundTransform.setOrigin(btVector3(0.0F, (terrain.minHeight() + terrain.maxHeight()) / 2.0F, 0.0F));

    auto motionState = std::make_unique<btDefaultMotionState>(groundTransform);
    btRigidBody::btRigidBodyConstructionInfo info(0.0F, motionState.get(), shape.get(), btVector3(0, 0, 0));
    auto body = std::make_unique<btRigidBody>(info);
    body->setFriction(1.0F);

    m_world->addRigidBody(body.get());

    m_shapes.push_back(std::move(shape));
    m_motionStates.push_back(std::move(motionState));
    m_bodies.push_back(std::move(body));

    Log::info("terrain collider added: {}x{} heightfield, spacing {:.2f}", resolution, resolution, spacing);
}

btRigidBody* PhysicsWorld::spawnBody(const PhysicsBodyDesc& desc)
{
    std::unique_ptr<btCollisionShape> shape;
    switch (desc.shape) {
    case PhysicsBodyDesc::Shape::Sphere:
        shape = std::make_unique<btSphereShape>(desc.extents.x);
        break;
    case PhysicsBodyDesc::Shape::Box:
        shape = std::make_unique<btBoxShape>(btVector3(desc.extents.x, desc.extents.y, desc.extents.z));
        break;
    case PhysicsBodyDesc::Shape::Cylinder:
        shape = std::make_unique<btCylinderShape>(btVector3(desc.extents.x, desc.extents.y, desc.extents.x));
        break;
    }

    btTransform start;
    start.setIdentity();
    start.setOrigin(btVector3(desc.position.x, desc.position.y, desc.position.z));

    auto motionState = std::make_unique<btDefaultMotionState>(start);
    btVector3 localInertia(0.0F, 0.0F, 0.0F);
    if (desc.mass > 0.0F) {
        shape->calculateLocalInertia(desc.mass, localInertia);
    }

    btRigidBody::btRigidBodyConstructionInfo info(desc.mass, motionState.get(), shape.get(), localInertia);
    auto body = std::make_unique<btRigidBody>(info);
    body->setLinearVelocity(btVector3(desc.initialVelocity.x, desc.initialVelocity.y, desc.initialVelocity.z));
    body->setFriction(0.7F);
    body->setRestitution(0.3F);
    body->setUserIndex(static_cast<int>(desc.shape)); // shape kind for snapshot grouping

    btRigidBody* raw = body.get();
    m_world->addRigidBody(raw);

    m_shapes.push_back(std::move(shape));
    m_motionStates.push_back(std::move(motionState));
    m_bodies.push_back(std::move(body));
    return raw;
}

void PhysicsWorld::step(float deltaSeconds)
{
    constexpr int maxSubSteps = 5;
    constexpr btScalar fixedTimeStep = btScalar(1.0) / btScalar(120.0);
    m_world->stepSimulation(btScalar(deltaSeconds), maxSubSteps, fixedTimeStep);
}

std::vector<PhysicsWorld::BodySnapshot> PhysicsWorld::snapshots() const
{
    std::vector<BodySnapshot> out;
    out.reserve(m_bodies.size());

    for (const auto& body : m_bodies) {
        if (body->getMass() <= 0.0F) {
            continue; // static geometry is drawn by dedicated renderers
        }
        btTransform transform;
        body->getMotionState()->getWorldTransform(transform);

        BodySnapshot snapshot;
        snapshot.shape = static_cast<PhysicsBodyDesc::Shape>(body->getUserIndex());
        const btVector3& origin = transform.getOrigin();
        snapshot.position = {origin.x(), origin.y(), origin.z()};
        const btQuaternion& rotation = transform.getRotation();
        snapshot.rotation = {rotation.x(), rotation.y(), rotation.z(), rotation.w()};
        out.push_back(snapshot);
    }
    return out;
}

} // namespace engine
