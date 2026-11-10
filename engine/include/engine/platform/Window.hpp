#pragma once

#include <GLFW/glfw3.h> // GLFW_INCLUDE_NONE is defined by the engine target

#include <string_view>
#include <utility>

namespace engine {

class Input;

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

    // Installs GLFW key/mouse/scroll callbacks feeding the given Input tracker.
    void setInputListener(Input* input);

    [[nodiscard]] bool keyPressed(int key) const;
    [[nodiscard]] std::pair<int, int> framebufferSize() const;

    [[nodiscard]] GLFWwindow* handle() const { return m_window; }

private:
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    GLFWwindow* m_window = nullptr;
    Input* m_input = nullptr;
};

} // namespace engine
