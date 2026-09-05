// test_strafer.cpp
#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include "../movement.hpp"
#include "../movement_utils.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

[[nodiscard]] bool test_strafer::is_active() const
{
    if (!settings::g_movement.m_test_strafer.enabled.value)
    {
        return false;
    }

    if (settings::g_combat.m_antiaim.enabled.value)
    {
        return false;
    }
    if (settings::g_combat.m_antiaim.spinbot.value)
    {
        return false;
    }

    return CONVAR("sv_quantize_movement_input")->get<bool>();
}

math::vector2 test_strafer::movement_from_buttons(std::uintptr_t pressed)
{
    auto forward_move{ 0.0f };
    auto left_move{ 0.0f };

    if (pressed & cstypes::command_buttons::in_forward)
    {
        forward_move = 1.0f;
    }
    else if (pressed & cstypes::command_buttons::in_back)
    {
        forward_move = -1.0f;
    }

    if (pressed & cstypes::command_buttons::in_moveleft)
    {
        left_move = 1.0f;
    }
    else if (pressed & cstypes::command_buttons::in_moveright)
    {
        left_move = -1.0f;
    }

    return { forward_move, left_move };
}

void test_strafer::on_create_move(systems::input::usercmd* cmd)
{
    this->m_handled_this_tick = false;

    if (!this->is_active())
    {
        return;
    }

    // Do not overwrite the primary strafer's analog fallback either.
    if (features::movement::g_airstrafe.active_this_tick())
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

    const auto& prestate = systems::g_prediction.pre();
    if (prestate.flags & cstypes::entity_flags::on_ground)
    {
        return;
    }

    if (features::combat::g_rage.is_firing_this_tick())
    {
        return;
    }

    this->quantized_path(cmd);
}

bool test_strafer::apply_yaw_subtick(proto::base_usercmd_pb* base, float when, float yaw_delta) const
{
    math::helpers::normalize_angle(yaw_delta);
    if (std::fabsf(yaw_delta) <= 0.01f)
    {
        return false;
    }

    const auto subtick_moves = base->mutable_subtick_moves();
    if (!subtick_moves)
    {
        return false;
    }

    const auto step = systems::g_input.acquire_subtick_step(subtick_moves);
    if (!step)
    {
        return false;
    }

    step->set_when(when);
    step->set_button(0);
    step->set_pressed(false);
    step->set_analog_forward_delta(0.0f);
    step->set_analog_left_delta(0.0f);
    step->set_yaw_delta(yaw_delta);
    step->set_pitch_delta(0.0f);

    return true;
}

void test_strafer::quantized_path(systems::input::usercmd* cmd)
{
    const auto current_buttons = cmd->buttons.value;
    constexpr auto angle_sensitive_buttons = static_cast<std::uintptr_t>(
        cstypes::command_buttons::in_attack | cstypes::command_buttons::in_second_attack |
        cstypes::command_buttons::in_use | cstypes::command_buttons::in_jump);
    if (current_buttons & angle_sensitive_buttons)
        return;
    if (current_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_sprint))
    {
        return;
    }

    const auto base = cmd->csgo_user_cmd.mutable_base();
    if (!base)
    {
        return;
    }

    this->check_button(current_buttons, cstypes::command_buttons::in_moveleft);
    this->check_button(current_buttons, cstypes::command_buttons::in_moveright);
    this->check_button(current_buttons, cstypes::command_buttons::in_forward);
    this->check_button(current_buttons, cstypes::command_buttons::in_back);
    this->m_last_buttons = current_buttons;

    const auto& prestate = systems::g_prediction.pre();
    const auto velocity = prestate.networked_velocity;
    const auto speed_2d = velocity.length_2d();

    const auto command_yaw = base->viewangles()
        ? base->viewangles()->y()
        : systems::g_input.get_view_angles().y;

    const auto player_move = movement_from_buttons(this->m_last_pressed);
    if (player_move.x == 0.0f && player_move.y == 0.0f)
    {
        return;
    }

    if (speed_2d < utils::k_min_strafe_speed)
    {
        return;
    }

    const auto start_when = utils::get_max_subtick_when(base);
    if (start_when >= 0.99f)
    {
        return;
    }

    const auto sv_airaccelerate = CONVAR("sv_airaccelerate")->get<float>();
    const auto sv_maxspeed = utils::get_player_maxspeed(systems::g_local.get().pawn);
    const auto sv_air_max_wishspeed = CONVAR("sv_air_max_wishspeed")->get<float>();
    const auto surface_friction = prestate.surface_friction;

    const auto base_yaw_offset = std::atan2f(player_move.y, player_move.x) * (180.0f / std::numbers::pi_v<float>);
    auto target_yaw = command_yaw + base_yaw_offset;
    math::helpers::normalize_angle(target_yaw);

    const auto when_step = (1.0f - start_when) / static_cast<float>(utils::k_max_subticks + 1);
    const auto sub_frame = when_step * cstypes::tick_interval;

    auto acc_yaw = command_yaw;
    auto sim_vx = velocity.x;
    auto sim_vy = velocity.y;
    auto injected = 0;
    utils::simulate_air_accel(sim_vx, sim_vy, command_yaw + base_yaw_offset,
        (start_when + when_step) * cstypes::tick_interval, surface_friction,
        sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed);

    const auto entry_side = (this->m_substep_counter % 2) == 0;
    for (auto i = 1; i <= utils::k_max_subticks; ++i)
    {
        const auto wishdir_yaw = utils::compute_strafe_yaw(
            sim_vx, sim_vy, target_yaw, sub_frame, entry_side,
            sv_maxspeed, sv_airaccelerate, sv_air_max_wishspeed, surface_friction);

        auto target_view_yaw = wishdir_yaw - base_yaw_offset;
        math::helpers::normalize_angle(target_view_yaw);

        auto yaw_delta = target_view_yaw - acc_yaw;
        math::helpers::normalize_angle(yaw_delta);

        const auto when_frac = start_when + static_cast<float>(i) * when_step;

        if (!this->apply_yaw_subtick(base, when_frac, yaw_delta))
        {
            break;
        }

        acc_yaw = target_view_yaw;
        utils::simulate_air_accel(sim_vx, sim_vy, wishdir_yaw, sub_frame,
                                  surface_friction, sv_maxspeed,
                                  sv_airaccelerate, sv_air_max_wishspeed);
        ++injected;
    }

    if (injected > 0)
    {
        this->m_handled_this_tick = true;
        ++this->m_substep_counter;
    }
}

void test_strafer::check_button(std::uintptr_t current_buttons, std::uintptr_t button)
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

} // namespace features::movement