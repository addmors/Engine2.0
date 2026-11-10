#pragma once

#include <GLFW/glfw3.h> // GLFW_INCLUDE_NONE is defined by the engine target

#include <string_view>
#include <utility>

namespace engine {

struct WindowDesc {
    int width = 1920;
    int height = 1080;
    std::string_view title = "candy";
    int glMajor = 4;
    int glMinor = 6;
    bool resizable = false;
};

// RAII wrapper around a GLFW window with an OpenGL core-profile context.
// Fatal setup failures (no GL context) are reported as exceptions.
class Window final {
public:
    explicit Window(const WindowDesc& desc = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    [[nodiscard]] bool shouldClose() const;
    void requestClose() const;

    // Drives the global GLFW event queue.
    static void pollEvents();

    void swapBuffers() const;

    [[nodiscard]] bool keyPressed(int key) const;
    [[nodiscard]] std::pair<int, int> framebufferSize() const;

    [[nodiscard]] GLFWwindow* handle() const { return m_window; }

private:
    GLFWwindow* m_window = nullptr;
};

} // namespace engine
