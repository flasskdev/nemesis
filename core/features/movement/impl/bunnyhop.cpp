// bunnyhop.cpp
#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include "../movement.hpp"
#include "../movement_utils.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

namespace {

[[nodiscard]] std::optional<float> predict_landing_fraction(
    std::uintptr_t local_pawn,
    std::uintptr_t movement_services,
    const systems::prediction::state& prestate,
    bool holding_duck)
{
    const auto vel_check = utils::pick_velocity(prestate);
    if (vel_check.z > 0.0f)
    {
        return std::nullopt;
    }

    const auto duck_amount = memory::read<float>(movement_services + SCHEMA("CCSPlayer_MovementServices", "m_flDuckAmount"_hash));
    const auto mins = memory::read<math::vector3>(local_pawn + SCHEMA("C_BaseModelEntity", "m_Collision"_hash) + SCHEMA("CCollisionProperty", "m_vecMins"_hash));
    auto maxs = memory::read<math::vector3>(local_pawn + SCHEMA("C_BaseModelEntity", "m_Collision"_hash) + SCHEMA("CCollisionProperty", "m_vecMaxs"_hash));

    auto trace_origin = prestate.networked_origin;
    if (holding_duck && duck_amount > 0.0f)
    {
        const auto standing_height{ 72.0f };
        const auto duck_hull_diff = standing_height - maxs.z;
        trace_origin.z -= duck_hull_diff * 0.5f;
        maxs.z = standing_height;
    }

    auto trace_mask{ 0ull };
    {
        const auto pawn_ptr = memory::read<std::uintptr_t>(movement_services + 56);
        if (!pawn_ptr)
            return std::nullopt;
        trace_mask = memory::read<std::uintptr_t>(pawn_ptr + 0xd48);
        if (memory::read<std::uint32_t>(pawn_ptr + 0x3f8) & 0x10)
        {
            trace_mask |= 0x20;
        }
    }

    const auto filter = systems::g_tracing.make_player_movement_filter(local_pawn, trace_mask, 11);
    const auto sv_gravity = CONVAR("sv_gravity")->get<float>();
    const auto sv_standable_normal = CONVAR("sv_standable_normal")->get<float>();
    const auto gravity_scale = memory::read<float>(local_pawn + SCHEMA("C_BaseEntity", "m_flGravityScale"_hash));

    auto velocity = utils::pick_velocity(prestate);
    const float eff_gravity = (gravity_scale > 0.0f ? gravity_scale : 1.0f) * (sv_gravity > 0.0f ? sv_gravity : 800.0f);
    velocity.z -= eff_gravity * cstypes::tick_interval * 0.5f;

    // Trace the player's movement for this tick, PLUS 2 extra units downward
    // to account for Source 2's ground-snap (CategorizePosition snaps the
    // player to ground within ~2 units).  Without this buffer, borderline
    // landings are missed and the player falls back to the on-ground path
    // next tick, eating one full tick of ground friction.
    const math::vector3 trace_start = trace_origin;
    math::vector3 trace_end{};
    trace_end.x = trace_origin.x + velocity.x * cstypes::tick_interval;
    trace_end.y = trace_origin.y + velocity.y * cstypes::tick_interval;
    trace_end.z = trace_origin.z + velocity.z * cstypes::tick_interval - 2.0f;

    const auto result = systems::g_tracing.trace_player_bbox(trace_start, trace_end, { mins, maxs }, filter, movement_services);

    if (result.normal.z < sv_standable_normal)
    {
        return std::nullopt;
    }

    if (result.fraction >= 1.0f)
    {
        return std::nullopt;
    }

    if (result.fraction <= 0.0f)
    {
        return 1.0f / 64.0f;
    }

    // Map trace fraction back to the tick fraction.
    // The trace covers |vel.z * dt| + 2 units of Z, but only |vel.z * dt|
    // corresponds to actual tick movement.
    const float travel_z = std::fabsf(velocity.z * cstypes::tick_interval);
    const float total_trace_z = travel_z + 2.0f;
    const float hit_z_dist = result.fraction * total_trace_z;

    // If hit point lies in the extra buffer beyond this tick's travel,
    // landing occurs next tick. Do not trigger a premature jump in the air!
    if (hit_z_dist > travel_z)
    {
        return std::nullopt;
    }

    const float when = (travel_z > 0.01f) ? (hit_z_dist / travel_z) : result.fraction;
    return std::clamp(when, 1.0f / 64.0f, 63.0f / 64.0f);
}

void apply_landing_jump(proto::base_usercmd_pb* base, float when)
{
    const auto subtick_moves = base->mutable_subtick_moves();
    if (!subtick_moves)
    {
        return;
    }

    // Release jump slightly before landing so that pressing jump creates a clean rising edge
    const float release = std::max(0.0f, when - (1.0f / 64.0f));
    // Press jump EXACTLY at the moment of landing.
    // Pressing after landing (e.g. when + 1/128) subjects the player to ground friction,
    // dropping speed from 370 down to 320.
    const float press = when;

    if (const auto jump_up = systems::g_input.acquire_subtick_step(subtick_moves))
    {
        jump_up->set_button(cstypes::command_buttons::in_jump);
        jump_up->set_pressed(false);
        jump_up->set_when(release);
        jump_up->set_analog_forward_delta(0.0f);
        jump_up->set_analog_left_delta(0.0f);
    }

    if (const auto jump_down = systems::g_input.acquire_subtick_step(subtick_moves))
    {
        jump_down->set_button(cstypes::command_buttons::in_jump);
        jump_down->set_pressed(true);
        jump_down->set_when(press);
        jump_down->set_analog_forward_delta(0.0f);
        jump_down->set_analog_left_delta(0.0f);
    }
}

} // namespace

void bhop::on_create_move(systems::input::usercmd* cmd)
{
    if (!settings::g_movement.bhop.value)
    {
        return;
    }

    if (CONVAR("sv_autobunnyhopping")->get<bool>())
    {
        return;
    }

    if (!(cmd->buttons.value & cstypes::command_buttons::in_jump))
    {
        this->m_landed_last_tick = false;
        return;
    }

    if (features::movement::g_jumpbug.active_this_tick())
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

    const auto base = cmd->csgo_user_cmd.mutable_base();
    if (!base)
    {
        return;
    }

    const auto& prestate = systems::g_prediction.pre();

    if (prestate.flags & cstypes::entity_flags::on_ground)
    {
        if (const auto jump_up = systems::g_input.acquire_subtick_step(base->mutable_subtick_moves()))
        {
            jump_up->set_button(cstypes::command_buttons::in_jump);
            jump_up->set_pressed(false);
            jump_up->set_when(0.0f);
            jump_up->set_analog_forward_delta(0.0f);
            jump_up->set_analog_left_delta(0.0f);
        }

        if (const auto jump_down = systems::g_input.acquire_subtick_step(base->mutable_subtick_moves()))
        {
            jump_down->set_button(cstypes::command_buttons::in_jump);
            jump_down->set_pressed(true);
            jump_down->set_when(1.0f / 128.0f);
            jump_down->set_analog_forward_delta(0.0f);
            jump_down->set_analog_left_delta(0.0f);
        }

        cmd->buttons.value |= cstypes::command_buttons::in_jump;
        cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
        this->m_landed_last_tick = false;
        return;
    }

    const auto holding_duck = (cmd->buttons.value & cstypes::command_buttons::in_duck) != 0;
    const auto movement_services = memory::read<std::uintptr_t>(local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
    if (!movement_services)
    {
        return;
    }

    const auto landing = predict_landing_fraction(local.pawn, movement_services, prestate, holding_duck);

    if (landing.has_value())
    {
        this->m_landed_last_tick = true;
        apply_landing_jump(base, *landing);
        cmd->buttons.value |= cstypes::command_buttons::in_jump;
        cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
        return;
    }

    this->m_landed_last_tick = false;
    const bool was_jump_pressed = (cmd->buttons.value & cstypes::command_buttons::in_jump) != 0;
    cmd->buttons.value &= ~cstypes::command_buttons::in_jump;
    if (was_jump_pressed)
    {
        cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
    }
    cmd->buttons.value_scroll &= ~cstypes::command_buttons::in_jump;
}

} // namespace features::movement