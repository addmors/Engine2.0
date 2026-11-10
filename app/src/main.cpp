#include <exception>
#include <filesystem>
#include <print>
#include <ranges>
#include <span>
#include <string_view>

#include <imgui.h>

#include <engine/assets/Model.hpp>
#include <engine/core/Log.hpp>
#include <engine/core/Time.hpp>
#include <engine/platform/Window.hpp>
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
    if (ImGui::Button("Spawn sphere")) {
        engine::Log::info("physics spawning arrives in M1 (Bullet)");
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

int main(int argc, char* argv[]) try
{
    const std::span<char*> args{argv + 1, argv + argc};
    const bool selftest = std::ranges::find(args, std::string_view{"--selftest"}) != args.end();

    std::println("=== candy: Engine 2.0 (M0 skeleton) ===");
    engine::Log::init("candy");
    engine::Log::info("starting (selftest: {})", selftest);

    if (selftest && !testAssets()) {
        engine::Log::error("asset selftest failed");
        return 1;
    }

    engine::WindowDesc desc;
    if (selftest) {
        desc.width = 1280;
        desc.height = 720;
        desc.title = "candy [selftest]";
    }

    engine::Window window{desc};
    engine::Ui ui{window.handle()};

    engine::FrameClock clock;
    bool showDemo = !selftest;
    int frameIndex = 0;

    while (!window.shouldClose()) {
        engine::Window::pollEvents();
        if (window.keyPressed(GLFW_KEY_ESCAPE)) {
            window.requestClose();
        }

        clock.beginFrame();
        ui.beginFrame();

        drawDockspace(ImGui::GetIO());
        drawStatsWindow(clock, frameIndex, showDemo);
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
