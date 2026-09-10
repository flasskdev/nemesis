#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace skin_inspect {

    // Numeric CEconItemPreviewDataBlock fields only: no inventory ownership,
    // external service, executable script, or user-supplied console text.
    struct item
    {
        std::uint32_t def_index{};
        std::uint32_t paint_kit{};
        std::uint32_t rarity{};
        std::uint32_t quality{4};
        float wear{};
        std::uint32_t seed{};
        std::optional<std::uint32_t> stattrak{};
    };

    inline constexpr std::uint32_t crc32(std::span<const std::uint8_t> bytes)
    {
        std::uint32_t crc = 0xffffffffu;
        for (const auto byte : bytes)
        {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        }
        return ~crc;
    }

    inline constexpr std::uint32_t checksum(std::span<const std::uint8_t> bytes)
    {
        const auto crc = crc32(bytes);
        return (crc & 0xffffu) ^ (static_cast<std::uint32_t>(bytes.size()) * crc);
    }

    namespace detail {
        inline void varint(std::vector<std::uint8_t>& bytes, std::uint32_t value)
        {
            while (value >= 0x80u)
            {
                bytes.push_back(static_cast<std::uint8_t>((value & 0x7fu) | 0x80u));
                value >>= 7;
            }
            bytes.push_back(static_cast<std::uint8_t>(value));
        }

        inline void field(std::vector<std::uint8_t>& bytes, std::uint32_t number, std::uint32_t value)
        {
            varint(bytes, number << 3); // wire type 0, including paintwear's float bits
            varint(bytes, value);
        }
    }

    [[nodiscard]] inline std::optional<std::vector<std::uint8_t>> encode(const item& value)
    {
        static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559);
        if (!value.def_index || value.def_index > 65535u ||
            value.paint_kit > 0x7fffffffu || value.rarity > 7u || value.quality > 12u ||
            !std::isfinite(value.wear) || value.wear < 0.0f || value.wear > 1.0f ||
            value.seed > 1000u || (value.stattrak && *value.stattrak > 0x7fffffffu))
            return std::nullopt;

        // Masked inspect envelope: a zero XOR key, protobuf, then a big-endian
        // checksum of the key and protobuf. Zero is a valid masking key.
        std::vector<std::uint8_t> bytes{0};
        bytes.reserve(64);
        detail::field(bytes, 1, 0); // accountid
        detail::field(bytes, 2, 0); // itemid; a preview is not an owned inventory item
        detail::field(bytes, 3, value.def_index);
        detail::field(bytes, 4, value.paint_kit);
        detail::field(bytes, 5, value.rarity);
        detail::field(bytes, 6, value.quality);
        detail::field(bytes, 7, std::bit_cast<std::uint32_t>(value.wear == 0.0f ? 0.0f : value.wear));
        detail::field(bytes, 8, value.seed);
        if (value.stattrak)
        {
            detail::field(bytes, 9, 0); // kill-eater score type: kills
            detail::field(bytes, 10, *value.stattrak);
        }
        const auto sum = checksum(bytes);
        for (int shift = 24; shift >= 0; shift -= 8)
            bytes.push_back(static_cast<std::uint8_t>(sum >> shift));
        return bytes;
    }

    [[nodiscard]] inline std::optional<std::string> command(const item& value)
    {
        const auto bytes = encode(value);
        if (!bytes) return std::nullopt;
        constexpr char hex[] = "0123456789ABCDEF";
        std::string result = "csgo_econ_action_preview ";
        result.reserve(result.size() + bytes->size() * 2);
        for (const auto byte : *bytes)
        {
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 0xf]);
        }
        return result;
    }
}
