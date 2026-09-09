#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names[ ]{ "head", "chest", "stomach", "arms", "legs", "feet" };
		constexpr const char* pitch_items[ ]{ "none", "down", "up" };
        constexpr const char* weapon_group_items[]{ "Pistol", "SMG", "Rifle", "Shotgun", "Sniper", "LMG" };
        inline int weapon_group_idx{ 2 };

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
			auto& wg = rb.groups[std::clamp(detail::weapon_group_idx, 0, 5)];

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
			xui::combo("Weapon Group", detail::weapon_group_idx, detail::weapon_group_items, 6);
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

			xui::toggle( "Auto Revolver", autos.revolver );
			xui::layout::spacing( 3.0f );
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
