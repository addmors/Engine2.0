#include <exception>
#include <print>
#include <ranges>
#include <span>
#include <string_view>

#include <imgui.h>

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

int main(int argc, char* argv[]) try
{
    const std::span<char*> args{argv + 1, argv + argc};
    const bool selftest = std::ranges::find(args, std::string_view{"--selftest"}) != args.end();

    std::println("=== candy: Engine 2.0 (M0 skeleton) ===");
    engine::Log::init("candy");
    engine::Log::info("starting (selftest: {})", selftest);

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
