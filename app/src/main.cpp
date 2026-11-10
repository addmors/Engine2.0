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
#include <engine/scene/Entity.hpp>
#include <engine/scene/Frustum.hpp>
#include <engine/scene/Terrain.hpp>
#include <engine/ui/Ui.hpp>

namespace {

// Full-viewport dockspace so panels can snap anywhere, like a real editor.
void drawDockspace(const ImGuiIO& io)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags windowFlags
        = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    ImGui::Begin("##DockSpaceHost", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0) {
        const ImGuiID dockspaceId = ImGui::GetID("CandyDockspace");
        ImGui::DockSpace(dockspaceId, ImVec2{0.0F, 0.0F});
    }
    ImGui::End();
}

void drawStatsWindow(const engine::FrameClock& clock, int frameIndex, bool& showDemo)
{
    ImGui::Begin("Candy");
    ImGui::Text("fps: %.1f (%.2f ms)", clock.fps(), 1000.0F * clock.smoothedDelta());
    ImGui::Text("frame: %d", frameIndex);
    ImGui::Text("renderer: OpenGL 4.6 placeholder overlay (RHI arrives in M2)");
    ImGui::Checkbox("ImGui demo window", &showDemo);
    ImGui::End();
}

void drawAnimationPanel(engine::Animator& animator)
{
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

void drawPhysicsPanel(engine::PhysicsWorld& physics)
{
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

    engine::Log::info("testAnimation: {} bones, animated in idle {} / walk {}",
        boneCount, animatedIdle, animatedWalk);
    return animator.bonePalette().size() == static_cast<size_t>(boneCount) && animatedIdle >= boneCount / 2
        && animatedWalk >= boneCount / 2;
}

bool testPhysics()
{
    namespace fs = std::filesystem;
    const fs::path assetsDir{CANDY_ASSETS_DIR};

    auto terrain = engine::Terrain::loadFromHeightmap((assetsDir / "terrain/heightmap6.png").generic_string());
    if (!terrain) {
        engine::Log::error("testPhysics: terrain failed: {}", terrain.error().message);
        return false;
    }

    const auto grid = terrain->heights2d();
    engine::Log::info("testPhysics: terrain {}x{}, heights2d[128,128] = {:.2f}, world size {}",
        terrain->resolution(), terrain->resolution(), grid[128, 128], terrain->worldSize());

    engine::PhysicsWorld world;
    world.addStaticTerrain(*terrain);

    world.spawnBody({engine::PhysicsBodyDesc::Shape::Box, {0.0F, 20.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F});
    world.spawnBody({engine::PhysicsBodyDesc::Shape::Box, {6.0F, 26.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F});
    world.spawnBody({engine::PhysicsBodyDesc::Shape::Box, {12.0F, 32.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F});
    world.spawnBody({engine::PhysicsBodyDesc::Shape::Sphere, {-6.0F, 32.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1.0F});

    for (int i = 0; i < 480; ++i) {
        world.step(1.0F / 120.0F); // 4 seconds: enough for everything to land
    }

    bool ok = true;
    for (const auto& snapshot : world.snapshots()) {
        const float ground = terrain->heightAt(snapshot.position.x, snapshot.position.z);
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
bool testPlayer()
{
    engine::Log::info("testPlayer: begin");
    namespace fs = std::filesystem;
    const fs::path assetsDir{CANDY_ASSETS_DIR};

    auto terrain = engine::Terrain::loadFromHeightmap((assetsDir / "terrain/heightmap6.png").generic_string());
    if (!terrain) {
        engine::Log::error("testPlayer: terrain failed: {}", terrain.error().message);
        return false;
    }

    engine::Log::info("testPlayer: terrain ok, creating world...");
    engine::PhysicsWorld world;
    world.addStaticTerrain(*terrain);

    engine::Log::info("testPlayer: spawning controller...");
    const glm::vec3 spawn{0.0F, terrain->heightAt(0.0F, 0.0F) + 2.0F, 0.0F};
    engine::PlayerController player(world, spawn);
    engine::Log::info("testPlayer: controller created at ({:.2f}, {:.2f}, {:.2f})", spawn.x, spawn.y, spawn.z);

    engine::Input input;
    input.onKey(GLFW_KEY_W, 1); // walk forward
    for (int i = 0; i < 120; ++i) {
        player.update(input, 1.0F / 120.0F, {0.0F, 0.0F, -1.0F}, {1.0F, 0.0F, 0.0F});
        world.step(1.0F / 120.0F);
        if (i % 30 == 0) {
            const glm::vec3 p = player.position();
            engine::Log::info("testPlayer: step {}: ({:.2f}, {:.2f}, {:.2f})", i, p.x, p.y, p.z);
        }
    }

    const glm::vec3 position = player.position();
    const float walked = glm::length(position - spawn);
    engine::Log::info("testPlayer: walked {:.2f} units, now at ({:.2f}, {:.2f}, {:.2f})",
        walked, position.x, position.y, position.z);
    return walked > 1.0F;
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

int main(int argc, char* argv[]) try
{
    const std::span<char*> args{argv + 1, argv + argc};
    const bool selftest = std::ranges::find(args, std::string_view{"--selftest"}) != args.end();

    std::println("=== candy: Engine 2.0 (M0 skeleton) ===");
    engine::Log::init("candy");
    engine::Log::info("starting (selftest: {})", selftest);

    if (selftest) {
        const bool ok = testAssets() && testAnimation() && testPhysics() && testPlayer() && testSceneGraph();
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
    engine::Ui ui{window.handle()};

    // --- M1b scene state (ImGui panels + headless simulation; rendering in M2) ---
    engine::Input input;
    window.setInputListener(&input);

    std::optional<engine::Model> dragonModel;
    std::optional<engine::Animator> dragonAnimator;
    std::optional<engine::PhysicsWorld> physics;

    if (!selftest) {
        const std::filesystem::path assetsDir{CANDY_ASSETS_DIR};

        auto dragon = engine::Model::loadFromFile(
            (assetsDir / "objects/dragon/ganfaul_m_aure.dae").generic_string());
        if (dragon) {
            dragon->loadAnimations((assetsDir / "objects/dragon/Idle.dae").generic_string());
            dragon->loadAnimations((assetsDir / "objects/dragon/Walking.dae").generic_string());
            dragon->loadAnimations((assetsDir / "objects/dragon/Walking Backwards.dae").generic_string());
            dragonModel.emplace(std::move(*dragon));
            dragonAnimator.emplace(*dragonModel);
            dragonAnimator->play(1); // clip 0 is the DAE's built-in stub, 1 = Idle
        } else {
            engine::Log::error("failed to load dragon: {}", dragon.error().message);
        }

        if (auto terrain = engine::Terrain::loadFromHeightmap(
                (assetsDir / "terrain/heightmap6.png").generic_string())) {
            physics.emplace();
            physics->addStaticTerrain(*terrain);
        } else {
            engine::Log::error("failed to load terrain: {}", terrain.error().message);
        }
    }

    engine::FrameClock clock;
    bool showDemo = !selftest;
    int frameIndex = 0;

    while (!window.shouldClose()) {
        input.beginFrame();
        engine::Window::pollEvents();
        if (window.keyPressed(GLFW_KEY_ESCAPE)) {
            window.requestClose();
        }

        clock.beginFrame();
        const float deltaSeconds = clock.deltaTime();

        if (dragonAnimator) {
            dragonAnimator->update(deltaSeconds);
        }
        if (physics) {
            physics->step(deltaSeconds);
            // Spawn keys, like the 2017 build (SPACE was taken by jump -> N).
            if (input.pressed(GLFW_KEY_B)) {
                physics->spawnBody(
                    {engine::PhysicsBodyDesc::Shape::Box, {0.0F, 15.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, 1.0F});
            }
            if (input.pressed(GLFW_KEY_C)) {
                physics->spawnBody(
                    {engine::PhysicsBodyDesc::Shape::Cylinder, {2.0F, 15.0F, 0.0F}, {0.5F, 1.0F, 1.0F}, 1.0F});
            }
            if (input.pressed(GLFW_KEY_N)) {
                physics->spawnBody(
                    {engine::PhysicsBodyDesc::Shape::Sphere, {-2.0F, 15.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1.0F});
            }
        }

        ui.beginFrame();

        drawDockspace(ImGui::GetIO());
        drawStatsWindow(clock, frameIndex, showDemo);
        if (dragonAnimator) {
            drawAnimationPanel(*dragonAnimator);
        }
        if (physics) {
            drawPhysicsPanel(*physics);
        }
        if (showDemo) {
            ImGui::ShowDemoWindow(&showDemo);
        }

        ui.renderFrame();
        window.swapBuffers();
        ++frameIndex;

        if (selftest && frameIndex >= 2) {
            break;
        }
    }

    engine::Log::info("clean exit after {} frames", frameIndex);
    return 0;
}
catch (const std::exception& e) {
    engine::Log::error("fatal: {}", e.what());
    std::println(stderr, "fatal: {}", e.what());
    return 1;
}
