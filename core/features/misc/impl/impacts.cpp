#include <pch/pch.hpp>
#include <ShlObj.h>
#include <filesystem>

#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/rendering/rendering.hpp>
#include <core/rendering/theme.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
#include <core/features/misc/koch_sound.hpp>

namespace features::misc {

	namespace detail {

		constexpr std::uint32_t invalid_particle_effect{ static_cast<std::uint32_t>( -1 ) };
		constexpr auto k_hit_color_hex{ "#4ADE80" };
		constexpr auto k_miss_color_hex{ "#FF5C80" };

		void chat_print_raw( const char* formatted_msg )
		{
			const auto fn_find_hud = PATTERN (patterns::find_hud_element);
			if ( !fn_find_hud )
			{
				return;
			}

			// 1. Primary CS2 HUD message displayer: triggers on-screen HUD toast notification
			// so the message is visible immediately during gameplay without needing to open chat
			const auto fn_voice = PATTERN (patterns::set_voice_data);
			if ( fn_voice )
			{
				const auto voice_hud = memory::call<std::uintptr_t>( fn_find_hud, xs( "CCSGO_HudVoiceStatus" ) );
				if ( voice_hud )
				{
					const auto voice = voice_hud - 0x20;
					std::uint8_t flags[ 2 ]{ 1, 0 };
					memory::call<void>( fn_voice, voice, formatted_msg, 0xFFFFFFFF, flags );
					return;
				}
			}

			// 2. Direct Panorama chat scrollview fallback
			const auto fn_chat = PATTERN (patterns::print_hud_chat);
			if ( fn_chat )
			{
				const auto chat_hud = memory::call<std::uintptr_t>( fn_find_hud, xs( "CCSGO_HudChat" ) );
				if ( chat_hud )
				{
					const auto chat = chat_hud - 0x20;
					memory::call<void>( fn_chat, chat, formatted_msg );
				}
			}
		}

		inline void print_mintaly_chat( const char* text_color_hex, const std::string& log_text )
		{
			const auto r = tokens::col_accent.r;
			const auto g = tokens::col_accent.g;
			const auto b = tokens::col_accent.b;

			const auto formatted = std::format(
				"<font color='#{:02X}{:02X}{:02X}'>mintaly</font> <font color='#888888'>:</font> <font color='{}'>{}</font>",
				r, g, b, text_color_hex, log_text
			);

			chat_print_raw( formatted.c_str( ) );
		}

	} // namespace detail

	void impacts::on_render_early( xdraw::draw_list& draw_list )
	{
		const auto global_vars = memory::safe_read<std::uintptr_t>( addresses::globals::global_vars ).value_or( 0 );
		if ( !global_vars )
		{
			return;
		}
		const auto time = memory::safe_read<float>( global_vars + 0x30 );
		if ( !time || !std::isfinite( *time ) )
		{
			return;
		}
		const auto current_time = *time;

		this->render_hit_effect( draw_list, current_time );
		this->render_bullet_impact_overlays( draw_list, current_time );
	}

	void impacts::on_frame_stage_notify( )
	{
		// Frame-stage updates can overlap Present, which renders the same impact vectors.
		std::unique_lock lock( this->m_mtx );

		if ( this->m_buffered_impact_time > 0.0f )
		{
			this->flush_buffered_impacts( );
			this->m_buffered_impacts.clear( );
			this->m_buffered_impact_time = -1.0f;
		}
	}

	void impacts::on_level_change( )
	{
		std::unique_lock lock( this->m_mtx );

		this->m_hitmarkers.clear( );
		this->m_logs.clear( );
		this->m_pending_hits.clear( );
		this->m_pending_shots.clear( );
		this->m_prepared_revolver_shots.clear( );
		this->m_revolver_weapon_handle = 0;
		this->m_revolver_observed_shot_time = 0.0f;
		this->m_bullet_impacts.clear( );
		this->m_buffered_impacts.clear( );
		this->m_buffered_impact_time = -1.0f;
		this->m_hit_effect_time = 0.0f;
	}

	void impacts::on_render( xdraw::draw_list& draw_list )
	{
		const auto global_vars = memory::safe_read<std::uintptr_t>( addresses::globals::global_vars ).value_or( 0 );
		if ( !global_vars )
		{
			return;
		}
		const auto time = memory::safe_read<float>( global_vars + 0x30 );
		if ( !time || !std::isfinite( *time ) )
		{
			return;
		}
		const auto current_time = *time;

		this->check_misses( );

		this->render_hit_markers( draw_list, current_time );
		this->render_logs( draw_list, current_time );
	}

	void impacts::on_report_hit( std::uintptr_t msg )
	{
		const auto& cfg = settings::g_misc.m_impacts;

		if ( !cfg.hit_marker.value && !cfg.hit_sound.value && !cfg.hit_effect.value )
		{
			return;
		}

		const auto position = memory::read<math::vector3>( msg + 0x18 );
		if ( !std::isfinite( position.x ) || !std::isfinite( position.y ) || !std::isfinite( position.z ) || position.length_sqr( ) < 1.0f )
		{
			return;
		}

		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );

		std::unique_lock lock( this->m_mtx );

		this->m_pending_hits.push_back( { position, current_time } );

		if ( this->m_pending_hits.size( ) > 10 )
		{
			this->m_pending_hits.erase( this->m_pending_hits.begin( ) );
		}
	}

	void impacts::on_player_hurt( std::uintptr_t event )
	{
		if ( !event )
		{
			return;
		}

		// Local-server events may precede the next CreateMove observation.
		this->observe_revolver_shot( );
		const auto data = this->parse_event( event );
		if ( !data.victim_pawn )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_impacts;
		const auto is_kill = data.health <= 0;

		if ( cfg.hit_marker )
		{
			const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
			const auto current_time = memory::read<float>( global_vars + 0x30 );
			auto position = math::vector3{};
			auto has_position{ false };

			// Report-hit contains the most accurate contact point, but it is not
			// guaranteed to arrive before player_hurt. Use it when available and
			// fall back to the confirmed shot impact or the victim's position.
			const auto game_scene_node = memory::read<std::uintptr_t>( data.victim_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( game_scene_node )
			{
				const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
				const auto view_offset = memory::read<math::vector3>( data.victim_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );

				position = origin + view_offset * 0.75f;
				has_position = true;
			}

			std::unique_lock lock( this->m_mtx );

			auto best_idx{ SIZE_MAX };
			auto best_diff{ 0.5f };

			for ( auto i = 0ull; i < this->m_pending_hits.size( ); ++i )
			{
				const auto diff = std::abs( current_time - this->m_pending_hits[ i ].time );
				if ( diff < best_diff )
				{
					best_diff = diff;
					best_idx = i;
				}
			}

			if ( best_idx != SIZE_MAX )
			{
				position = this->m_pending_hits[ best_idx ].position;
				has_position = true;
				this->m_pending_hits.erase( this->m_pending_hits.begin( ) + best_idx );
			}
			else
			{
				for ( auto it = this->m_pending_shots.rbegin( ); it != this->m_pending_shots.rend( ); ++it )
				{
					if ( it->victim_pawn == data.victim_pawn && it->impact_confirmed && current_time - it->impact_time <= 1.0f )
					{
						position = it->impact_position;
						has_position = true;
						break;
					}
				}
			}

			if ( has_position )
			{
				this->m_hitmarkers.push_back( { position, current_time, data.damage } );

				if ( this->m_hitmarkers.size( ) > 10 )
				{
					this->m_hitmarkers.erase( this->m_hitmarkers.begin( ) );
				}
			}

			std::erase_if( this->m_pending_hits, [ & ]( const auto& entry ) { return current_time - entry.time > 0.5f; } );
		}

		if ( is_kill && cfg.death_sound.value )
		{
			this->play_sound( cfg.death_sound_type, cfg.death_sound_volume, cfg.custom_death_sound.value );
		}

		if ( cfg.hit_sound.value )
		{
			this->play_sound( cfg.hit_sound_type, cfg.hit_sound_volume, cfg.custom_hit_sound.value );
		}

		if ( is_kill && cfg.death_effect.value )
		{
			this->play_death_effect( data.victim_pawn );
		}

		if ( cfg.hit_effect.value )
		{
			this->play_hit_effect( data.victim_pawn );
		}

		if ( cfg.hit_log.value || cfg.console_log.value || cfg.chat_log.value )
		{
			this->add_hit_log( data );
		}
	}

	void impacts::on_bullet_impact( std::uintptr_t event )
	{
		if ( !event )
		{
			return;
		}

		const auto userid_key = cstypes::event_hash{ 0, "userid" };
		const auto controller = memory::call<std::uintptr_t>(PATTERN (patterns::game_event_get_controller), event, &userid_key );

		if ( controller != systems::g_local.get( ).controller )
		{
			return;
		}

		this->observe_revolver_shot( );
		const auto x = memory::call<float>(PATTERN (patterns::game_event_get_float), event, "x", 0.0f );
		const auto y = memory::call<float>(PATTERN (patterns::game_event_get_float), event, "y", 0.0f );
		const auto z = memory::call<float>(PATTERN (patterns::game_event_get_float), event, "z", 0.0f );

		{
			std::unique_lock lock( this->m_mtx );
			const auto pos = math::vector3{ x, y, z };
			const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
			const auto current_time = memory::read<float>( global_vars + 0x30 );
			shot_record* matched_shot{};
			auto best_score{ -FLT_MAX };

			// Server shot callbacks and bullet-impact events preserve command order.
			// Match the oldest shot that does not have an impact before considering
			// extra impacts from an already matched shot.
			for ( auto& shot : this->m_pending_shots )
			{
				if ( shot.resolved || shot.impact_confirmed )
				{
					continue;
				}

				const auto age = current_time - shot.time;
				if ( age < -0.05f || age > 1.0f )
				{
					continue;
				}

				const auto origin = shot.server_shoot_position_confirmed ? shot.server_shoot_position : shot.shoot_position;
				const auto impact_delta = pos - origin;
				if ( impact_delta.length_sqr( ) < 1.0f )
				{
					continue;
				}

				math::vector3 expected_direction{};
				math::helpers::angle_vectors_left( shot.aim_angle, &expected_direction );
				const auto alignment = expected_direction.dot( impact_delta.normalized( ) );
				if ( alignment > -0.25f )
				{
					matched_shot = &shot;
					break;
				}
			}

			if ( !matched_shot )
			{
				for ( auto& shot : this->m_pending_shots )
				{
					if ( shot.resolved || !shot.impact_confirmed )
					{
						continue;
					}

					const auto age = current_time - shot.time;
					if ( age < -0.05f || age > 1.0f )
					{
						continue;
					}

					const auto origin = shot.server_shoot_position_confirmed ? shot.server_shoot_position : shot.shoot_position;
					const auto impact_delta = pos - origin;
					if ( impact_delta.length_sqr( ) < 1.0f )
					{
						continue;
					}

					math::vector3 expected_direction{};
					math::helpers::angle_vectors_left( shot.aim_angle, &expected_direction );
					const auto score = expected_direction.dot( impact_delta.normalized( ) ) * 4.0f - std::fabs( age );

					if ( score > best_score )
					{
						best_score = score;
						matched_shot = &shot;
					}
				}
			}

			if ( matched_shot )
			{
				const auto target_origin = matched_shot->skeleton[ 0 ].position;
				const auto dist_sq = ( pos - target_origin ).length_sqr( );

				if ( !matched_shot->impact_confirmed || dist_sq < matched_shot->best_impact_dist_sq )
				{
					matched_shot->impact_position = pos;
					matched_shot->best_impact_dist_sq = dist_sq;
					matched_shot->impact_time = current_time;
					matched_shot->impact_confirmed = true;
				}
			}

			const auto& cfg = settings::g_misc.m_impacts;
			if ( cfg.bullet_impact_effect.value || cfg.bullet_tracers.value )
			{
				const auto type = cfg.bullet_impact_effect_type.value;
				const auto show_overlay = type == settings::misc::impacts::bullet_impact_type::overlay || type == settings::misc::impacts::bullet_impact_type::both;

				if ( cfg.bullet_tracers.value || show_overlay )
				{
					const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
					const auto current_time = memory::read<float>( global_vars + 0x30 );

					if ( current_time != this->m_buffered_impact_time )
					{
						this->flush_buffered_impacts( );

						this->m_buffered_impact_time = current_time;
						this->m_buffered_impacts.clear( );

						const auto local_pawn = systems::g_local.get( ).pawn;
						if ( local_pawn )
						{
							const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
							const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
							const auto view_offset = memory::read<math::vector3>( local_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
							this->m_buffered_eye_position = origin + view_offset;
						}
					}

					this->m_buffered_impacts.push_back( math::vector3{ x, y, z } );
				}
			}
		}

		const auto& cfg = settings::g_misc.m_impacts;
		if ( cfg.bullet_impact_effect.value )
		{
			const auto type = cfg.bullet_impact_effect_type.value;
			const auto show_sparks = type == settings::misc::impacts::bullet_impact_type::sparks || type == settings::misc::impacts::bullet_impact_type::both;

			if ( show_sparks )
			{
				this->play_bullet_impact_effect( math::vector3{ x, y, z } );
			}
		}
	}

	void impacts::on_base_fire_guns_get_inaccuracy( std::uintptr_t weapon, float inaccuracy )
	{
		const auto owner_handle = memory::read<std::uint32_t>( weapon + SCHEMA( "C_BaseEntity", "m_hOwnerEntity"_hash ) );
		const auto owner = systems::g_entities.lookup( owner_handle );

		if ( !owner || owner != systems::g_local.get( ).pawn )
		{
			return;
		}

		std::unique_lock lock( this->m_mtx );

		for ( auto it = this->m_pending_shots.begin( ); it != this->m_pending_shots.end( ); ++it )
		{
			if ( !it->server_confirmed )
			{
				it->server_inaccuracy = inaccuracy;
				it->server_confirmed = true;
				break;
			}
		}
	}

	void impacts::on_get_interpolated_shoot_position( std::uintptr_t weapon_services, float* out )
	{
		const auto local_pawn = systems::g_local.get( ).pawn;
		if ( !local_pawn )
		{
			return;
		}

		const auto local_weapon_services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
		if ( !local_weapon_services || weapon_services != local_weapon_services )
		{
			return;
		}

		const auto shoot_position = math::vector3{ out[ 0 ], out[ 1 ], out[ 2 ] };

		std::unique_lock lock( this->m_mtx );

		for ( auto& shot : this->m_pending_shots )
		{
			if ( !shot.resolved && !shot.server_shoot_position_confirmed )
			{
				shot.server_shoot_position = shoot_position;
				shot.server_shoot_position_confirmed = true;
				break;
			}
		}
	}

	void impacts::on_boom( std::uintptr_t victim_pawn, int hitgroup, float damage, float hitchance, float inaccuracy, float spread, const math::vector3& aim_angle, const math::vector3& shoot_position, int tick, const std::array<systems::bones::data, 27>& skeleton, bool forced, std::uint32_t deferred_weapon, int command_tick )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );

		std::unique_lock lock( this->m_mtx );

		const auto target_velocity = memory::read<math::vector3>( victim_pawn + SCHEMA( "C_BaseEntity", "m_vecVelocity"_hash ) );
		const auto current_tick = features::combat::g_shared.ctx( ).current_tick;
		const auto bt_ticks = ( tick > 0 && current_tick >= tick ) ? ( current_tick - tick ) : 0;

		shot_record shot
			{
				.victim_pawn = victim_pawn,
				.hitgroup = hitgroup,
				.damage = damage,
				.hitchance = hitchance,
				.predicted_inaccuracy = inaccuracy,
				.predicted_spread = spread,
				.server_inaccuracy = 0.0f,
				.aim_angle = aim_angle,
				.shoot_position = shoot_position,
				.tick = tick,
				.bt_ticks = bt_ticks,
				.time = current_time,
				.skeleton = skeleton,
				.resolved = false,
				.server_confirmed = false,
				.impact_confirmed = false,
				.target_velocity = target_velocity,
				.forced = forced,
				.weapon_type = features::combat::g_shared.ctx( ).weapon_type,
			};

		if ( deferred_weapon )
		{
			const auto weapon = systems::g_entities.lookup( deferred_weapon );
			if ( !weapon ) return;
			if ( this->m_revolver_weapon_handle != deferred_weapon )
			{
				this->m_prepared_revolver_shots.clear( );
				this->m_revolver_weapon_handle = deferred_weapon;
				this->m_revolver_observed_shot_time = memory::read<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_fLastShotTime"_hash ) );
			}
			const auto controller_handle = memory::read<std::uint32_t>( victim_pawn + SCHEMA( "C_BasePlayerPawn", "m_hController"_hash ) );
			const auto controller = systems::g_entities.lookup( controller_handle );
			if ( !controller ) return;
			const auto target_handle = memory::read<std::uint32_t>( controller + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			if ( systems::g_entities.lookup( target_handle ) != victim_pawn ) return;
			auto& prepared = this->m_prepared_revolver_shots;
			if ( !prepared.empty( ) && command_tick < prepared.back( ).command_tick ) prepared.clear( );
			if ( !prepared.empty( ) && command_tick == prepared.back( ).command_tick ) prepared.pop_back( );
			prepared.push_back( { shot, command_tick, target_handle } );
			if ( prepared.size( ) > 128 ) prepared.erase( prepared.begin( ) );
			return;
		}

		this->m_pending_shots.push_back( shot );
		if ( this->m_pending_shots.size( ) > 64 )
		{
			this->m_pending_shots.erase( this->m_pending_shots.begin( ) );
		}
	}

	void impacts::observe_revolver_shot( )
	{
		std::unique_lock lock( this->m_mtx );
		if ( !this->m_revolver_weapon_handle ) return;
		const auto weapon = systems::g_entities.lookup( this->m_revolver_weapon_handle );
		if ( !weapon )
		{
			this->m_prepared_revolver_shots.clear( );
			this->m_revolver_weapon_handle = 0;
			return;
		}
		const auto shot_time = memory::read<float>( weapon + SCHEMA( "C_CSWeaponBase", "m_fLastShotTime"_hash ) );
		if ( !std::isfinite( shot_time ) || shot_time <= this->m_revolver_observed_shot_time ) return;
		// A monotonic watermark prevents replay/correction from logging twice.
		this->m_revolver_observed_shot_time = shot_time;
		auto& prepared = this->m_prepared_revolver_shots;
		auto best = prepared.size( );
		auto best_delta = cstypes::tick_interval + 0.0001f;
		for ( auto i = std::size_t{}; i < prepared.size( ); ++i )
		{
			// Commands simulate tickbase + 1. Allow one tick of observation
			// boundary difference, but never attach an arbitrary old target.
			const auto command_time = ( static_cast<float>( prepared[i].command_tick ) + 1.0f ) * cstypes::tick_interval;
			const auto delta = std::fabs( shot_time - command_time );
			if ( delta < best_delta )
			{
				best_delta = delta;
				best = i;
			}
		}
		if ( best == prepared.size( ) )
		{
			if ( settings::g_misc.m_impacts.console_log.value )
				logging::console::print( xs( "[r8:unmatched] discharge at {:.6f}; no prepared command within one tick" ), shot_time );
			std::erase_if( prepared, [shot_time]( const auto& entry )
			{
				return static_cast<float>( entry.command_tick ) * cstypes::tick_interval <= shot_time;
			} );
			return;
		}
		const auto confirmed = prepared[best];
		prepared.erase( prepared.begin( ), prepared.begin( ) + best + 1 );
		if ( systems::g_entities.lookup( confirmed.target_handle ) != confirmed.shot.victim_pawn ) return;
		this->m_pending_shots.push_back( confirmed.shot );
		if ( this->m_pending_shots.size( ) > 64 ) this->m_pending_shots.erase( this->m_pending_shots.begin( ) );
		if ( settings::g_misc.m_impacts.console_log.value )
			logging::console::print( xs( "[rage] R8 discharge at {:.6f}, command {}, damage {:.0f}, hc {:.0f}%" ),
				shot_time, confirmed.command_tick, confirmed.shot.damage, confirmed.shot.hitchance * 100.0f );
		lock.unlock( );
		features::esp::player::g_chams.os( ).push( confirmed.shot.victim_pawn );
	}

	const char* impacts::classify_shot_deviation( const shot_record& shot ) const
	{
		// 1. Check if the victim is actually dead
		bool victim_dead = false;
		if ( shot.victim_pawn )
		{
			const auto life_state = memory::read<std::uint8_t>( shot.victim_pawn + SCHEMA( "C_BaseEntity", "m_lifeState"_hash ) );
			const auto health = memory::read<int>( shot.victim_pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) );
			if ( life_state != 0 || health <= 0 )
			{
				victim_dead = true;
			}
		}

		if ( victim_dead )
		{
			return "death";
		}

		// 2. If no impact on world was confirmed, it flew past due to weapon spread
		if ( !shot.impact_confirmed )
		{
			return "spread";
		}

		// 3. Prediction errors
		if ( shot.server_confirmed && std::fabsf( shot.server_inaccuracy - shot.predicted_inaccuracy ) > 0.003f )
		{
			return "prediction error";
		}

		if ( shot.server_shoot_position_confirmed && ( shot.server_shoot_position - shot.shoot_position ).length_sqr( ) > 1.0f )
		{
			return "prediction error";
		}

		const auto shoot_position = shot.server_shoot_position_confirmed ? shot.server_shoot_position : shot.shoot_position;

		math::vector3 ideal_forward{};
		math::helpers::angle_vectors_left( shot.aim_angle, &ideal_forward );

		const auto to_impact = shot.impact_position - shoot_position;
		const auto impact_dist = to_impact.length( );

		if ( impact_dist <= 0.1f )
		{
			return "occlusion";
		}

		const auto impact_dir = to_impact * ( 1.0f / impact_dist );
		const auto dot = ideal_forward.dot( impact_dir );
		const auto angular_deviation = std::acosf( std::clamp( dot, -1.0f, 1.0f ) );

		const auto inaccuracy = shot.server_confirmed ? shot.server_inaccuracy : shot.predicted_inaccuracy;
		const auto max_spread_angle = std::atanf( std::max( inaccuracy, 0.0f ) + std::max( shot.predicted_spread, 0.0f ) );
		const auto impact_mismatch_angle = std::max( max_spread_angle * 3.0f, math::helpers::deg_to_rad( 2.0f ) );

		const auto hitbox_dist = this->distance_to_nearest_hitbox( shot );
		const auto ray_dist = this->ray_distance_to_nearest_hitbox( shot, impact_dir );
		const auto ideal_ray_dist = this->ray_distance_to_nearest_hitbox( shot, ideal_forward );
		const auto target_dist = ( shot.skeleton[ 0 ].position - shoot_position ).length( );

		if ( angular_deviation > impact_mismatch_angle )
		{
			return "spread";
		}

		if ( ideal_ray_dist <= 1.0f && impact_dist < target_dist * 0.85f )
		{
			return "occlusion";
		}

		if ( ray_dist > 1.0f )
		{
			return "spread";
		}

		if ( impact_dist > target_dist * 1.05f )
		{
			return "resolver";
		}

		if ( hitbox_dist > 16.0f )
		{
			return "spread";
		}

		return "resolver";
	}

	impacts::hit_data impacts::parse_event( std::uintptr_t event )
	{
		const auto attacker_key = cstypes::event_hash{ 0, "attacker" };
		const auto userid_key = cstypes::event_hash{ 0, "userid" };

		const auto attacker = memory::call<std::uintptr_t>( PATTERN (patterns::game_event_get_controller), event, &attacker_key );
		const auto victim = memory::call<std::uintptr_t>( PATTERN (patterns::game_event_get_controller), event, &userid_key );

		const auto local = systems::g_local.get( );

		if ( attacker != local.controller || victim == local.controller )
		{
			return {};
		}

		const auto victim_pawn = memory::call<std::uintptr_t>(PATTERN (patterns::game_event_get_pawn), event, &userid_key );
		if ( !victim_pawn )
		{
			return {};
		}

		const auto victim_team = memory::read<int>( victim_pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
		if ( !local.is_this_other_team( victim_team ) )
		{
			return {};
		}

		const auto damage = memory::call<int>( PATTERN (patterns::game_event_get_int), event, "dmg_health", false );
		const auto hitgroup = memory::call<int>( PATTERN (patterns::game_event_get_int), event, "hitgroup", false );

		auto expected_hitgroup{ 0 };
		auto expected_damage{ 0.0f };
		auto was_aimbot{ false };
		auto weapon_type{ 0u };
		auto hitchance{ 0.0f };
		auto bt_ticks{ 0 };
		std::string mismatch_reason{};
		math::vector3 impact_pos{};

		{
			std::unique_lock lock( this->m_mtx );

			if ( !this->m_pending_hits.empty( ) )
			{
				impact_pos = this->m_pending_hits.back( ).position;
			}

			const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
			const auto current_time = global_vars ? memory::read<float>( global_vars + 0x30 ) : 0.0f;

			auto matched_shot = this->m_pending_shots.end( );
			for ( auto it = this->m_pending_shots.begin( ); it != this->m_pending_shots.end( ); ++it )
			{
				if ( it->victim_pawn != victim_pawn || it->resolved )
				{
					continue;
				}

				if ( it->impact_confirmed && ( current_time - it->impact_time ) > 0.12f )
				{
					continue;
				}

				matched_shot = it;
				break;
			}

			if ( matched_shot != this->m_pending_shots.end( ) )
			{
				expected_hitgroup = matched_shot->hitgroup;
				was_aimbot = true;
				weapon_type = matched_shot->weapon_type;
				expected_damage = matched_shot->damage;
				hitchance = matched_shot->hitchance;
				bt_ticks = matched_shot->bt_ticks;
				matched_shot->resolved = true;

				if ( expected_hitgroup > 0 && hitgroup != expected_hitgroup )
				{
					mismatch_reason = this->classify_shot_deviation( *matched_shot );
				}
			}
		}

		if ( !was_aimbot )
		{
			if ( local.pawn )
			{
				const auto weapon_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
				if ( weapon_services )
				{
					const auto weapon_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
					const auto weapon = weapon_handle ? systems::g_entities.lookup( weapon_handle ) : 0;

					if ( weapon )
					{
						const auto weapon_vdata = memory::read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 );
						if ( weapon_vdata )
						{
							weapon_type = memory::read<std::uint32_t>( weapon_vdata + SCHEMA( "CCSWeaponBaseVData", "m_WeaponType"_hash ) );
						}
					}
				}
			}
		}

		return
		{
			.victim = victim,
			.victim_pawn = victim_pawn,
			.damage = damage,
			.health = memory::call<int>( PATTERN (patterns::game_event_get_int), event, "health", false ),
			.hitgroup = hitgroup,
			.expected_hitgroup = expected_hitgroup,
			.was_aimbot = was_aimbot,
			.mismatch_reason = std::move( mismatch_reason ),
			.weapon_type = weapon_type,
			.expected_damage = expected_damage,
			.hitchance = hitchance,
			.bt_ticks = bt_ticks
		};
	}

	std::string impacts::get_player_name( std::uintptr_t controller )
	{
		const auto name_ptr = memory::read<std::uintptr_t>( controller + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) );
		if ( !name_ptr )
		{
			return "unknown";
		}

		auto name = memory::read_string( name_ptr, 64 );

		std::transform( name.begin( ), name.end( ), name.begin( ), ::tolower );

		return name;
	}

	std::string impacts::get_player_name_from_pawn( std::uintptr_t pawn )
	{
		const auto controller_handle = memory::read<std::uint32_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_hController"_hash ) );
		if ( !controller_handle )
		{
			return "unknown";
		}

		const auto controller = systems::g_entities.lookup( controller_handle );
		if ( !controller )
		{
			return "unknown";
		}

		return this->get_player_name( controller );
	}

	float impacts::distance_to_nearest_hitbox( const shot_record& shot ) const
	{
		if ( shot.impact_position.length_sqr( ) < 1.0f )
		{
			return -1.0f;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( shot.victim_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return -1.0f;
		}

		const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
		if ( hitbox_set.count <= 0 )
		{
			return -1.0f;
		}

		auto best_dist{ FLT_MAX };

		for ( const auto& entry : hitbox_set )
		{
			if ( entry.bone < 0 || entry.bone >= static_cast< int >( shot.skeleton.size( ) ) )
			{
				continue;
			}

			const auto& bone = shot.skeleton[ entry.bone ];
			if ( bone.position.length_sqr( ) < 1.0f )
			{
				continue;
			}

			auto dist{ FLT_MAX };
			if ( entry.radius > 0.001f )
			{
				const auto capsule_start = bone.rotation.rotate_vector( entry.mins ) + bone.position;
				const auto capsule_end = bone.rotation.rotate_vector( entry.maxs ) + bone.position;
				const auto segment = capsule_end - capsule_start;
				const auto segment_length_sq = segment.length_sqr( );
				const auto t = segment_length_sq > 0.001f
					? std::clamp( ( shot.impact_position - capsule_start ).dot( segment ) / segment_length_sq, 0.0f, 1.0f )
					: 0.0f;
				dist = ( shot.impact_position - ( capsule_start + segment * t ) ).length( ) - entry.radius;
			}
			else
			{
				auto inverse = bone.rotation;
				inverse.x = -inverse.x;
				inverse.y = -inverse.y;
				inverse.z = -inverse.z;
				const auto local = inverse.rotate_vector( shot.impact_position - bone.position );
				const math::vector3 closest
				{
					std::clamp( local.x, entry.mins.x, entry.maxs.x ),
					std::clamp( local.y, entry.mins.y, entry.maxs.y ),
					std::clamp( local.z, entry.mins.z, entry.maxs.z )
				};
				dist = ( local - closest ).length( );
			}

			if ( dist < best_dist )
			{
				best_dist = dist;
			}
		}

		return best_dist;
	}

	float impacts::ray_distance_to_nearest_hitbox( const shot_record& shot, const math::vector3& direction ) const
	{
		const auto game_scene_node = memory::read<std::uintptr_t>( shot.victim_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return FLT_MAX;
		}

		const auto hitbox_set = systems::g_hitboxes.query( game_scene_node );
		if ( hitbox_set.count <= 0 )
		{
			return FLT_MAX;
		}

		auto best_dist{ FLT_MAX };
		const auto shoot_position = shot.server_shoot_position_confirmed ? shot.server_shoot_position : shot.shoot_position;

		for ( const auto& entry : hitbox_set )
		{
			if ( entry.bone < 0 || entry.bone >= static_cast< int >( shot.skeleton.size( ) ) )
			{
				continue;
			}

			const auto& bone = shot.skeleton[ entry.bone ];
			if ( bone.position.length_sqr( ) < 1.0f )
			{
				continue;
			}

			auto dist{ FLT_MAX };
			if ( entry.radius > 0.001f )
			{
				const auto capsule_start = bone.rotation.rotate_vector( entry.mins ) + bone.position;
				const auto capsule_end = bone.rotation.rotate_vector( entry.maxs ) + bone.position;
				const auto segment = capsule_end - capsule_start;
				const auto to_start = capsule_start - shoot_position;
				const auto perpendicular_segment = segment - direction * segment.dot( direction );
				const auto perpendicular_start = to_start - direction * to_start.dot( direction );
				const auto perpendicular_length_sq = perpendicular_segment.length_sqr( );
				auto segment_t = perpendicular_length_sq > 1.0e-6f
					? std::clamp( -perpendicular_start.dot( perpendicular_segment ) / perpendicular_length_sq, 0.0f, 1.0f )
					: 0.0f;
				auto segment_point = capsule_start + segment * segment_t;
				auto ray_t = ( segment_point - shoot_position ).dot( direction );

				if ( ray_t < 0.0f )
				{
					const auto segment_length_sq = segment.length_sqr( );
					segment_t = segment_length_sq > 1.0e-6f
						? std::clamp( ( shoot_position - capsule_start ).dot( segment ) / segment_length_sq, 0.0f, 1.0f )
						: 0.0f;
					segment_point = capsule_start + segment * segment_t;
					ray_t = 0.0f;
				}

				const auto ray_point = shoot_position + direction * ray_t;
				dist = ( segment_point - ray_point ).length( ) - entry.radius;
			}
			else
			{
				auto inverse = bone.rotation;
				inverse.x = -inverse.x;
				inverse.y = -inverse.y;
				inverse.z = -inverse.z;
				const auto local_origin = inverse.rotate_vector( shoot_position - bone.position );
				const auto local_direction = inverse.rotate_vector( direction );
				auto entry_t{ 0.0f };
				auto exit_t{ FLT_MAX };

				const auto intersect_axis = [ & ]( float origin, float delta, float minimum, float maximum )
				{
					if ( std::fabs( delta ) < 1.0e-8f )
					{
						return origin >= minimum && origin <= maximum;
					}

					auto first = ( minimum - origin ) / delta;
					auto second = ( maximum - origin ) / delta;
					if ( first > second ) std::swap( first, second );
					entry_t = std::max( entry_t, first );
					exit_t = std::min( exit_t, second );
					return entry_t <= exit_t;
				};

				if ( intersect_axis( local_origin.x, local_direction.x, entry.mins.x, entry.maxs.x ) &&
					intersect_axis( local_origin.y, local_direction.y, entry.mins.y, entry.maxs.y ) &&
					intersect_axis( local_origin.z, local_direction.z, entry.mins.z, entry.maxs.z ) )
				{
					dist = 0.0f;
				}
				else
				{
					const auto center = ( entry.mins + entry.maxs ) * 0.5f;
					const auto extents = ( entry.maxs - entry.mins ) * 0.5f;
					const auto to_center = center - local_origin;
					const auto projection = std::max( to_center.dot( local_direction ), 0.0f );
					dist = std::max( ( to_center - local_direction * projection ).length( ) - extents.length( ), 0.0f );
				}
			}

			if ( dist < best_dist )
			{
				best_dist = dist;
			}
		}

		return best_dist;
	}

	void impacts::add_hit_log( const hit_data& data )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto& cfg = settings::g_misc.m_impacts;

		std::unique_lock lock( this->m_mtx );

		log entry{};
		entry.name = this->get_player_name( data.victim );
		entry.damage = data.damage;
		entry.health = data.health;
		entry.time = current_time;
		entry.offset.set_stiffness( 180.0f );
		entry.offset.set_damping( 16.0f );
		entry.offset.snap( -150.0f );
		entry.offset.set_target( 0.0f );
		entry.alpha.fade_in( 0.15f );
		entry.snapped = false;
		entry.duration = cfg.hit_log_duration;
		entry.hitgroup = systems::g_hitboxes.hitgroup_to_name( data.hitgroup );
		entry.weapon_type = data.weapon_type;
		entry.hitchance = data.hitchance;
		entry.bt_ticks = data.bt_ticks;

		if ( data.was_aimbot && !data.mismatch_reason.empty( ) )
		{
			const auto expected_name = systems::g_hitboxes.hitgroup_to_name( data.expected_hitgroup );
			entry.reason = std::format( "{} was expected ({})", expected_name, data.mismatch_reason );
		}
		else if ( data.was_aimbot && data.health > 0 && data.expected_damage > 0.0f && static_cast< float >( data.damage ) < data.expected_damage )
		{
			entry.reason = std::format( "expected {:.0f} damage, dealt {}", data.expected_damage, data.damage );
		}

		if ( cfg.console_log.value || cfg.chat_log.value )
		{
			const auto group = data.weapon_type == cstypes::weapon_type::knife ? "knife" :
				( data.weapon_type == cstypes::weapon_type::taser ? "zeus" :
				( entry.hitgroup.empty( ) ? "body" : entry.hitgroup.c_str( ) ) );
			const auto target_name = entry.name.empty( ) || entry.name == "unknown" ? "enemy" : entry.name.c_str( );
			const auto health_str = entry.health <= 0 ? "fatal" : std::format( "{} hp rem", entry.health );

			const auto log_text = std::format( "hit {} in {} for {} dmg ({})", target_name, group, entry.damage, health_str );

			if ( cfg.console_log.value )
			{
				logging::console::print( xs( "[mintaly] {}" ), log_text );
			}

			if ( cfg.chat_log.value )
			{
				detail::print_mintaly_chat( detail::k_hit_color_hex, log_text );
			}
		}

		if ( cfg.hit_log.value )
		{
			this->m_logs.insert( this->m_logs.begin( ), std::move( entry ) );

			if ( this->m_logs.size( ) > 5 )
			{
				this->m_logs.pop_back( );
			}
		}

	}

	void impacts::add_miss_log( const shot_record& shot, const char* reason )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto& cfg = settings::g_misc.m_impacts;

		const auto raw_name = this->get_player_name_from_pawn( shot.victim_pawn );
		const auto name = ( raw_name.empty( ) || raw_name == "unknown" ) ? "enemy" : raw_name;
		const char* raw_group = systems::g_hitboxes.hitgroup_to_name( shot.hitgroup );
		const char* group = ( !raw_group || !*raw_group ) ? "body" : raw_group;
		const auto bt_ticks = shot.bt_ticks;
		const auto hc = static_cast< int >( std::round( shot.hitchance * 100.0f ) );

		const char* reason_str = reason;
		if ( !reason_str || !*reason_str )
		{
			reason_str = "unknown";
		}

		const auto log_text = std::format(
			"missed {} ({}) due to {} (hc: {}%, bt: {}t)",
			name, group, reason_str, hc, bt_ticks
		);

		if ( cfg.console_log.value )
		{
			logging::console::print( xs( "[mintaly] {}" ), log_text );
		}

		if ( cfg.chat_log.value )
		{
			detail::print_mintaly_chat( detail::k_miss_color_hex, log_text );
		}

		if ( !cfg.miss_log.value )
		{
			return;
		}

		std::unique_lock lock( this->m_mtx );

		log entry{};
		entry.name = name;
		entry.reason = reason_str;
		entry.hitgroup = group;
		entry.hitchance = shot.hitchance;
		entry.bt_ticks = bt_ticks;

		entry.damage = 0;
		entry.health = -1;
		entry.time = current_time;
		entry.offset.set_stiffness( 180.0f );
		entry.offset.set_damping( 16.0f );
		entry.offset.snap( -150.0f );
		entry.offset.set_target( 0.0f );
		entry.alpha.fade_in( 0.15f );
		entry.snapped = false;
		entry.is_miss = true;
		entry.duration = settings::g_misc.m_impacts.miss_log_duration;
		entry.weapon_type = shot.weapon_type;

		this->m_logs.insert( this->m_logs.begin( ), std::move( entry ) );

		if ( this->m_logs.size( ) > 5 )
		{
			this->m_logs.pop_back( );
		}

	}

	void impacts::check_misses( )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		if ( !global_vars )
		{
			return;
		}
		const auto current_time = memory::read<float>( global_vars + 0x30 );

		const auto& cfg = settings::g_misc.m_impacts;

		std::vector<std::pair<shot_record, const char*>> misses_to_log;

		{
			std::unique_lock lock( this->m_mtx );

			for ( auto it = this->m_pending_shots.begin( ); it != this->m_pending_shots.end( ); )
			{
				if ( it->resolved )
				{
					it = this->m_pending_shots.erase( it );
					continue;
				}

				const auto elapsed = std::max( 0.0f, current_time - it->time );
				const auto impact_elapsed = it->impact_confirmed ? std::max( 0.0f, current_time - it->impact_time ) : 0.0f;
				const auto is_stale = it->impact_confirmed && impact_elapsed > 0.12f;
				const auto is_expired = elapsed > 0.35f;

				if ( is_stale || is_expired )
				{
					it->resolved = true;

					if ( cfg.miss_log.value || cfg.console_log.value || cfg.chat_log.value )
					{
						const char* reason = this->classify_shot_deviation( *it );
						misses_to_log.emplace_back( *it, reason );
					}

					it = this->m_pending_shots.erase( it );
					continue;
				}

				++it;
			}
		}

		for ( const auto& [shot, reason] : misses_to_log )
		{
			this->add_miss_log( shot, reason );
		}
	}

	void impacts::render_hit_markers( xdraw::draw_list& draw_list, float time )
	{
		const auto& cfg = settings::g_misc.m_impacts;

		std::unique_lock lock( this->m_mtx );

		for ( auto it = this->m_hitmarkers.begin( ); it != this->m_hitmarkers.end( ); )
		{
			const auto duration = cfg.hit_marker_duration;
			const auto elapsed = time - it->time;

			if ( elapsed > duration )
			{
				it = this->m_hitmarkers.erase( it );
				continue;
			}

			const auto screen = systems::g_view.project( it->position );
			const auto progress = elapsed / duration;

			const auto ease_out = 1.0f - ( progress * progress );
			const auto alpha = static_cast< std::uint8_t >( ease_out * 255.0f );
			const auto color = xdraw::color( cfg.hit_marker_color.value.r, cfg.hit_marker_color.value.g, cfg.hit_marker_color.value.b, alpha );

			const auto x = screen.x, y = screen.y;

			const auto show_classic = cfg.hit_marker_type == settings::misc::impacts::marker_type::classic || cfg.hit_marker_type == settings::misc::impacts::marker_type::both;
			const auto show_damage = cfg.hit_marker_type == settings::misc::impacts::marker_type::damage || cfg.hit_marker_type == settings::misc::impacts::marker_type::both;

			auto size{ 0.0f };
			auto gap{ 0.0f };

			if ( show_classic )
			{
				const auto expand_progress = std::min( elapsed * 20.0f, 1.0f );
				const auto ease_expand = 1.0f - std::pow( 1.0f - expand_progress, 4.0f );

				const auto base_size{ 10.0f };
				const auto base_gap{ 3.0f };
				const auto expand_amount = 6.0f * ( 1.0f - ease_expand );

				size = base_size + expand_amount;
				gap = base_gap + expand_amount * 0.3f;
			}

			constexpr auto thickness{ 1.25f };

			auto draw_arm = [ & ]( xdraw::draw_list& target, float x1, float y1, float x2, float y2, xdraw::color col, float thick )
				{
					const auto dx = x2 - x1;
					const auto dy = y2 - y1;
					const auto len = std::sqrt( dx * dx + dy * dy );

					if ( len < 0.001f )
					{
						return;
					}

					const auto col_clear = xdraw::color{ col.r, col.g, col.b, 0 };
					const auto mx = ( x1 + x2 ) * 0.5f;
					const auto my = ( y1 + y2 ) * 0.5f;

					const float pts[ ]{ x1, y1, mx, my, x2, y2 };
					const xdraw::color cols[ ]{ col_clear, col, col };

					target.polyline_gradient( pts, cols, false, thick );
				};

			auto damage_text = std::string{};
			auto draw_x{ 0.0f };
			auto draw_y{ 0.0f };

			if ( show_damage )
			{
				const auto base_offset = show_classic ? 20.0f : 0.0f;

				damage_text = std::to_string( it->damage );
				const auto [text_w, text_h] = xdraw::measure_text( damage_text );

				const auto text_x = x - text_w * 0.5f;
				const auto text_y = y - base_offset - text_h * 0.5f;

				const auto shake_progress = std::max( 0.0f, 1.0f - elapsed * 8.0f );
				const auto shake_x = std::sin( elapsed * 50.0f ) * shake_progress * 3.0f;
				const auto shake_y = std::cos( elapsed * 45.0f ) * shake_progress * 2.0f;

				draw_x = text_x + shake_x;
				draw_y = text_y + shake_y;
			}

			if ( cfg.hit_marker_glow && alpha > 0 )
			{
				auto& glow = xdraw::get_glow( );
				const auto glow_a = static_cast< std::uint8_t >( static_cast< float >( alpha ) * cfg.hit_marker_glow_strength );
				const auto glow_col = xdraw::color{ cfg.hit_marker_color.value.r, cfg.hit_marker_color.value.g, cfg.hit_marker_color.value.b, glow_a };

				if ( show_classic )
				{
					for ( auto t = 0; t < 3; ++t )
					{
						const auto glow_thick = thickness + 1.0f + static_cast< float >( t ) * 2.0f;

						draw_arm( glow, x - size, y - size, x - gap, y - gap, glow_col, glow_thick );
						draw_arm( glow, x + size, y - size, x + gap, y - gap, glow_col, glow_thick );
						draw_arm( glow, x - size, y + size, x - gap, y + gap, glow_col, glow_thick );
						draw_arm( glow, x + size, y + size, x + gap, y + gap, glow_col, glow_thick );
					}
				}

				if ( show_damage )
				{
					glow.text( draw_x, draw_y, damage_text, glow_col );
				}
			}

			if ( show_classic )
			{
				draw_arm( draw_list, x - size, y - size, x - gap, y - gap, color, thickness );
				draw_arm( draw_list, x + size, y - size, x + gap, y - gap, color, thickness );
				draw_arm( draw_list, x - size, y + size, x - gap, y + gap, color, thickness );
				draw_arm( draw_list, x + size, y + size, x + gap, y + gap, color, thickness );
			}

			if ( show_damage )
			{
				draw_list.text( draw_x, draw_y, damage_text, color );
			}

			++it;
		}
	}

	void impacts::render_logs( xdraw::draw_list& draw_list, float time )
	{
		std::unique_lock lock( this->m_mtx );
		const auto& cfg = settings::g_misc.m_impacts;
		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto width = std::min( 340.0f, std::max( 0.0f, screen_w - 32.0f ) );
		auto y = 16.0f;

		xdraw::push_font( rendering::g_fonts.inter_medium[ rendering::fonts::size::petite ] );

		for ( auto it = this->m_logs.begin( ); it != this->m_logs.end( ); )
		{
			const auto enabled = it->is_miss ? cfg.miss_log.value : cfg.hit_log.value;
			const auto elapsed = time - it->time;
			const auto duration = std::isfinite( it->duration ) ? std::max( it->duration, 0.1f ) : 3.5f;
			if ( !enabled || !std::isfinite( elapsed ) || elapsed < -1.0f || elapsed >= duration )
			{
				it = this->m_logs.erase( it );
				continue;
			}

			const auto safe_elapsed = std::max( 0.0f, elapsed );
			it->alpha.update( );
			it->offset.update( );

			const auto fade_window = std::min( 0.45f, duration * 0.2f );
			const auto fade_out = std::clamp( ( duration - safe_elapsed ) / fade_window, 0.0f, 1.0f );
			const auto alpha = std::clamp( it->alpha.alpha( ) * fade_out, 0.0f, 1.0f );

			constexpr auto height = 28.0f;
			if ( width < 140.0f || y + height > screen_h ) { ++it; continue; }

			const auto slide_x = it->offset.value( );
			const auto x = 16.0f + slide_x * ( 1.0f - alpha * 0.5f ) - ( 1.0f - fade_out ) * 14.0f;

			const auto tint = [alpha]( xdraw::color color ) {
				return color.alpha( static_cast<std::uint8_t>( color.a * alpha ) );
			};

			const bool is_miss = it->is_miss;
			const bool is_kill = !is_miss && it->health == 0;

			// Hit / Miss specific colors for the text
			// Miss: pink-red (#FF5C80)
			// Hit:  mint green (#4ADE80) / emerald (#34D399) on kill
			const auto log_color = is_miss
				? xdraw::color{ 255, 92, 128 }
				: ( is_kill ? xdraw::color{ 52, 211, 153 } : xdraw::color{ 74, 222, 128 } );

			// Left badge: [MISS], [KILL], [HIT]
			const auto badge = is_miss ? "MISS" : ( is_kill ? "KILL" : "HIT" );
			const auto [badge_tw, badge_th] = xdraw::measure_text( badge );
			const auto badge_w = badge_tw + 12.0f;
			const auto badge_h = 17.0f;
			const auto badge_y = y + ( height - badge_h ) * 0.5f;

			// Right pill:
			// For miss: "{hc}% HC" (or "{reason}" if hc <= 0)
			// For kill: "FATAL"
			// For hit:  "{health} HP"
			std::string right_label;
			if ( is_miss )
			{
				const auto hc = static_cast< int >( std::round( it->hitchance * 100.0f ) );
				if ( hc > 0 )
				{
					right_label = std::format( "{}% HC", hc );
				}
				else
				{
					right_label = it->reason.empty( ) ? "MISSED" : it->reason;
				}
			}
			else if ( is_kill )
			{
				right_label = "FATAL";
			}
			else
			{
				right_label = std::format( "{} HP", it->health );
			}

			const auto [rw, rh] = xdraw::measure_text( right_label );
			const auto right_pill_w = rw + 10.0f;
			const auto right_pill_h = 17.0f;
			const auto right_pill_x = x + width - 10.0f - right_pill_w;
			const auto right_pill_y = y + ( height - right_pill_h ) * 0.5f;

			// Single line text:
			// Miss: "{name} ({hitgroup}) - {reason}"
			// Hit:  "{name} in {hitgroup} [-{damage}]"
			std::string text;
			const auto target_name = ( it->name.empty( ) || it->name == "unknown" ) ? "enemy" : it->name.c_str( );
			if ( is_miss )
			{
				const auto group = it->hitgroup.empty( ) ? "body" : it->hitgroup.c_str( );
				const auto r_str = it->reason.empty( ) ? "unknown" : it->reason.c_str( );
				text = std::format( "{} ({}) - {}", target_name, group, r_str );
			}
			else
			{
				const auto group = it->weapon_type == cstypes::weapon_type::knife ? "knife" :
					( it->weapon_type == cstypes::weapon_type::taser ? "zeus" :
					( it->hitgroup.empty( ) ? "body" : it->hitgroup.c_str( ) ) );
				text = std::format( "{} in {} [-{}]", target_name, group, it->damage );
			}

			const auto title_x = x + 10.0f + badge_w + 8.0f;
			const auto max_title_w = std::max( 0.0f, right_pill_x - 8.0f - title_x );
			const auto title = rendering::theme::fit_text( text, max_title_w );
			const auto title_h = xdraw::measure_text( title ).second;

			// Card styling — clean dark translucent glass (same as menu)
			// 1. Soft drop shadow
			draw_list.rect_filled( x, y + 2.0f, width, height, tint( { 0, 0, 0, 60 } ), xdraw::corner_radius{ 6.0f } );

			// 2. Frosted glass blur
			draw_list.rect_filled_blurred( x, y, width, height, xdraw::corner_radius{ 6.0f }, tint( { 255, 255, 255, 210 } ) );

			// 3. Dark card body (tokens::col_card — pitch dark translucent, NOT grey)
			draw_list.rect_filled( x, y, width, height, tint( tokens::col_card.alpha( 242 ) ), xdraw::corner_radius{ 6.0f } );

			// 4. Subtle border
			const auto card_border = xui::lerp( tokens::col_border, tokens::col_accent, 0.15f );
			draw_list.rect( x, y, width, height, tint( card_border.alpha( 180 ) ), xdraw::corner_radius{ 6.0f } );

			// 5. Thin accent strip on the left edge
			draw_list.rect_filled( x + 3.0f, y + 5.0f, 3.0f, height - 10.0f, tint( tokens::col_accent.alpha( 40 ) ), xdraw::corner_radius{ 1.5f } );
			draw_list.rect_filled( x + 3.5f, y + 6.0f, 2.0f, height - 12.0f, tint( tokens::col_accent ), xdraw::corner_radius{ 1.0f } );

			// Left badge [HIT] / [KILL] / [MISS]
			draw_list.rect_filled( x + 10.0f, badge_y, badge_w, badge_h, tint( tokens::col_elevated ), xdraw::corner_radius{ 4.0f } );
			draw_list.rect( x + 10.0f, badge_y, badge_w, badge_h, tint( tokens::col_border.alpha( 160 ) ), xdraw::corner_radius{ 4.0f } );
			draw_list.text( x + 10.0f + ( badge_w - badge_tw ) * 0.5f, badge_y + ( badge_h - badge_th ) * 0.5f, badge, tint( log_color ) );

			// Right pill (MISSED / FATAL / victim name / -XX HP)
			if ( !right_label.empty( ) )
			{
				draw_list.rect_filled( right_pill_x, right_pill_y, right_pill_w, right_pill_h, tint( tokens::col_elevated ), xdraw::corner_radius{ 4.0f } );
				draw_list.rect( right_pill_x, right_pill_y, right_pill_w, right_pill_h, tint( tokens::col_border.alpha( 160 ) ), xdraw::corner_radius{ 4.0f } );
				draw_list.text( right_pill_x + ( right_pill_w - rw ) * 0.5f, right_pill_y + ( right_pill_h - rh ) * 0.5f, right_label, tint( log_color ) );
			}

			// Main single-line text (hit to <group> [<damage>] / miss to <reason>) in hit/miss color
			draw_list.text( title_x, y + ( height - title_h ) * 0.5f, title, tint( log_color ) );

			y += height + 6.0f;
			++it;
		}

		xdraw::pop_font( );
	}

	void impacts::render_hit_effect( xdraw::draw_list& draw_list, float time )
	{
		const auto& cfg = settings::g_misc.m_impacts;

		if ( !cfg.hit_effect.value || this->m_hit_effect_time <= 0.0f )
		{
			return;
		}

		const auto elapsed = time - this->m_hit_effect_time;
		const auto duration = cfg.hit_effect_duration;

		if ( elapsed > duration )
		{
			this->m_hit_effect_time = 0.0f;
			return;
		}

		const auto progress = elapsed / duration;
		const auto fade_in = std::min( elapsed * 15.0f, 1.0f );
		const auto fade_in_smooth = 1.0f - std::pow( 1.0f - fade_in, 3.0f );
		const auto fade_out = 1.0f - std::pow( progress, 0.6f );
		const auto intensity = fade_in_smooth * fade_out * ( cfg.hit_effect_strength / 100.0f );

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto sw = static_cast< float >( screen_w );
		const auto sh = static_cast< float >( screen_h );

		const auto r = cfg.hit_effect_color.value.r;
		const auto g = cfg.hit_effect_color.value.g;
		const auto b = cfg.hit_effect_color.value.b;

		constexpr auto band_count{ 32 };

		for ( auto i = 0; i < band_count; ++i )
		{
			const auto t = static_cast< float >( i ) / static_cast< float >( band_count - 1 );
			const auto size = 0.01f + t * 0.35f;
			const auto falloff = std::pow( 1.0f - t, 2.5f );
			const auto a = static_cast< std::uint8_t >( std::min( intensity * falloff * 280.0f, 255.0f ) );

			if ( a == 0 )
			{
				continue;
			}

			const auto edge = xdraw::color{ r, g, b, a };
			const auto clear = xdraw::color{ r, g, b, 0 };

			const auto tx = sw * size;
			const auto ty = sh * size;

			draw_list.rect_filled_gradient( 0.0f, 0.0f, tx, sh, edge, clear, clear, edge );
			draw_list.rect_filled_gradient( sw - tx, 0.0f, tx, sh, clear, edge, edge, clear );
			draw_list.rect_filled_gradient( 0.0f, 0.0f, sw, ty, edge, edge, clear, clear );
			draw_list.rect_filled_gradient( 0.0f, sh - ty, sw, ty, clear, clear, edge, edge );
		}
	}

	void impacts::render_bullet_impact_overlays( xdraw::draw_list& draw_list, float time )
	{
		const auto& cfg = settings::g_misc.m_impacts;

		if ( !cfg.bullet_impact_effect.value )
		{
			return;
		}

		const auto type = cfg.bullet_impact_effect_type.value;
		if ( type == settings::misc::impacts::bullet_impact_type::sparks )
		{
			return;
		}

		std::unique_lock lock( this->m_mtx );

		const auto duration = cfg.bullet_impact_effect_duration.value;
		constexpr auto half_size{ 1.75f };

		for ( auto it = this->m_bullet_impacts.begin( ); it != this->m_bullet_impacts.end( ); )
		{
			const auto elapsed = time - it->time;

			if ( elapsed > duration )
			{
				it = this->m_bullet_impacts.erase( it );
				continue;
			}

			const auto progress = elapsed / duration;
			const auto fade = 1.0f - ( progress * progress );
			const auto alpha = static_cast< std::uint8_t >( fade * 255.0f );

			if ( alpha == 0 )
			{
				++it;
				continue;
			}

			const math::vector3 corners[ 8 ]
			{
				it->position + math::vector3{ -half_size, -half_size, -half_size },
				it->position + math::vector3{  half_size, -half_size, -half_size },
				it->position + math::vector3{  half_size,  half_size, -half_size },
				it->position + math::vector3{ -half_size,  half_size, -half_size },
				it->position + math::vector3{ -half_size, -half_size,  half_size },
				it->position + math::vector3{  half_size, -half_size,  half_size },
				it->position + math::vector3{  half_size,  half_size,  half_size },
				it->position + math::vector3{ -half_size,  half_size,  half_size },
			};

			float sx[ 8 ]{}, sy[ 8 ]{};
			auto all_valid{ true };

			for ( auto i = 0; i < 8; ++i )
			{
				const auto proj = systems::g_view.project( corners[ i ] );
				if ( !systems::g_view.projection_valid( proj ) )
				{
					all_valid = false;
					break;
				}

				sx[ i ] = proj.x;
				sy[ i ] = proj.y;
			}

			if ( !all_valid )
			{
				++it;
				continue;
			}

			const auto scale_alpha = [ alpha ]( xdraw::color c ) -> xdraw::color { return xdraw::color{ c.r, c.g, c.b, static_cast< std::uint8_t >( ( c.a / 255.0f ) * ( alpha / 255.0f ) * 255.0f ) }; };
			const auto fill_col = scale_alpha( cfg.bullet_impact_effect_fill_color.value );
			const auto edge_col = scale_alpha( cfg.bullet_impact_effect_edge_color.value );

			constexpr int faces[ 6 ][ 4 ]
			{
				{ 0, 3, 2, 1 },
				{ 4, 5, 6, 7 },
				{ 0, 1, 5, 4 },
				{ 2, 3, 7, 6 },
				{ 0, 4, 7, 3 },
				{ 1, 2, 6, 5 },
			};

			for ( const auto& f : faces )
			{
				float poly[ 8 ]
				{
					sx[ f[ 0 ] ], sy[ f[ 0 ] ],
					sx[ f[ 1 ] ], sy[ f[ 1 ] ],
					sx[ f[ 2 ] ], sy[ f[ 2 ] ],
					sx[ f[ 3 ] ], sy[ f[ 3 ] ],
				};

				draw_list.convex_filled( { poly, 8 }, fill_col );
			}

			constexpr std::pair<int, int> edges[ 12 ]
			{
				{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
				{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
				{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
			};

			if ( cfg.bullet_impact_effect_glow )
			{
				auto& glow = xdraw::get_glow( );

				const auto edge_glow_a = static_cast< std::uint8_t >( static_cast< float >( edge_col.a ) * cfg.bullet_impact_effect_glow_strength );
				const auto edge_glow_col = xdraw::color{ edge_col.r, edge_col.g, edge_col.b, edge_glow_a };

				const auto fill_glow_a = static_cast< std::uint8_t >( static_cast< float >( fill_col.a ) * cfg.bullet_impact_effect_glow_strength );
				const auto fill_glow_col = xdraw::color{ fill_col.r, fill_col.g, fill_col.b, fill_glow_a };

				for ( const auto& f : faces )
				{
					float poly[ 8 ]
					{
						sx[ f[ 0 ] ], sy[ f[ 0 ] ],
						sx[ f[ 1 ] ], sy[ f[ 1 ] ],
						sx[ f[ 2 ] ], sy[ f[ 2 ] ],
						sx[ f[ 3 ] ], sy[ f[ 3 ] ],
					};

					glow.convex_filled( { poly, 8 }, fill_glow_col );
				}

				for ( const auto& [a, b] : edges )
				{
					const float line[ 4 ]{ sx[ a ], sy[ a ], sx[ b ], sy[ b ] };
					glow.polyline( { line, 4 }, edge_glow_col, false, 1.0f );
				}
			}

			for ( const auto& [a, b] : edges )
			{
				const float line[ 4 ]{ sx[ a ], sy[ a ], sx[ b ], sy[ b ] };
				draw_list.polyline( { line, 4 }, edge_col, false, 1.0f );
			}

			++it;
		}
	}

	namespace custom_sound_detail {

		inline void ensure_directories( )
		{
			std::error_code ec{};
			std::filesystem::create_directories( L"C:\\mintaly\\sounds", ec );
		}

		[[nodiscard]] std::wstring sounds_directory( )
		{
			ensure_directories( );
			return L"C:\\mintaly";
		}

		[[nodiscard]] std::string sanitize_filename( std::string_view name )
		{
			std::string out{};
			out.reserve( name.size( ) );

			for ( const auto c : name )
			{
				if ( std::isalnum( static_cast< unsigned char >( c ) ) || c == '_' || c == '-' || c == '.' )
				{
					out.push_back( static_cast< char >( c ) );
				}
			}

			return out;
		}

		[[nodiscard]] bool has_extension( std::string_view name, std::string_view ext )
		{
			if ( name.size( ) < ext.size( ) )
			{
				return false;
			}

			return _strnicmp( name.data( ) + name.size( ) - ext.size( ), ext.data( ), ext.size( ) ) == 0;
		}

void play_engine_path( const char* sound_path, float volume )
		{
			struct
			{
				char padding[ 1080 ];
				int argc;
				const char** argv;
			} args{};

			char volume_str[ 16 ];
			std::snprintf( volume_str, sizeof( volume_str ), "%.2f", volume / 100.0f );

			const char* sound_args[ ]{ "playvol", sound_path, volume_str };
			args.argc = 3;
			args.argv = sound_args;

			memory::call<void>( PATTERN (patterns::play_sound), 0.0f, &args );
		}

		void play_wav_direct( const std::wstring& path, float volume )
		{
			using PlaySoundW_t = BOOL( WINAPI* )( LPCWSTR, HMODULE, DWORD );
			using waveOutSetVolume_t = UINT( WINAPI* )( UINT_PTR, DWORD );

			static const auto winmm = []() -> HMODULE {
				HMODULE mod = GetModuleHandleW( L"winmm.dll" );
				return mod ? mod : LoadLibraryW( L"winmm.dll" );
			}();

			if ( !winmm )
			{
				return;
			}

			static const auto play_fn = reinterpret_cast<PlaySoundW_t>( GetProcAddress( winmm, "PlaySoundW" ) );
			if ( !play_fn )
			{
				return;
			}

			static const auto set_vol_fn = reinterpret_cast<waveOutSetVolume_t>( GetProcAddress( winmm, "waveOutSetVolume" ) );
			if ( set_vol_fn )
			{
				const auto level = static_cast<WORD>( std::clamp( volume / 100.0f, 0.0f, 1.0f ) * 0xFFFFu );
				const DWORD vol = static_cast<DWORD>( level ) | ( static_cast<DWORD>( level ) << 16 );
				set_vol_fn( static_cast<UINT_PTR>( static_cast<UINT>( -1 ) ), vol ); // WAVE_MAPPER
			}

			// SND_FILENAME(0x20000) | SND_ASYNC(0x1) | SND_NODEFAULT(0x2)
			play_fn( path.c_str( ), nullptr, 0x00020003u );
		}

		void play_wav_memory( const void* data, float volume )
		{
			using PlaySoundW_t = BOOL( WINAPI* )( LPCWSTR, HMODULE, DWORD );
			using waveOutSetVolume_t = UINT( WINAPI* )( UINT_PTR, DWORD );

			static const auto winmm = []() -> HMODULE {
				HMODULE mod = GetModuleHandleW( L"winmm.dll" );
				return mod ? mod : LoadLibraryW( L"winmm.dll" );
			}();

			if ( !winmm || !data )
			{
				return;
			}

			static const auto play_fn = reinterpret_cast<PlaySoundW_t>( GetProcAddress( winmm, "PlaySoundW" ) );
			if ( !play_fn )
			{
				return;
			}

			static const auto set_vol_fn = reinterpret_cast<waveOutSetVolume_t>( GetProcAddress( winmm, "waveOutSetVolume" ) );
			if ( set_vol_fn )
			{
				const auto level = static_cast<WORD>( std::clamp( volume / 100.0f, 0.0f, 1.0f ) * 0xFFFFu );
				const DWORD vol = static_cast<DWORD>( level ) | ( static_cast<DWORD>( level ) << 16 );
				set_vol_fn( static_cast<UINT_PTR>( static_cast<UINT>( -1 ) ), vol ); // WAVE_MAPPER
			}

			// SND_MEMORY(0x4) | SND_ASYNC(0x1) | SND_NODEFAULT(0x2)
			play_fn( reinterpret_cast<LPCWSTR>( data ), nullptr, 0x00000007u );
		}

		[[nodiscard]] std::wstring resolve_sound_path( std::string_view filename )
		{
			const auto sanitized = sanitize_filename( filename );
			if ( sanitized.empty( ) )
			{
				return {};
			}

			// Ensure directories exist
			ensure_directories( );

			const auto wide_name = std::wstring( sanitized.begin( ), sanitized.end( ) );

			// Check C:\mintaly\<filename>
			const auto root_path = std::wstring( L"C:\\mintaly\\" ) + wide_name;
			if ( std::filesystem::exists( root_path ) )
			{
				return root_path;
			}

			// Check C:\mintaly\sounds\<filename>
			const auto sounds_path = std::wstring( L"C:\\mintaly\\sounds\\" ) + wide_name;
			if ( std::filesystem::exists( sounds_path ) )
			{
				return sounds_path;
			}

			return {};
		}


} // namespace custom_sound_detail

	std::string impacts::custom_sounds_directory_narrow( )
	{
		return "C:\\mintaly";
	}

	std::vector<std::string> impacts::list_custom_sounds( )
	{
		std::vector<std::string> files{};

		const auto scan_dir = [&]( const std::wstring& dir_path ) {
			std::error_code ec{};
			if ( !std::filesystem::exists( dir_path, ec ) || ec )
			{
				return;
			}

			for ( const auto& entry : std::filesystem::directory_iterator( dir_path, ec ) )
			{
				if ( ec || !entry.is_regular_file( ) )
				{
					continue;
				}

				const auto filename = entry.path( ).filename( ).string( );
				if ( filename.empty( ) || !custom_sound_detail::has_extension( filename, ".wav" ) )
				{
					continue;
				}

				if ( std::find( files.begin( ), files.end( ), filename ) == files.end( ) )
				{
					files.push_back( filename );
				}
			}
		};

		// Ensure C:\mintaly and C:\mintaly\sounds exist
		custom_sound_detail::ensure_directories( );

		// Check C:\mintaly directly and C:\mintaly\sounds
		scan_dir( L"C:\\mintaly" );
		scan_dir( L"C:\\mintaly\\sounds" );

		std::sort( files.begin( ), files.end( ) );
		return files;
	}

	void impacts::play_custom_sound( std::string_view filename, float volume ) const
	{
		const auto path = custom_sound_detail::resolve_sound_path( filename );
		if ( !path.empty( ) )
		{
			custom_sound_detail::play_wav_direct( path, volume );
		}
	}

	void impacts::play_sound( settings::misc::impacts::sound_type type, float volume, std::string_view custom_file )
	{
		if ( type == settings::misc::impacts::sound_type::custom )
		{
			this->play_custom_sound( custom_file, volume );
			return;
		}

		if ( type == settings::misc::impacts::sound_type::koch )
		{
			custom_sound_detail::play_wav_memory( sounds::g_koch_wav, volume );
			return;
		}

		const char* sound_path{ nullptr };

		switch ( type )
		{
		case settings::misc::impacts::sound_type::shop_click:
			sound_path = "sounds/ui/panorama/mainmenu_press_shop_01";
			break;
		case settings::misc::impacts::sound_type::home_click:
			sound_path = "sounds/ui/panorama/mainmenu_press_home_01";
			break;
		case settings::misc::impacts::sound_type::bell:
			sound_path = "sounds/training/timer_bell";
			break;
		case settings::misc::impacts::sound_type::killcard:
			sound_path = "sounds/ui/killcard_1";
			break;
		case settings::misc::impacts::sound_type::bullet_casing:
			sound_path = "sounds/weapons/fx/tink/bullet_casing_07";
			break;
		case settings::misc::impacts::sound_type::coin_pickup:
			sound_path = "sounds/ui/coin_pickup_01";
			break;
		case settings::misc::impacts::sound_type::item_drop:
			sound_path = "sounds/ui/item_drop";
			break;
		case settings::misc::impacts::sound_type::popcan:
			sound_path = "sounds/physics/metal/metal_popcan_impact_hard3";
			break;
		case settings::misc::impacts::sound_type::key_press:
			sound_path = "sounds/weapons/c4/key_press7";
			break;
		default:
			return;
		}

		custom_sound_detail::play_engine_path( sound_path, volume );
	}

	void impacts::play_hit_effect( std::uintptr_t victim_pawn )
	{
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );
		const auto current_time = memory::read<float>( global_vars + 0x30 );

		this->m_hit_effect_time = current_time;
	}

	void impacts::play_death_effect( std::uintptr_t victim_pawn )
	{
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}

		constexpr auto particle_path{ "particles/embedded/fade.vpcf" };

		if ( !this->m_death_effect_loaded )
		{
			struct buffer_string
			{
				std::uint32_t m_unknown1{};
				std::uint32_t m_unknown2{ 0xc00000c8 };

				union
				{
					std::uintptr_t m_str_ptr;
					std::uint8_t data[ 0xc8 ];
				};

				std::uintptr_t m_unknown3{};
				std::uintptr_t m_unknown4{};
			} buffer;

			memory::call<void>(PATTERN (patterns::init_particle_path_buffer), &buffer, particle_path );

			buffer.m_unknown4 = 'fcpv';

			memory::call<void>(PATTERN (patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "" );

			this->m_death_effect_loaded = true;
		}

		auto effect_index{ detail::invalid_particle_effect };
		memory::call<int*>(PATTERN (patterns::particle_create_effect), particle_manager, &effect_index, particle_path, 8, 0ll, 0ll, 0ll, 0 );

		if ( effect_index == detail::invalid_particle_effect )
		{
			return;
		}

		const math::vector3 color{ static_cast< float >( settings::g_misc.m_impacts.death_effect_color.value.r ), static_cast< float >( settings::g_misc.m_impacts.death_effect_color.value.g ), static_cast< float >( settings::g_misc.m_impacts.death_effect_color.value.b ) };
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 2, &color, 0 );

		struct
		{
			std::intptr_t xy{ 0x7f7fffff7f7fffffll };
			int z{ 0x7f7fffff };
		} default_pos;

		memory::call<bool>(PATTERN (patterns::particle_set_entity_binding), particle_manager, effect_index, 0, victim_pawn, 1, nullptr, &default_pos, 1, 0ll );
		memory::call<bool>(PATTERN (patterns::particle_set_entity_binding), particle_manager, effect_index, 1, victim_pawn, 1, nullptr, &default_pos, 1, 0ll );
	}

	void impacts::play_bullet_impact_effect( const math::vector3& position )
	{
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager )
		{
			return;
		}

		constexpr auto particle_path{ "particles/embedded/sparks.vpcf" };

		if ( !this->m_bullet_impact_effect_loaded )
		{
			struct buffer_string
			{
				std::uint32_t m_unknown1{};
				std::uint32_t m_unknown2{ 0xc00000c8 };

				union
				{
					std::uintptr_t m_str_ptr;
					std::uint8_t data[ 0xc8 ];
				};

				std::uintptr_t m_unknown3{};
				std::uintptr_t m_unknown4{};
			} buffer;

			memory::call<void>(PATTERN (patterns::init_particle_path_buffer), &buffer, particle_path );

			buffer.m_unknown4 = 'fcpv';

			memory::call<void>(PATTERN (patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "" );

			this->m_bullet_impact_effect_loaded = true;
		}

		auto effect_index{ detail::invalid_particle_effect };
		memory::call<int*>(PATTERN (patterns::particle_create_effect), particle_manager, &effect_index, particle_path, 8, 0ll, 0ll, 0ll, 0 );

		if ( effect_index == detail::invalid_particle_effect )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_impacts;

		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 0, &position, 0 );

		const math::vector3 color{ static_cast< float >( cfg.bullet_impact_effect_color_spark.value.r ), static_cast< float >( cfg.bullet_impact_effect_color_spark.value.g ), static_cast< float >( cfg.bullet_impact_effect_color_spark.value.b ) };
		memory::call<bool>(PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 1, &color, 0 );
	}

	void impacts::play_bullet_tracer( const math::vector3& position )
	{
		const auto particle_manager = memory::read<std::uintptr_t>( addresses::globals::particle_manager );
		if ( !particle_manager || !systems::g_local.get( ).is_alive )
		{
			return;
		}

		constexpr auto particle_path{ "particles/embedded/tracer.vpcf" };

		if ( !this->m_bullet_tracers_loaded )
		{
			struct buffer_string
			{
				std::uint32_t m_unknown1{};
				std::uint32_t m_unknown2{ 0xc00000c8 };

				union
				{
					std::uintptr_t m_str_ptr;
					std::uint8_t data[ 0xc8 ];
				};

				std::uintptr_t m_unknown3{};
				std::uintptr_t m_unknown4{};
			} buffer;

			memory::call<void>(PATTERN (patterns::init_particle_path_buffer), &buffer, particle_path );

			buffer.m_unknown4 = 'fcpv';

			memory::call<void>(PATTERN (patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "" );

			this->m_bullet_tracers_loaded = true;
		}

		auto effect_index{ detail::invalid_particle_effect };
		memory::call<int*>(PATTERN (patterns::particle_create_effect), particle_manager, &effect_index, particle_path, 8, 0ll, 0ll, 0ll, 0 );

		if ( effect_index == detail::invalid_particle_effect )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_impacts;

		memory::call<bool>( PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 0, &this->m_buffered_eye_position, 0 );
		memory::call<bool>( PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 1, &position, 0 );

		const math::vector3 color{ static_cast< float >( cfg.bullet_tracer_color.value.r ), static_cast< float >( cfg.bullet_tracer_color.value.g ), static_cast< float >( cfg.bullet_tracer_color.value.b ) };
		memory::call<bool>( PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 2, &color, 0 );

		const math::vector3 lifetime{ cfg.bullet_tracer_duration, 0.0f, 0.0f };
		memory::call<bool>( PATTERN (patterns::particle_set_control_point), particle_manager, effect_index, 3, &lifetime, 0 );
	}

	void impacts::flush_buffered_impacts( )
	{
		if ( this->m_buffered_impacts.empty( ) )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_impacts;
		const auto& final_pos = this->m_buffered_impacts.back( );

		const auto ray = final_pos - this->m_buffered_eye_position;
		const auto ray_len = ray.length( );

		if ( ray_len < 0.1f )
		{
			return;
		}

		const auto ray_dir = ray * ( 1.0f / ray_len );

		for ( const auto& impact : this->m_buffered_impacts )
		{
			const auto to_impact = impact - this->m_buffered_eye_position;
			const auto proj = to_impact.dot( ray_dir );
			const auto corrected = this->m_buffered_eye_position + ray_dir * std::max( proj, 0.0f );

			const auto type = cfg.bullet_impact_effect_type.value;
			const auto show_overlay = type == settings::misc::impacts::bullet_impact_type::overlay || type == settings::misc::impacts::bullet_impact_type::both;

			if ( cfg.bullet_impact_effect.value && show_overlay )
			{
				this->m_bullet_impacts.push_back( { corrected,  this->m_buffered_impact_time } );

				if ( this->m_bullet_impacts.size( ) > 64 )
				{
					this->m_bullet_impacts.erase( this->m_bullet_impacts.begin( ) );
				}
			}
		}

		if ( cfg.bullet_tracers.value )
		{
			this->play_bullet_tracer( final_pos );
		}
	}

} // namespace features::misc
