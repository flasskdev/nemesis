#include "../core/features/movement/movement_math.hpp"
#include <cassert>
#include <iostream>
#include <random>

namespace m = features::movement::math2d;
static bool near(float a, float b, float tolerance = 0.0005f) { return std::fabs(a - b) <= tolerance; }
int main()
{
    const auto left = m::world({0.0f, 1.0f}, 0.0f);
    assert(near(left.x, 0.0f) && near(left.y, 1.0f));
    const auto turned = m::rebase({1.0f, 0.0f}, 0.0f, 90.0f);
    assert(near(turned.x, 0.0f) && near(turned.y, -1.0f));
    std::mt19937 rng(20260906);
    std::uniform_real_distribution<float> angles(-720.0f, 720.0f);
    std::uniform_real_distribution<float> magnitude(0.0f, 1.0f);
    for (int i = 0; i < 20000; ++i)
    {
        auto command = m::direction(angles(rng));
        const float scale = magnitude(rng);
        command.x *= scale; command.y *= scale;
        const float a = angles(rng), b = angles(rng);
        const auto before = m::world(command, a);
        const auto after = m::world(m::rebase(command, a, b), b);
        assert(near(before.x, after.x) && near(before.y, after.y));
        const m::vector velocity{350.0f * before.x, 350.0f * before.y};
        for (const bool quantized : {false, true})
        {
            const auto move = m::choose_movement(velocity, a, b, 1.0f / 64.0f, 250.0f, 12.0f, 30.0f, 1.0f, quantized, false);
            const auto repeated = m::choose_movement(velocity, a, b, 1.0f / 64.0f, 250.0f, 12.0f, 30.0f, 1.0f, quantized, false);
            assert(std::isfinite(move.x) && std::isfinite(move.y));
            assert(std::fabs(move.x) <= 1.00001f && std::fabs(move.y) <= 1.00001f);
            assert(move.x == repeated.x && move.y == repeated.y);
            if (quantized) assert((move.x == -1 || move.x == 0 || move.x == 1) && (move.y == -1 || move.y == 0 || move.y == 1));
        }
    }
    for (const float speed : {15.0f, 100.0f, 350.0f, 1000.0f})
        for (const float accel : {1.0f, 12.0f, 100.0f})
        {
            const float theta = m::ideal_angle(speed, 1.0f / 64.0f, 250.0f, accel, 30.0f, 1.0f);
            const auto optimal = m::air_step({speed, 0.0f}, m::direction(theta), 1.0f / 64.0f, 250.0f, accel, 30.0f, 1.0f);
            for (int degree = -1800; degree <= 1800; ++degree)
            {
                const auto sample = m::air_step({speed, 0.0f}, m::direction(float(degree) * 0.1f), 1.0f / 64.0f, 250.0f, accel, 30.0f, 1.0f);
                assert(m::length(optimal) + 0.001f >= m::length(sample));
            }
        }
    std::cout << "movement_math_tests: PASS\n";
}
