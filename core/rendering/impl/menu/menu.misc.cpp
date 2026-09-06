#include <pch/pch.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* sound_types[ ]{ "shop click", "home click", "bell", "killcard", "bullet casing", "coin pickup", "item drop", "popcan", "key press", "custom" };
		constexpr auto k_sound_type_count{ static_cast< int >( std::size( sound_types ) ) };

		void draw_custom_sound_picker( config::str& file_setting, std::string_view combo_label, std::string_view preview_id, float preview_volume )
		{
			const auto files = features::misc::impacts::list_custom_sounds( );

			static std::vector<std::string> cached_files{};
			static std::vector<const char*> cached_ptrs{};
			cached_files = files;
			cached_ptrs.clear( );
			cached_ptrs.reserve( cached_files.size( ) );

			for ( const auto& file : cached_files )
			{
				cached_ptrs.push_back( file.c_str( ) );
			}

			if ( !cached_ptrs.empty( ) )
			{
				auto selected{ 0 };
				for ( auto i = 0; i < static_cast< int >( cached_files.size( ) ); ++i )
				{
					if ( cached_files[ static_cast< std::size_t >( i ) ] == file_setting.value )
					{
						selected = i;
						break;
					}
				}

				if ( xui::combo( combo_label, selected, cached_ptrs.data( ), static_cast< int >( cached_ptrs.size( ) ) ) )
				{
					file_setting = cached_files[ static_cast< std::size_t >( selected ) ];
				}
			}

			xui::text_input( "file", file_setting.value, 64, "hit.wav" );

			if ( xui::button( preview_id, 96.0f, 22.0f ) )
			{
				features::misc::g_impacts.play_custom_sound( file_setting.value, preview_volume );
			}
		}
		constexpr const char* marker_types[ ]{ "classic", "damage", "both" };
		constexpr const char* impact_types[ ]{ "overlay", "sparks", "both" };

		constexpr const char* primary_weapons[ ]{ "none", "rifle", "scoped rifle", "scout", "awp", "auto sniper" };
		constexpr const char* secondary_weapons[ ]{ "none", "dual elites", "five-seven/tec-9", "deagle", "revolver" };
		constexpr const char* grenade_names[ ]{ "molotov", "he grenade", "smoke", "flashbang", "decoy" };

		constexpr const char* hat_types[ ]{ "kasa", "bucket" };

	} // namespace detail

	void menu::draw_misc( float group_w ) const
	{
		auto& m = settings::g_misc;
		auto& impacts = m.m_impacts;
		auto& pen = settings::g_combat.m_penetration_crosshair;
		auto& rem = m.m_removals;
		auto& cam = m.m_camera;
		auto& vm = m.m_viewmodel_adjust;

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + menu::k_sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto body_h = this->m_body_h;
		const auto content_w = this->m_w - tokens::gap * 2.0f - menu::k_sidebar_w - tokens::gap;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		// LEFT COLUMN: GAMEPLAY & LOGS
		xui::layout::set_cursor( content_x - wx, body_y - wy );

		if ( xui::begin_child( "##misc_gameplay_logs", col_w, body_h, true ) )
		{
            xui::section_header("MOVEMENT");
            xui::toggle("Bunnyhop", settings::g_movement.bhop);
            xui::toggle("Silent Air Strafe", settings::g_movement.airstrafe);
            xui::toggle("Directional WASD", settings::g_movement.airstrafe_fully_directional);
            xui::slider_float("Strafe Smoothing", settings::g_movement.airstrafe_heading_slack, 0.0f, 12.0f, "%.1f deg");
            xui::layout::spacing(8.0f);
			xui::section_header("GAMEPLAY & LOGS");

			xui::toggle( "Hit Logs", impacts.hit_log );
			if ( xui::begin_popup( "##hitlog_popup", 220.0f ) )
			{
				xui::slider_float( "duration##hl", impacts.hit_log_duration, 0.5f, 10.0f, "%.1fs" );
				xui::end_popup( );
			}

			xui::layout::spacing( 3.0f );
			xui::toggle( "Console Logs", impacts.console_log );
            xui::toggle("Movement Debug", settings::g_movement.movement_debug, "Log movement and server parameters every 16 commands");
			xui::layout::spacing( 3.0f );
			xui::toggle( "Chat Logs", impacts.chat_log );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Miss Logs", impacts.miss_log );

			xui::layout::spacing( 6.0f );
			xui::toggle( "Hit Sound", impacts.hit_sound );
			if ( xui::begin_popup( "##hitsound_popup", 220.0f ) )
			{
				xui::combo( "type##hs", impacts.hit_sound_type.value, detail::sound_types, detail::k_sound_type_count );
				xui::slider_float( "volume##hs", impacts.hit_sound_volume, 1.0f, 100.0f, "%.0f%%" );
				if ( impacts.hit_sound_type.value == settings::misc::impacts::sound_type::custom )
				{
					detail::draw_custom_sound_picker( impacts.custom_hit_sound, "sound##hs", "preview##hs", impacts.hit_sound_volume.value );
				}
				xui::end_popup( );
			}

			xui::layout::spacing( 6.0f );
			xui::toggle( "Hit Marker", impacts.hit_marker );
			if ( xui::begin_popup( "##hitmarker_popup", 220.0f ) )
			{
				xui::combo( "type##hm", impacts.hit_marker_type.value, detail::marker_types, 3 );
				xui::slider_float( "duration##hm", impacts.hit_marker_duration, 0.1f, 5.0f, "%.1fs" );
				xui::color_picker( "color##hm", impacts.hit_marker_color );
				xui::end_popup( );
			}

			xui::layout::spacing( 6.0f );
			xui::toggle( "Penetration Crosshair", pen.enabled );
			if ( xui::begin_popup( "##pen_popup", 220.0f ) )
			{
				xui::checkbox( "glow##pen", pen.glow );
				xui::slider_float( "glow strength##pen", pen.glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::color_picker( "can penetrate##pen", pen.can_penetrate_fill );
				xui::color_picker( "can pen outline##pen", pen.can_penetrate_outline );
				xui::color_picker( "blocked##pen", pen.blocked_fill );
				xui::color_picker( "blocked outline##pen", pen.blocked_outline );
				xui::end_popup( );
			}

			xui::layout::spacing( 6.0f );
			xui::toggle( "Reveal Radar", m.reveal_radar );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Preserve Killfeed", m.preserve_killfeed );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Disable Game Logs", m.disable_game_logs );

			xui::end_child( );
		}

		// RIGHT COLUMN: VIEW & REMOVALS
		xui::layout::set_cursor( right_x - wx, body_y - wy );

		if ( xui::begin_child( "##misc_view_removals", col_w, body_h, true ) )
		{
			xui::section_header("VIEW & REMOVALS");

			xui::toggle( "Custom FOV", cam.change_fov );
			if ( xui::begin_popup( "##fov_popup", 220.0f ) )
			{
				xui::slider_float( "fov", cam.fov, 60.0f, 150.0f, "%.0f" );
				xui::checkbox( "scoped fov override", cam.scoped_fov_override );
				xui::slider_float( "scoped fov", cam.scoped_fov, 10.0f, 90.0f, "%.0f" );
				xui::end_popup( );
			}

			xui::layout::spacing( 3.0f );
			xui::toggle( "Thirdperson", cam.thirdperson );
			if ( xui::begin_popup( "##tp_popup", 220.0f ) )
			{
				xui::slider_float( "distance", cam.thirdperson_distance, 35.0f, 200.0f, "%.0f" );
				xui::slider_float( "hull size", cam.thirdperson_hull_size, 0.0f, 20.0f, "%.0f" );
				xui::end_popup( );
			}

			xui::layout::spacing( 3.0f );
			xui::toggle( "Viewmodel Adjust", vm.enabled );
			if ( xui::begin_popup( "##vm_popup", 220.0f ) )
			{
				xui::slider_float( "offset x", vm.offset_x, -10.0f, 10.0f, "%.1f" );
				xui::slider_float( "offset y", vm.offset_y, -10.0f, 10.0f, "%.1f" );
				xui::slider_float( "offset z", vm.offset_z, -10.0f, 10.0f, "%.1f" );
				xui::slider_float( "fov", vm.fov, 54.0f, 90.0f, "%.0f" );
				xui::end_popup( );
			}

			xui::layout::spacing( 8.0f );
			xui::text( "REMOVALS", tokens::col_accent );
			xui::layout::spacing( 4.0f );
			xui::toggle( "Remove Crosshair", rem.crosshair );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Remove Scope", rem.scope );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Remove Smoke", rem.smoke );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Remove Visual Recoil", rem.recoil );
			xui::layout::spacing( 3.0f );
			xui::slider_float( "Flash Alpha", rem.flash_alpha, 0.0f, 100.0f, "%.0f%%" );

			xui::end_child( );
		}
	}

} // namespace rendering
