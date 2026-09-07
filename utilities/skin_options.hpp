#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace skin_options {
    inline float clamp_wear(float value, float low = 0.0f, float high = 1.0f)
    {
        low = std::isfinite(low) ? std::clamp(low, 0.0f, 1.0f) : 0.0f;
        high = std::isfinite(high) ? std::clamp(high, low, 1.0f) : 1.0f;
        return std::clamp(std::isfinite(value) ? value : 0.01f, low, high);
    }

    inline constexpr std::array<float, 6> wear_edges{0.0f, 0.07f, 0.15f, 0.38f, 0.45f, 1.0f};
    inline constexpr std::array<const char*, 5> wear_names{
        "Factory New", "Minimal Wear", "Field-Tested", "Well-Worn", "Battle-Scarred"
    };
    inline constexpr std::array<const char*, 5> wear_short{"FN", "MW", "FT", "WW", "BS"};

    inline int wear_tier(float value)
    {
        value = clamp_wear(value);
        for (int i = 0; i < 4; ++i)
            if (value < wear_edges[i + 1]) return i;
        return 4;
    }

    inline std::pair<float, float> wear_limits(float low, float high)
    {
        // Invalid/unavailable schema bounds must not produce an inverted slider.
        if (!std::isfinite(low) || !std::isfinite(high) || low < 0.0f || high > 1.0f || high < low)
            return {0.0f, 1.0f};
        return {low, high};
    }

    inline std::optional<float> wear_preset(int tier, float low, float high)
    {
        if (tier < 0 || tier >= 5) return std::nullopt;
        const auto bounds = wear_limits(low, high);
        const auto begin = std::max(bounds.first, wear_edges[tier]);
        const auto tier_end = tier == 4 ? 1.0f : std::nextafter(wear_edges[tier + 1], 0.0f);
        const auto end = std::min(bounds.second, tier_end);
        if (begin > end) return std::nullopt;
        return begin + (end - begin) * 0.5f;
    }
}
