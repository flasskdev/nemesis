#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

namespace features::movement::math2d {
struct vector { float x{}, y{}; };
constexpr float radians = std::numbers::pi_v<float> / 180.0f;
inline float yaw(float value) { return std::isfinite(value) ? std::remainder(value, 360.0f) : 0.0f; }
inline float length(vector v) { return std::hypot(v.x, v.y); }
inline vector direction(float angle) { const auto a = yaw(angle) * radians; return {std::cos(a), std::sin(a)}; }
inline vector world(vector move, float view_yaw)
{
    const auto basis = direction(view_yaw);
    return {basis.x * move.x - basis.y * move.y, basis.y * move.x + basis.x * move.y};
}
inline vector local(vector wish, float view_yaw)
{
    const auto basis = direction(view_yaw);
    return {basis.x * wish.x + basis.y * wish.y, -basis.y * wish.x + basis.x * wish.y};
}
inline vector limit(vector move)
{
    if (!std::isfinite(move.x) || !std::isfinite(move.y)) return {};
    const float scale = 1.0f / std::max({1.0f, std::fabs(move.x), std::fabs(move.y)});
    return {move.x * scale, move.y * scale};
}
inline vector rebase(vector move, float from_yaw, float to_yaw)
{
    return limit(local(world(move, from_yaw), to_yaw));
}
inline float ideal_angle(float speed, float dt, float wishspeed, float accel, float cap, float friction)
{
    if (speed < 0.001f) return 0.0f;
    const float wish_cap = std::min(std::max(0.0f, wishspeed), std::max(0.0f, cap));
    const float step = std::max(0.0f, wishspeed * accel * friction * dt);
    const float projection = std::max(0.0f, wish_cap - step);
    return std::acos(std::clamp(projection / speed, 0.0f, 1.0f)) / radians;
}
inline vector air_step(vector velocity, vector wish, float dt, float wishspeed, float accel, float cap, float friction)
{
    const float n = length(wish);
    if (n < 0.0001f) return velocity;
    wish.x /= n; wish.y /= n;
    const float add = std::min(wishspeed, cap) - velocity.x * wish.x - velocity.y * wish.y;
    if (add <= 0.0f) return velocity;
    const float step = std::min(add, std::max(0.0f, wishspeed * accel * friction * dt));
    return {velocity.x + step * wish.x, velocity.y + step * wish.y};
}
inline vector choose_movement(vector v, float target_yaw, float command_yaw, float dt,
    float wishspeed, float accel, float cap, float friction, bool quantized, bool braking)
{
    const float speed = length(v);
    const auto target = direction(target_yaw);
    vector best{};
    float best_score = -std::numeric_limits<float>::infinity();
    const auto evaluate = [&](vector command)
    {
        const auto wish = world(command, command_yaw);
        const auto next = air_step(v, wish, dt, wishspeed, accel, cap, friction);
        const float next_speed = length(next);
        const float heading = next_speed > 0.001f ? (next.x * target.x + next.y * target.y) / next_speed : 1.0f;
        const float score = braking ? -next_speed
            : next_speed - std::max(30.0f, speed) * (1.0f - std::clamp(heading, -1.0f, 1.0f));
        if (score > best_score + 0.00001f) { best_score = score; best = command; }
    };
    if (quantized)
    {
        // Evaluate the eight realizable input directions, never synthesize camera turns.
        for (int f = -1; f <= 1; ++f)
            for (int l = -1; l <= 1; ++l)
                if (f || l) evaluate({float(f), float(l)});
    }
    else if (braking)
    {
        best = speed > 1.0f ? local({-v.x / speed, -v.y / speed}, command_yaw) : vector{};
    }
    else if (speed < 15.0f)
    {
        best = local(target, command_yaw);
    }
    else
    {
        const float velocity_yaw = std::atan2(v.y, v.x) / radians;
        const float ideal = ideal_angle(speed, dt, wishspeed, accel, cap, friction);
        evaluate(local(direction(velocity_yaw + ideal), command_yaw));
        evaluate(local(direction(velocity_yaw - ideal), command_yaw));
    }
    return limit(best);
}
}
