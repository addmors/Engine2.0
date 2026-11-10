#pragma once

#include <chrono>

namespace engine {

// Frame clock: delta time, total elapsed time and a smoothed FPS estimate.
class FrameClock final {
public:
    // Call once at the beginning of every frame.
    void beginFrame()
    {
        const auto now = clock::now();
        m_delta = (m_last == time_point{}) ? duration{} : now - m_last;
        m_last = now;
        m_total += m_delta;

        // Exponential moving average keeps fps() readable under jitter.
        m_smoothedDelta = (m_smoothedDelta == duration{}) ? m_delta
            : std::chrono::duration_cast<duration>(m_smoothedDelta * 0.9 + m_delta * 0.1);
    }

    [[nodiscard]] float deltaTime() const { return seconds(m_delta); }
    [[nodiscard]] float totalTime() const { return seconds(m_total); }
    [[nodiscard]] float smoothedDelta() const { return seconds(m_smoothedDelta); }

    [[nodiscard]] float fps() const
    {
        const float d = smoothedDelta();
        return d > 0.0F ? 1.0F / d : 0.0F;
    }

private:
    using clock = std::chrono::steady_clock;
    using duration = clock::duration;
    using time_point = clock::time_point;

    static float seconds(duration d) { return std::chrono::duration<float>(d).count(); }

    time_point m_last{};
    duration m_delta{};
    duration m_total{};
    duration m_smoothedDelta{};
};

} // namespace engine
