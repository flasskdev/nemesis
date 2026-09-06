#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace features::combat::angle_math {
class spin_clock
{
public:
    void reset() { m_initialized = false; }
    float update(float view_yaw, std::intptr_t command, std::uintptr_t pawn, float speed, float dt)
    {
        if (!std::isfinite(view_yaw) || !std::isfinite(dt) || dt <= 0.0f)
        { reset(); return 0.0f; }
        speed = std::isfinite(speed) ? std::clamp(speed, 0.0f, 1440.0f) : 360.0f;
        if (!m_initialized || pawn != m_pawn || command < m_command ||
            (command > m_command && static_cast<std::uintmax_t>(command) - static_cast<std::uintmax_t>(m_command) > 4096))
        {
            m_angle = std::remainder(view_yaw, 360.0f);
            m_initialized = true;
        }
        else if (command > m_command)
        {
            // Pause during suppressed commands; never catch up a whole attack/use gap in one tick.
            m_angle = std::remainder(m_angle + speed * dt, 360.0f);
        }
        m_pawn = pawn;
        m_command = command;
        return m_angle;
    }
private:
    float m_angle{};
    std::intptr_t m_command{};
    std::uintptr_t m_pawn{};
    bool m_initialized{};
};
}
