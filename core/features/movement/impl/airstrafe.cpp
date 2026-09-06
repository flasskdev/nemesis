#include <pch/pch.hpp>
#include <limits>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>
#include "../movement.hpp"
#include "../movement_utils.hpp"
#include "../movement_math.hpp"

namespace features::movement {
void airstrafe::store_angles()
{
    const auto cmd = systems::g_input.get();
    m_active_this_tick = false;
    m_input_valid = cmd != nullptr;
    if (!cmd) return;
    m_input_angles = systems::g_input.get_view_angles();
    m_input_buttons = cmd->buttons.value;
    m_input_valid = std::isfinite(m_input_angles.y);
}

void airstrafe::on_create_move(systems::input::usercmd* cmd)
{
    m_active_this_tick = false;
    if (!cmd || !m_input_valid ||
        (!settings::g_movement.airstrafe.value && !settings::g_movement.m_test_strafer.enabled.value)) return;
    const auto local = systems::g_local.get();
    const auto base = cmd->csgo_user_cmd.mutable_base();
    if (!base || !local.pawn || !local.is_alive || utils::is_movement_blocked(local.pawn)) return;
    if (g_jumpbug.active_this_tick() || g_edgebug.active_this_tick()) return;
    const auto& state = systems::g_prediction.pre();
    if (utils::is_on_ground(state, local.pawn)) return;
    const auto velocity = utils::pick_velocity(state);
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y)) return;

    constexpr auto left = static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveleft);
    constexpr auto right = static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveright);
    constexpr auto forward = static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward);
    constexpr auto back = static_cast<std::uintptr_t>(cstypes::command_buttons::in_back);
    const int f = int((m_input_buttons & forward) != 0) - int((m_input_buttons & back) != 0);
    const int l = int((m_input_buttons & left) != 0) - int((m_input_buttons & right) != 0);
    m_target_yaw = m_input_angles.y;
    if (settings::g_movement.airstrafe_fully_directional.value && (f || l))
        m_target_yaw = math2d::yaw(m_target_yaw + std::atan2(float(l), float(f)) / math2d::radians);
    m_braking = (m_input_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_sprint)) != 0;
    m_active_this_tick = true;
    finalize(cmd);
}

void airstrafe::finalize(systems::input::usercmd* cmd)
{
    if (!cmd || !m_active_this_tick) return;
    const auto base = cmd->csgo_user_cmd.mutable_base();
    if (!base || !base->viewangles()) return;
    const float command_yaw = base->viewangles()->y();
    if (!std::isfinite(command_yaw)) return;
    const auto& state = systems::g_prediction.pre();
    const auto velocity = utils::pick_velocity(state);
    const math2d::vector v{velocity.x, velocity.y};
    const float wishspeed = utils::get_player_maxspeed(systems::g_local.get().pawn);
    const auto aa = CONVAR("sv_airaccelerate");
    const auto cap_cvar = CONVAR("sv_air_max_wishspeed");
    if (!aa || !cap_cvar) return;
    const float accel = aa->get<float>();
    const float cap = cap_cvar->get<float>();
    const float friction = std::isfinite(state.surface_friction)
        ? std::max(0.0f, state.surface_friction) : 1.0f;
    if (!std::isfinite(accel) || !std::isfinite(cap) || accel < 0.0f || cap < 0.0f) return;
    const auto quantize_cvar = CONVAR("sv_quantize_movement_input");
    const bool quantized = quantize_cvar && quantize_cvar->get<bool>();
    auto best = math2d::choose_movement(v, m_target_yaw, command_yaw, cstypes::tick_interval,
        wishspeed, accel, cap, friction, quantized, m_braking);
    best = math2d::limit(best);
    base->set_forwardmove(best.x);
    base->set_leftmove(best.y);
    systems::g_input.sync_movement_buttons(cmd);
}
}
