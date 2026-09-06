#include <pch/pch.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>

namespace features::movement {
bool test_strafer::is_active() const
{
    return settings::g_movement.m_test_strafer.enabled.value;
}
void test_strafer::on_create_move(systems::input::usercmd* cmd)
{
    // Legacy toggle is an alias for the shared controller, not a second angle writer.
    m_handled_this_tick = cmd && is_active() && g_airstrafe.active_this_tick();
}
}
