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
        { "Default", {32,228,143}, {10,15,13}, {238,246,241}, {128,151,140}, {15,23,19}, {20,31,25}, {32,47,39} },
        { "Dark",    {198,204,216}, {12,13,16}, {240,242,248}, {153,160,175}, {20,22,27}, {28,31,38}, {43,47,57} },
        { "White",   {0,119,83}, {244,247,245}, {25,40,33}, {86,105,94}, {255,255,255}, {232,239,235}, {204,219,211} },
        { "Blue",    {83,161,255}, {11,15,22}, {237,243,255}, {137,154,183}, {16,23,34}, {22,33,48}, {34,48,68} },
        { "Purple",  {177,135,255}, {16,12,23}, {244,239,255}, {162,143,188}, {24,18,34}, {34,25,48}, {49,37,66} },
        { "Pink",    {255,125,187}, {23,12,18}, {255,239,247}, {189,143,165}, {34,18,26}, {48,25,36}, {65,35,49} },
        { "Red",     {255,104,115}, {23,12,14}, {255,240,240}, {188,143,149}, {34,18,21}, {48,25,29}, {66,36,42} },
        { "Orange",  {255,170,82}, {22,16,10}, {255,246,234}, {186,159,128}, {33,24,16}, {46,34,22}, {63,47,32} },
        { "Yellow",  {236,206,92}, {21,19,11}, {253,249,233}, {180,171,131}, {31,28,17}, {43,39,24}, {59,54,34} },
        { "Cyan",    {66,219,236}, {10,19,22}, {234,250,252}, {130,169,177}, {15,28,33}, {21,39,46}, {30,53,62} },
        { "Mint",    {128,229,187}, {11,19,16}, {236,250,244}, {134,171,153}, {17,28,23}, {24,39,32}, {34,54,44} }
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
