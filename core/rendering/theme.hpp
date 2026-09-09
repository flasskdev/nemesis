#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <external/xdraw/xui/xui.hpp>

namespace rendering::theme {
    struct palette {
        const char* name;
        xdraw::color accent, background, text, muted, card, elevated, border;
    };

    inline constexpr std::array<palette, 11> presets{{
        { "Default", { 36, 220, 145}, { 11, 15, 13}, {240, 246, 242}, {138, 152, 144}, { 17, 22, 19}, { 23, 30, 26}, { 33, 44, 38} },
        { "Dark",    {168, 178, 194}, { 12, 13, 16}, {240, 242, 248}, {142, 148, 160}, { 18, 20, 25}, { 25, 28, 35}, { 38, 42, 52} },
        { "White",   {245, 248, 255}, { 13, 15, 18}, {248, 250, 255}, {146, 154, 166}, { 19, 22, 27}, { 26, 30, 37}, { 40, 45, 54} },
        { "Blue",    { 75, 155, 255}, { 11, 14, 20}, {240, 245, 255}, {138, 150, 168}, { 17, 21, 29}, { 23, 29, 40}, { 34, 43, 58} },
        { "Purple",  {172, 128, 255}, { 14, 12, 19}, {246, 242, 255}, {148, 142, 166}, { 20, 18, 28}, { 28, 25, 39}, { 42, 37, 58} },
        { "Pink",    {255, 115, 185}, { 16, 12, 15}, {252, 242, 248}, {154, 142, 150}, { 23, 18, 22}, { 32, 25, 31}, { 48, 37, 46} },
        { "Red",     {255,  85,  95}, { 16, 12, 13}, {252, 242, 243}, {154, 142, 144}, { 23, 18, 19}, { 32, 25, 27}, { 48, 37, 40} },
        { "Orange",  {255, 152,  62}, { 16, 13, 11}, {252, 245, 240}, {154, 146, 138}, { 23, 19, 16}, { 32, 27, 22}, { 48, 40, 33} },
        { "Yellow",  {245, 210,  68}, { 15, 14, 11}, {250, 248, 240}, {152, 150, 138}, { 22, 21, 16}, { 31, 29, 23}, { 46, 44, 34} },
        { "Cyan",    { 48, 216, 240}, { 11, 15, 18}, {240, 248, 252}, {138, 152, 160}, { 17, 22, 27}, { 23, 31, 38}, { 33, 44, 54} },
        { "Mint",    { 95, 235, 180}, { 11, 16, 14}, {240, 248, 245}, {138, 154, 148}, { 17, 23, 21}, { 23, 32, 29}, { 34, 46, 42} }
    }};

    inline int normalize(int value) {
        return value >= 0 && value < static_cast<int>(presets.size()) ? value : 0;
    }

    inline const palette& get(int value) { return presets[normalize(value)]; }

    inline void apply(int value) {
        const auto& p = get(value);
        tokens::col_accent = p.accent;
        tokens::col_dark = p.background;
        tokens::col_text = p.text;
        tokens::col_text_dim = p.muted;
        tokens::col_card = p.card;
        tokens::col_elevated = p.elevated;
        tokens::col_border = p.border;
    }

    // Remove a complete UTF-8 code point, never half a multibyte character.
    inline void pop_codepoint(std::string& text) {
        if (text.empty()) return;
        auto pos = text.size() - 1;
        while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) --pos;
        text.resize(pos);
    }

    inline std::string fit_text(std::string_view text, float width) {
        if (width <= 0.0f) return {};
        std::string out(text);
        if (xdraw::measure_text(out).first <= width) return out;
        constexpr std::string_view suffix = "...";
        if (xdraw::measure_text(suffix).first > width) return {};
        do { pop_codepoint(out); }
        while (!out.empty() && xdraw::measure_text(out + std::string(suffix)).first > width);
        out += suffix;
        return out;
    }
}
