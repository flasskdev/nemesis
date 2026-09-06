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
    const auto base = cmd->csgo_user_cmd.mutable_base();
    m_input_move = base ? math::vector2{base->forwardmove(), base->leftmove()} : math::vector2{};
    m_input_valid = std::isfinite(m_input_angles.y);
}

void airstrafe::on_create_move(systems::input::usercmd* cmd)
{
    m_active_this_tick = false;
    const auto local_state = systems::g_local.get();
    if (!cmd || m_strafe_pawn != local_state.pawn || cmd->command_number < m_strafe_command ||
        (cmd->command_number > m_strafe_command && cmd->command_number - m_strafe_command > 1))
        m_previous_world = {};
    if (cmd && (m_strafe_command != cmd->command_number || m_strafe_pawn != local_state.pawn))
    {
        m_reference_world = m_previous_world;
        m_strafe_command = cmd->command_number;
        m_strafe_pawn = local_state.pawn;
    }
    if (!cmd || !m_input_valid ||
        (!settings::g_movement.airstrafe.value && !settings::g_movement.m_test_strafer.enabled.value))
    { m_previous_world = {}; m_reference_world = {}; return; }
    const auto local = systems::g_local.get();
    const auto base = cmd->csgo_user_cmd.mutable_base();
    if (!base || !local.pawn || !local.is_alive || utils::is_movement_blocked(local.pawn)) return;
    if (g_jumpbug.active_this_tick() || g_edgebug.active_this_tick()) return;
    const auto& state = systems::g_prediction.pre();
    if (utils::is_on_ground(state, local.pawn)) { m_previous_world = {}; m_reference_world = {}; return; }
    const auto velocity = utils::pick_velocity(state);
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y)) return;

    constexpr auto left = static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveleft);
    constexpr auto right = static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveright);
    constexpr auto forward = static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward);
    constexpr auto back = static_cast<std::uintptr_t>(cstypes::command_buttons::in_back);
    const int f = int((m_input_buttons & forward) != 0) - int((m_input_buttons & back) != 0);
    const int l = int((m_input_buttons & left) != 0) - int((m_input_buttons & right) != 0);
    const auto quantize = CONVAR("sv_quantize_movement_input");
    const bool analog = quantize && !quantize->get<bool>();
    const bool valid_move = std::isfinite(m_input_move.x) && std::isfinite(m_input_move.y);
    const float input_f = analog && valid_move ? m_input_move.x : float(f);
    const float input_l = analog && valid_move ? m_input_move.y : float(l);
    m_target_yaw = m_input_angles.y;
    if (settings::g_movement.airstrafe_fully_directional.value && std::hypot(input_f, input_l) > 0.001f)
        m_target_yaw = math2d::yaw(m_target_yaw + std::atan2(input_l, input_f) / math2d::radians);
    m_braking = (m_input_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_sprint)) != 0;
    m_active_this_tick = true;
    finalize(cmd, false);
}

void airstrafe::finalize(systems::input::usercmd* cmd, bool final_pass)
{
    if (!cmd || !m_active_this_tick) return;
    const auto base = cmd->csgo_user_cmd.mutable_base();
    if (!base || !base->viewangles()) return;
    float command_yaw = base->viewangles()->y();
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
    const bool quantized = !quantize_cvar || quantize_cvar->get<bool>();
    const auto& combat = settings::g_combat;
    const auto& legit = combat.m_legitbot.get_group(features::combat::g_shared.ctx().weapon_type);
    constexpr auto interaction = cstypes::command_buttons::in_attack | cstypes::command_buttons::in_second_attack |
        cstypes::command_buttons::in_use;
    const bool aim_owns_angles = features::combat::g_rage.is_firing_this_tick() ||
        ((cmd->buttons.value | m_input_buttons) & interaction) ||
        (combat.m_legitbot.enabled.value && (legit.aimbot.value || legit.standalone_rcs.value));
    const bool can_align = final_pass && quantized && !aim_owns_angles &&
        !combat.m_antiaim.enabled.value && !combat.m_antiaim.spinbot.value &&
        !systems::g_local.is_in_cinematic() && !systems::g_local.is_in_time_freeze();
    auto best = math2d::choose_movement(v, m_target_yaw, command_yaw, cstypes::tick_interval,
        wishspeed, accel, cap, friction, quantized && !can_align, m_braking,
        {m_reference_world.x, m_reference_world.y}, settings::g_movement.airstrafe_heading_slack.value);
    if (can_align)
    {
        // Realize the world wish using legal digital input plus at most 22.5 degrees of COMMAND yaw.
        // Never change the camera, or steal angles from anti-aim, spin, interaction or a shot.
        const auto frame = math2d::realize_quantized(math2d::world(best, command_yaw), command_yaw);
        command_yaw = frame.view_yaw;
        best = frame.movement;
        base->mutable_viewangles()->set_y(command_yaw);
        for (int i = 0; i < cmd->csgo_user_cmd.input_history_size(); ++i)
            if (const auto entry = cmd->csgo_user_cmd.mutable_input_history(i))
                if (const auto angles = entry->mutable_view_angles()) angles->set_y(command_yaw);
    }
    best = math2d::limit(best);
    const auto chosen_world = math2d::world(best, command_yaw);
    m_previous_world = {chosen_world.x, chosen_world.y};
    base->set_forwardmove(best.x);
    base->set_leftmove(best.y);
    systems::g_input.sync_movement_buttons(cmd);
}
}
