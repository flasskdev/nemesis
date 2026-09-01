#pragma once

#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <protection/game_addresses.hpp>

namespace features::movement::utils {

[[nodiscard]] inline bool is_movement_blocked(std::uintptr_t pawn)
{
    const auto move_type = memory::read<std::uint8_t>(
        pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));

    return move_type == cstypes::move_type::ladder ||
           move_type == cstypes::move_type::noclip;
}

[[nodiscard]] inline bool is_on_ground(
    const systems::prediction::state& prestate,
    std::uintptr_t pawn)
{
    auto flags = prestate.flags;

    if (!flags && pawn)
    {
        flags = memory::read<std::uint32_t>(
            pawn + SCHEMA("C_BaseEntity", "m_fFlags"_hash));
    }

    return prestate.on_ground ||
           (flags & static_cast<std::uint32_t>(cstypes::entity_flags::on_ground)) != 0;
}

[[nodiscard]] inline math::vector3 pick_velocity(
    const systems::prediction::state& prestate)
{
    auto velocity = prestate.velocity;

    const bool abs_invalid = velocity.length_sqr() < 1.0f;
    const bool network_valid = prestate.networked_velocity.length_sqr() > 1.0f;

    if (abs_invalid && network_valid)
    {
        velocity = prestate.networked_velocity;
    }

    if (std::fabsf(velocity.z) < 1.0f && std::fabsf(prestate.networked_velocity.z) > 1.0f)
    {
        velocity.z = prestate.networked_velocity.z;
    }

    return velocity;
}

[[nodiscard]] inline float get_player_maxspeed(std::uintptr_t pawn)
{
    const auto movement_services = memory::read<std::uintptr_t>(
        pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));

    if (!movement_services)
        return 250.0f;

    auto maxspeed = memory::read<float>(
        movement_services + SCHEMA("CPlayer_MovementServices", "m_flMaxspeed"_hash));

    if (maxspeed <= 1.0f)
        maxspeed = 250.0f;

    const auto* c_max = CONVAR("sv_maxspeed");
    const auto sv_maxspeed = c_max ? c_max->get<float>() : 320.0f;

    return std::clamp(maxspeed, 1.0f, sv_maxspeed);
}

[[nodiscard]] inline float get_ideal_strafe_angle(
    float speed,
    float dt,
    float wishspeed,
    float air_accel,
    float air_max_wishspeed)
{
    if (speed < 1.0f)
        return 45.0f;

    if (air_accel <= 0.0f || air_max_wishspeed <= 0.0f)
        return 0.0f;

    const auto accel_speed = wishspeed * air_accel * dt;

    float cos_theta{};

    if (accel_speed >= air_max_wishspeed)
    {
        cos_theta = air_max_wishspeed / (2.0f * speed);
    }
    else
    {
        cos_theta = (air_max_wishspeed - accel_speed) / speed;
    }

    cos_theta = std::clamp(cos_theta, -1.0f, 1.0f);

    const auto angle =
        std::acosf(cos_theta) * (180.0f / std::numbers::pi_v<float>);

    return std::clamp(angle, 0.5f, 89.0f);
}

inline void set_world_movement(
    proto::base_usercmd_pb* base,
    float world_yaw,
    float reference_yaw)
{
    const auto delta = math::helpers::normalize_yaw(world_yaw - reference_yaw);
    const auto delta_rad = delta * (std::numbers::pi_v<float> / 180.0f);

    base->set_forwardmove(std::clamp(std::cosf(delta_rad), -1.0f, 1.0f));
    base->set_leftmove(std::clamp(std::sinf(delta_rad), -1.0f, 1.0f));
}

[[nodiscard]] inline float get_target_yaw(
    float reference_yaw,
    std::uintptr_t buttons)
{
    const bool has_moveleft =
        (buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveleft)) != 0;

    const bool has_moveright =
        (buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveright)) != 0;

    const bool has_forward =
        (buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward)) != 0;

    const bool has_back =
        (buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_back)) != 0;

    float offset = 0.0f;

    if (has_moveleft && !has_moveright)
    {
        offset = has_forward ? 45.0f : (has_back ? 135.0f : 90.0f);
    }
    else if (has_moveright && !has_moveleft)
    {
        offset = has_forward ? -45.0f : (has_back ? -135.0f : -90.0f);
    }
    else if (has_back && !has_forward)
    {
        offset = 180.0f;
    }

    float target = reference_yaw + offset;
    math::helpers::normalize_angle(target);
    return target;
}

} // namespace features::movement::utils
