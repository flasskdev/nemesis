#include <pch/pch.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* sound_type_names[]{ "shop click", "home click", "bell", "killcard", "bullet casing", "coin pickup", "item drop", "popcan", "key press", "custom" };
		constexpr const char* marker_type_names[]{ "classic", "damage", "both" };
		constexpr const char* bullet_impact_type_names[]{ "overlay", "sparks", "both" };
		constexpr const char* primary_weapon_names[]{ "none", "galil / famas", "ak-47 / m4a4 / m4a1-s", "ssg 08", "sg 553 / aug", "awp", "g3sg1 / scar-20", "mac-10 / mp9", "mp7", "mp5-sd", "ump-45", "p90", "pp-bizon", "nova", "xm1014", "sawed-off / mag-7", "m249", "negev" };
		constexpr const char* secondary_weapon_names[]{ "none", "glock-18 / usp-s / p2000", "dual berettas", "p250", "cz75-auto / five-seven / tec-9", "desert eagle / r8 revolver" };
		constexpr const char* grenade_names[]{ "molotov / inc", "decoy", "flashbang", "he grenade", "smoke" };

	} // namespace detail

	void menu::draw_misc( float group_w ) const
	{
		auto& m = settings::g_misc;
		auto& mov = settings::g_movement;

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + tokens::sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto content_w = this->m_w - tokens::gap * 2.0f - tokens::sidebar_w - tokens::gap;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		const auto subtab = this->m_subtab;

		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( subtab == 0 )
		{
			if ( xui::begin_child( "##misc_main_left", col_w ) )
			{
				xui::checkbox( "clantag", m.m_name_changer.clantag );
				xui::checkbox( "override name", m.m_name_changer.override_name );
				if ( m.m_name_changer.override_name.value )
				{
					xui::text_input( "##name_input", m.m_name_changer.name );
				}

				xui::checkbox( "scoreboard weapons", m.m_scoreboard_weapons.enabled );
				if ( xui::begin_popup( "##sbw_popup", 220.0f ) )
				{
					xui::color_picker( "color##sbw", m.m_scoreboard_weapons.color );
					xui::end_popup( );
				}

				xui::checkbox( "auto buy", m.m_autobuy.enabled );
				if ( xui::begin_popup( "##ab_popup", 240.0f ) )
				{
					xui::combo( "primary##ab", m.m_autobuy.primary_weapon.value, detail::primary_weapon_names, 18 );
					xui::combo( "secondary##ab", m.m_autobuy.secondary_weapon.value, detail::secondary_weapon_names, 6 );
					xui::checkbox( "armor##ab", m.m_autobuy.armor );
					xui::checkbox( "defuser##ab", m.m_autobuy.defuser );
					xui::checkbox( "taser##ab", m.m_autobuy.taser );
					xui::multicombo( "grenades##ab", m.m_autobuy.grenades, detail::grenade_names, 5 );
					xui::end_popup( );
				}

				xui::checkbox( "dynamic light", m.m_dlight.enabled );
				if ( xui::begin_popup( "##dlight_popup", 220.0f ) )
				{
					xui::color_picker( "color##dl", m.m_dlight.color );
					xui::slider_float( "radius##dl", m.m_dlight.radius, 50.0f, 1000.0f, "%.0f" );
					xui::slider_float( "z offset##dl", m.m_dlight.z_offset, -50.0f, 100.0f, "%.1f" );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			if ( xui::begin_child( "##misc_trajectory", col_w ) )
			{
				auto& traj = m.m_projectile_trajectory;
				xui::checkbox( "projectile trajectory", traj.enabled );
				if ( xui::begin_popup( "##traj_popup", 240.0f ) )
				{
					xui::checkbox( "straight throw##traj", traj.straight_throw );
					xui::color_picker( "held color##traj", traj.held_color );
					xui::color_picker( "thrown color##traj", traj.thrown_color );
					xui::color_picker( "damage held color##traj", traj.will_deal_damage_held_color );
					xui::color_picker( "damage thrown color##traj", traj.will_deal_damage_thrown_color );
					xui::checkbox( "glow##traj", traj.glow );
					xui::slider_float( "glow strength##traj", traj.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				auto& imp = m.m_impacts;
				xui::checkbox( "hit logs", imp.hit_log );
				if ( xui::begin_popup( "##hitlog_popup", 220.0f ) )
				{
					xui::slider_float( "duration##hl", imp.hit_log_duration, 1.0f, 10.0f, "%.1fs" );
					xui::checkbox( "console##hl", imp.console_log );
					xui::checkbox( "chat##hl", imp.chat_log );
					xui::end_popup( );
				}

				xui::checkbox( "miss logs", imp.miss_log );
				if ( xui::begin_popup( "##misslog_popup", 220.0f ) )
				{
					xui::slider_float( "duration##ml", imp.miss_log_duration, 1.0f, 10.0f, "%.1fs" );
					xui::end_popup( );
				}

				xui::checkbox( "hit sound", imp.hit_sound );
				if ( xui::begin_popup( "##hitsound_popup", 220.0f ) )
				{
					xui::combo( "sound##hs", reinterpret_cast<int&>( imp.hit_sound_type.value ), detail::sound_type_names, 10 );
					xui::slider_float( "volume##hs", imp.hit_sound_volume, 0.0f, 100.0f, "%.0f%%" );
					xui::end_popup( );
				}

				xui::checkbox( "death sound", imp.death_sound );
				if ( xui::begin_popup( "##deathsound_popup", 220.0f ) )
				{
					xui::combo( "sound##ds", reinterpret_cast<int&>( imp.death_sound_type.value ), detail::sound_type_names, 10 );
					xui::slider_float( "volume##ds", imp.death_sound_volume, 0.0f, 100.0f, "%.0f%%" );
					xui::end_popup( );
				}

				xui::checkbox( "bullet impacts", imp.bullet_impact_effect );
				if ( xui::begin_popup( "##impacts_popup", 240.0f ) )
				{
					xui::combo( "type##bi", reinterpret_cast<int&>( imp.bullet_impact_effect_type.value ), detail::bullet_impact_type_names, 3 );
					xui::color_picker( "fill color##bi", imp.bullet_impact_effect_fill_color );
					xui::color_picker( "edge color##bi", imp.bullet_impact_effect_edge_color );
					xui::color_picker( "spark color##bi", imp.bullet_impact_effect_color_spark );
					xui::slider_float( "duration##bi", imp.bullet_impact_effect_duration, 0.5f, 10.0f, "%.1fs" );
					xui::checkbox( "glow##bi", imp.bullet_impact_effect_glow );
					xui::slider_float( "glow strength##bi", imp.bullet_impact_effect_glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::checkbox( "hit marker", imp.hit_marker );
				if ( xui::begin_popup( "##hitmarker_popup", 220.0f ) )
				{
					xui::combo( "type##hm", reinterpret_cast<int&>( imp.hit_marker_type.value ), detail::marker_type_names, 3 );
					xui::color_picker( "color##hm", imp.hit_marker_color );
					xui::slider_float( "duration##hm", imp.hit_marker_duration, 0.5f, 5.0f, "%.1fs" );
					xui::checkbox( "glow##hm", imp.hit_marker_glow );
					xui::slider_float( "glow strength##hm", imp.hit_marker_glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::checkbox( "bullet tracers", imp.bullet_tracers );
				if ( xui::begin_popup( "##tracers_popup", 220.0f ) )
				{
					xui::color_picker( "color##bt", imp.bullet_tracer_color );
					xui::slider_float( "duration##bt", imp.bullet_tracer_duration, 0.1f, 3.0f, "%.1fs" );
					xui::end_popup( );
				}

				xui::checkbox( "hit effect", imp.hit_effect );
				if ( xui::begin_popup( "##hiteffect_popup", 220.0f ) )
				{
					xui::color_picker( "color##he", imp.hit_effect_color );
					xui::slider_float( "duration##he", imp.hit_effect_duration, 0.1f, 2.0f, "%.2fs" );
					xui::slider_float( "strength##he", imp.hit_effect_strength, 1.0f, 100.0f, "%.0f" );
					xui::end_popup( );
				}

				xui::checkbox( "death effect", imp.death_effect );
				if ( xui::begin_popup( "##deatheffect_popup", 220.0f ) )
				{
					xui::color_picker( "color##de", imp.death_effect_color );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			xui::layout::set_cursor( right_x - wx, body_y - wy );

			if ( xui::begin_child( "##misc_movement", col_w ) )
			{
				xui::checkbox( "bhop", mov.bhop );
				xui::checkbox( "autostrafe", mov.airstrafe );
				if ( xui::begin_popup( "##autostrafe_popup", 220.0f ) )
				{
					static const char* strafe_modes[]{ "viewangles", "directional", "rage" };
					xui::combo( "mode##as", reinterpret_cast<int&>( mov.airstrafe_mode.value ), strafe_modes, 3 );
					xui::slider_float( "retrack speed##as", mov.airstrafe_smooth, 10.0f, 100.0f, "%.0f%%" );
					xui::checkbox( "directional WASD##as", mov.airstrafe_fully_directional );
					xui::end_popup( );
				}
				xui::checkbox( "jumpbug", mov.jumpbug );
				xui::checkbox( "fastladder", mov.fastladder );
				xui::checkbox( "edgejump", mov.edgejump );
				xui::checkbox( "edgestop", mov.edgestop );
				xui::checkbox( "edgebug", mov.edgebug );
				if ( xui::begin_popup( "##edgebug_popup", 240.0f ) )
				{
					static const char* edgebug_modes[]{ "0: loose", "1: edge trace (default)", "2: no jump held", "3: min speed", "4: strict vz" };
					xui::combo( "mode##eb", mov.edgebug_mode.value, edgebug_modes, 5 );
					xui::slider_int( "passes##eb", mov.edgebug_passes, 1, 5, "%d" );
					xui::checkbox( "jump steps##eb", mov.edgebug_include_jump_steps );
					xui::end_popup( );
				}
				xui::checkbox( "slowwalk", mov.slowwalk );
				if ( xui::begin_popup( "##slowwalk_popup", 220.0f ) )
				{
					xui::slider_float( "speed", mov.slowwalk_speed, 1.0f, 100.0f, "%.2fs" );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			if ( xui::begin_child( "##misc_other", col_w ) )
			{
				xui::checkbox( "reveal radar", m.reveal_radar );
				xui::checkbox( "preserve killfeed", m.preserve_killfeed );
				xui::checkbox( "disable game logs", m.disable_game_logs );

				xui::end_child( );
			}
		}

		if ( subtab == 1 )
		{
			auto& rem = m.m_removals;

			if ( xui::begin_child( "##misc_removals_left", col_w ) )
			{
				xui::checkbox( "remove crosshair", rem.crosshair );
				xui::checkbox( "remove scope", rem.scope );
				xui::checkbox( "remove skybox fog", rem.skybox_fog );
				xui::checkbox( "remove overhead", rem.overhead );
				xui::checkbox( "remove legs", rem.legs );
				xui::checkbox( "remove 3d skybox", rem.skybox_3d );
				xui::checkbox( "remove recoil", rem.recoil );
				xui::checkbox( "remove decals", rem.decals );
				xui::checkbox( "remove smoke", rem.smoke );
				xui::slider_float( "flash alpha", rem.flash_alpha, 0.0f, 255.0f, "%.0f" );

				xui::end_child( );
			}
		}

		if ( subtab == 2 )
		{
			auto& cam = m.m_camera;
			auto& vm = m.m_viewmodel_adjust;

			if ( xui::begin_child( "##misc_camera_left", col_w ) )
			{
				xui::checkbox( "custom fov", cam.change_fov );
				if ( cam.change_fov.value )
				{
					xui::slider_float( "fov##cam", cam.fov, 60.0f, 140.0f, "%.0f" );
				}

				xui::checkbox( "scoped fov override", cam.scoped_fov_override );
				if ( cam.scoped_fov_override.value )
				{
					xui::slider_float( "scoped fov##cam", cam.scoped_fov, 10.0f, 90.0f, "%.0f" );
				}

				xui::checkbox( "thirdperson", cam.thirdperson );
				if ( xui::begin_popup( "##thirdperson_popup", 220.0f ) )
				{
					xui::slider_float( "distance##tp", cam.thirdperson_distance, 30.0f, 300.0f, "%.0f" );
					xui::slider_float( "hull size##tp", cam.thirdperson_hull_size, 2.0f, 30.0f, "%.1f" );
					xui::end_popup( );
				}

				xui::checkbox( "custom aspect ratio", cam.change_aspect_ratio );
				if ( cam.change_aspect_ratio.value )
				{
					xui::slider_float( "aspect ratio", cam.aspect_ratio, 0.5f, 2.5f, "%.2f" );
				}

				xui::end_child( );
			}

			xui::layout::set_cursor( right_x - wx, body_y - wy );

			if ( xui::begin_child( "##misc_viewmodel_right", col_w ) )
			{
				xui::checkbox( "viewmodel adjust", vm.enabled );
				if ( vm.enabled.value )
				{
					xui::slider_float( "offset x##vm", vm.offset_x, -10.0f, 10.0f, "%.1f" );
					xui::slider_float( "offset y##vm", vm.offset_y, -10.0f, 10.0f, "%.1f" );
					xui::slider_float( "offset z##vm", vm.offset_z, -10.0f, 10.0f, "%.1f" );
					xui::slider_float( "viewmodel fov##vm", vm.fov, 50.0f, 120.0f, "%.0f" );
				}

				xui::end_child( );
			}
		}

		if ( subtab == 3 )
		{
			auto& hud = m.m_hud;

			if ( xui::begin_child( "##misc_hud_left", col_w ) )
			{
				auto& cross = hud.m_crosshair;
				xui::checkbox( "crosshair overlay", cross.enabled );
				if ( xui::begin_popup( "##cross_popup", 220.0f ) )
				{
					xui::slider_float( "size##cr", cross.size, 1.0f, 20.0f, "%.1f" );
					xui::slider_float( "outline##cr", cross.outline, 0.0f, 5.0f, "%.1f" );
					xui::color_picker( "color##cr", cross.color );
					xui::color_picker( "outline color##cr", cross.outline_color );
					xui::end_popup( );
				}

				auto& sc = hud.m_scope;
				xui::checkbox( "scope overlay", sc.enabled );
				if ( xui::begin_popup( "##scope_popup", 240.0f ) )
				{
					xui::slider_float( "length##sc", sc.line_length, 10.0f, 500.0f, "%.0f" );
					xui::slider_float( "gap##sc", sc.gap, 0.0f, 50.0f, "%.0f" );
					xui::slider_float( "thickness##sc", sc.thickness, 0.1f, 5.0f, "%.1f" );
					xui::slider_float( "anim speed##sc", sc.anim_speed, 1.0f, 30.0f, "%.1f" );
					xui::color_picker( "color##sc", sc.color );
					xui::checkbox( "fade in##sc", sc.fade_in );
					xui::checkbox( "glow##sc", sc.glow );
					xui::slider_float( "glow strength##sc", sc.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				auto& hat = hud.m_hat;
				xui::checkbox( "hat", hat.enabled );
				if ( xui::begin_popup( "##hat_popup", 220.0f ) )
				{
					static const char* hat_types[]{ "kasa", "bucket" };
					xui::combo( "type##hat", reinterpret_cast<int&>( hat.type.value ), hat_types, 2 );
					xui::color_picker( "color##hat", hat.color );
					xui::color_picker( "secondary color##hat", hat.secondary_color );
					xui::checkbox( "glow##hat", hat.glow );
					xui::slider_float( "glow strength##hat", hat.glow_strength, 0.1f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			xui::layout::set_cursor( right_x - wx, body_y - wy );

			if ( xui::begin_child( "##misc_hud_right", col_w ) )
			{
				auto& vel = hud.m_velocity;
				xui::checkbox( "velocity counter", vel.counter );
				xui::checkbox( "velocity chart", vel.chart );
				if ( xui::begin_popup( "##velchart_popup", 220.0f ) )
				{
					xui::color_picker( "color##vel", vel.color );
					xui::slider_float( "bottom offset##vel", vel.bottom_offset, 0.0f, 300.0f, "%.0f" );
					xui::slider_float( "width##vel", vel.chart_width, 50.0f, 400.0f, "%.0f" );
					xui::slider_float( "height##vel", vel.chart_height, 20.0f, 150.0f, "%.0f" );
					xui::end_popup( );
				}

				auto& wm = m.m_watermark;
				xui::checkbox( "watermark", wm.enabled );
				if ( xui::begin_popup( "##wm_popup", 220.0f ) )
				{
					xui::checkbox( "fps##wm", wm.show_fps );
					xui::checkbox( "ping##wm", wm.show_ping );
					xui::checkbox( "time##wm", wm.show_time );
					xui::checkbox( "user##wm", wm.show_user );
					xui::checkbox( "map##wm", wm.show_map );
					xui::checkbox( "tick##wm", wm.show_tick );
					xui::checkbox( "velocity##wm", wm.show_velocity );
					xui::end_popup( );
				}

				xui::end_child( );
			}
		}
	}

} // namespace rendering