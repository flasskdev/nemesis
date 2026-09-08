#include <pch/pch.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <limits>

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
			xui::section_header("GAMEPLAY & LOGS");

			xui::toggle( "Hit Logs", impacts.hit_log );
			if ( xui::begin_popup( "##hitlog_popup", 220.0f ) )
			{
				xui::slider_float( "duration##hl", impacts.hit_log_duration, 0.5f, 10.0f, "%.1fs" );
				xui::end_popup( );
			}

			xui::layout::spacing( 3.0f );
			xui::toggle( "Console Logs", impacts.console_log );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Chat Logs", impacts.chat_log );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Miss Logs", impacts.miss_log );
			if ( xui::begin_popup( "##restore_misslogs", 250.0f ) )
			{
				xui::slider_float( "duration##ml", impacts.miss_log_duration, 0.5f, 10.0f, "%.1fs" );
				xui::end_popup( );
			}

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
				xui::checkbox( "glow##hm", impacts.hit_marker_glow );
				xui::slider_float( "glow strength##hm", impacts.hit_marker_glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::end_popup( );
			}
			xui::toggle("Disable Game Logs", m.disable_game_logs);
			xui::layout::spacing(6.0f);
			xui::toggle("Preserve Killfeed", m.preserve_killfeed);
			xui::layout::spacing(6.0f);
			xui::toggle("Reveal Radar", m.reveal_radar);
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

			xui::layout::spacing( 8.0f );
			xui::section_header( "IDENTITY & UTILITY" );
			xui::toggle( "Nickname Override", m.m_name_changer.override_name );
			if ( xui::begin_popup( "##restore_name", 250.0f ) )
			{
				xui::text_input( "nickname##override", m.m_name_changer.name.value, 127, "Player" );
				xui::end_popup( );
			}
			xui::toggle( "Clantag", m.m_name_changer.clantag );
			xui::toggle( "Scoreboard Weapons", m.m_scoreboard_weapons.enabled );
			if ( xui::begin_popup( "##restore_scoreboard", 250.0f ) )
			{
				xui::color_picker( "color##scoreboard", m.m_scoreboard_weapons.color );
				xui::end_popup( );
			}
			xui::toggle( "Auto Buy", m.m_autobuy.enabled );
			if ( xui::begin_popup( "##restore_autobuy", 250.0f ) )
			{
				xui::combo( "primary##buy", m.m_autobuy.primary_weapon.value, detail::primary_weapons, 6 );
				xui::combo( "secondary##buy", m.m_autobuy.secondary_weapon.value, detail::secondary_weapons, 5 );
				xui::checkbox( "armor##buy", m.m_autobuy.armor );
				xui::checkbox( "defuser##buy", m.m_autobuy.defuser );
				xui::checkbox( "taser##buy", m.m_autobuy.taser );
				xui::multicombo( "grenades##buy", m.m_autobuy.grenades, detail::grenade_names, 5 );
				xui::end_popup( );
			}

			xui::layout::spacing( 8.0f );
			xui::section_header( "EFFECTS" );
			xui::toggle( "Death Sound", impacts.death_sound );
			if ( xui::begin_popup( "##restore_deathsound", 250.0f ) )
			{
				xui::combo( "type##ds", impacts.death_sound_type.value, detail::sound_types, detail::k_sound_type_count );
				xui::slider_float( "volume##ds", impacts.death_sound_volume, 1.0f, 100.0f, "%.0f%%" );
				if ( impacts.death_sound_type.value == settings::misc::impacts::sound_type::custom )
				{
					detail::draw_custom_sound_picker( impacts.custom_death_sound, "sound##ds", "preview##ds", impacts.death_sound_volume.value );
				}
				xui::end_popup( );
			}
			xui::toggle( "Hit Effect", impacts.hit_effect );
			if ( xui::begin_popup( "##restore_hit_effect", 250.0f ) )
			{
				xui::color_picker( "color##he", impacts.hit_effect_color );
				xui::slider_float( "duration##he", impacts.hit_effect_duration, 0.05f, 5.0f, "%.2fs" );
				xui::slider_float( "strength##he", impacts.hit_effect_strength, 0.0f, 100.0f, "%.0f%%" );
				xui::end_popup( );
			}
			xui::toggle( "Death Effect", impacts.death_effect );
			if ( xui::begin_popup( "##restore_death_effect", 250.0f ) )
			{
				xui::color_picker( "color##de", impacts.death_effect_color );
				xui::end_popup( );
			}
			xui::toggle( "Bullet Impacts", impacts.bullet_impact_effect );
			if ( xui::begin_popup( "##restore_bullet_impact", 250.0f ) )
			{
				xui::combo( "type##bi", impacts.bullet_impact_effect_type.value, detail::impact_types, 3 );
				xui::color_picker( "fill##bi", impacts.bullet_impact_effect_fill_color );
				xui::color_picker( "edge##bi", impacts.bullet_impact_effect_edge_color );
				xui::color_picker( "spark##bi", impacts.bullet_impact_effect_color_spark );
				xui::slider_float( "duration##bi", impacts.bullet_impact_effect_duration, 0.1f, 10.0f, "%.1fs" );
				xui::checkbox( "glow##bi", impacts.bullet_impact_effect_glow );
				xui::slider_float( "glow strength##bi", impacts.bullet_impact_effect_glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::end_popup( );
			}
			xui::toggle( "Bullet Tracers", impacts.bullet_tracers );
			if ( xui::begin_popup( "##restore_tracers", 250.0f ) )
			{
				xui::color_picker( "color##tracer", impacts.bullet_tracer_color );
				xui::slider_float( "duration##tracer", impacts.bullet_tracer_duration, 0.1f, 10.0f, "%.1fs" );
				xui::end_popup( );
			}
			xui::toggle( "Projectile Trajectory", m.m_projectile_trajectory.enabled );
			if ( xui::begin_popup( "##restore_trajectory", 250.0f ) )
			{
				xui::checkbox( "straight throw##traj", m.m_projectile_trajectory.straight_throw );
				xui::color_picker( "held##traj", m.m_projectile_trajectory.held_color );
				xui::color_picker( "thrown##traj", m.m_projectile_trajectory.thrown_color );
				xui::color_picker( "held damage##traj", m.m_projectile_trajectory.will_deal_damage_held_color );
				xui::color_picker( "thrown damage##traj", m.m_projectile_trajectory.will_deal_damage_thrown_color );
				xui::checkbox( "glow##traj", m.m_projectile_trajectory.glow );
				xui::slider_float( "glow strength##traj", m.m_projectile_trajectory.glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::end_popup( );
			}

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
			if ( xui::begin_popup( "##vm_popup", 240.0f ) )
			{
				xui::slider_float( "Viewmodel X##vm_x", vm.offset_x.value, -10.0f, 10.0f, "%.1f" );
				xui::slider_float( "Viewmodel Y##vm_y", vm.offset_y.value, -10.0f, 10.0f, "%.1f" );
				xui::slider_float( "Viewmodel Z##vm_z", vm.offset_z.value, -10.0f, 10.0f, "%.1f" );
				xui::slider_float( "Viewmodel FOV##vm_fov", vm.fov.value, 54.0f, 90.0f, "%.0f" );
                if ( xui::button( "Log Viewmodel State##vm", 200.0f, 22.0f ) )
                {
                    auto* owner = addresses::globals::cvar;
                    if ( !owner )
                    {
                        logging::console::print( "viewmodel: cvar interface is unavailable" );
                    }
                    else
                    {
                        const std::array<float, 4> requested{
                            vm.offset_x.value, vm.offset_y.value, vm.offset_z.value, vm.fov.value
                        };
                        constexpr std::array<std::uint32_t, 4> hashes{
                            "viewmodel_offset_x"_hash, "viewmodel_offset_y"_hash,
                            "viewmodel_offset_z"_hash, "viewmodel_fov"_hash
                        };
                        constexpr std::array<const char*, 4> names{
                            "viewmodel_offset_x", "viewmodel_offset_y",
                            "viewmodel_offset_z", "viewmodel_fov"
                        };
                        for ( std::size_t i = 0; i < hashes.size( ); ++i )
                        {
                            const auto* cvar = owner->find( hashes[i] );
                            if ( !cvar )
                            {
                                logging::console::print( "viewmodel: missing {}", names[i] );
                                continue;
                            }
                            const auto missing = std::numeric_limits<float>::quiet_NaN( );
                            const auto low = memory::safe_read<float>(
                                reinterpret_cast<std::uintptr_t>( cvar->m_min_value ) ).value_or( missing );
                            const auto high = memory::safe_read<float>(
                                reinterpret_cast<std::uintptr_t>( cvar->m_max_value ) ).value_or( missing );
                            logging::console::print(
                                "viewmodel {}: enabled={} requested={} raw_float={} raw_i32={} type={} min={} max={} changes={}",
                                names[i], vm.enabled.value, requested[i], cvar->m_value.fl,
                                cvar->m_value.i32, cvar->m_type, low, high, cvar->m_change_count );
                        }
                        if ( const auto* preset = owner->find( "viewmodel_presetpos"_hash ) )
                        {
                            logging::console::print( "viewmodel preset: {}", preset->m_value.i32 );
                        }
                    }
                }
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

			xui::layout::spacing( 8.0f );
			xui::section_header( "ADDITIONAL VIEW SETTINGS" );
			xui::toggle( "Custom Aspect Ratio", cam.change_aspect_ratio );
			if ( xui::begin_popup( "##restore_aspect", 250.0f ) )
			{
				xui::slider_float( "aspect ratio##camera", cam.aspect_ratio, 0.5f, 3.0f, "%.3f" );
				xui::end_popup( );
			}
			xui::toggle( "Remove Skybox Fog", rem.skybox_fog );
			xui::toggle( "Remove Overhead", rem.overhead );
			xui::toggle( "Remove Legs", rem.legs );
			xui::toggle( "Remove 3D Skybox", rem.skybox_3d );
			xui::toggle( "Remove Decals", rem.decals );

			xui::layout::spacing( 8.0f );
			xui::section_header( "HUD" );
			xui::toggle( "Crosshair Overlay", m.m_hud.m_crosshair.enabled );
			if ( xui::begin_popup( "##restore_crosshair", 250.0f ) )
			{
				xui::slider_float( "size##hudcross", m.m_hud.m_crosshair.size, 0.1f, 10.0f, "%.1f" );
				xui::slider_float( "outline##hudcross", m.m_hud.m_crosshair.outline, 0.0f, 5.0f, "%.1f" );
				xui::color_picker( "color##hudcross", m.m_hud.m_crosshair.color );
				xui::color_picker( "outline color##hudcross", m.m_hud.m_crosshair.outline_color );
				xui::end_popup( );
			}
			xui::toggle( "Scope Overlay", m.m_hud.m_scope.enabled );
			if ( xui::begin_popup( "##restore_scope", 250.0f ) )
			{
				xui::slider_float( "line length##hudscope", m.m_hud.m_scope.line_length, 1.0f, 500.0f, "%.0f" );
				xui::slider_float( "gap##hudscope", m.m_hud.m_scope.gap, 0.0f, 100.0f, "%.1f" );
				xui::slider_float( "thickness##hudscope", m.m_hud.m_scope.thickness, 0.1f, 10.0f, "%.1f" );
				xui::slider_float( "animation speed##hudscope", m.m_hud.m_scope.anim_speed, 0.1f, 30.0f, "%.1f" );
				xui::color_picker( "color##hudscope", m.m_hud.m_scope.color );
				xui::checkbox( "fade in##hudscope", m.m_hud.m_scope.fade_in );
				xui::checkbox( "glow##hudscope", m.m_hud.m_scope.glow );
				xui::slider_float( "glow strength##hudscope", m.m_hud.m_scope.glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::end_popup( );
			}
			xui::toggle( "Hat", m.m_hud.m_hat.enabled );
			if ( xui::begin_popup( "##restore_hat", 250.0f ) )
			{
				xui::combo( "type##hat", m.m_hud.m_hat.type.value, detail::hat_types, 2 );
				xui::color_picker( "color##hat", m.m_hud.m_hat.color );
				xui::color_picker( "secondary color##hat", m.m_hud.m_hat.secondary_color );
				xui::checkbox( "glow##hat", m.m_hud.m_hat.glow );
				xui::slider_float( "glow strength##hat", m.m_hud.m_hat.glow_strength, 0.1f, 1.0f, "%.2f" );
				xui::end_popup( );
			}
			xui::toggle( "Velocity Counter", m.m_hud.m_velocity.counter );
			if ( xui::begin_popup( "##restore_velocity", 250.0f ) )
			{
				xui::color_picker( "color##velocity", m.m_hud.m_velocity.color );
				xui::slider_float( "bottom offset##velocity", m.m_hud.m_velocity.bottom_offset, 0.0f, 500.0f, "%.0f" );
				xui::end_popup( );
			}
			xui::toggle( "Velocity Chart", m.m_hud.m_velocity.chart );
			if ( xui::begin_popup( "##restore_velocitychart", 250.0f ) )
			{
				xui::slider_float( "width##velocity", m.m_hud.m_velocity.chart_width, 50.0f, 600.0f, "%.0f" );
				xui::slider_float( "height##velocity", m.m_hud.m_velocity.chart_height, 10.0f, 200.0f, "%.0f" );
				xui::end_popup( );
			}

			xui::layout::spacing( 8.0f );
			xui::section_header( "LIGHT & POST PROCESS" );
			xui::toggle( "Dynamic Light", m.m_dlight.enabled );
			if ( xui::begin_popup( "##restore_dlight", 250.0f ) )
			{
				xui::color_picker( "color##dlight", m.m_dlight.color );
				xui::slider_float( "radius##dlight", m.m_dlight.radius, 1.0f, 1000.0f, "%.0f" );
				xui::slider_float( "z offset##dlight", m.m_dlight.z_offset, -100.0f, 100.0f, "%.1f" );
				xui::end_popup( );
			}
			xui::toggle( "Chromatic Aberration", m.m_post_process.m_chromatic_aberration.enabled );
			if ( xui::begin_popup( "##restore_chromatic", 250.0f ) )
			{
				xui::slider_float( "intensity##chromatic", m.m_post_process.m_chromatic_aberration.intensity, 0.0f, 0.03f, "%.4f" );
				xui::end_popup( );
			}

			xui::end_child( );
		}
	}

} // namespace rendering
