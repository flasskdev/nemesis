#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"
#include "menu.weapons.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names[ ]{ "head", "chest", "stomach", "arms", "legs", "feet" };
		constexpr const char* pitch_items[ ]{ "none", "down", "up" };
		inline menu_weapons::weapon_selection weapon_sel_rage{ 2, -1 };

	} // namespace detail

	void menu::draw_ragebot( float group_w ) const
	{
		auto& s = settings::g_combat;
		auto& rb = s.m_ragebot;
		auto& aa = s.m_antiaim;
		auto& qp = s.m_quickpeek;
		auto& dp = s.m_duckpeek;
		auto& zb = s.m_zeusbot;
		auto& kb = s.m_knifebot;
		auto& autos = s.m_autos;
		auto& lg = s.m_lagcomp;

		const auto is_custom_wep = (detail::weapon_sel_rage.weapon_flat_idx >= 0 &&
			detail::weapon_sel_rage.weapon_flat_idx < static_cast<int>(cstypes::weapons::k_total_weapons));

		auto& wg = is_custom_wep
			? rb.weapons[detail::weapon_sel_rage.weapon_flat_idx].cfg
			: rb.groups[std::clamp(detail::weapon_sel_rage.group_idx, 0, 5)];

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + menu::k_sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto body_h = this->m_body_h;
		const auto content_w = this->m_w - tokens::gap * 2.0f - menu::k_sidebar_w - tokens::gap;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		constexpr float k_header_h = 22.0f;
		auto draw_col_title = [&]( float x, const char* title ) {
			auto& dl = xui::draw::current( );
			xdraw::push_font( rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );
			dl.text( x + 2.0f, body_y + 2.0f, title, tokens::col_text );
			xdraw::pop_font( );
		};

		// LEFT COLUMN: RAGEBOT MAIN
		draw_col_title( content_x, "RAGEBOT MAIN" );
		xui::layout::set_cursor( content_x - wx, body_y + k_header_h - wy );

		if ( xui::begin_child( "##ragebot_main", col_w, body_h - k_header_h, true ) )
		{
			xui::toggle( "Enable Ragebot", rb.enabled );
			if ( xui::begin_popup( "##rb_popup", 220.0f ) )
			{
				xui::checkbox( "Force Bodyaim", wg.body_aim );
				xui::checkbox( "Extrapolation", lg.extrapolation );
				if ( lg.extrapolation.value )
				{
					xui::layout::spacing( 3.0f );
					xui::slider_int( "Max Ticks", lg.max_extrapolate_ticks, 1, 16, "%d ticks" );
				}
				xui::end_popup( );
			}

			xui::layout::spacing( 3.0f );
			menu_weapons::draw_selector( "##rb_weapon_select", detail::weapon_sel_rage, false );
			if ( is_custom_wep )
			{
				auto& ow = rb.weapons[ detail::weapon_sel_rage.weapon_flat_idx ];
				xui::layout::spacing( 2.0f );
				xui::toggle( "Custom Weapon Settings", ow.override_group );
				if ( !ow.override_group.value )
				{
					xui::layout::spacing( 2.0f );
					if ( xui::button( "Copy Group Settings##rb" ) )
					{
						ow.cfg.copy_values_from( rb.groups[ std::clamp( detail::weapon_sel_rage.group_idx, 0, 5 ) ] );
						ow.override_group.value = true;
					}
				}
			}
			xui::layout::spacing( 3.0f );
			xui::toggle("Silent Aim", wg.silent);
			xui::layout::spacing( 3.0f );
			xui::slider_float( "Field of View", wg.max_fov, 1.0f, 180.0f, "%.0f°" );

			xui::layout::spacing( 8.0f );
			xui::slider_int( "Hit Chance", wg.hitchance, 0, 100, "%d%%" );
			xui::layout::spacing( 3.0f );
			xui::slider_int( "Minimum Damage", wg.min_damage, 5, 125, "%d hp" );

			xui::layout::spacing( 8.0f );
			xui::toggle( "Auto Stop", wg.autostop );
			xui::toggle( "Auto Scope", autos.scope );
			xui::toggle( "No Spread", wg.no_spread );

			xui::layout::spacing( 3.0f );
			xui::slider_float( "Pointscale", wg.pointscale, 0.0f, 100.0f, "%.0f%%" );
			xui::layout::spacing( 4.0f );
			xui::multicombo( "Hitboxes", wg.hitboxes, detail::hitbox_names, 6 );

			xui::end_child( );
		}

		// RIGHT COLUMN: ANTI AIM
		draw_col_title( right_x, "ANTI AIM" );
		xui::layout::set_cursor( right_x - wx, body_y + k_header_h - wy );

		if ( xui::begin_child( "##ragebot_antiaim", col_w, body_h - k_header_h, true ) )
		{

			xui::toggle( "Anti Aim", aa.enabled );
			if ( xui::begin_popup( "##aa_popup", 220.0f ) )
			{
				xui::checkbox( "auto yaw adjust", aa.auto_yaw_adjust );
				xui::checkbox( "hide onshot", aa.hide_shots );
				xui::checkbox( "avoid backstab", aa.avoid_backstab );
				xui::checkbox( "direction indicator", aa.direction_indicator );
				xui::end_popup( );
			}
			xui::layout::spacing( 3.0f );
			xui::combo( "Pitch", aa.pitch.value, detail::pitch_items, 3 );

			xui::layout::spacing( 8.0f );
			xui::layout::separator( );
			xui::layout::spacing( 8.0f );

			// Плавная анимация скрытия/появления авторевольвера (только для пистолетов при выключенном No Spread)
			const auto group_idx = std::clamp( detail::weapon_sel_rage.group_idx, 0, 5 );
			const bool should_show_revolver = ( group_idx == 0 && !wg.no_spread.value );

			auto revolver_anim = xui::anim::lerp(
				xui::fnv1a( "auto_revolver_menu_anim" ),
				should_show_revolver ? 1.0f : 0.0f,
				10.0f,
				should_show_revolver ? 1.0f : 0.0f
			);

			if ( revolver_anim <= 0.001f )
			{
				revolver_anim = 0.0f;
				xui::anim::set( xui::fnv1a( "auto_revolver_menu_anim" ), 0.0f );
			}
			else if ( revolver_anim >= 0.999f )
			{
				revolver_anim = 1.0f;
				xui::anim::set( xui::fnv1a( "auto_revolver_menu_anim" ), 1.0f );
			}

			if ( revolver_anim > 0.0f )
			{
				auto* win = xui::layout::current_window( );
				auto& st = xui::ctx( ).style;

				// Базовая позиция первого элемента под разделителем со стандартным отступом
				if ( win && win->line_h > 0.0f )
				{
					win->cursor_y += win->line_h + st.item_spacing_y;
					win->line_h = 0.0f;
				}

				const auto [start_cx, base_y] = xui::layout::get_cursor( );
				const float scroll_y = win ? win->scroll_y : 0.0f;
				const float win_bx = win ? win->bounds.x : 0.0f;
				const float win_by = win ? win->bounds.y : 0.0f;
				const float screen_x = win_bx + start_cx;
				const float screen_y = win_by + base_y - scroll_y;

				// Полный шаг переключателя: высота строки (30px) + spacing (3px) + стандартный межэлементный отступ (item_spacing_y)
				const float full_stride = 30.0f + 3.0f + st.item_spacing_y;
				const float anim_ease = xui::ease::out_cubic( revolver_anim );
				const float current_offset = full_stride * anim_ease;
				const float clip_h = 30.0f * anim_ease;
				const float clip_w = win ? win->bounds.w : col_w;

				auto& dl = xui::draw::current( );
				dl.push_clip( screen_x - 10.0f, screen_y - 2.0f, clip_w + 20.0f, clip_h + 4.0f );

				// Плавная модуляция прозрачности элементов переключателя
				const auto a = std::clamp( revolver_anim, 0.0f, 1.0f );
				xui::push_style_color( xui::style_col::text, st.text.alpha( static_cast< std::uint8_t >( st.text.a * a ) ) );
				xui::push_style_color( xui::style_col::checkbox_bg, st.checkbox_bg.alpha( static_cast< std::uint8_t >( st.checkbox_bg.a * a ) ) );
				xui::push_style_color( xui::style_col::checkbox_border, st.checkbox_border.alpha( static_cast< std::uint8_t >( st.checkbox_border.a * a ) ) );
				xui::push_style_color( xui::style_col::accent, st.accent.alpha( static_cast< std::uint8_t >( st.accent.a * a ) ) );
				xui::push_style_color( xui::style_col::text_dim, st.text_dim.alpha( static_cast< std::uint8_t >( st.text_dim.a * a ) ) );

				// Блокировка клика во время анимации скрытия
				auto& input = xui::ctx( ).input;
				const auto saved_clicked = input.mouse_clicked;
				if ( !should_show_revolver || revolver_anim < 0.95f )
				{
					input.mouse_clicked = false;
				}

				xui::toggle( "Auto Revolver", autos.revolver );

				input.mouse_clicked = saved_clicked;
				xui::pop_style_color( 5 );
				dl.pop_clip( );

				// Курсор для следующего виджета сдвигается ровно на анимированный шаг
				xui::layout::set_cursor( start_cx, base_y + current_offset );
			}
			xui::toggle( "Quick Peek Assist", qp.enabled );
			if ( xui::begin_popup( "##qp_popup", 220.0f ) )
			{
				xui::color_picker( "start color", qp.color );
				xui::color_picker( "retrack color", qp.retrack_color );
				xui::end_popup( );
			}
			xui::layout::spacing( 3.0f );
			xui::toggle( "Duck Peek Assist", dp.enabled );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Zeusbot", zb.enabled );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Knifebot", kb.enabled );

			xui::end_child( );
		}
	}

} // namespace rendering
