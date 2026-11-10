#include "engine/platform/Window.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include "engine/core/Input.hpp"
#include "engine/core/Log.hpp"

namespace engine {

namespace {

void glfwErrorCallback(int code, const char* description)
{
    Log::error("GLFW error {}: {}", code, description != nullptr ? description : "unknown");
}

} // namespace

Window::Window(const WindowDesc& desc)
{
    glfwSetErrorCallback(glfwErrorCallback);

    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("glfwInit() failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, desc.glMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, desc.glMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);

    m_window = glfwCreateWindow(desc.width, desc.height, std::string{desc.title}.c_str(), nullptr, nullptr);
    if (m_window == nullptr) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow() failed (OpenGL "
            + std::to_string(desc.glMajor) + "." + std::to_string(desc.glMinor)
            + " core context not available)");
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // vsync, same as the 2017 build
    glfwSetWindowUserPointer(m_window, this);

    Log::info("window created: {}x{}, OpenGL {}.{} core",
        desc.width, desc.height, desc.glMajor, desc.glMinor);
}

void Window::setInputListener(Input* input)
{
    m_input = input;
    if (m_input != nullptr) {
        glfwSetKeyCallback(m_window, keyCallback);
        glfwSetCursorPosCallback(m_window, cursorPosCallback);
        glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
        glfwSetScrollCallback(m_window, scrollCallback);
    } else {
        glfwSetKeyCallback(m_window, nullptr);
        glfwSetCursorPosCallback(m_window, nullptr);
        glfwSetMouseButtonCallback(m_window, nullptr);
        glfwSetScrollCallback(m_window, nullptr);
    }
}

void Window::keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->m_input != nullptr) {
        self->m_input->onKey(key, action);
    }
}

void Window::cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->m_input != nullptr) {
        self->m_input->onMouseMove(xpos, ypos);
    }
}

void Window::mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->m_input != nullptr) {
        self->m_input->onMouseButton(button, action);
    }
}

void Window::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr && self->m_input != nullptr) {
        self->m_input->onScroll(xoffset, yoffset);
    }
}

Window::~Window()
{
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        glfwTerminate();
    }
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(m_window) == GLFW_TRUE;
}

void Window::requestClose() const
{
    glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Window::pollEvents()
{
    glfwPollEvents();
}

void Window::swapBuffers() const
{
    glfwSwapBuffers(m_window);
}

bool Window::keyPressed(int key) const
{
    return glfwGetKey(m_window, key) == GLFW_PRESS;
}

std::pair<int, int> Window::framebufferSize() const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    return {width, height};
}

} // namespace engine
