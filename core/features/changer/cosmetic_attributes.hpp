#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <core/settings.hpp>
#include <core/systems/systems.hpp>
#include <utilities/memory/memory.hpp>
#include <protection/game_addresses.hpp>

namespace features::changer::cosmetic_attributes {
    inline constexpr std::array<std::uint16_t, 5> indices{ 6, 7, 8, 80, 81 };
    inline constexpr std::array<const char*, 5> names{
        "set item texture prefab", "set item texture seed", "set item texture wear",
        "kill eater", "kill eater score type"
    };

    struct attribute_state {
        std::uint32_t bits{};
        bool present{};
    };
    using snapshot = std::array<attribute_state, indices.size()>;

    [[nodiscard]] inline settings::changer::applied_skin normalize(settings::changer::applied_skin skin) {
        skin.wear = std::isfinite(skin.wear) ? std::clamp(skin.wear, 0.0f, 1.0f) : 0.01f;
        skin.seed = std::clamp(skin.seed, 0, 1000);
        return skin;
    }

    [[nodiscard]] inline bool available() {
        return PATTERN(patterns::econ_item_view_set_attribute)
            && PATTERN(patterns::econ_item_view_remove_attribute);
    }

    [[nodiscard]] inline bool capture(std::uintptr_t item_view, snapshot& out) {
        out = {};
        // Same build-dependent local-attribute layout already used in gloves.cpp.
        const auto count = memory::safe_read<int>(item_view + 0x210);
        const auto data = memory::safe_read<std::uintptr_t>(item_view + 0x218);
        if (!count || !data || *count < 0 || *count > 16384 || (*count && !*data))
            return false;
        for (int i = 0; i < *count; ++i) {
            const auto attribute = *data + static_cast<std::uintptr_t>(i) * 0x48;
            const auto index = memory::safe_read<std::uint16_t>(attribute + 0x30);
            if (!index) return false;
            for (std::size_t slot = 0; slot < indices.size(); ++slot) {
                if (*index != indices[slot] || out[slot].present) continue;
                const auto bits = memory::safe_read<std::uint32_t>(attribute + 0x34);
                if (!bits) return false;
                out[slot] = { *bits, true };
                break;
            }
        }
        return true;
    }

    [[nodiscard]] inline bool restore(std::uintptr_t item_view, const snapshot& saved) {
        if (!available()) return false;
        const auto set = PATTERN(patterns::econ_item_view_set_attribute);
        const auto remove = PATTERN(patterns::econ_item_view_remove_attribute);
        for (std::size_t slot = 0; slot < indices.size(); ++slot) {
            if (saved[slot].present)
                memory::call<void>(set, item_view, names[slot], std::bit_cast<float>(saved[slot].bits));
            else
                memory::call<void>(remove, item_view, static_cast<int>(indices[slot]));
        }
        return true;
    }

    [[nodiscard]] inline bool apply(std::uintptr_t item_view, const settings::changer::applied_skin& skin) {
        if (!available()) return false;
        const auto set = PATTERN(patterns::econ_item_view_set_attribute);
        const auto remove = PATTERN(patterns::econ_item_view_remove_attribute);
        memory::call<void>(set, item_view, names[0], static_cast<float>(skin.paint_kit_id));
        memory::call<void>(set, item_view, names[1], static_cast<float>(skin.seed));
        memory::call<void>(set, item_view, names[2], skin.wear);
        if (skin.stattrak) {
            const auto count_val = std::bit_cast<float>(static_cast<std::int32_t>(skin.stattrak_count));
            const auto score_type = std::bit_cast<float>(std::int32_t{0});
            memory::call<void>(set, item_view, names[3], count_val);
            memory::call<void>(set, item_view, names[4], score_type);
        } else {
            memory::call<void>(remove, item_view, static_cast<int>(indices[3]));
            memory::call<void>(remove, item_view, static_cast<int>(indices[4]));
        }
        return true;
    }
}
