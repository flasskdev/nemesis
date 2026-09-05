// movement_utils.hpp
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
    auto sv_maxspeed = c_max ? c_max->get<float>() : 320.0f;
    if (!std::isfinite(sv_maxspeed) || sv_maxspeed < 1.0f)
        sv_maxspeed = 320.0f;
    if (!std::isfinite(maxspeed))
        maxspeed = 250.0f;
    return std::clamp(maxspeed, 1.0f, sv_maxspeed);
}

// Optimal wish direction for simulate_air_accel below, not a server-physics guarantee.
[[nodiscard]] inline float compute_ideal_strafe_angle(
    float speed,
    float dt,
    float wishspeed,
    float air_accel,
    float air_max_wishspeed,
    float friction = 1.0f)
{
    if (!std::isfinite(speed) || speed < 0.0001f)
        return 0.0f;
    if (!std::isfinite(dt) || !std::isfinite(wishspeed) ||
        !std::isfinite(air_accel) || !std::isfinite(air_max_wishspeed) ||
        !std::isfinite(friction))
        return 90.0f;

    const float capped = std::min(std::max(wishspeed, 0.0f),
                                  std::max(air_max_wishspeed, 0.0f));
    const float acceleration = std::max(wishspeed, 0.0f) *
        std::max(air_accel, 0.0f) * std::max(friction, 0.0f) * std::max(dt, 0.0f);
    const float projection = std::max(0.0f, capped - acceleration);
    const float cosine = std::clamp(projection / speed, 0.0f, 1.0f);
    return std::acos(cosine) * (180.0f / std::numbers::pi_v<float>);
}

[[nodiscard]] inline float get_ideal_strafe_angle(
    float speed,
    float dt,
    float wishspeed,
    float air_accel,
    float air_max_wishspeed)
{
    return compute_ideal_strafe_angle(speed, dt, wishspeed, air_accel, air_max_wishspeed);
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

// ---------------------------------------------------------------------------
// Shared subtick strafe helpers
// ---------------------------------------------------------------------------

constexpr int k_max_subticks{ 16 };
constexpr float k_min_strafe_speed{ 5.0f };



// Given current velocity and a target yaw the player wishes to travel,
// returns the optimal yaw to set as the wish-direction this sub-frame.
// `side_switch` controls left/right curving for the S-curve.
[[nodiscard]] inline float compute_strafe_yaw(
    float vel_x, float vel_y,
    float target_yaw,
    float dt, bool side_switch,
    float wishspeed, float air_accel, float air_max_wishspeed, float friction = 1.0f)
{
    const auto speed = std::sqrtf(vel_x * vel_x + vel_y * vel_y);
    const auto theta = compute_ideal_strafe_angle(speed, dt, wishspeed, air_accel, air_max_wishspeed, friction);

    if (speed < 15.0f)
        return target_yaw;

    const auto vel_angle = std::atan2f(vel_y, vel_x) * (180.0f / std::numbers::pi_v<float>);
    auto vel_delta = target_yaw - vel_angle;
    math::helpers::normalize_angle(vel_delta);

    // If velocity direction has deviated substantially from target direction
    // (e.g. sharp turn or manual redirection), steer aggressively toward target.
    // 45° allows full graceful S-curve swings (30°) without interrupting them.
    constexpr float k_steer_threshold = 45.0f;
    if (std::fabsf(vel_delta) > k_steer_threshold)
    {
        if (vel_delta > 0.0f)
        {
            auto yaw = vel_angle + theta;
            math::helpers::normalize_angle(yaw);
            return yaw;
        }
        auto yaw = vel_angle - theta;
        math::helpers::normalize_angle(yaw);
        return yaw;
    }

    if (side_switch)
    {
        auto yaw = vel_angle + theta;
        math::helpers::normalize_angle(yaw);
        return yaw;
    }
    auto yaw = vel_angle - theta;
    math::helpers::normalize_angle(yaw);
    return yaw;
}

// Simulate one sub-frame of Source-engine AirAccelerate on XY velocity.
inline void simulate_air_accel(
    float& vel_x, float& vel_y,
    float wishdir_yaw,
    float frame_time, float friction,
    float wishspeed, float air_accel, float air_max_wishspeed)
{
    const auto yaw_rad = wishdir_yaw * (std::numbers::pi_v<float> / 180.0f);
    const auto wish_dir_x = std::cosf(yaw_rad);
    const auto wish_dir_y = std::sinf(yaw_rad);

    const auto capped = std::fminf(wishspeed, air_max_wishspeed);
    const auto dot = vel_x * wish_dir_x + vel_y * wish_dir_y;
    const auto add_speed = capped - dot;

    if (add_speed <= 0.0f)
        return;

    const auto accel_speed = wishspeed * air_accel * friction * frame_time;
    const auto step = std::fminf(accel_speed, add_speed);

    vel_x += wish_dir_x * step;
    vel_y += wish_dir_y * step;
}

// Returns the maximum `when` fraction already used by existing subtick steps.
[[nodiscard]] inline float get_max_subtick_when(proto::base_usercmd_pb* base)
{
    auto max_when{ 0.0f };
    for (auto i = 0; i < base->subtick_moves_size(); ++i)
    {
        if (const auto step = base->mutable_subtick_moves(i))
        {
            max_when = std::fmaxf(max_when, step->when());
        }
    }
    return max_when;
}

} // namespace features::movement::utils