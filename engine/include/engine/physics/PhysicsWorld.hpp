#pragma once

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/BroadphaseCollision/btDbvtBroadphase.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
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
    glm::vec3 extents{};    // sphere: x = radius; box: half-extents (spans 2*extents); cylinder: x = radius, y = half height
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

    // Fixed substep the world simulates with; also used by the character
    // controller to convert walk speed into a per-substep displacement.
    [[nodiscard]] static constexpr float fixedStepTime() { return 1.0F / 120.0F; }

    struct BodySnapshot {
        PhysicsBodyDesc::Shape shape = PhysicsBodyDesc::Shape::Sphere;
        glm::vec3 position{};
        glm::quat rotation{};
        // Collider size in PhysicsBodyDesc terms, read back from the live
        // shape (sphere: x = radius; box: half-extents; cylinder: x = radius,
        // y = half height). The renderer scales its unit-template meshes by
        // these, so visuals can never drift from the collider again — the box
        // used to render exactly half its collider (btBoxShape takes
        // half-extents while the cube template is a 1x1x1 unit mesh).
        glm::vec3 extents{};
    };

    // World transforms of all dynamic bodies, in spawn order (renderer feeds
    // its instance buffers from this).
    [[nodiscard]] std::vector<BodySnapshot> snapshots() const;

    [[nodiscard]] size_t bodyCount() const { return m_bodies.size(); }
    [[nodiscard]] btDynamicsWorld* world() { return m_world.get(); }

private:
    // Declared first so it is destroyed last: the broadphase's pair cache
    // keeps a raw pointer to it as long as it lives. It is what tells
    // btPairCachingGhostObject-s (the kinematic character) about overlaps;
    // without it the character's sweeps see an empty pair list and it falls
    // through everything.
    btGhostPairCallback m_ghostPairCallback;

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
