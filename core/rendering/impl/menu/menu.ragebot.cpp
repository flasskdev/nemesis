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

		// LEFT COLUMN: AIMBOT MAIN
		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( xui::begin_child( "##ragebot_aimbot_main", col_w, body_h, true ) )
		{
			xui::section_header("AIMBOT MAIN");

			xui::toggle( "Enable Ragebot", rb.enabled, "Activate automated targeting engine" );
			xui::layout::spacing( 3.0f );
            xui::combo("Weapon Group", detail::weapon_group_idx, detail::weapon_group_items, 6);
            auto& wg = rb.groups[std::clamp(detail::weapon_group_idx, 0, 5)];
            xui::toggle("Silent Aim", wg.silent, "Aim without moving the screen view");
			xui::layout::spacing( 3.0f );
			xui::slider_float( "Field of View", wg.max_fov, 1.0f, 180.0f, "%.0f°" );

			xui::layout::spacing( 8.0f );
			xui::toggle( "No Spread", wg.no_spread );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Extrapolation", lg.extrapolation );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Force Bodyaim", wg.body_aim );
			xui::layout::spacing( 3.0f );
			xui::slider_float( "Pointscale", wg.pointscale, 0.0f, 100.0f, "%.0f%%" );
			xui::layout::spacing( 4.0f );
			xui::multicombo( "Hitboxes", wg.hitboxes, detail::hitbox_names, 6 );

			xui::end_child( );
		}

		// RIGHT COLUMN: ACCURACY ENGINE
		xui::layout::set_cursor( right_x - wx, body_y - wy );

		if ( xui::begin_child( "##ragebot_accuracy_engine", col_w, body_h, true ) )
		{
            auto& wg = rb.groups[std::clamp(detail::weapon_group_idx, 0, 5)];
			xui::section_header("ACCURACY ENGINE");

            xui::toggle("Auto Stop", wg.autostop, "Decelerate before taking a shot");
            xui::toggle("Auto Scope", autos.scope, "Scope automatically with sniper rifles");
            xui::layout::spacing(8.0f);
            xui::layout::separator();
            xui::layout::spacing(8.0f);

			xui::slider_int( "Hit Chance", wg.hitchance, 0, 100, "%d%%" );
			xui::layout::spacing( 3.0f );
			xui::slider_int( "Minimum Damage", wg.min_damage, 5, 125, "%d hp" );

			xui::layout::spacing( 8.0f );
			xui::toggle( "Hitchance Override", wg.hitchance_override );
			if ( xui::begin_popup( "##hitchance_popup", 220.0f ) )
			{
				xui::slider_int( "value##hc", wg.hitchance_override_value, 0, 100, "%d%%" );
				xui::end_popup( );
			}

			xui::layout::spacing( 3.0f );
			xui::toggle( "Mindamage Override", wg.min_damage_override );
			if ( xui::begin_popup( "##mindamage_popup", 220.0f ) )
			{
				xui::slider_int( "value##md", wg.min_damage_override_value, 0, 130, "%d" );
				xui::end_popup( );
			}

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
			xui::layout::spacing( 3.0f );
			xui::toggle( "Quick Peek Assist", qp.enabled );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Auto Revolver", autos.revolver );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Zeusbot", zb.enabled );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Knifebot", kb.enabled );

			xui::end_child( );
		}
	}

} // namespace rendering
