#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string_view>

#include <imgui.h>

#include <engine/assets/Animator.hpp>
#include <engine/assets/Model.hpp>
#include <engine/core/Input.hpp>
#include <engine/core/Log.hpp>
#include <engine/core/Time.hpp>
#include <engine/platform/Window.hpp>
#include <engine/physics/PhysicsWorld.hpp>
#include <engine/physics/PlayerController.hpp>
#include <engine/physics/Ragdoll.hpp>
#include <engine/renderers/gl/GlRenderer.hpp>
#include <engine/scene/Camera.hpp>
#include <engine/scene/Entity.hpp>
#include <engine/scene/Frustum.hpp>
#include <engine/scene/Primitives.hpp>
#include <engine/scene/Terrain.hpp>
#include <engine/scene/TerrainMesh.hpp>
#include <engine/ui/Ui.hpp>

namespace {

// Full-viewport dockspace so panels can snap anywhere, like a real editor.
// The host window is transparent and the central dock node is pass-through,
// so the 3D scene stays visible and keeps receiving mouse clicks underneath.
void drawDockspace(const ImGuiIO& io)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0.0F); // scene renders underneath the host

    constexpr ImGuiWindowFlags windowFlags
        = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::Begin("##DockSpaceHost", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0) {
        constexpr ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
        const ImGuiID dockspaceId = ImGui::GetID("CandyDockspace");
        ImGui::DockSpace(dockspaceId, ImVec2{0.0F, 0.0F}, dockspaceFlags);
    }
    ImGui::End();
}

void drawStatsWindow(const engine::FrameClock& clock, int frameIndex, bool& showDemo)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2{viewport->WorkPos.x + 12.0F, viewport->WorkPos.y + 12.0F},
        ImGuiCond_FirstUseEver, ImVec2{0.0F, 0.0F});
    ImGui::SetNextWindowSize(ImVec2{270.0F, 130.0F}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Candy");
    ImGui::Text("fps: %.1f (%.2f ms)", clock.fps(), 1000.0F * clock.smoothedDelta());
    ImGui::Text("frame: %d", frameIndex);
    ImGui::TextDisabled("F1: toggle UI");
    ImGui::Checkbox("ImGui demo window", &showDemo);
    ImGui::End();
}

void drawAnimationPanel(engine::Animator& animator)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2{viewport->WorkPos.x + viewport->WorkSize.x - 292.0F,
        viewport->WorkPos.y + 12.0F}, ImGuiCond_FirstUseEver, ImVec2{0.0F, 0.0F});
    ImGui::SetNextWindowSize(ImVec2{280.0F, 176.0F}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Animation")) {
        ImGui::Text("clips: %d", static_cast<int>(animator.clipCount()));
        ImGui::Text("time: %.2f s (norm %.2f)", animator.currentTime(), animator.normalizedTime());

        int clip = static_cast<int>(animator.currentClip());
        if (ImGui::SliderInt("clip", &clip, 0, static_cast<int>(animator.clipCount()) - 1)) {
            animator.crossFadeTo(static_cast<size_t>(clip));
        }

        ImGui::Separator();
        ImGui::Text("bone palette: %d matrices", static_cast<int>(animator.bonePalette().size()));
    }
    ImGui::End();
}

// Instance scale matrix mapping a body's collider dimensions (snapshot
// extents, PhysicsBodyDesc terms) onto the shared unit-template meshes:
// box template = unit cube (full size 1), sphere template = radius 0.5,
// cylinder template = radius 0.5 / half-height 1.0. btBoxShape takes
// half-extents, so a spawned {1,1,1} box collider is 2x2x2 and must render
// 2x2x2 — scaling from the collider's true size instead of trusting the
// spawn constants is what keeps the visual glued to the collider.
glm::mat4 shapeInstanceScale(engine::PhysicsBodyDesc::Shape shape, const glm::vec3& extents)
{
    switch (shape) {
    case engine::PhysicsBodyDesc::Shape::Box:
        return glm::scale(glm::mat4{1.0F}, 2.0F * extents);
    case engine::PhysicsBodyDesc::Shape::Sphere:
        return glm::scale(glm::mat4{1.0F}, glm::vec3{2.0F * extents.x});
    case engine::PhysicsBodyDesc::Shape::Cylinder:
        return glm::scale(glm::mat4{1.0F}, glm::vec3{2.0F * extents.x, extents.y, 2.0F * extents.x});
    }
    return glm::mat4{1.0F};
}

void drawPhysicsPanel(engine::PhysicsWorld& physics, bool& ragdollToggleRequested,
    bool& ragdollSelfCollide, bool ragdollActive)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2{viewport->WorkPos.x + 12.0F,
        viewport->WorkPos.y + viewport->WorkSize.y - 232.0F}, ImGuiCond_FirstUseEver,
        ImVec2{0.0F, 0.0F});
    ImGui::SetNextWindowSize(ImVec2{300.0F, 220.0F}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Physics")) {
        ImGui::Text("dynamic bodies: %d", static_cast<int>(physics.bodyCount()));
        ImGui::TextDisabled("keys: B = box, C = cylinder, N = sphere");
        if (ImGui::Button("Spawn box")) {
            physics.spawnBody({engine::PhysicsBodyDesc::Shape::Box, {0.0F, 15.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F});
        }
        ImGui::SameLine();
        if (ImGui::Button("Spawn cylinder")) {
            physics.spawnBody({engine::PhysicsBodyDesc::Shape::Cylinder, {2.0F, 15.0F, 0.0F}, {0.5F, 1.0F, 1.0F}, 1.0F});
        }
        ImGui::SameLine();
        if (ImGui::Button("Spawn sphere")) {
            physics.spawnBody({engine::PhysicsBodyDesc::Shape::Sphere, {-2.0F, 15.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1.0F});
        }

        ImGui::Separator();
        ImGui::Text("avatar ragdoll: %s", ragdollActive ? "active" : "off");
        if (ImGui::Button(ragdollActive ? "Restore animation" : "Make ragdoll")) {
            ragdollToggleRequested = true; // applied in the next frame's update section
        }
        ImGui::Checkbox("limb self-collision (on next activation)", &ragdollSelfCollide);
        ImGui::TextDisabled(ragdollSelfCollide
                ? "non-adjacent parts collide; joint-linked stay filtered"
                : "parts never collide with each other (smoothest)");
    }
    ImGui::End();
}

} // namespace

// M1a asset-layer check: parse the models from the 2017 project and log stats.
// Returns false on failure so the selftest exits with code 1.
bool testAssets()
{
    namespace fs = std::filesystem;
    const fs::path assetsDir{CANDY_ASSETS_DIR};

    bool ok = true;

    auto dragon = engine::Model::loadFromFile((assetsDir / "objects/dragon/ganfaul_m_aure.dae").generic_string());
    if (!dragon) {
        engine::Log::error("dragon load failed: {}", dragon.error().message);
        ok = false;
    } else {
        auto& model = *dragon;
        for (const char* clip : {"Idle.dae", "Walking.dae", "Walking Backwards.dae"}) {
            if (auto r = model.loadAnimations((assetsDir / "objects/dragon" / clip).generic_string()); !r) {
                engine::Log::error("{} failed: {}", clip, r.error().message);
                ok = false;
            }
        }
        for (const auto& a : model.animations()) {
            engine::Log::info("anim '{}': {:.1f} ticks, {:.1f} tps, {} channels",
                a.name, a.duration, a.ticksPerSecond, a.channels.size());
        }
    }

    auto tree = engine::Model::loadFromFile((assetsDir / "objects/tree/conifer_macedonian_pine.fbx").generic_string());
    if (!tree) {
        engine::Log::error("tree load failed: {}", tree.error().message);
        ok = false;
    } else {
        const auto& firstMesh = tree->meshes().front();
        engine::Log::info("tree mesh '{}': {} vertices, {} indices, aabb x[{:.2f}..{:.2f}] y[{:.2f}..{:.2f}] z[{:.2f}..{:.2f}]",
            firstMesh.name, firstMesh.vertices.size(), firstMesh.indices.size(),
            firstMesh.aabb.min.x, firstMesh.aabb.max.x,
            firstMesh.aabb.min.y, firstMesh.aabb.max.y,
            firstMesh.aabb.min.z, firstMesh.aabb.max.z);
    }

    return ok;
}

// M1b checks: animation evaluation and physics simulation.
// Each returns false on failure so the selftest exits with 1.
bool testAnimation()
{
    namespace fs = std::filesystem;
    const fs::path assetsDir{CANDY_ASSETS_DIR};

    auto dragon = engine::Model::loadFromFile((assetsDir / "objects/dragon/ganfaul_m_aure.dae").generic_string());
    if (!dragon) {
        engine::Log::error("testAnimation: dragon load failed: {}", dragon.error().message);
        return false;
    }
    for (const char* clip : {"Idle.dae", "Walking.dae", "Walking Backwards.dae"}) {
        if (auto r = dragon->loadAnimations((assetsDir / "objects/dragon" / clip).generic_string()); !r) {
            engine::Log::error("testAnimation: {} failed: {}", clip, r.error().message);
            return false;
        }
    }

    engine::Animator animator{*dragon};
    if (animator.clipCount() != 4) {
        engine::Log::error("testAnimation: expected 4 clips, got {}", animator.clipCount());
        return false;
    }

    const int boneCount = dragon->boneCount();
    const auto countAnimated = [&animator]() {
        int nonIdentity = 0;
        for (const auto& m : animator.bonePalette()) {
            if (glm::length(glm::vec3{m[3]}) > 1e-3F || glm::abs(m[0].x - 1.0F) > 1e-4F) {
                ++nonIdentity;
            }
        }
        return nonIdentity;
    };

    animator.play(1); // Idle
    for (int i = 0; i < 30; ++i) {
        animator.update(1.0F / 60.0F);
    }
    const int animatedIdle = countAnimated();

    animator.crossFadeTo(2); // Walking
    for (int i = 0; i < 30; ++i) {
        animator.update(1.0F / 60.0F);
    }
    const int animatedWalk = countAnimated();

    // Skeleton-based capsule fit (height = Head.y - Foot.y, radius = half the
    // hips->foot leg length) must produce a sane capsule from THIS rig — it
    // feeds the game's player collider. The model's units are large (bind
    // height ~170), so also verify the world-scale fit (~1.8 m total) that
    // the game derives alongside its mesh-AABB scale.
    const engine::CapsuleFit rigFit = engine::capsuleFitFromRig(*dragon, 1.0F, 0.4F);
    const float rigTotal = rigFit.height + 2.0F * rigFit.radius;
    const engine::CapsuleFit worldFit = engine::capsuleFitFromRig(*dragon, 1.8F / rigTotal, 0.4F);
    const float worldTotal = worldFit.height + 2.0F * worldFit.radius;
    engine::Log::info("testAnimation: rig capsule fit: radius {:.3f}, cylinder {:.3f} (total {:.3f}); world fit: radius {:.3f}, cylinder {:.3f} (total {:.3f})",
        rigFit.radius, rigFit.height, rigTotal, worldFit.radius, worldFit.height, worldTotal);

    engine::Log::info("testAnimation: {} bones, animated in idle {} / walk {}",
        boneCount, animatedIdle, animatedWalk);
    return animator.bonePalette().size() == static_cast<size_t>(boneCount) && animatedIdle >= boneCount / 2
        && animatedWalk >= boneCount / 2 && rigFit.radius > 0.02F && rigFit.radius <= 0.4F + 1e-3F
        && rigFit.height > 4.0F * rigFit.radius && worldTotal > 1.6F && worldTotal < 2.0F
        && worldFit.radius > 0.1F;
}

bool testPhysics()
{
    // Procedural plain + ramp: exact heights (no heightmap quantization), so
    // "resting" means ON the ground at heightAt().
    const engine::Terrain terrain = engine::Terrain::makePlainWithRamp();

    const auto grid = terrain.heights2d();
    engine::Log::info("testPhysics: terrain {}x{}, world size {}, heights [{:.2f}, {:.2f}], heights2d[128,128] = {:.2f}",
        terrain.resolution(), terrain.resolution(), terrain.worldSize(), terrain.minHeight(),
        terrain.maxHeight(), grid[128, 128]);

    engine::PhysicsWorld world;
    world.addStaticTerrain(terrain);

    world.spawnBody({engine::PhysicsBodyDesc::Shape::Box, {0.0F, 20.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F});
    world.spawnBody({engine::PhysicsBodyDesc::Shape::Box, {6.0F, 26.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F});
    world.spawnBody({engine::PhysicsBodyDesc::Shape::Box, {12.0F, 32.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F});
    world.spawnBody({engine::PhysicsBodyDesc::Shape::Sphere, {-6.0F, 32.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1.0F});

    for (int i = 0; i < 480; ++i) {
        world.step(1.0F / 120.0F); // 4 seconds: enough for everything to land
    }

    // The snapshot must report the collider's TRUE size — the renderer scales
    // its unit-template meshes by these extents, and the box visual used to be
    // exactly half its collider (btBoxShape takes half-extents, so a {1,1,1}
    // spawn is a 2x2x2 collider). Sizes round-trip through Bullet's embedded
    // collision margin (half - margin + margin in float), so compare with a
    // small tolerance instead of exact equality.
    const auto nearEq = [](float a, float b) { return (a < b ? b - a : a - b) <= 1e-4F; };
    const auto all = world.snapshots();
    bool extentsOk = all.size() == 4;
    for (size_t i = 0; extentsOk && i < 3; ++i) {
        extentsOk = all[i].shape == engine::PhysicsBodyDesc::Shape::Box
            && nearEq(all[i].extents.x, 1.0F) && nearEq(all[i].extents.y, 1.0F)
            && nearEq(all[i].extents.z, 1.0F);
    }
    if (extentsOk) {
        extentsOk = all[3].shape == engine::PhysicsBodyDesc::Shape::Sphere
            && nearEq(all[3].extents.x, 1.0F) && all[3].extents.y == 0.0F;
    }
    engine::Log::info("testPhysics: snapshot extents {} (3x box half-extents 1, sphere radius 1)",
        extentsOk ? "match colliders" : "MISMATCH");

    bool ok = extentsOk;
    for (const auto& snapshot : world.snapshots()) {
        const float ground = terrain.heightAt(snapshot.position.x, snapshot.position.z);
        // Bodies may stack on each other, so only check they did not fall through.
        const bool resting = snapshot.position.y > ground - 2.0F && snapshot.position.y < ground + 20.0F;
        engine::Log::info("testPhysics: body at ({:.2f}, {:.2f}, {:.2f}), ground {:.2f} [{}]",
            snapshot.position.x, snapshot.position.y, snapshot.position.z, ground, resting ? "resting" : "FAIL");
        ok = ok && resting;
    }

    engine::Log::info("testPhysics: simulation finished, destroying world...");
    return ok;
}

// M1b checks: player controller and frustum culling.
//
// testPlayer doubles as the GROUNDING PROBE: on the procedural plain+ramp
// terrain the ground under any point is known exactly (heightAt), so the
// capsule bottom must hug it at every checkpoint — standing AND walking, on
// the flat plain and on the ramp. This is the regression net for
// "feet always on the ground" (no levitation, no sinking into the floor).
bool testPlayer()
{
    engine::Log::info("testPlayer: begin (grounding probe on plain+ramp terrain)");

    const engine::Terrain terrain = engine::Terrain::makePlainWithRamp();
    engine::Log::info("testPlayer: terrain {}x{}, world {}, heights [{:.2f}, {:.2f}]",
        terrain.resolution(), terrain.resolution(), terrain.worldSize(),
        terrain.minHeight(), terrain.maxHeight());

    engine::PhysicsWorld world;
    world.addStaticTerrain(terrain);

    const glm::vec3 spawn{0.0F, terrain.heightAt(0.0F, 0.0F) + 2.0F, 0.0F};
    // Synthetic humanoid capsule (0.4 radius + 1.0 cylinder = 1.8 m tall) —
    // the shape capsuleFitFromRig derives from the avatar's skeleton in the
    // game. spawn is the FEET point (see PlayerController).
    const engine::CapsuleFit fit{0.4F, 1.0F};
    engine::PlayerController player(world, spawn, fit);
    engine::Log::info("testPlayer: controller spawned at ({:.2f}, {:.2f}, {:.2f})", spawn.x, spawn.y, spawn.z);

    engine::Input input;
    const glm::vec3 forward{0.0F, 0.0F, -1.0F};
    const glm::vec3 right{1.0F, 0.0F, 0.0F};

    bool ok = true;
    float worstGap = 0.0F;
    const auto run = [&](int steps) {
        for (int i = 0; i < steps; ++i) {
            player.update(input, 1.0F / 120.0F, forward, right);
            world.step(1.0F / 120.0F);
        }
    };
    const auto checkpoint = [&](const char* label, float tolerance) {
        const glm::vec3 p = player.position();
        const float bottom = p.y - player.halfHeight();
        const float ground = terrain.heightAt(p.x, p.z);
        const float gap = bottom - ground;
        worstGap = glm::max(worstGap, glm::abs(gap));
        const bool pass = glm::abs(gap) < tolerance;
        ok = ok && pass;
        engine::Log::info("testPlayer: {}: at ({:.2f}, {:.2f}, {:.2f}) bottom {:.3f} ground {:.3f} gap {:+.3f} [{}]",
            label, p.x, p.y, p.z, bottom, ground, gap, pass ? "grounded" : "FAIL");
    };

    // 1) Drop in and settle on the flat plain (idle).
    run(120);
    checkpoint("idle on plain", 0.05F);

    // 2) Walk along the plain — the reported "sinks into the floor" case.
    input.onKey(GLFW_KEY_W, 1);
    run(240); // 2 s ≈ 16 units, still on the plain
    checkpoint("walk on plain", 0.05F);

    // 3) Keep walking up the ramp onto higher ground.
    run(960); // 8 s ≈ 64 units more → ~20 units up the ramp
    checkpoint("walk up ramp", 0.35F); // support point sits beside the center on a slope

    // 4) Release the key and settle standing on the ramp.
    input.onKey(GLFW_KEY_W, 0);
    run(120);
    checkpoint("idle on ramp", 0.35F);

    engine::Log::info("testPlayer: walked {:.2f} units, worst |gap| {:.3f}",
        glm::length(player.position() - spawn), worstGap);
    return ok;
}

bool testSceneGraph()
{
    namespace fs = std::filesystem;
    const fs::path assetsDir{CANDY_ASSETS_DIR};

    auto dragon = engine::Model::loadFromFile((assetsDir / "objects/dragon/ganfaul_m_aure.dae").generic_string());
    if (!dragon) {
        engine::Log::error("testSceneGraph: dragon load failed: {}", dragon.error().message);
        return false;
    }

    engine::Entity root;
    auto& character = root.addChild(*dragon);
    character.transform.setPosition({0.0F, 0.0F, 0.0F});
    character.transform.setScale({0.1F, 0.1F, 0.1F});
    root.updateSelfAndChild();

    const glm::mat4 view
        = glm::lookAt(glm::vec3{0.0F, 0.0F, 5.0F}, glm::vec3{0.0F}, glm::vec3{0.0F, 1.0F, 0.0F});
    const glm::mat4 projection = glm::perspective(glm::radians(60.0F), 16.0F / 9.0F, 0.1F, 100.0F);
    const engine::Frustum frustum = engine::Frustum::fromViewProjection(projection * view);

    const bool visibleInFront = character.isOnFrustum(frustum);

    character.transform.setPosition({0.0F, 0.0F, 50.0F}); // behind the camera
    root.updateSelfAndChild();
    const bool visibleBehind = character.isOnFrustum(frustum);

    engine::Log::info("testSceneGraph: visible={} culledBehind={}", visibleInFront, !visibleBehind);
    return visibleInFront && !visibleBehind;
}

// Ragdoll check: seed the Bullet ragdoll from the animated avatar pose, let
// it fall for 2 s of simulation and verify the bodies actually drop onto the
// terrain while the palette round-trip (bodies -> node TRS -> skinning
// matrices) keeps producing finite transforms. Runs twice — classic (linked
// pairs filtered) and with self-collision — logging joint stretch / body
// speed so the experiment is quantitative. Only non-finite blowups (or a
// failed classic fall) fail the test. Headless: no GL needed.
bool testRagdoll()
{
    namespace fs = std::filesystem;
    const fs::path assetsDir{CANDY_ASSETS_DIR};

    auto dragon = engine::Model::loadFromFile((assetsDir / "objects/dragon/ganfaul_m_aure.dae").generic_string());
    if (!dragon) {
        engine::Log::error("testRagdoll: dragon failed: {}", dragon.error().message);
        return false;
    }
    if (auto loaded = dragon->loadAnimations((assetsDir / "objects/dragon/Idle.dae").generic_string()); !loaded) {
        engine::Log::error("testRagdoll: Idle failed: {}", loaded.error().message);
        return false;
    }

    // Same 1.8 m avatar fit as the game loop, spawned mid-air over the plain.
    const engine::Aabb dragonAabb = engine::generateAabb(*dragon);
    const float dragonScale = 1.8F / glm::max(2.0F * dragonAabb.halfExtents.y, 1e-3F);
    const glm::mat4 modelToWorld = glm::translate(glm::mat4{1.0F}, glm::vec3{0.0F, 2.5F, 0.0F})
        * glm::scale(glm::mat4{1.0F}, glm::vec3{dragonScale});

    engine::PhysicsWorld physics;
    const engine::Terrain terrain = engine::Terrain::makePlainWithRamp();
    physics.addStaticTerrain(terrain);

    bool ok = true;
    for (const bool selfCollide : {false, true}) {
        // Fresh animator per phase so the previous phase's override pose does
        // not leak into the next seed; the seed pose is the same Idle frame.
        engine::Animator animator{*dragon};
        animator.play(0);
        animator.update(1.0F / 60.0F); // seed pose for the ragdoll

        engine::Ragdoll ragdoll(physics, animator, modelToWorld, selfCollide);
        if (!ragdoll.valid() || ragdoll.bodyCount() != 15) {
            engine::Log::error("testRagdoll: build failed (self={}, valid={}, bodies={})",
                selfCollide, ragdoll.valid(), ragdoll.bodyCount());
            return false;
        }

        const float hipsStart = ragdoll.hipsPosition().y;
        float worstStretch = 0.0F;
        bool finite = true;
        for (int step = 0; step < 120; ++step) {
            physics.step(1.0F / 60.0F);
            ragdoll.update();
            worstStretch = glm::max(worstStretch, ragdoll.maxJointStretch());
            const glm::vec3 hips = ragdoll.hipsPosition();
            finite = finite && std::isfinite(hips.x) && std::isfinite(hips.y) && std::isfinite(hips.z);
        }
        const float hipsEnd = ragdoll.hipsPosition().y;

        engine::Log::info("testRagdoll: self={} hips {:.2f} -> {:.2f}, jointStretch end={:.4f} "
            "worst={:.4f}, maxSpeed={:.2f}, finite={}",
            selfCollide, hipsStart, hipsEnd, ragdoll.maxJointStretch(), worstStretch,
            ragdoll.maxBodySpeed(), finite);
        const std::vector<float> hinges = ragdoll.hingeAngles();
        if (hinges.size() >= 4) {
            // Knee flexion is positive (heel backwards), elbow flexion
            // negative (hand forwards): an inverted sign here would mean the
            // hinge axes point the wrong anatomical way.
            engine::Log::info("testRagdoll: self={} knees {:+.2f}/{:+.2f} rad, "
                "elbows {:+.2f}/{:+.2f} rad",
                selfCollide, hinges[0], hinges[1], hinges[2], hinges[3]);
        }

        ok = ok && finite && (selfCollide || hipsStart - hipsEnd > 0.2F);
        // The ragdoll destructs here: constraints then bodies detach cleanly
        // (the second phase reuses the same world over the same terrain).
    }
    return ok;
}

// M2 check: full GL pipeline render + screenshot dump for manual verification.
bool testRenderer(engine::Window& window);
bool continueRendererTest(engine::Window& window, engine::GlRenderer& renderer,
    const engine::Terrain& terrain, uint32_t terrainMesh, const std::array<uint32_t, 5>& terrainTextures,
    engine::Animator&& animator, const std::vector<uint32_t>& dragonMeshHandles,
    const std::array<uint32_t, 3>& dragonTextures, const glm::mat4& dragonFit);

bool testRenderer(engine::Window& window)
{
    namespace fs = std::filesystem;
    const fs::path assetsDir{CANDY_ASSETS_DIR};

    engine::GlRenderer renderer;
    if (auto initialized = renderer.init(); !initialized) {
        engine::Log::error("testRenderer: init failed: {}", initialized.error().message);
        return false;
    }
    const auto [framebufferWidth, framebufferHeight] = window.framebufferSize();
    renderer.resize(framebufferWidth, framebufferHeight);

    // Terrain: procedural plain + ramp (the same level the game runs on).
    const engine::Terrain terrain = engine::Terrain::makePlainWithRamp();
    const uint32_t terrainMesh = renderer.uploadMesh(engine::generateTerrainMesh(terrain));
    const std::array<uint32_t, 5> terrainTextures = {
        renderer.createTextureFromFile((assetsDir / "terrain/blendMap.png").string(), true),
        renderer.createTextureFromFile((assetsDir / "terrain/grass.png").string(), true),
        renderer.createTextureFromFile((assetsDir / "terrain/rTexture.png").string(), true),
        renderer.createTextureFromFile((assetsDir / "terrain/gTexture.png").string(), true),
        renderer.createTextureFromFile((assetsDir / "terrain/bTexture.png").string(), true),
    };

    // CONTROL RIG: back to the 2017 hero (ganfaul) with the Walking clip.
    auto dragon = engine::Model::loadFromFile((assetsDir / "objects/dragon/ganfaul_m_aure.dae").generic_string());
    if (!dragon) {
        engine::Log::error("testRenderer: dragon failed: {}", dragon.error().message);
        return false;
    }
    dragon->loadAnimations((assetsDir / "objects/dragon/Idle.dae").generic_string());
    dragon->loadAnimations((assetsDir / "objects/dragon/Walking.dae").generic_string());

    engine::Animator animator{*dragon};
    animator.play(2); // Walking: compare limbs against the bind pose

    const engine::Aabb dragonAabb = engine::generateAabb(*dragon);
    const float dragonMaxExtent = glm::max(dragonAabb.halfExtents.x,
        glm::max(dragonAabb.halfExtents.y, dragonAabb.halfExtents.z));
    const float dragonScale = 1.8F / (2.0F * dragonMaxExtent);
    const glm::vec3 spot = terrain.findScenicSpot();
    const glm::mat4 dragonFit = glm::translate(glm::mat4{1.0F}, spot + glm::vec3{0.0F, 3.0F, 0.0F})
        * glm::scale(glm::mat4{1.0F}, glm::vec3{dragonScale})
        * glm::translate(glm::mat4{1.0F}, -dragonAabb.center);

    const auto& material = dragon->materials().empty() ? engine::Model::Material{} : dragon->materials().front();
    std::string diffuseFile = material.diffuse;
    if (diffuseFile.empty() || !fs::exists(fs::path{dragon->sourceDirectory()} / diffuseFile)) {
        diffuseFile = (assetsDir / "objects/dragon/textures/Ganfaul_diffuse.png").generic_string();
    }
    std::string normalFile = material.normal;
    if (normalFile.empty() || !fs::exists(fs::path{dragon->sourceDirectory()} / normalFile)) {
        normalFile = (assetsDir / "objects/dragon/textures/Ganfaul_normal.png").generic_string();
    }
    std::string specularFile = material.specular;
    if (specularFile.empty() || !fs::exists(fs::path{dragon->sourceDirectory()} / specularFile)) {
        specularFile = (assetsDir / "objects/dragon/textures/Ganfaul_specular.png").generic_string();
    }
    const std::array<uint32_t, 3> dragonTextures = {
        renderer.createTextureFromFile(diffuseFile, true),
        renderer.createTextureFromFile(normalFile),
        renderer.createTextureFromFile(specularFile),
    };
    // GPU skinning (the palette formula handles the unit compensation).
    std::vector<uint32_t> dragonMeshHandles;
    for (const auto& mesh : dragon->meshes()) {
        dragonMeshHandles.push_back(renderer.uploadMesh(mesh));
    }

    return continueRendererTest(window, renderer, terrain, terrainMesh, terrainTextures,
        std::move(animator), dragonMeshHandles, dragonTextures, dragonFit);
}

// Second half of the renderer selftest: physics bodies, camera, render passes.
bool continueRendererTest(engine::Window& window, engine::GlRenderer& renderer,
    const engine::Terrain& terrain, uint32_t terrainMesh, const std::array<uint32_t, 5>& terrainTextures,
    engine::Animator&& animator, const std::vector<uint32_t>& dragonMeshHandles,
    const std::array<uint32_t, 3>& dragonTextures, const glm::mat4& dragonFit)
{
    const uint32_t boxMesh = renderer.uploadMesh(engine::makeBoxMesh({0.5F, 0.5F, 0.5F}));
    const uint32_t sphereMesh = renderer.uploadMesh(engine::makeSphereMesh(0.5F)); // same template as the interactive path
    const uint32_t cylinderMesh = renderer.uploadMesh(engine::makeCylinderMesh(0.5F, 1.0F));

    engine::PhysicsWorld physics;
    physics.addStaticTerrain(terrain);
    physics.spawnBody({engine::PhysicsBodyDesc::Shape::Box, {2.0F, 4.0F, 0.0F}, {0.5F, 0.5F, 0.5F}, 1.0F});
    physics.spawnBody({engine::PhysicsBodyDesc::Shape::Sphere, {-2.0F, 6.0F, 1.0F}, {1.0F, 0.0F, 0.0F}, 1.0F});
    physics.spawnBody({engine::PhysicsBodyDesc::Shape::Cylinder, {0.0F, 8.0F, 2.0F}, {0.5F, 1.0F, 0.0F}, 1.0F});

    const auto [framebufferWidth, framebufferHeight] = window.framebufferSize();
    // The avatar is feet-anchored (see dragonFit in main): stand it on the
    // terrain at capsule-center height so the selftest scene stays sensible.
    const glm::mat4 dragonWorld = glm::translate(glm::mat4{1.0F},
        glm::vec3{0.0F, terrain.heightAt(0.0F, 0.0F) + 0.9F, 0.0F}) * dragonFit;

    engine::Camera camera;
    camera.target = glm::vec3{dragonWorld[3]} + glm::vec3{0.0F, 0.8F, 0.0F}; // the standing avatar
    camera.distance = 16.0F;
    camera.pitch = 25.0F;
    camera.yaw = 210.0F;

    engine::GlRenderer::Environment environment{
        .view = camera.viewMatrix(),
        .projection = camera.projectionMatrix(
            static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight)),
        .cameraPosition = camera.position(),
        .sunDirection = glm::normalize(glm::vec3{0.4F, -1.0F, 0.2F}),
    };
    const glm::mat4 lightView = glm::lookAt(
        camera.target - environment.sunDirection * 100.0F, camera.target, glm::vec3{0.0F, 1.0F, 0.0F});
    const glm::mat4 lightViewProjection
        = glm::ortho(-60.0F, 60.0F, -60.0F, 60.0F, 1.0F, 300.0F) * lightView;

    const auto buildInstances = [&physics](engine::PhysicsBodyDesc::Shape shape) {
        std::vector<glm::mat4> matrices;
        for (const auto& snapshot : physics.snapshots()) {
            if (snapshot.shape != shape) {
                continue;
            }
            matrices.push_back(glm::translate(glm::mat4{1.0F}, snapshot.position)
                * glm::mat4_cast(snapshot.rotation)
                * shapeInstanceScale(shape, snapshot.extents));
        }
        return matrices;
    };

    // Warm up into a clear mid-stride pose before the captured frames.
    for (int warmup = 0; warmup < 15; ++warmup) {
        animator.update(1.0F / 30.0F);
    }

    for (int frame = 0; frame < 3; ++frame) {
        animator.update(1.0F / 30.0F); // advance deeper into the walk cycle
        if (frame >= 1) {
            renderer.setBonePalette(animator.bonePalette());
        }
        physics.step(1.0F / 60.0F);

        renderer.beginShadowPass(lightViewProjection);
        renderer.drawTerrain(terrainMesh, terrainTextures);
        for (const uint32_t meshHandle : dragonMeshHandles) {
            renderer.drawSkinned(meshHandle, dragonWorld, dragonTextures.data());
        }
        renderer.drawInstanced(
            boxMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Box), {0.6F, 0.4F, 0.3F});
        renderer.drawInstanced(
            sphereMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Sphere), {0.3F, 0.5F, 0.7F});
        renderer.drawInstanced(
            cylinderMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Cylinder), {0.4F, 0.6F, 0.4F});
        renderer.endShadowPass();

        renderer.beginFrame(environment);
        renderer.drawSky();
        renderer.drawTerrain(terrainMesh, terrainTextures);
        for (const uint32_t meshHandle : dragonMeshHandles) {
            renderer.drawSkinned(meshHandle, dragonWorld, dragonTextures.data());
        }
        renderer.drawInstanced(
            boxMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Box), {0.6F, 0.4F, 0.3F});
        renderer.drawInstanced(
            sphereMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Sphere), {0.3F, 0.5F, 0.7F});
        renderer.drawInstanced(
            cylinderMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Cylinder), {0.4F, 0.6F, 0.4F});
        renderer.finishFrame();

        if (frame == 0) {
            engine::saveGlScreenshot("build/selftest_bind.png");
        } else if (frame == 1) {
            engine::saveGlScreenshot("build/selftest_probe.png");
        }
    }

    return engine::saveGlScreenshot("build/selftest_scene.png");
}

int main(int argc, char* argv[]) try
{
    const std::span<char*> args{argv + 1, argv + argc};
    const bool selftest = std::ranges::find(args, std::string_view{"--selftest"}) != args.end();

    std::println("=== candy: Engine 2.0 (M0 skeleton) ===");
    engine::Log::init("candy");
    engine::Log::info("starting (selftest: {})", selftest);

    if (selftest) {
        const bool ok = testAssets() && testAnimation() && testPhysics() && testPlayer()
            && testSceneGraph() && testRagdoll();
        if (!ok) {
            engine::Log::error("selftest failed");
            return 1;
        }
    }

    engine::WindowDesc desc;
    if (selftest) {
        desc.width = 1280;
        desc.height = 720;
        desc.title = "candy [selftest]";
    }

    engine::Window window{desc};

    // NOTE: the input listener must be installed BEFORE the ImGui GLFW
    // backend init. ImGui_ImplGlfw_InitForOpenGL installs its own event
    // handlers that chain to whatever was already installed; installing ours
    // afterwards silently replaces ImGui's handlers and the UI stops
    // receiving any mouse/keyboard events (buttons appear completely dead).
    engine::Input input;
    window.setInputListener(&input);

    engine::Ui ui{window.handle()};

    if (selftest) {
        if (!testRenderer(window)) {
            engine::Log::error("renderer selftest failed");
            return 1;
        }
        engine::Log::info("renderer selftest passed, exiting cleanly");
        return 0;
    }

    // --- M2 scene state (input already wired above, before Ui init) ---------
    engine::GlRenderer renderer;
    bool rendererReady = false;

    std::optional<engine::Model> dragonModel;
    std::optional<engine::Animator> dragonAnimator;
    std::optional<engine::PhysicsWorld> physics;
    std::optional<engine::PlayerController> player;
    std::optional<engine::Ragdoll> dragonRagdoll; // skeleton ragdoll over the avatar
    engine::Camera camera;

    // Ragdoll toggle (Physics panel button) + the avatar transform frozen at
    // activation: the ragdoll seeds from it and must not spin with camera
    // orbiting afterwards.
    bool ragdollToggleRequested = false;
    // Real self-collision: non-adjacent parts collide (hands can rest on the
    // chest) while joint-linked capsules stay filtered — measured smoothest
    // and most lifelike; the joints never fight their own contacts.
    bool ragdollSelfCollide = true;
    glm::mat4 dragonRagdollTransform{1.0F};
    glm::mat4 dragonTransform{1.0F};

    uint32_t terrainMesh = 0;
    std::array<uint32_t, 5> terrainTextures{};
    uint32_t dragonMesh = 0;
    std::array<uint32_t, 3> dragonTextures{};
    uint32_t boxMesh = 0;
    uint32_t sphereMesh = 0;
    uint32_t cylinderMesh = 0;
    // Avatar fit: the FEET anchor is resolved every frame from the CURRENT
    // clip's animated lowest point (see the game loop) — the walk cycle dips
    // the feet below the bind-pose AABB bottom, which is what made the avatar
    // visibly sink into flat ground while walking.
    float dragonScale = 1.0F;           // model -> capsule height fit (1.8 m)
    glm::vec3 dragonCenter{0.0F};       // model-space center, for XZ centering
    float dragonBindBottomY = 0.0F;     // bind-pose bottom, fallback anchor
    std::vector<float> dragonClipFeetY; // animated lowest point per clip

    {
        const std::filesystem::path assetsDir{CANDY_ASSETS_DIR};

        if (auto initialized = renderer.init(); !initialized) {
            engine::Log::error("renderer init failed: {}", initialized.error().message);
        } else {
            rendererReady = true;
            const auto [framebufferWidth, framebufferHeight] = window.framebufferSize();
            renderer.resize(framebufferWidth, framebufferHeight);

            // Procedural level: a flat plain with one long ramp — no heightmap
            // quantization, so heightAt(), the rendered mesh and the physics
            // collider agree exactly and grounding is verifiable.
            const engine::Terrain terrain = engine::Terrain::makePlainWithRamp();
            terrainMesh = renderer.uploadMesh(engine::generateTerrainMesh(terrain));
            terrainTextures = {
                renderer.createTextureFromFile((assetsDir / "terrain/blendMap.png").string(), true),
                renderer.createTextureFromFile((assetsDir / "terrain/grass.png").string(), true),
                renderer.createTextureFromFile((assetsDir / "terrain/rTexture.png").string(), true),
                renderer.createTextureFromFile((assetsDir / "terrain/gTexture.png").string(), true),
                renderer.createTextureFromFile((assetsDir / "terrain/bTexture.png").string(), true),
            };

            physics.emplace();
            physics->addStaticTerrain(terrain);

            const glm::vec3 spawn{0.0F, terrain.heightAt(0.0F, 0.0F) + 2.0F, 0.0F};
            camera.target = spawn; // capsule center ≈ chest height of the avatar
            camera.distance = 7.0F;

            // Fallback capsule (0.4 radius + 1.0 cylinder = 1.8 m tall), used
            // only when the model fails to load; otherwise rebuilt from the
            // model's SKELETON (capsuleFitFromRig — ignores T-pose arms).
            engine::CapsuleFit avatarFit{0.4F, 1.0F};
            if (auto dragon = engine::Model::loadFromFile(
                    (assetsDir / "objects/dragon/ganfaul_m_aure.dae").generic_string());
                dragon) {
                dragon->loadAnimations((assetsDir / "objects/dragon/Idle.dae").generic_string());
                dragon->loadAnimations((assetsDir / "objects/dragon/Walking.dae").generic_string());
                dragon->loadAnimations((assetsDir / "objects/dragon/Walking Backwards.dae").generic_string());

                const engine::Aabb dragonAabb = engine::generateAabb(*dragon);
                // Fit the avatar to the player capsule: exactly 1.8 tall (the
                // capsule's total height). The FEET anchor is resolved every
                // frame from the current clip's animated lowest point (see the
                // game loop) so the planted foot stays exactly on the capsule
                // bottom — no hover while idle, no sinking into the floor
                // mid-stride.
                dragonScale = 1.8F / glm::max(2.0F * dragonAabb.halfExtents.y, 1e-3F);
                dragonCenter = dragonAabb.center;
                dragonBindBottomY = dragonAabb.center.y - dragonAabb.halfExtents.y;

                // Capsule from the SKELETON (Head/Hips/Foot bones in bind
                // pose — height and body radius, arms excluded), scaled to
                // world units. spawn stays the feet point (PlayerController).
                avatarFit = engine::capsuleFitFromRig(*dragon, dragonScale, 0.4F);

                const auto& material = dragon->materials().front();
                dragonTextures = {
                    renderer.createTextureFromFile(
                        (std::filesystem::path{dragon->sourceDirectory()} / material.diffuse).string(), true),
                    renderer.createTextureFromFile(
                        (std::filesystem::path{dragon->sourceDirectory()} / material.normal).string()),
                    renderer.createTextureFromFile(
                        (std::filesystem::path{dragon->sourceDirectory()} / material.specular).string()),
                };
                dragonMesh = renderer.uploadMesh(dragon->meshes().front());

                dragonModel.emplace(std::move(*dragon));
                dragonAnimator.emplace(*dragonModel);
                dragonAnimator->play(1); // clip 0 is the DAE's built-in stub

                // Measure the animated lowest point of every clip (CPU
                // skinning, same palette math as the GPU) for per-clip foot
                // anchoring.
                dragonClipFeetY.reserve(dragonAnimator->clipCount());
                for (size_t clip = 0; clip < dragonAnimator->clipCount(); ++clip) {
                    dragonClipFeetY.push_back(dragonAnimator->clipLowestPoint(clip));
                }
                engine::Log::info("avatar foot anchor: bind bottom {:.3f}, idle {:.3f}, walk {:.3f}, back {:.3f}",
                    dragonBindBottomY,
                    dragonClipFeetY.size() > 1 ? dragonClipFeetY[1] : dragonBindBottomY,
                    dragonClipFeetY.size() > 2 ? dragonClipFeetY[2] : dragonBindBottomY,
                    dragonClipFeetY.size() > 3 ? dragonClipFeetY[3] : dragonBindBottomY);
            } else {
                engine::Log::error("dragon failed: {}", dragon.error().message);
            }

            // The capsule follows the avatar's skeleton, so the model has to
            // load (or the fallback fit apply) before the player is created.
            player.emplace(*physics, spawn, avatarFit);

            boxMesh = renderer.uploadMesh(engine::makeBoxMesh({0.5F, 0.5F, 0.5F}));
            sphereMesh = renderer.uploadMesh(engine::makeSphereMesh(0.5F));
            cylinderMesh = renderer.uploadMesh(engine::makeCylinderMesh(0.5F, 1.0F));
        }
    }

    engine::FrameClock clock;
    bool showDemo = false; // opt-in via the checkbox in the stats panel
    bool showUi = true;    // F1 hides the whole overlay for a clean view
    int frameIndex = 0;

    while (!window.shouldClose()) {
        input.beginFrame();
        engine::Window::pollEvents();
        if (window.keyPressed(GLFW_KEY_ESCAPE)) {
            window.requestClose();
        }
        if (input.pressed(GLFW_KEY_F1)) {
            showUi = !showUi;
        }

        clock.beginFrame();
        const float deltaSeconds = clock.deltaTime();

        // Physics-panel ragdoll toggle (deferred from the previous frame's UI
        // pass). Activation seeds the ragdoll from the current animated pose
        // at the current avatar transform and parks the player capsule's
        // controller — its action would keep stepping the capsule, and its
        // ghost overlaps the freshly seeded bodies. Restore tears the ragdoll
        // down (the destructor detaches bodies + joints) and hands the
        // palette back to the clips.
        if (ragdollToggleRequested) {
            ragdollToggleRequested = false;
            if (dragonRagdoll) {
                dragonRagdoll.reset();
                if (player) {
                    player->setEnabled(true);
                }
            } else if (dragonAnimator && physics) {
                dragonRagdoll.emplace(*physics, *dragonAnimator, dragonTransform, ragdollSelfCollide);
                dragonRagdollTransform = dragonTransform;
                if (player) {
                    player->setEnabled(false);
                }
            }
        }

        // Camera: mouse orbit + wheel zoom (2017-style free orbit).
        {
            // WantCaptureMouse here lags one frame (read before ImGui's
            // NewFrame), which is fine: while the pointer hovers an ImGui
            // window the camera ignores it, so dragging/scrolling the UI
            // doesn't spin or zoom the camera at the same time.
            if (!ImGui::GetIO().WantCaptureMouse) {
                const auto [mouseX, mouseY] = input.mouseDelta();
                camera.orbit(static_cast<float>(mouseX), static_cast<float>(mouseY));
                const auto [scrollX, scrollY] = input.scrollDelta();
                camera.zoom(static_cast<float>(scrollY));
            }
        }

        bool playerMoving = false;
        if (player && physics) {
            if (!dragonRagdoll) {
                // While the ragdoll owns the avatar the capsule stays parked
                // (its action is removed; input must not queue movement).
                player->update(input, deltaSeconds, camera.forward(), camera.right());
            }
            physics->step(deltaSeconds); // the ragdoll bodies keep simulating
            playerMoving = input.isDown(GLFW_KEY_W) || input.isDown(GLFW_KEY_S)
                || input.isDown(GLFW_KEY_A) || input.isDown(GLFW_KEY_D);

            // Spawn keys (B/C/N) like the 2017 build: from the camera into the scene.
            const glm::vec3 spawnPoint = camera.target + camera.forward() * 3.0F + glm::vec3{0.0F, 3.0F, 0.0F};
            const glm::vec3 spawnVelocity = camera.forward() * 8.0F + glm::vec3{0.0F, 4.0F, 0.0F};
            if (input.pressed(GLFW_KEY_B)) {
                physics->spawnBody({engine::PhysicsBodyDesc::Shape::Box, spawnPoint,
                    {0.5F, 0.5F, 0.5F}, 1.0F, spawnVelocity});
            }
            if (input.pressed(GLFW_KEY_C)) {
                physics->spawnBody({engine::PhysicsBodyDesc::Shape::Cylinder, spawnPoint,
                    {0.5F, 1.0F, 0.0F}, 1.0F, spawnVelocity});
            }
            if (input.pressed(GLFW_KEY_N)) {
                physics->spawnBody({engine::PhysicsBodyDesc::Shape::Sphere, spawnPoint,
                    {0.5F, 0.0F, 0.0F}, 1.0F, spawnVelocity});
            }
        }

        if (dragonRagdoll) {
            camera.target = dragonRagdoll->hipsPosition(); // follow the falling body
        } else if (player) {
            camera.target = player->position(); // capsule center ≈ chest height
        }

        if (dragonRagdoll) {
            // Ragdoll owns the pose: read the simulated bodies back into the
            // animator (same composePalette path the clips use).
            dragonRagdoll->update();
        } else if (dragonAnimator) {
            // Walk when the player moves, idle otherwise (smooth cross-fade).
            dragonAnimator->crossFadeTo(playerMoving ? 2 : 1);
            dragonAnimator->update(deltaSeconds);
        }

        // Dragon = the player's avatar: follows position, faces the movement
        // direction. Feet parked at the capsule's bottom (`position()` is the
        // capsule CENTER), so the visual stands exactly where the physics
        // capsule stands. The fit matrix is rebuilt per frame: the feet anchor
        // uses the CURRENT clip's animated lowest point, so the planted foot
        // rests exactly on the capsule bottom in every animation state —
        // no hover while idle, no sinking into flat ground mid-stride.
        const glm::vec3 dragonPosition = player
            ? player->position() - glm::vec3{0.0F, player->halfHeight(), 0.0F}
            : glm::vec3{0.0F};
        float dragonAnchorY = dragonBindBottomY;
        if (dragonAnimator && dragonClipFeetY.size() > dragonAnimator->currentClip()) {
            dragonAnchorY = dragonClipFeetY[dragonAnimator->currentClip()];
        }
        const glm::mat4 dragonFitFrame = glm::scale(glm::mat4{1.0F}, glm::vec3{dragonScale})
            * glm::translate(glm::mat4{1.0F}, glm::vec3{-dragonCenter.x, -dragonAnchorY, -dragonCenter.z});
        dragonTransform = glm::translate(glm::mat4{1.0F}, dragonPosition)
            * glm::rotate(glm::mat4{1.0F}, glm::radians(camera.yaw + 180.0F), glm::vec3{0.0F, 1.0F, 0.0F})
            * dragonFitFrame;
        if (dragonRagdoll) {
            // Frozen at activation (persistent, declared above): the ragdoll
            // seeded from this exact transform and must not rotate with the
            // camera orbit afterwards.
            dragonTransform = dragonRagdollTransform;
        }

        const auto [framebufferWidth, framebufferHeight] = window.framebufferSize();
        renderer.resize(framebufferWidth, framebufferHeight);

        const engine::GlRenderer::Environment environment{
            .view = camera.viewMatrix(),
            .projection = camera.projectionMatrix(
                static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight)),
            .cameraPosition = camera.position(),
            .sunDirection = glm::normalize(glm::vec3{0.4F, -1.0F, 0.2F}),
        };
        const glm::mat4 lightView = glm::lookAt(
            camera.target - environment.sunDirection * 100.0F, camera.target, glm::vec3{0.0F, 1.0F, 0.0F});
        const glm::mat4 lightViewProjection
            = glm::ortho(-60.0F, 60.0F, -60.0F, 60.0F, 1.0F, 300.0F) * lightView;

        const auto buildInstances = [&physics](engine::PhysicsBodyDesc::Shape shape) {
            std::vector<glm::mat4> matrices;
            if (physics) {
                for (const auto& snapshot : physics->snapshots()) {
                    if (snapshot.shape != shape) {
                        continue;
                    }
                    matrices.push_back(glm::translate(glm::mat4{1.0F}, snapshot.position)
                        * glm::mat4_cast(snapshot.rotation)
                        * shapeInstanceScale(shape, snapshot.extents));
                }
            }
            return matrices;
        };

        if (rendererReady) {
            if (dragonAnimator) {
                renderer.setBonePalette(dragonAnimator->bonePalette());
            }

            renderer.beginShadowPass(lightViewProjection);
            renderer.drawTerrain(terrainMesh, terrainTextures);
            renderer.drawSkinned(dragonMesh, dragonTransform, dragonTextures.data());
            renderer.drawInstanced(
                boxMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Box), {0.6F, 0.4F, 0.3F});
            renderer.drawInstanced(
                sphereMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Sphere), {0.3F, 0.5F, 0.7F});
            renderer.drawInstanced(
                cylinderMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Cylinder), {0.4F, 0.6F, 0.4F});
            renderer.endShadowPass();

            renderer.beginFrame(environment);
            renderer.drawSky();
            renderer.drawTerrain(terrainMesh, terrainTextures);
            renderer.drawSkinned(dragonMesh, dragonTransform, dragonTextures.data());
            renderer.drawInstanced(
                boxMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Box), {0.6F, 0.4F, 0.3F});
            renderer.drawInstanced(
                sphereMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Sphere), {0.3F, 0.5F, 0.7F});
            renderer.drawInstanced(
                cylinderMesh, buildInstances(engine::PhysicsBodyDesc::Shape::Cylinder), {0.4F, 0.6F, 0.4F});
            renderer.finishFrame();
        }

        ui.beginFrame();

        if (showUi) {
            drawDockspace(ImGui::GetIO());
            drawStatsWindow(clock, frameIndex, showDemo);
            if (dragonAnimator) {
                drawAnimationPanel(*dragonAnimator);
            }
            if (physics) {
                drawPhysicsPanel(*physics, ragdollToggleRequested, ragdollSelfCollide,
                    dragonRagdoll.has_value());
            }
            if (showDemo) {
                ImGui::ShowDemoWindow(&showDemo);
            }
        }

        ui.renderFrame();
        window.swapBuffers();
        ++frameIndex;
    }

    engine::Log::info("clean exit after {} frames", frameIndex);
    return 0;
}
catch (const std::exception& e) {
    engine::Log::error("fatal: {}", e.what());
    std::println(stderr, "fatal: {}", e.what());
    return 1;
}
