#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"
#include "menu.weapons.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names_legit[ ]{ "head", "chest", "stomach", "arms", "legs" };
		inline menu_weapons::weapon_selection weapon_sel_legit{ 2, -1 };

	} // namespace detail

	void menu::draw_legitbot( float group_w ) const
	{
		auto& s = settings::g_combat;
		auto& lb = s.m_legitbot;

		const auto is_custom_wep = (detail::weapon_sel_legit.weapon_flat_idx >= 0 &&
			detail::weapon_sel_legit.weapon_flat_idx < static_cast<int>(cstypes::weapons::k_total_weapons));

		auto& wg = is_custom_wep
			? lb.weapons[detail::weapon_sel_legit.weapon_flat_idx].cfg
			: lb.groups[std::clamp(detail::weapon_sel_legit.group_idx, 0, 5)];

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

		// LEFT COLUMN: LEGITBOT MAIN
		draw_col_title( content_x, "LEGITBOT MAIN" );
		xui::layout::set_cursor( content_x - wx, body_y + k_header_h - wy );

		if ( xui::begin_child( "##legitbot_main", col_w, body_h - k_header_h, true ) )
		{
			xui::toggle( "Enable Legitbot", lb.enabled );
			xui::layout::spacing( 3.0f );
			menu_weapons::draw_selector( "##lb_weapon_select", detail::weapon_sel_legit, true );
			if ( is_custom_wep )
			{
				auto& ow = lb.weapons[ detail::weapon_sel_legit.weapon_flat_idx ];
				xui::layout::spacing( 2.0f );
				xui::toggle( "Custom Weapon Settings", ow.override_group );
				if ( !ow.override_group.value )
				{
					xui::layout::spacing( 2.0f );
					if ( xui::button( "Copy Group Settings##lb" ) )
					{
						ow.cfg.copy_values_from( lb.groups[ std::clamp( detail::weapon_sel_legit.group_idx, 0, 5 ) ] );
						ow.override_group.value = true;
					}
				}
			}
			xui::layout::spacing( 3.0f );
			xui::toggle( "Aimbot", wg.aimbot );
			xui::layout::spacing( 3.0f );
			xui::slider_float( "Target FOV", wg.fov, 0.5f, 30.0f, "%.1f°" );
			xui::layout::spacing( 3.0f );
			xui::slider_int( "Smoothness", wg.smooth, 0, 100, "%d" );
			xui::layout::spacing( 4.0f );
			xui::multicombo( "Hitboxes", wg.hitboxes, detail::hitbox_names_legit, 5 );

			xui::layout::spacing( 6.0f );
			xui::toggle( "Draw FOV", wg.visualize_fov );
			if ( xui::begin_popup( "##fov_color_popup", 220.0f ) )
			{
				xui::color_picker( "color##fov", wg.fov_color );
				xui::end_popup( );
			}

			xui::layout::spacing( 6.0f );
			xui::toggle( "Recoil Control (RCS)", wg.rcs );
			if ( xui::begin_popup( "##rcs_popup", 220.0f ) )
			{
				xui::slider_int( "min##rcs", wg.rcs_min, 50, 150, "%d%%" );
				xui::slider_int( "max##rcs", wg.rcs_max, 50, 150, "%d%%" );
				xui::end_popup( );
			}

			xui::end_child( );
		}

		// RIGHT COLUMN: TRIGGER & WEAPON ACCURACY
		draw_col_title( right_x, "TRIGGER & ACCURACY" );
		xui::layout::set_cursor( right_x - wx, body_y + k_header_h - wy );

		if ( xui::begin_child( "##legitbot_trigger_accuracy", col_w, body_h - k_header_h, true ) )
		{

			xui::toggle( "Check Smoke", wg.smoke_check );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Check Scope", wg.scope_check );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Flash Check", wg.flash_check );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Only On Ground", wg.ground_check );
			xui::layout::spacing( 8.0f );

			xui::toggle( "Triggerbot", wg.triggerbot );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Trigger Head Only", wg.trigger_head_only );
			xui::layout::spacing( 3.0f );
			xui::slider_int( "Reaction Delay", wg.trigger_delay, 0, 250, "%d ms" );
			xui::layout::spacing( 3.0f );
			xui::slider_int( "Hitchance", wg.trigger_hitchance, 0, 100, "%d%%" );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Seed Prediction", wg.give_me_your_seed );

			xui::layout::spacing( 8.0f );
			xui::toggle( "Standalone RCS", wg.standalone_rcs );
			if ( xui::begin_popup( "##srcs_popup", 220.0f ) )
			{
				xui::slider_int( "strength##srcs", wg.standalone_rcs_strength, 0, 100, "%d%%" );
				xui::slider_int( "min##srcs", wg.standalone_rcs_min, 50, 150, "%d%%" );
				xui::slider_int( "max##srcs", wg.standalone_rcs_max, 50, 150, "%d%%" );
				xui::end_popup( );
			}

			xui::layout::spacing( 6.0f );
			xui::toggle( "Autowall", wg.autowall );
			if ( xui::begin_popup( "##aw_popup", 220.0f ) )
			{
				xui::slider_int( "min damage##aw", wg.min_damage, 1, 125, "%d" );
				xui::end_popup( );
			}

			xui::end_child( );
		}
	}

} // namespace rendering