#pragma once

#include <string_view>

struct GLFWwindow;

namespace engine {

// Owns the Dear ImGui context and the GLFW/OpenGL3 backend lifecycle.
// The OpenGL3 backend uses imgui's built-in GL loader, so no external GL
// loader is needed at this stage (the engine's own GL renderer arrives in M2).
class Ui final {
public:
    explicit Ui(GLFWwindow* window, std::string_view glslVersion = {});
    ~Ui();

    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;
    Ui(Ui&&) = delete;
    Ui& operator=(Ui&&) = delete;

    void beginFrame() const;
    void renderFrame() const;

private:
    GLFWwindow* m_window = nullptr;
};

} // namespace engine
