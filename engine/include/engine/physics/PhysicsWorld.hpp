#pragma once

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <vector>

namespace engine {

class Terrain;

struct PhysicsBodyDesc {
    enum class Shape {
        Sphere,
        Box,
        Cylinder,
    };

    Shape shape = Shape::Sphere;
    glm::vec3 position{};   // world position of the body center
    glm::vec3 extents{};    // sphere: x = radius; box: full half-extents; cylinder: x = radius, y = half height
    float mass = 1.0F;
    glm::vec3 initialVelocity{};
};

// Clean rewrite of the 2017 `pEngine`: Bullet dynamics world with typed body
// spawning and per-frame snapshots for instanced rendering.
class PhysicsWorld final {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void setGravity(glm::vec3 gravity);

    // Static heightfield collider built from the terrain height data.
    void addStaticTerrain(const Terrain& terrain);

    // Spawns a dynamic body; ownership stays with the world.
    [[nodiscard]] btRigidBody* spawnBody(const PhysicsBodyDesc& desc);

    // Advances the simulation with a fixed substep of 1/120 s.
    void step(float deltaSeconds);

    struct BodySnapshot {
        PhysicsBodyDesc::Shape shape = PhysicsBodyDesc::Shape::Sphere;
        glm::vec3 position{};
        glm::quat rotation{};
    };

    // World transforms of all dynamic bodies, in spawn order (renderer feeds
    // its instance buffers from this).
    [[nodiscard]] std::vector<BodySnapshot> snapshots() const;

    [[nodiscard]] size_t bodyCount() const { return m_bodies.size(); }
    [[nodiscard]] btDynamicsWorld* world() { return m_world.get(); }

private:
    btDefaultCollisionConfiguration m_collisionConfig;
    btCollisionDispatcher m_dispatcher;
    btDbvtBroadphase m_broadphase;
    btSequentialImpulseConstraintSolver m_solver;
    std::unique_ptr<btDiscreteDynamicsWorld> m_world;

    // Bullet objects must outlive the world -> owned here in destruction order.
    std::vector<std::unique_ptr<btCollisionShape>> m_shapes;
    std::vector<std::unique_ptr<btDefaultMotionState>> m_motionStates;
    std::vector<std::unique_ptr<btRigidBody>> m_bodies;
    std::vector<std::vector<float>> m_heightfieldBuffers; // keeps raw height data for heightfield shapes
};

} // namespace engine
