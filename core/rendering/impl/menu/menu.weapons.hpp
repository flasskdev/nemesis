#pragma once

#include <pch/pch.hpp>
#include <utilities/cstypes.hpp>
#include <core/settings.hpp>
#include "../../rendering.hpp"
#include "../../theme.hpp"

namespace rendering::menu_weapons {

    struct weapon_selection
    {
        int group_idx{ 2 };         // 0..5 (Pistols, SMGs, Rifles, Shotguns, Snipers, LMGs)
        int weapon_flat_idx{ -1 };   // -1 = whole group, 0..33 = specific weapon
    };

    inline weapon_selection weapon_sel_rage{ 2, -1 };
    inline weapon_selection weapon_sel_legit{ 2, -1 };

    class weapon_selector_overlay : public xui::overlay
    {
    public:
        weapon_selector_overlay(std::uintptr_t id, const xui::rect& anchor, weapon_selection& sel, bool is_legit)
            : overlay{ id, anchor }, m_sel{ sel }, m_is_legit{ is_legit }
        {
            m_hovered_group = std::clamp(sel.group_idx, 0, 5);
            m_open_anim = 0.02f;
            m_flyout_anim = 1.0f;
        }

        [[nodiscard]] std::pair<xui::rect, xui::rect> compute_rects() const
        {
            constexpr float k_main_w = 145.0f;
            constexpr float k_row_h = 30.0f;
            constexpr float k_pad = 5.0f;
            const float k_main_h = k_pad * 2.0f + 6.0f * k_row_h;

            const float main_x = this->m_anchor.x;
            const float main_y = this->m_anchor.y + this->m_anchor.h + 4.0f;
            const xui::rect main_r{ main_x, main_y, k_main_w, k_main_h };

            const auto safe_hovered = std::clamp(m_hovered_group, 0, 5);
            const auto& grp_info = cstypes::weapons::k_groups[safe_hovered];
            const float k_sub_w = 175.0f;
            const float k_sub_h = k_pad * 2.0f + (grp_info.count + 1) * k_row_h + 4.0f;

            float sub_x = main_x + k_main_w + 4.0f;
            const auto [vw, vh] = xdraw::viewport_size();
            const float max_right = static_cast<float>(vw);
            if (sub_x + k_sub_w > max_right - 10.0f)
            {
                sub_x = main_x - k_sub_w - 4.0f;
            }
            const float sub_y = main_y;
            const xui::rect sub_r{ sub_x, sub_y, k_sub_w, k_sub_h };

            return { main_r, sub_r };
        }

        [[nodiscard]] bool hit_test(float x, float y) const override
        {
            const auto [main_r, sub_r] = compute_rects();
            return this->m_anchor.contains(x, y) || main_r.contains(x, y) || sub_r.contains(x, y);
        }

        bool process_input(const xui::input_state& input) override
        {
            if (this->m_closing)
            {
                return false;
            }

            const auto [main_r, sub_r] = compute_rects();

            // Hover over main group rows
            if (main_r.contains(input.mouse_x, input.mouse_y))
            {
                constexpr float k_pad = 5.0f;
                constexpr float k_row_h = 30.0f;
                const auto rel_y = input.mouse_y - (main_r.y + k_pad);
                if (rel_y >= 0.0f)
                {
                    const int grp = static_cast<int>(rel_y / k_row_h);
                    if (grp >= 0 && grp < 6 && grp != m_hovered_group)
                    {
                        m_hovered_group = grp;
                        m_flyout_anim = 0.0f;
                    }
                }
            }

            if (input.mouse_clicked)
            {
                // If clicked on anchor button:
                // When opening, input.mouse_clicked is true on m_anchor in frame 0.
                // When closing via anchor click, draw_selector button handling already calls overlays::close(id).
                // So NEVER close here on anchor click, just consume the click so nothing behind it reacts!
                if (this->m_anchor.contains(input.mouse_x, input.mouse_y))
                {
                    return true;
                }

                // Click inside main panel (groups)
                if (main_r.contains(input.mouse_x, input.mouse_y))
                {
                    constexpr float k_pad = 5.0f;
                    constexpr float k_row_h = 30.0f;
                    const auto rel_y = input.mouse_y - (main_r.y + k_pad);
                    if (rel_y >= 0.0f)
                    {
                        const int grp = static_cast<int>(rel_y / k_row_h);
                        if (grp >= 0 && grp < 6)
                        {
                            m_sel.group_idx = grp;
                            m_sel.weapon_flat_idx = -1;
                            this->m_closing = true;
                            return true;
                        }
                    }
                    return true;
                }

                // Click inside sub panel (weapons)
                if (sub_r.contains(input.mouse_x, input.mouse_y))
                {
                    constexpr float k_pad = 5.0f;
                    constexpr float k_row_h = 30.0f;
                    const auto safe_hovered = std::clamp(m_hovered_group, 0, 5);
                    const auto& grp_info = cstypes::weapons::k_groups[safe_hovered];
                    const auto rel_y = input.mouse_y - (sub_r.y + k_pad);
                    if (rel_y >= 0.0f)
                    {
                        const int row = static_cast<int>(rel_y / k_row_h);
                        if (row == 0)
                        {
                            // [ All in Group ]
                            m_sel.group_idx = safe_hovered;
                            m_sel.weapon_flat_idx = -1;
                            this->m_closing = true;
                            return true;
                        }
                        else if (row >= 1 && row <= static_cast<int>(grp_info.count))
                        {
                            const int flat_idx = static_cast<int>(grp_info.start_idx) + (row - 1);
                            m_sel.group_idx = safe_hovered;
                            m_sel.weapon_flat_idx = flat_idx;
                            this->m_closing = true;
                            return true;
                        }
                    }
                    return true;
                }

                // Click outside both panels and anchor -> close overlay and swallow the click
                this->m_closing = true;
                return true;
            }

            // Consume all mouse interactions while within overlay bounds
            return this->hit_test(input.mouse_x, input.mouse_y);
        }

        void render(const xui::style& style, const xui::input_state& input) override
        {
            const auto dt = xdraw::delta_time();
            const auto speed = this->m_closing ? 18.0f : 16.0f;
            const auto target = this->m_closing ? 0.0f : 1.0f;
            this->m_open_anim += (target - this->m_open_anim) * std::min(speed * dt, 1.0f);

            if (this->m_open_anim < 0.01f && this->m_closing)
            {
                this->m_closed = true;
                return;
            }

            this->m_flyout_anim += (1.0f - this->m_flyout_anim) * std::min(20.0f * dt, 1.0f);

            const auto ease_t = xui::ease::smoothstep(this->m_open_anim);
            const auto alpha_mult = ease_t;
            const auto main_alpha = static_cast<std::uint8_t>(255.0f * alpha_mult);

            const auto [full_main_r, full_sub_r] = compute_rects();
            const xui::rect main_rect{ full_main_r.x, full_main_r.y, full_main_r.w, full_main_r.h * ease_t };
            const xui::rect sub_rect{ full_sub_r.x, full_sub_r.y, full_sub_r.w, full_sub_r.h * ease_t };

            auto& top_dl = xdraw::get(xdraw::layer::top);
            constexpr float k_pad = 5.0f;
            constexpr float k_row_h = 30.0f;

            // Draw Left Panel (Groups)
            const auto bg_card = tokens::col_card.alpha(static_cast<std::uint8_t>(235.0f * alpha_mult));
            const auto border_card = tokens::col_border.alpha(static_cast<std::uint8_t>(180.0f * alpha_mult));
            top_dl.rect_filled_blurred(main_rect.x, main_rect.y, main_rect.w, main_rect.h, xdraw::corner_radius{ 6.0f },
                xdraw::color{ 45, 48, 55, static_cast<std::uint8_t>(180.0f * alpha_mult) });
            top_dl.rect_filled(main_rect.x, main_rect.y, main_rect.w, main_rect.h, bg_card, xdraw::corner_radius{ 6.0f });
            top_dl.rect(main_rect.x, main_rect.y, main_rect.w, main_rect.h, border_card, xdraw::corner_radius{ 6.0f }, 1.0f);

            // Accent line on top of main panel
            const auto m_half_w = (main_rect.w - 12.0f) * 0.5f;
            const auto m_edge = tokens::col_accent.alpha(0);
            const auto m_center = tokens::col_accent.alpha(static_cast<std::uint8_t>(150.0f * alpha_mult));
            top_dl.rect_filled_gradient(main_rect.x + 6.0f, main_rect.y, m_half_w, 2.0f, m_edge, m_center, m_center, m_edge);
            top_dl.rect_filled_gradient(main_rect.x + 6.0f + m_half_w, main_rect.y, m_half_w, 2.0f, m_center, m_edge, m_edge, m_center);

            xdraw::push_font(rendering::g_fonts.inter_medium[rendering::fonts::size::petite]);

            // Draw group rows with clipping
            top_dl.push_clip(main_rect.x, main_rect.y, main_rect.w, main_rect.h);
            for (int i = 0; i < 6; ++i)
            {
                const float row_y = main_rect.y + k_pad + i * k_row_h;
                const xui::rect row_rect{ main_rect.x + k_pad, row_y, main_rect.w - k_pad * 2.0f, k_row_h };
                const bool is_hovered = main_rect.contains(input.mouse_x, input.mouse_y) && row_rect.contains(input.mouse_x, input.mouse_y);
                const bool is_current_group = (m_hovered_group == i);
                const bool is_selected_group = (m_sel.group_idx == i && m_sel.weapon_flat_idx == -1);

                const auto row_bg = is_current_group
                    ? tokens::col_accent.alpha(static_cast<std::uint8_t>(32.0f * alpha_mult))
                    : (is_hovered ? tokens::col_elevated.alpha(static_cast<std::uint8_t>(160.0f * alpha_mult))
                                  : xdraw::color{ 0, 0, 0, 0 });
                if (is_current_group || is_hovered)
                {
                    top_dl.rect_filled(row_rect.x, row_rect.y, row_rect.w, row_rect.h, row_bg, xdraw::corner_radius{ 4.0f });
                }

                // Group name
                const auto text_col = (is_current_group || is_selected_group)
                    ? tokens::col_accent.alpha(main_alpha)
                    : tokens::col_text.alpha(main_alpha);
                top_dl.text(row_rect.x + 8.0f, row_rect.y + (k_row_h - 14.0f) * 0.5f, cstypes::weapons::k_groups[i].name, text_col);

                // Check if group has any custom weapons
                bool group_has_custom = m_is_legit
                    ? settings::g_combat.m_legitbot.is_group_overridden(i)
                    : settings::g_combat.m_ragebot.is_group_overridden(i);
                if (group_has_custom)
                {
                    top_dl.circle_filled(row_rect.x + row_rect.w - 20.0f, row_rect.y + k_row_h * 0.5f, 2.5f,
                        tokens::col_accent.alpha(main_alpha));
                }

                // Chevron ▸
                const auto chx = row_rect.x + row_rect.w - 10.0f;
                const auto chy = row_rect.y + k_row_h * 0.5f;
                const auto chcol = is_current_group ? tokens::col_accent.alpha(main_alpha) : tokens::col_text_dim.alpha(main_alpha);
                top_dl.line(chx - 3.0f, chy - 3.5f, chx + 0.5f, chy, chcol, 1.2f);
                top_dl.line(chx + 0.5f, chy, chx - 3.0f, chy + 3.5f, chcol, 1.2f);
            }
            top_dl.pop_clip();

            // Draw Right Flyout (Weapons)
            const auto fly_ease = xui::ease::smoothstep(this->m_flyout_anim) * alpha_mult;
            const auto fly_alpha = static_cast<std::uint8_t>(255.0f * fly_ease);

            top_dl.rect_filled_blurred(sub_rect.x, sub_rect.y, sub_rect.w, sub_rect.h, xdraw::corner_radius{ 6.0f },
                xdraw::color{ 45, 48, 55, static_cast<std::uint8_t>(180.0f * fly_ease) });
            top_dl.rect_filled(sub_rect.x, sub_rect.y, sub_rect.w, sub_rect.h, bg_card, xdraw::corner_radius{ 6.0f });
            top_dl.rect(sub_rect.x, sub_rect.y, sub_rect.w, sub_rect.h, border_card, xdraw::corner_radius{ 6.0f }, 1.0f);

            // Accent top stripe on sub-panel
            const auto s_half_w = (sub_rect.w - 12.0f) * 0.5f;
            top_dl.rect_filled_gradient(sub_rect.x + 6.0f, sub_rect.y, s_half_w, 2.0f, m_edge, m_center, m_center, m_edge);
            top_dl.rect_filled_gradient(sub_rect.x + 6.0f + s_half_w, sub_rect.y, s_half_w, 2.0f, m_center, m_edge, m_edge, m_center);

            const auto safe_hovered = std::clamp(m_hovered_group, 0, 5);
            const auto& grp_info = cstypes::weapons::k_groups[safe_hovered];

            top_dl.push_clip(sub_rect.x, sub_rect.y, sub_rect.w, sub_rect.h);

            // Item 0: [ All (Group) ]
            float cur_sub_y = sub_rect.y + k_pad;
            {
                const xui::rect all_rect{ sub_rect.x + k_pad, cur_sub_y, sub_rect.w - k_pad * 2.0f, k_row_h };
                const bool is_hovered = sub_rect.contains(input.mouse_x, input.mouse_y) && all_rect.contains(input.mouse_x, input.mouse_y);
                const bool is_selected = (m_sel.group_idx == safe_hovered && m_sel.weapon_flat_idx == -1);

                if (is_hovered || is_selected)
                {
                    top_dl.rect_filled(all_rect.x, all_rect.y, all_rect.w, all_rect.h,
                        is_selected ? tokens::col_accent.alpha(static_cast<std::uint8_t>(36.0f * fly_ease))
                                    : tokens::col_elevated.alpha(static_cast<std::uint8_t>(160.0f * fly_ease)),
                        xdraw::corner_radius{ 4.0f });
                }

                const auto label_col = is_selected ? tokens::col_accent.alpha(fly_alpha) : tokens::col_text.alpha(fly_alpha);
                top_dl.text(all_rect.x + 8.0f, all_rect.y + (k_row_h - 14.0f) * 0.5f, "[ All in Group ]", label_col);
            }
            cur_sub_y += k_row_h + 2.0f;

            // Separator in flyout
            top_dl.line(sub_rect.x + 8.0f, cur_sub_y, sub_rect.x + sub_rect.w - 8.0f, cur_sub_y,
                tokens::col_border.alpha(static_cast<std::uint8_t>(120.0f * fly_ease)));
            cur_sub_y += 3.0f;

            // Individual weapons
            for (std::size_t i = 0; i < grp_info.count; ++i)
            {
                const auto flat_idx = static_cast<int>(grp_info.start_idx + i);
                const auto& wep = cstypes::weapons::k_weapons[flat_idx];
                const xui::rect wep_rect{ sub_rect.x + k_pad, cur_sub_y, sub_rect.w - k_pad * 2.0f, k_row_h };

                const bool is_hovered = sub_rect.contains(input.mouse_x, input.mouse_y) && wep_rect.contains(input.mouse_x, input.mouse_y);
                const bool is_selected = (m_sel.weapon_flat_idx == flat_idx);

                if (is_hovered || is_selected)
                {
                    top_dl.rect_filled(wep_rect.x, wep_rect.y, wep_rect.w, wep_rect.h,
                        is_selected ? tokens::col_accent.alpha(static_cast<std::uint8_t>(36.0f * fly_ease))
                                    : tokens::col_elevated.alpha(static_cast<std::uint8_t>(160.0f * fly_ease)),
                        xdraw::corner_radius{ 4.0f });
                }

                const auto label_col = is_selected ? tokens::col_accent.alpha(fly_alpha) : tokens::col_text.alpha(fly_alpha);
                top_dl.text(wep_rect.x + 8.0f, wep_rect.y + (k_row_h - 14.0f) * 0.5f, wep.name, label_col);

                // Check if this individual weapon is overridden
                const bool wep_is_overridden = m_is_legit
                    ? settings::g_combat.m_legitbot.weapons[flat_idx].override_group.value
                    : settings::g_combat.m_ragebot.weapons[flat_idx].override_group.value;

                if (wep_is_overridden)
                {
                    const float dot_x = wep_rect.x + wep_rect.w - 12.0f;
                    const float dot_y = wep_rect.y + k_row_h * 0.5f;
                    top_dl.circle_filled(dot_x, dot_y, 3.0f, tokens::col_accent.alpha(fly_alpha));
                }

                cur_sub_y += k_row_h;
            }
            top_dl.pop_clip();

            xdraw::pop_font();
        }

    private:
        weapon_selection& m_sel;
        bool m_is_legit{ false };
        int m_hovered_group{ 2 };
        float m_open_anim{ 0.0f };
        float m_flyout_anim{ 1.0f };
    };

    inline void draw_selector(const char* label_id, weapon_selection& sel, bool is_legit)
    {
        auto& ctx = xui::ctx();
        auto& input = ctx.input;
        const auto& s = ctx.style;
        const auto id = xui::make_id(label_id);
        const auto is_open = xui::overlays::is_open(id);

        const auto [display, full] = xui::parse_label(label_id);
        const auto width = xui::layout::item_width();
        const auto combo_h = s.combo_h > 0.0f ? s.combo_h : 28.0f;
        const auto [lw, lh] = xdraw::measure_text("Weapon Target");
        const auto sp = s.item_spacing_y * 0.25f;
        const auto total_h = lh + sp + combo_h;

        const auto abs = xui::layout::item(width, total_h, lh);
        const auto button_y = abs.y + lh + sp;
        const xui::rect button_rect{ abs.x, button_y, width, combo_h };

        if (is_open)
        {
            xui::overlays::touch(id);
            if (auto ov = xui::overlays::find(id))
            {
                ov->update_anchor(button_rect);
            }
        }

        // Label above combo
        auto& dl = xui::draw::current();
        xdraw::push_font(rendering::g_fonts.inter_medium[rendering::fonts::size::petite]);
        dl.text(abs.x, abs.y, "Weapon Target", tokens::col_text);
        xdraw::pop_font();

        const auto can_interact = !ctx.overlay_blocking() || is_open;
        const auto btn_hovered = can_interact && (input.in_rect(button_rect) || input.in_rect(abs));
        const auto btn_anim = xui::anim::lerp(id + 1, (btn_hovered || is_open) ? 1.0f : 0.0f, 14.0f);

        if (btn_hovered && input.mouse_clicked)
        {
            if (is_open)
            {
                xui::overlays::close(id);
            }
            else
            {
                xui::overlays::add(std::make_unique<weapon_selector_overlay>(id, button_rect, sel, is_legit));
            }
        }

        // Background & border
        const auto bg_col = xui::lerp(tokens::col_elevated.alpha(180), tokens::col_elevated.alpha(240), btn_anim);
        const auto border_col = xui::lerp(tokens::col_border.alpha(160), tokens::col_accent.alpha(200), btn_anim);
        dl.rect_filled(button_rect.x, button_rect.y, button_rect.w, button_rect.h, bg_col, xdraw::corner_radius{ 5.0f });
        dl.rect(button_rect.x, button_rect.y, button_rect.w, button_rect.h, border_col, xdraw::corner_radius{ 5.0f }, 1.0f);

        // Format button text
        std::string btn_text;
        const auto safe_grp = std::clamp(sel.group_idx, 0, 5);
        if (sel.weapon_flat_idx < 0 || sel.weapon_flat_idx >= static_cast<int>(cstypes::weapons::k_total_weapons))
        {
            btn_text = std::string(cstypes::weapons::k_groups[safe_grp].name) + " (Group)";
        }
        else
        {
            const auto& wep = cstypes::weapons::k_weapons[sel.weapon_flat_idx];
            btn_text = std::string(cstypes::weapons::k_groups[safe_grp].name) + " ▸ " + wep.name;
        }

        bool is_custom = false;
        if (sel.weapon_flat_idx >= 0 && sel.weapon_flat_idx < static_cast<int>(cstypes::weapons::k_total_weapons))
        {
            is_custom = is_legit
                ? settings::g_combat.m_legitbot.weapons[sel.weapon_flat_idx].override_group.value
                : settings::g_combat.m_ragebot.weapons[sel.weapon_flat_idx].override_group.value;
        }

        xdraw::push_font(rendering::g_fonts.inter_medium[rendering::fonts::size::petite]);
        const auto [tw, th] = xdraw::measure_text(btn_text);
        dl.text(button_rect.x + 10.0f, button_rect.y + (button_rect.h - th) * 0.5f, btn_text, tokens::col_text);

        if (is_custom)
        {
            const float pill_x = button_rect.x + 10.0f + tw + 6.0f;
            const float pill_y = button_rect.y + (button_rect.h - 14.0f) * 0.5f;
            dl.rect_filled(pill_x, pill_y, 44.0f, 14.0f, tokens::col_accent.alpha(40), xdraw::corner_radius{ 3.0f });
            dl.rect(pill_x, pill_y, 44.0f, 14.0f, tokens::col_accent.alpha(160), xdraw::corner_radius{ 3.0f }, 1.0f);
            dl.text(pill_x + 5.0f, pill_y + 1.0f, "custom", tokens::col_accent);
        }

        // Down/Up chevron
        const auto ch_x = button_rect.x + button_rect.w - 14.0f;
        const auto ch_y = button_rect.y + button_rect.h * 0.5f;
        const auto ch_col = is_open ? tokens::col_accent : tokens::col_text_dim;
        if (is_open)
        {
            dl.line(ch_x - 3.5f, ch_y + 1.5f, ch_x, ch_y - 2.5f, ch_col, 1.2f);
            dl.line(ch_x, ch_y - 2.5f, ch_x + 3.5f, ch_y + 1.5f, ch_col, 1.2f);
        }
        else
        {
            dl.line(ch_x - 3.5f, ch_y - 2.0f, ch_x, ch_y + 2.0f, ch_col, 1.2f);
            dl.line(ch_x, ch_y + 2.0f, ch_x + 3.5f, ch_y - 2.0f, ch_col, 1.2f);
        }
        xdraw::pop_font();
    }

} // namespace rendering::menu_weapons
