#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>

namespace features::movement {
    void quickstop::on_create_move(systems::input::usercmd* cmd) const
    {
        constexpr auto move_buttons = cstypes::command_buttons::in_forward |
            cstypes::command_buttons::in_back |
            cstypes::command_buttons::in_moveleft |
            cstypes::command_buttons::in_moveright;

        const bool has_movement_input = (cmd->buttons.value & move_buttons) != 0;
        const bool is_bound = (settings::g_movement.quickstop.bind.key != 0);
        const bool is_key_active = is_bound && settings::g_movement.quickstop.bind.active;

        // Активация ТОЛЬКО если:
        // 1. Кнопка бинда зажата
        // 2. ИЛИ включён quickstop И нет movement input (отпустил кнопки = стоп)
        if (!is_key_active && !(settings::g_movement.quickstop.value && !has_movement_input))
            return;

        if (features::movement::g_slowwalk.active_this_tick())
            return;

        const auto local = systems::g_local.get();
        if (!local.pawn)
            return;

        const auto& prestate = systems::g_prediction.pre();
        if (!(prestate.flags & cstypes::entity_flags::on_ground))
            return;

        const auto move_type = memory::read<std::uint8_t>(local.pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));
        if (move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip)
            return;

        const auto velocity = prestate.networked_velocity;
        const auto speed = velocity.length_2d();

        const auto base = cmd->csgo_user_cmd.mutable_base();
        if (!base)
            return;

        // HARD ZERO THRESHOLD: ниже 30 u/s → полное обнуление ввода + subtick delta cancellation
        constexpr float HARD_STOP_THRESHOLD = 30.0f;
        if (speed <= HARD_STOP_THRESHOLD)
        {
            base->set_forwardmove(0.0f);
            base->set_leftmove(0.0f);
            cmd->buttons.value &= ~static_cast<std::uintptr_t>(move_buttons);

            const auto subtick_moves = base->mutable_subtick_moves();
            if (subtick_moves)
            {
                if (const auto step = systems::g_input.acquire_subtick_step(subtick_moves))
                {
                    step->set_button(0);
                    step->set_pressed(false);
                    step->set_when(0.0f);
                    step->set_analog_forward_delta(-prestate.last_movement_impulses.x);
                    step->set_analog_left_delta(-prestate.last_movement_impulses.y);
                }
            }
            return;
        }

        // Counter-strafe для скоростей выше порога
        const auto view_yaw = base->viewangles()->y();
        const auto view_yaw_rad = view_yaw * (std::numbers::pi_v<float> / 180.0f);

        const auto wish_norm_x = -velocity.x / speed;
        const auto wish_norm_y = -velocity.y / speed;

        const auto cy = std::cosf(view_yaw_rad);
        const auto sy = std::sinf(view_yaw_rad);

        const auto forward_component = wish_norm_x * cy + wish_norm_y * sy;
        const auto left_component = -(wish_norm_x * sy - wish_norm_y * cy);

        const auto sv_accelerate = CONVAR("sv_accelerate")->get<float>();
        const auto max_weapon_speed = combat::g_shared.ctx().valid ? combat::g_shared.ctx().weapon_max_speed : 250.0f;
        const auto accel_drop = sv_accelerate * max_weapon_speed * prestate.surface_friction * cstypes::tick_interval;

        float magnitude = 1.0f;
        if (speed < accel_drop && accel_drop > 0.001f)
            magnitude = std::clamp(speed / accel_drop, 0.05f, 1.0f);

        const auto forward_move = std::clamp(forward_component * magnitude, -1.0f, 1.0f);
        const auto left_move = std::clamp(left_component * magnitude, -1.0f, 1.0f);

        base->set_forwardmove(forward_move);
        base->set_leftmove(left_move);

        auto buttons = cmd->buttons.value;
        buttons &= ~static_cast<std::uintptr_t>(move_buttons);
        if (forward_move > 0.05f)      buttons |= cstypes::command_buttons::in_forward;
        else if (forward_move < -0.05f) buttons |= cstypes::command_buttons::in_back;
        if (left_move > 0.05f)          buttons |= cstypes::command_buttons::in_moveleft;
        else if (left_move < -0.05f)    buttons |= cstypes::command_buttons::in_moveright;
        cmd->buttons.value = buttons;

        const auto subtick_moves = base->mutable_subtick_moves();
        if (subtick_moves)
        {
            if (const auto step = systems::g_input.acquire_subtick_step(subtick_moves))
            {
                step->set_button(0);
                step->set_pressed(false);
                step->set_when(0.0f);
                step->set_analog_forward_delta(forward_move - prestate.last_movement_impulses.x);
                step->set_analog_left_delta(left_move - prestate.last_movement_impulses.y);
            }
        }
    }
} // namespace features::movement