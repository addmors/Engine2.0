#pragma once

#include <array>
#include <utility>

namespace engine {

// Keyboard/mouse state with edge detection, fed by GLFW callbacks
// (replaces the 2017 global `Key.keys` boolean vector).
class Input final {
public:
    static constexpr int maxKeys = 512;          // GLFW_KEY_LAST + 1
    static constexpr int maxMouseButtons = 8;    // GLFW_MOUSE_BUTTON_LAST + 1

    // --- callbacks (installed by Window) -----------------------------------
    void onKey(int key, int action);
    void onMouseMove(double x, double y);
    void onMouseButton(int button, int action);
    void onScroll(double xoffset, double yoffset);

    // --- frame bookkeeping ---------------------------------------------------
    // Call at the very start of a frame, BEFORE polling events: snapshots the
    // current state so pressed()/released() report per-frame edges.
    void beginFrame();

    // --- queries -------------------------------------------------------------
    [[nodiscard]] bool isDown(int key) const;
    [[nodiscard]] bool pressed(int key) const;   // went down this frame
    [[nodiscard]] bool released(int key) const;  // went up this frame
    [[nodiscard]] bool anyKeyDown() const;

    [[nodiscard]] bool mouseDown(int button) const;
    [[nodiscard]] bool mousePressed(int button) const;

    [[nodiscard]] std::pair<double, double> mousePosition() const { return {m_mouseX, m_mouseY}; }
    [[nodiscard]] std::pair<double, double> mouseDelta() const { return {m_mouseDX, m_mouseDY}; }
    [[nodiscard]] std::pair<double, double> scrollDelta() const { return {m_scrollDX, m_scrollDY}; }

private:
    [[nodiscard]] static int clampKey(int key) { return (key >= 0 && key < maxKeys) ? key : 0; }

    std::array<bool, maxKeys> m_keys{};          // current
    std::array<bool, maxKeys> m_prevKeys{};      // previous frame
    std::array<bool, maxMouseButtons> m_mouseButtons{};
    std::array<bool, maxMouseButtons> m_prevMouseButtons{};

    double m_mouseX = 0.0;
    double m_mouseY = 0.0;
    double m_mouseDX = 0.0;
    double m_mouseDY = 0.0;
    double m_lastX = 0.0;
    double m_lastY = 0.0;
    bool m_firstMouse = true;

    double m_scrollDX = 0.0;
    double m_scrollDY = 0.0;
};

} // namespace engine
