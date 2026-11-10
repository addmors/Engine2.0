#include "engine/core/Input.hpp"

namespace engine {

void Input::onKey(int key, int action)
{
    const int k = clampKey(key);
    if (k == 0 && key != 0) {
        return; // out-of-range key (e.g. GLFW_KEY_UNKNOWN = -1)
    }
    m_keys[k] = (action != 0); // GLFW_PRESS or GLFW_REPEAT count as down
}

void Input::onMouseMove(double x, double y)
{
    if (m_firstMouse) {
        m_lastX = x;
        m_lastY = y;
        m_firstMouse = false;
    }
    m_mouseDX += x - m_lastX;
    m_mouseDY += y - m_lastY;
    m_lastX = x;
    m_lastY = y;
    m_mouseX = x;
    m_mouseY = y;
}

void Input::onMouseButton(int button, int action)
{
    if (button >= 0 && button < maxMouseButtons) {
        m_mouseButtons[button] = (action != 0);
    }
}

void Input::onScroll(double xoffset, double yoffset)
{
    m_scrollDX += xoffset;
    m_scrollDY += yoffset;
}

void Input::beginFrame()
{
    m_prevKeys = m_keys;
    m_prevMouseButtons = m_mouseButtons;
    m_mouseDX = 0.0;
    m_mouseDY = 0.0;
    m_scrollDX = 0.0;
    m_scrollDY = 0.0;
}

bool Input::isDown(int key) const
{
    return m_keys[clampKey(key)];
}

bool Input::pressed(int key) const
{
    const int k = clampKey(key);
    return m_keys[k] && !m_prevKeys[k];
}

bool Input::released(int key) const
{
    const int k = clampKey(key);
    return !m_keys[k] && m_prevKeys[k];
}

bool Input::anyKeyDown() const
{
    for (int i = 1; i < maxKeys; ++i) {
        if (m_keys[i] && !m_prevKeys[i]) {
            return true;
        }
    }
    return false;
}

bool Input::mouseDown(int button) const
{
    return button >= 0 && button < maxMouseButtons && m_mouseButtons[button];
}

bool Input::mousePressed(int button) const
{
    return button >= 0 && button < maxMouseButtons && m_mouseButtons[button] && !m_prevMouseButtons[button];
}

} // namespace engine
