// airstrafe.cpp
#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include "../movement.hpp"
#include "../movement_utils.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

void airstrafe::store_angles()
{
    const auto cmd = systems::g_input.get();
    this->m_input_valid = cmd != nullptr;
    if (!cmd)
    {
        this->m_angles_valid = false;
        return;
    }
    this->m_input_angles = systems::g_input.get_view_angles();
    this->m_input_buttons = cmd->buttons.value;
    if (!this->m_angles_valid)
    {
        this->m_angles = this->m_input_angles;
        this->m_angles_valid = true;
    }
}

void airstrafe::check_button(std::uintptr_t current_buttons, std::uintptr_t button)
{
    constexpr auto moveleft = static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveleft);
    constexpr auto moveright = static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveright);
    constexpr auto forward = static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward);
    constexpr auto back = static_cast<std::uintptr_t>(cstypes::command_buttons::in_back);

    if (current_buttons & button && (!(this->m_last_buttons & button) || (button & moveleft && !(this->m_last_pressed & moveright)) || (button & moveright && !(this->m_last_pressed & moveleft)) || (button & forward && !(this->m_last_pressed & back)) || (button & back && !(this->m_last_pressed & forward))))
    {
        if (button & moveleft)
        {
            this->m_last_pressed &= ~moveright;
        }
        else if (button & moveright)
        {
            this->m_last_pressed &= ~moveleft;
        }
        else if (button & forward)
        {
            this->m_last_pressed &= ~back;
        }
        else if (button & back)
        {
            this->m_last_pressed &= ~forward;
        }

        this->m_last_pressed |= button;
    }
    else if (!(current_buttons & button))
    {
        this->m_last_pressed &= ~button;
    }
}

void airstrafe::rotate_to_stop(proto::base_usercmd_pb* base, const math::vector3& velocity) const
{
    const auto speed = velocity.length_2d();
    if (speed < 1.0f)
    {
        base->set_forwardmove(0.0f);
        base->set_leftmove(0.0f);
        return;
    }

    const auto stop_yaw = std::atan2f(velocity.y, velocity.x) * (180.0f / std::numbers::pi_v<float>) + 180.0f;
    const auto final_yaw = base->viewangles() ? base->viewangles()->y() : systems::g_input.get_view_angles().y;

    const auto delta = math::helpers::normalize_yaw(stop_yaw - final_yaw);
    const auto delta_rad = delta * (std::numbers::pi_v<float> / 180.0f);

    auto fwd = std::cosf(delta_rad);
    auto side = std::sinf(delta_rad);

    const auto max_c = std::max(std::fabsf(fwd), std::fabsf(side));
    if (max_c > 0.001f)
    {
        fwd /= max_c;
        side /= max_c;
    }

    base->set_forwardmove(std::clamp(fwd, -1.0f, 1.0f));
    base->set_leftmove(std::clamp(side, -1.0f, 1.0f));
}

void airstrafe::rotate_movement(proto::base_usercmd_pb* base, float target_yaw, float view_yaw) const
{
    const auto delta = math::helpers::normalize_yaw(target_yaw - view_yaw);
    const auto delta_rad = delta * (std::numbers::pi_v<float> / 180.0f);

    auto fwd = std::cosf(delta_rad);
    auto side = std::sinf(delta_rad);

    const auto max_c = std::max(std::fabsf(fwd), std::fabsf(side));
    if (max_c > 0.001f)
    {
        fwd /= max_c;
        side /= max_c;
    }

    base->set_forwardmove(std::clamp(fwd, -1.0f, 1.0f));
    base->set_leftmove(std::clamp(side, -1.0f, 1.0f));
}

// ---------------------------------------------------------------------------
// Subtick yaw-delta injection — neverlose-style multi-strafe per tick
// ---------------------------------------------------------------------------

bool airstrafe::apply_yaw_subtick(proto::base_usercmd_pb* base, float when, float yaw_delta) const
{
    math::helpers::normalize_angle(yaw_delta);
    if (std::fabsf(yaw_delta) <= 0.01f)
        return true;

    const auto subtick_moves = base->mutable_subtick_moves();
    if (!subtick_moves)
        return false;

    const auto step = systems::g_input.acquire_subtick_step(subtick_moves);
    if (!step)
        return false;

    step->set_when(when);
    step->set_button(0);
    step->set_pressed(false);
    step->set_analog_forward_delta(0.0f);
    step->set_analog_left_delta(0.0f);
    step->set_yaw_delta(yaw_delta);
    step->set_pitch_delta(0.0f);

    return true;
}

void airstrafe::inject_subtick_strafes(
    proto::base_usercmd_pb* base,
    systems::input::usercmd* cmd,
    const systems::prediction::state& prestate,
    float target_yaw,
    float view_yaw)
{
    const auto vel = utils::pick_velocity(prestate);
    const auto speed_2d = vel.length_2d();
    if (speed_2d < utils::k_min_strafe_speed)
        return;

    const auto start_when = utils::get_max_subtick_when(base);
    if (start_when >= 0.98f)
        return;

    const auto sv_airaccelerate = CONVAR("sv_airaccelerate")->get<float>();
    const auto sv_maxspeed = utils::get_player_maxspeed(systems::g_local.get().pawn);
    const auto sv_air_max_wishspeed = CONVAR("sv_air_max_wishspeed")->get<float>();
    const auto surface_friction = (prestate.surface_friction > 0.0f) ? prestate.surface_friction : 1.0f;

    const auto when_step = (1.0f - start_when) / static_cast<float>(utils::k_max_subticks + 1);
    const auto sub_frame = when_step * cstypes::tick_interval;

    auto acc_yaw = view_yaw;
    auto sim_vx = vel.x;
    auto sim_vy = vel.y;
    auto injected = 0;
    const float strafe_key_yaw_offset = this->m_side_switch ? 90.0f : -90.0f;
    // The original side input applies until the first generated event.
    utils::simulate_air_accel(sim_vx, sim_vy, view_yaw + strafe_key_yaw_offset,
        (start_when + when_step) * cstypes::tick_interval, surface_friction,
        sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed);

    for (auto i = 1; i <= utils::k_max_subticks; ++i)
    {
        // Do NOT flip m_side_switch intra-tick: conflicting yaw deltas within
        // one tick cancel each other and cause net zero or negative acceleration.
        // Direction is decided ONCE per tick in on_create_move.

        const auto wishdir_yaw = utils::compute_strafe_yaw(
            sim_vx, sim_vy, target_yaw, sub_frame, this->m_side_switch,
            sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed, surface_friction);

        // A/D key approach: leftmove = ±1, forwardmove = 0.
        // Wishdir = viewyaw ± 90°, so target_view_yaw = wishdir_yaw ∓ 90°.
        auto target_view_yaw = wishdir_yaw - strafe_key_yaw_offset;
        math::helpers::normalize_angle(target_view_yaw);

        auto yaw_delta = target_view_yaw - acc_yaw;
        math::helpers::normalize_angle(yaw_delta);

        const auto when_frac = start_when + static_cast<float>(i) * when_step;

        if (!this->apply_yaw_subtick(base, when_frac, yaw_delta))
            break;

        acc_yaw = target_view_yaw;
        utils::simulate_air_accel(sim_vx, sim_vy, wishdir_yaw, sub_frame,
                                  surface_friction, sv_maxspeed,
                                  sv_airaccelerate, sv_air_max_wishspeed);
        ++injected;
    }

    if (injected > 0)
    {
        this->m_handled_subtick = true;
        ++this->m_substep_counter;
    }
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

void airstrafe::on_create_move(systems::input::usercmd* cmd)
{
    this->m_active_this_tick = false;
    this->m_handled_subtick = false;

    if (!settings::g_movement.airstrafe.value)
    {
        return;
    }

    if (features::movement::g_jumpbug.active_this_tick())
    {
        return;
    }

    const auto base = cmd->csgo_user_cmd.mutable_base();
    if (!base)
    {
        return;
    }

    const auto local = systems::g_local.get();
    if (!local.pawn)
    {
        return;
    }

    const auto move_type = memory::read<std::uint8_t>(local.pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));
    if (move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip)
    {
        return;
    }

    const auto current_mouse = this->m_input_valid
        ? this->m_input_angles : systems::g_input.get_view_angles();
    if (!this->m_angles_valid)
    {
        this->m_angles = current_mouse;
        this->m_angles_valid = true;
    }

    const auto& prestate = systems::g_prediction.pre();
    if (prestate.flags & cstypes::entity_flags::on_ground)
    {
        this->m_angles = current_mouse;
        return;
    }

    const auto current_buttons = this->m_input_valid ? this->m_input_buttons : cmd->buttons.value;
    const bool shift_held = (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_sprint)) != 0;

    const auto vel = utils::pick_velocity(prestate);
    const auto speed_2d = vel.length_2d();

    // Shift held -> air brake (stop)
    if (shift_held)
    {
        this->m_active_this_tick = true;
        this->rotate_to_stop(base, vel);
        this->m_angles = current_mouse;
        return;
    }

    this->m_active_this_tick = true;

    // --- Key states ---
    const bool press_fwd = (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward)) != 0;
    const bool press_back = (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_back)) != 0;
    const bool press_left = (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveleft)) != 0;
    const bool press_right = (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveright)) != 0;

    const auto mouse_yaw = current_mouse.y;
    const auto mouse_delta = math::helpers::normalize_yaw(mouse_yaw - this->m_angles.y);
    this->m_angles = current_mouse;

    // Determine target world-space travel direction from user input (W/A/S/D/diagonals or view forward)
    const auto target_world_yaw = utils::get_target_yaw(mouse_yaw, current_buttons);

    // Current velocity direction in world space
    const auto velocity_yaw = (speed_2d > 1.0f)
        ? (std::atan2f(vel.y, vel.x) * (180.0f / std::numbers::pi_v<float>))
        : target_world_yaw;

    // How far has velocity drifted from intended direction
    const auto delta_yaw = math::helpers::normalize_yaw(target_world_yaw - velocity_yaw);

    // Side selection logic:
    // 1. Manual directional strafe with A or D (highest priority)
    if (press_left && !press_right)
    {
        this->m_side_switch = true;
    }
    else if (press_right && !press_left)
    {
        this->m_side_switch = false;
    }
    // 2. Mouse steering: steer into mouse turn
    else if (std::fabsf(mouse_delta) > 0.02f)
    {
        this->m_side_switch = (mouse_delta > 0.0f);
    }
    // 3. Auto-pilot S-curve (W held, S held, or Space only without movement keys)
    else
    {
        // 30° wide hysteresis: produces smooth, sweeping, pro-level S-curves (8-10 ticks per side).
        // Avoids the rapid 2-tick flip-flop at 350+ u/s that bleeds speed and causes jitter.
        constexpr float k_drift_threshold = 30.0f;
        if (delta_yaw > k_drift_threshold)
        {
            this->m_side_switch = true;
        }
        else if (delta_yaw < -k_drift_threshold)
        {
            this->m_side_switch = false;
        }
    }

    // ====================================================================
    // Subtick yaw-delta injection path (quantized movement / official servers)
    // ====================================================================
    const float command_yaw = base->viewangles() ? base->viewangles()->y() : mouse_yaw;
    constexpr auto angle_sensitive_buttons = static_cast<std::uintptr_t>(
        cstypes::command_buttons::in_attack | cstypes::command_buttons::in_second_attack |
        cstypes::command_buttons::in_use | cstypes::command_buttons::in_jump);
    const bool allow_yaw_subticks =
        !features::combat::g_misc.antiaim().is_active() &&
        !settings::g_combat.m_antiaim.spinbot.value &&
        !features::combat::g_rage.is_firing_this_tick() &&
        !(cmd->buttons.value & angle_sensitive_buttons) &&
        std::fabs(math::helpers::normalize_yaw(command_yaw - mouse_yaw)) < 0.01f;
    const auto quantize = CONVAR("sv_quantize_movement_input");
    if (allow_yaw_subticks && quantize && quantize->get<bool>() &&
        speed_2d > utils::k_min_strafe_speed)
    {
        this->inject_subtick_strafes(base, cmd, prestate, target_world_yaw, command_yaw);

        if (this->m_handled_subtick)
        {
            // Positive leftmove means left in this project's command convention.
            const float strafe_side = this->m_side_switch ? 1.0f : -1.0f;
            base->set_forwardmove(0.0f);
            base->set_leftmove(strafe_side);

            auto buttons = cmd->buttons.value;
            buttons &= ~static_cast<std::uintptr_t>(
                cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
                cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright);

            if (this->m_side_switch)
            {
                buttons |= static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveleft);
            }
            else
            {
                buttons |= static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveright);
            }

            cmd->buttons.value_changed |= (cmd->buttons.value ^ buttons);
            cmd->buttons.value = buttons;
            return;
        }
    }

    // ====================================================================
    // Analog path: low speed, non-quantized input, or another angle owner
    // ====================================================================
    const auto reference_yaw = base->viewangles() ? base->viewangles()->y() : mouse_yaw;

    // At low speed, push directly in target direction to begin moving
    float desired_yaw = target_world_yaw;
    if (speed_2d > 15.0f)
    {
        const auto sv_airaccelerate = CONVAR("sv_airaccelerate")->get<float>();
        const auto sv_maxspeed = utils::get_player_maxspeed(systems::g_local.get().pawn);
        const auto sv_air_max_wishspeed = CONVAR("sv_air_max_wishspeed")->get<float>();
        desired_yaw = utils::compute_strafe_yaw(
            vel.x, vel.y, target_world_yaw, cstypes::tick_interval, this->m_side_switch,
            sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed,
            prestate.surface_friction > 0.0f ? prestate.surface_friction : 1.0f);
    }

    const auto delta_angle = math::helpers::normalize_yaw(desired_yaw - reference_yaw);
    const auto delta_rad = delta_angle * (std::numbers::pi_v<float> / 180.0f);

    auto cmd_forward = std::cosf(delta_rad);
    auto cmd_side    = std::sinf(delta_rad);

    const auto max_c = std::max(std::fabsf(cmd_forward), std::fabsf(cmd_side));
    if (max_c > 0.001f)
    {
        cmd_forward /= max_c;
        cmd_side /= max_c;
    }

    base->set_forwardmove(std::clamp(cmd_forward, -1.0f, 1.0f));
    base->set_leftmove(std::clamp(cmd_side, -1.0f, 1.0f));

    auto buttons = cmd->buttons.value;
    buttons &= ~static_cast<std::uintptr_t>(
        cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
        cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright);

    if (base->forwardmove() > 0.2f)
        buttons |= static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward);
    else if (base->forwardmove() < -0.2f)
        buttons |= static_cast<std::uintptr_t>(cstypes::command_buttons::in_back);

    if (base->leftmove() > 0.2f)
        buttons |= static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveleft);
    else if (base->leftmove() < -0.2f)
        buttons |= static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveright);

    cmd->buttons.value_changed |= (cmd->buttons.value ^ buttons);
    cmd->buttons.value = buttons;
}

} // namespace features::movement