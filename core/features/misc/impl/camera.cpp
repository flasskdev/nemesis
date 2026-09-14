#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/rendering/rendering.hpp>

#include "../misc.hpp"
#include <protection/game_addresses.hpp>

namespace features::misc {
	namespace {
		constexpr std::ptrdiff_t k_fov_offset{ 0x498 };
		constexpr std::ptrdiff_t k_aspect_ratio_offset{ 0x4d4 };
		constexpr std::ptrdiff_t k_view_flags_offset{ 0x551 };
		constexpr std::uint8_t k_explicit_aspect_ratio_flag{ 1u << 1 };


		math::vector3 s_spec_freecam_angles{};
		bool s_was_spec_freecam{ false };

		math::vector3 s_spec_thirdperson_angles{};
		bool s_was_spec_thirdperson{ false };
		bool s_had_spec_mouse_event{ false };
		std::uintptr_t s_last_spec_pawn{ 0 };

		using SDL_GetRelativeMouseState_t = std::uint32_t( * )( float* x, float* y );
		using SDL_GetGlobalMouseState_t   = std::uint32_t( * )( float* x, float* y );

		inline SDL_GetRelativeMouseState_t s_get_relative_mouse_state = nullptr;
		inline SDL_GetGlobalMouseState_t   s_get_global_mouse_state   = nullptr;
		inline bool s_sdl_mouse_resolved = false;
		inline float s_last_global_x = 0.0f;
		inline float s_last_global_y = 0.0f;
		inline bool s_global_mouse_valid = false;
		inline POINT s_last_win_cursor{};
		inline bool s_win_cursor_valid = false;

		void query_mouse_delta( float& out_dx, float& out_dy )
		{
			out_dx = 0.0f;
			out_dy = 0.0f;

			if ( !s_sdl_mouse_resolved )
			{
				const auto sdl = GetModuleHandleA( "SDL3.dll" );
				if ( sdl )
				{
					s_get_relative_mouse_state = reinterpret_cast< SDL_GetRelativeMouseState_t >(
						GetProcAddress( sdl, "SDL_GetRelativeMouseState" )
					);
					s_get_global_mouse_state = reinterpret_cast< SDL_GetGlobalMouseState_t >(
						GetProcAddress( sdl, "SDL_GetGlobalMouseState" )
					);
				}
				s_sdl_mouse_resolved = true;
			}

			// 1. Primary: SDL_GetRelativeMouseState (native relative deltas in SDL3)
			if ( s_get_relative_mouse_state )
			{
				float r_dx = 0.0f, r_dy = 0.0f;
				__try
				{
					s_get_relative_mouse_state( &r_dx, &r_dy );
				}
				__except ( EXCEPTION_EXECUTE_HANDLER )
				{
					s_get_relative_mouse_state = nullptr;
				}

				if ( std::isfinite( r_dx ) && std::isfinite( r_dy ) )
				{
					if ( std::fabsf( r_dx ) > 0.0001f || std::fabsf( r_dy ) > 0.0001f )
					{
						out_dx = std::clamp( r_dx, -200.0f, 200.0f );
						out_dy = std::clamp( r_dy, -200.0f, 200.0f );
						return;
					}
				}
			}

			// 2. Secondary: SDL_GetGlobalMouseState
			if ( s_get_global_mouse_state )
			{
				float gx = 0.0f, gy = 0.0f;
				__try
				{
					s_get_global_mouse_state( &gx, &gy );
				}
				__except ( EXCEPTION_EXECUTE_HANDLER )
				{
					s_get_global_mouse_state = nullptr;
				}

				if ( std::isfinite( gx ) && std::isfinite( gy ) )
				{
					if ( !s_global_mouse_valid )
					{
						s_last_global_x = gx;
						s_last_global_y = gy;
						s_global_mouse_valid = true;
					}
					else
					{
						const float g_dx = gx - s_last_global_x;
						const float g_dy = gy - s_last_global_y;
						s_last_global_x = gx;
						s_last_global_y = gy;
						if ( std::fabsf( g_dx ) > 0.0001f || std::fabsf( g_dy ) > 0.0001f )
						{
							out_dx = std::clamp( g_dx, -200.0f, 200.0f );
							out_dy = std::clamp( g_dy, -200.0f, 200.0f );
							return;
						}
					}
				}
			}

			// 3. Fallback: Windows cursor delta (without SetCursorPos)
			POINT cur{};
			if ( GetCursorPos( &cur ) )
			{
				if ( !s_win_cursor_valid )
				{
					s_last_win_cursor = cur;
					s_win_cursor_valid = true;
				}
				else
				{
					const float w_dx = static_cast< float >( cur.x - s_last_win_cursor.x );
					const float w_dy = static_cast< float >( cur.y - s_last_win_cursor.y );
					s_last_win_cursor = cur;
					if ( std::fabsf( w_dx ) > 0.0001f || std::fabsf( w_dy ) > 0.0001f )
					{
						out_dx = std::clamp( w_dx, -200.0f, 200.0f );
						out_dy = std::clamp( w_dy, -200.0f, 200.0f );
						return;
					}
				}
			}
		}

		[[nodiscard]] float scale_horizontal_fov( float fov, float aspect_ratio )
		{
			constexpr auto degrees_to_half_radians{ std::numbers::pi_v<float> / 360.0f };
			constexpr auto half_radians_to_degrees{ 360.0f / std::numbers::pi_v<float> };
			constexpr auto four_by_three_inverse{ 0.75f };

			return std::atan( std::tan( fov * degrees_to_half_radians ) * aspect_ratio * four_by_three_inverse ) * half_radians_to_degrees;
		}
	}

	void camera::on_override_view( std::uintptr_t view_setup )
	{
		if ( !view_setup )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !systems::g_local.is_in_cinematic( ) )
		{
			const auto target_pawn = local.view_pawn( );
			const bool is_spec = !local.is_alive && local.observer_pawn != 0;
			const bool allow_thirdperson = local.is_alive ? settings::g_misc.m_camera.thirdperson.value : ( settings::g_misc.m_camera.spectator_thirdperson.value && is_spec );

			if ( this->do_freecam( view_setup ) )
			{
				// Freecam handled camera position and angles.
			}
			else if ( target_pawn && allow_thirdperson )
			{
				this->do_thirdperson( view_setup, target_pawn );
			}

			if ( target_pawn )
			{
				this->do_fov_change( view_setup, target_pawn );
			}
		}

		// Aspect conversion must run after the base FOV has been selected.
		this->do_aspect_ratio_change( view_setup );
	}

	void camera::update_fov_sensitivity( std::uintptr_t player_pawn ) const
	{
		if ( !settings::g_misc.m_camera.change_fov.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( player_pawn != local.pawn )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_camera;
		const auto is_scoped = memory::read<bool>( player_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		const auto target_fov = ( is_scoped && cfg.scoped_fov_override.value ) ? cfg.scoped_fov.value : cfg.fov.value;

		if ( is_scoped == this->m_cached_scoped && target_fov == this->m_cached_target_fov && this->m_cached_fov_sensitivity >= 0.0f )
		{
			const auto current_adjust = memory::read<float>( player_pawn + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash ) );
			if ( std::fabsf( current_adjust - this->m_cached_fov_sensitivity ) < 0.0001f )
			{
				return;
			}
		}

		this->m_cached_scoped = is_scoped;
		this->m_cached_target_fov = target_fov;

		const auto cvar = CONVAR( "zoom_sensitivity_ratio" );
		const auto ratio = cvar ? cvar->get<float>( ) : 1.0f;
		const auto desired = ratio * ( target_fov / 90.0f );

		this->m_cached_fov_sensitivity = desired;
		memory::safe_write<float>( player_pawn + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash ), desired );
	}

	void camera::do_thirdperson( std::uintptr_t view_setup, std::uintptr_t target_pawn )
	{
		if ( !view_setup || !target_pawn )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_camera;
		const auto local = systems::g_local.get( );
		const bool tp_enabled = local.is_alive ? cfg.thirdperson.value : ( cfg.spectator_thirdperson.value && local.observer_pawn != 0 );
		if ( !tp_enabled )
		{
			s_was_spec_thirdperson = false;
			return;
		}

		const auto game_scene_node = memory::safe_read<std::uintptr_t>( target_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
		math::vector3 eye_position = memory::safe_read<math::vector3>( view_setup + 0x4a0 ).value_or( math::vector3{} );
		if ( game_scene_node )
		{
			const auto origin = memory::safe_read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) ).value_or( math::vector3{} );
			const auto view_offset = memory::safe_read<math::vector3>( target_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) ).value_or( math::vector3{} );
			if ( origin.length_sqr( ) > 0.0f && std::isfinite( origin.x ) && std::isfinite( origin.y ) && std::isfinite( origin.z ) &&
			     std::isfinite( view_offset.x ) && std::isfinite( view_offset.y ) && std::isfinite( view_offset.z ) )
			{
				eye_position = origin + view_offset;
			}
		}

		if ( !std::isfinite( eye_position.x ) || !std::isfinite( eye_position.y ) || !std::isfinite( eye_position.z ) )
		{
			return;
		}

		math::vector3 view_angles{};
		if ( local.is_alive )
		{
			s_was_spec_thirdperson = false;
			view_angles = systems::g_input.get_view_angles( );
		}
		else
		{
			// Free camera orbital rotation around the spectated player
			if ( !s_was_spec_thirdperson || s_last_spec_pawn != target_pawn )
			{
				s_spec_thirdperson_angles = memory::safe_read<math::vector3>( target_pawn + SCHEMA( "C_CSPlayerPawn", "m_angEyeAngles"_hash ) ).value_or( math::vector3{} );
				if ( !std::isfinite( s_spec_thirdperson_angles.x ) || !std::isfinite( s_spec_thirdperson_angles.y ) || !std::isfinite( s_spec_thirdperson_angles.z ) || s_spec_thirdperson_angles.length_sqr( ) < 0.001f )
				{
					s_spec_thirdperson_angles = memory::safe_read<math::vector3>( view_setup + 0x4b8 ).value_or( math::vector3{} );
				}
				s_spec_thirdperson_angles.x = std::clamp( s_spec_thirdperson_angles.x, -89.0f, 89.0f );
				s_spec_thirdperson_angles.y = math::helpers::normalize_yaw( s_spec_thirdperson_angles.y );
				s_spec_thirdperson_angles.z = 0.0f;

				s_was_spec_thirdperson = true;
				s_last_spec_pawn = target_pawn;
			}

			// Fallback mouse look if WM_INPUT was not handled
			if ( !s_had_spec_mouse_event && !rendering::g_menu.is_open( ) )
			{
				float dx = 0.0f, dy = 0.0f;
				query_mouse_delta( dx, dy );
				if ( std::fabsf( dx ) > 0.0001f || std::fabsf( dy ) > 0.0001f )
				{
					float sens = 1.0f;
					if ( const auto cvar = CONVAR( "sensitivity" ) ) sens = cvar->get< float >( );
					sens = std::clamp( sens, 0.001f, 100.0f );

					float m_pitch = 0.022f;
					if ( const auto cvar = CONVAR( "m_pitch" ) ) m_pitch = cvar->get< float >( );
					float m_yaw = 0.022f;
					if ( const auto cvar = CONVAR( "m_yaw" ) ) m_yaw = cvar->get< float >( );

					s_spec_thirdperson_angles.x = std::clamp( s_spec_thirdperson_angles.x + dy * m_pitch * sens, -89.0f, 89.0f );
					s_spec_thirdperson_angles.y = math::helpers::normalize_yaw( s_spec_thirdperson_angles.y - dx * m_yaw * sens );
					s_spec_thirdperson_angles.z = 0.0f;
				}
			}
			s_had_spec_mouse_event = false;

			view_angles = s_spec_thirdperson_angles;
		}

		if ( !std::isfinite( view_angles.x ) || !std::isfinite( view_angles.y ) || !std::isfinite( view_angles.z ) )
		{
			view_angles = {};
		}

		view_angles.x = std::clamp( view_angles.x, -89.0f, 89.0f );
		view_angles.y = math::helpers::normalize_yaw( view_angles.y );
		view_angles.z = 0.0f;

		math::vector3 forward{};
		math::helpers::angle_vectors_left( view_angles, &forward );

		if ( !std::isfinite( forward.x ) || !std::isfinite( forward.y ) || !std::isfinite( forward.z ) )
		{
			forward = { 1.0f, 0.0f, 0.0f };
		}

		const float distance = std::clamp( cfg.thirdperson_distance.value, 10.0f, 500.0f );
		const float hull_size = std::clamp( cfg.thirdperson_hull_size.value, 0.0f, 50.0f );

		auto camera_position = eye_position - forward * distance;

		if ( std::isfinite( camera_position.x ) && std::isfinite( camera_position.y ) && std::isfinite( camera_position.z ) )
		{
			if ( hull_size > 0.001f )
			{
				if ( hull_size > 0.1f )
				{
					const auto hull_mins = math::vector3{ -hull_size, -hull_size, -hull_size };
					const auto hull_maxs = math::vector3{ hull_size, hull_size, hull_size };
					const auto result = systems::g_tracing.trace_hull( eye_position, camera_position, hull_mins, hull_maxs, target_pawn );

					if ( result.fraction < 1.0f )
					{
						const auto world = systems::g_entities.get_by_index( 0 );
						if ( result.hit_entity == world || result.hit_entity == 0 )
						{
							camera_position = eye_position + ( camera_position - eye_position ) * result.fraction;
						}
					}
				}
				else
				{
					const auto result = systems::g_tracing.trace( eye_position, camera_position, target_pawn );
					if ( result.fraction < 1.0f )
					{
						const auto world = systems::g_entities.get_by_index( 0 );
						if ( result.hit_entity == world || result.hit_entity == 0 )
						{
							camera_position = eye_position + ( camera_position - eye_position ) * result.fraction;
						}
					}
				}
			}
			// If hull_size <= 0.001f, wall tracing is disabled: camera passes through walls freely
		}

		if ( std::isfinite( camera_position.x ) && std::isfinite( camera_position.y ) && std::isfinite( camera_position.z ) )
		{
			memory::safe_write<math::vector3>( view_setup + 0x4a0, camera_position );
		}
		if ( !local.is_alive )
		{
			memory::safe_write<math::vector3>( view_setup + 0x4b8, view_angles );
		}
	}

	void camera::do_fov_change( std::uintptr_t view_setup, std::uintptr_t target_pawn ) const
	{
		if ( !view_setup || !target_pawn )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_camera;
		if ( !cfg.change_fov.value )
		{
			return;
		}

		const auto is_scoped = memory::safe_read<bool>( target_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) ).value_or( false );
		const auto target_fov = ( is_scoped && cfg.scoped_fov_override.value ) ? cfg.scoped_fov.value : cfg.fov.value;

		memory::safe_write<float>( view_setup + k_fov_offset, target_fov );

		this->update_fov_sensitivity( target_pawn );
	}

	void camera::do_aspect_ratio_change( std::uintptr_t view_setup )
	{
		if ( !view_setup )
		{
			return;
		}

		const auto& cfg = settings::g_misc.m_camera;

		if ( cfg.change_aspect_ratio.value )
		{
			const auto base_fov = memory::safe_read<float>( view_setup + k_fov_offset ).value_or( 90.0f );
			const auto flags = memory::safe_read<std::uint8_t>( view_setup + k_view_flags_offset ).value_or( 0 );

			// Explicit aspect bypasses the game's native 4:3-based FOV conversion.
			memory::safe_write<float>( view_setup + k_fov_offset, scale_horizontal_fov( base_fov, cfg.aspect_ratio ) );
			memory::safe_write<float>( view_setup + k_aspect_ratio_offset, cfg.aspect_ratio );
			memory::safe_write<std::uint8_t>( view_setup + k_view_flags_offset, flags | k_explicit_aspect_ratio_flag );
		}
		else
		{
			const auto flags = memory::safe_read<std::uint8_t>( view_setup + k_view_flags_offset ).value_or( 0 );
			memory::safe_write<std::uint8_t>( view_setup + k_view_flags_offset,
				flags & static_cast<std::uint8_t>( ~k_explicit_aspect_ratio_flag ) );
		}
	}

	bool camera::is_spec_thirdperson_active( ) const noexcept
	{
		const auto local = systems::g_local.get( );
		return !local.is_alive && local.observer_pawn != 0 && settings::g_misc.m_camera.spectator_thirdperson.value && !this->m_was_freecam_active;
	}

	void camera::on_spec_thirdperson_mouse_delta( float d_pitch, float d_yaw )
	{
		if ( !this->is_spec_thirdperson_active( ) || rendering::g_menu.is_open( ) )
		{
			return;
		}

		if ( ( std::fabsf( d_pitch ) > 0.0001f || std::fabsf( d_yaw ) > 0.0001f ) && std::isfinite( d_pitch ) && std::isfinite( d_yaw ) )
		{
			s_spec_thirdperson_angles.x = std::clamp( s_spec_thirdperson_angles.x + d_pitch, -89.0f, 89.0f );
			s_spec_thirdperson_angles.y = math::helpers::normalize_yaw( s_spec_thirdperson_angles.y + d_yaw );
			s_spec_thirdperson_angles.z = 0.0f;
			s_had_spec_mouse_event = true;
		}
	}

	void camera::on_mouse_delta( float d_pitch, float d_yaw )
	{
		if ( !this->m_was_freecam_active || rendering::g_menu.is_open( ) )
		{
			return;
		}

		if ( ( std::fabsf( d_pitch ) > 0.0001f || std::fabsf( d_yaw ) > 0.0001f ) && std::isfinite( d_pitch ) && std::isfinite( d_yaw ) )
		{
			this->m_freecam_angles.x = std::clamp( this->m_freecam_angles.x + d_pitch, -89.0f, 89.0f );
			this->m_freecam_angles.y = math::helpers::normalize_yaw( this->m_freecam_angles.y + d_yaw );
			this->m_freecam_angles.z = 0.0f;
			this->m_had_mouse_event = true;
		}
	}

	bool camera::do_freecam( std::uintptr_t view_setup )
	{
		if ( !view_setup )
		{
			return false;
		}

		const auto& cfg = settings::g_misc.m_camera;
		if ( !cfg.freecam.value )
		{
			if ( this->m_was_freecam_active )
			{
				if ( std::isfinite( this->m_saved_viewangles.x ) && std::isfinite( this->m_saved_viewangles.y ) && std::isfinite( this->m_saved_viewangles.z ) )
				{
					systems::g_input.set_view_angles( this->m_saved_viewangles );
				}

				this->m_was_freecam_active = false;
				this->m_had_mouse_event = false;
				this->m_cmd_buttons = 0;
				s_was_spec_freecam = false;
				s_global_mouse_valid = false;
				s_win_cursor_valid = false;
			}
			return false;
		}

		const auto now = std::chrono::steady_clock::now( );
		if ( !this->m_was_freecam_active )
		{
			this->m_freecam_pos = memory::safe_read<math::vector3>( view_setup + 0x4a0 ).value_or( math::vector3{} );
			if ( !std::isfinite( this->m_freecam_pos.x ) || !std::isfinite( this->m_freecam_pos.y ) || !std::isfinite( this->m_freecam_pos.z ) || this->m_freecam_pos.length_sqr( ) < 1.0f )
			{
				const auto local = systems::g_local.get( );
				const auto view_pawn = local.view_pawn( );
				if ( view_pawn )
				{
					const auto game_scene_node = memory::safe_read<std::uintptr_t>( view_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
					if ( game_scene_node )
					{
						const auto origin = memory::safe_read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) ).value_or( math::vector3{} );
						const auto view_offset = memory::safe_read<math::vector3>( view_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) ).value_or( math::vector3{} );
						if ( origin.length_sqr( ) > 0.0f && std::isfinite( origin.x ) && std::isfinite( origin.y ) && std::isfinite( origin.z ) &&
						     std::isfinite( view_offset.x ) && std::isfinite( view_offset.y ) && std::isfinite( view_offset.z ) )
						{
							this->m_freecam_pos = origin + view_offset;
						}
					}
				}
				if ( this->m_freecam_pos.length_sqr( ) < 1.0f )
				{
					this->m_freecam_pos = systems::g_view.origin( );
				}
			}

			this->m_saved_viewangles = systems::g_input.get_view_angles( );
			this->m_freecam_angles = memory::safe_read<math::vector3>( view_setup + 0x4b8 ).value_or( math::vector3{} );
			if ( !std::isfinite( this->m_freecam_angles.x ) || !std::isfinite( this->m_freecam_angles.y ) || !std::isfinite( this->m_freecam_angles.z ) || this->m_freecam_angles.length_sqr( ) < 0.001f )
			{
				this->m_freecam_angles = this->m_saved_viewangles;
			}
			this->m_freecam_angles.x = std::clamp( this->m_freecam_angles.x, -89.0f, 89.0f );
			this->m_freecam_angles.y = math::helpers::normalize_yaw( this->m_freecam_angles.y );
			this->m_freecam_angles.z = 0.0f;

			this->m_last_override_time = now;
			this->m_was_freecam_active = true;
			this->m_had_mouse_event = false;
			s_was_spec_freecam = false;
			s_global_mouse_valid = false;
			s_win_cursor_valid = false;
		}

		float dt = 0.016f;
		if ( this->m_last_override_time.time_since_epoch( ).count( ) > 0 )
		{
			const float raw_dt = std::chrono::duration<float>( now - this->m_last_override_time ).count( );
			if ( raw_dt >= 0.001f )
			{
				dt = std::clamp( raw_dt, 0.001f, 0.1f );
				this->m_last_override_time = now;
			}
			else
			{
				dt = 0.0f;
			}
		}
		else
		{
			this->m_last_override_time = now;
			dt = 0.016f;
		}

		// Fallback mouse look (when WM_INPUT wasn't handled or during spectator)
		if ( !this->m_had_mouse_event && !rendering::g_menu.is_open( ) )
		{
			float dx = 0.0f, dy = 0.0f;
			query_mouse_delta( dx, dy );
			if ( std::fabsf( dx ) > 0.0001f || std::fabsf( dy ) > 0.0001f )
			{
				float sens = 1.0f;
				if ( const auto cvar = CONVAR( "sensitivity" ) ) sens = cvar->get< float >( );
				sens = std::clamp( sens, 0.001f, 100.0f );

				float m_pitch = 0.022f;
				if ( const auto cvar = CONVAR( "m_pitch" ) ) m_pitch = cvar->get< float >( );
				float m_yaw = 0.022f;
				if ( const auto cvar = CONVAR( "m_yaw" ) ) m_yaw = cvar->get< float >( );

				this->m_freecam_angles.x = std::clamp( this->m_freecam_angles.x + dy * m_pitch * sens, -89.0f, 89.0f );
				this->m_freecam_angles.y = math::helpers::normalize_yaw( this->m_freecam_angles.y - dx * m_yaw * sens );
				this->m_freecam_angles.z = 0.0f;
			}
		}
		this->m_had_mouse_event = false;

		const bool can_move = !rendering::g_menu.is_open( );
		if ( can_move && dt > 0.0f )
		{
			math::vector3 forward{};
			math::vector3 right{};
			math::helpers::angle_vectors_left( this->m_freecam_angles, &forward, &right );

			math::vector3 move_dir{};

			const bool move_fwd = ( GetAsyncKeyState( 'W' ) & 0x8000 ) || ( GetAsyncKeyState( VK_UP ) & 0x8000 ) || ( this->m_cmd_buttons & cstypes::command_buttons::in_forward );
			const bool move_back = ( GetAsyncKeyState( 'S' ) & 0x8000 ) || ( GetAsyncKeyState( VK_DOWN ) & 0x8000 ) || ( this->m_cmd_buttons & cstypes::command_buttons::in_back );
			const bool move_left = ( GetAsyncKeyState( 'A' ) & 0x8000 ) || ( GetAsyncKeyState( VK_LEFT ) & 0x8000 ) || ( this->m_cmd_buttons & cstypes::command_buttons::in_moveleft );
			const bool move_right = ( GetAsyncKeyState( 'D' ) & 0x8000 ) || ( GetAsyncKeyState( VK_RIGHT ) & 0x8000 ) || ( this->m_cmd_buttons & cstypes::command_buttons::in_moveright );
			const bool move_up = ( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) || ( this->m_cmd_buttons & cstypes::command_buttons::in_jump );
			const bool move_down = ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) || ( GetAsyncKeyState( 'C' ) & 0x8000 ) || ( this->m_cmd_buttons & cstypes::command_buttons::in_duck );

			if ( move_fwd ) move_dir += forward;
			if ( move_back ) move_dir -= forward;
			if ( move_left ) move_dir -= right;
			if ( move_right ) move_dir += right;
			if ( move_up ) move_dir.z += 1.0f;
			if ( move_down ) move_dir.z -= 1.0f;

			float speed = std::clamp( cfg.freecam_speed.value, 100.0f, 10000.0f );
			if ( ( GetAsyncKeyState( VK_SHIFT ) & 0x8000 ) || ( this->m_cmd_buttons & cstypes::command_buttons::in_sprint ) )
			{
				speed *= 2.5f;
			}
			else if ( ( GetAsyncKeyState( VK_MENU ) & 0x8000 ) )
			{
				speed *= 0.3f;
			}

			if ( move_dir.length_sqr( ) > 0.0001f )
			{
				move_dir = move_dir.normalized( );
				this->m_freecam_pos += move_dir * ( speed * dt );
			}
		}

		if ( std::isfinite( this->m_freecam_pos.x ) && std::isfinite( this->m_freecam_pos.y ) && std::isfinite( this->m_freecam_pos.z ) )
		{
			memory::safe_write<math::vector3>( view_setup + 0x4a0, this->m_freecam_pos );
		}
		if ( std::isfinite( this->m_freecam_angles.x ) && std::isfinite( this->m_freecam_angles.y ) && std::isfinite( this->m_freecam_angles.z ) )
		{
			memory::safe_write<math::vector3>( view_setup + 0x4b8, this->m_freecam_angles );
		}
		return true;
	}

	void camera::on_create_move( systems::input::usercmd* cmd )
	{
		if ( !cmd )
		{
			return;
		}

		this->m_cmd_buttons = cmd->buttons.value;

		const auto& cfg = settings::g_misc.m_camera;
		if ( !cfg.freecam.value )
		{
			return;
		}

		if ( !this->m_was_freecam_active || !std::isfinite( this->m_saved_viewangles.x ) || !std::isfinite( this->m_saved_viewangles.y ) || !std::isfinite( this->m_saved_viewangles.z ) )
		{
			this->m_saved_viewangles = systems::g_input.get_view_angles( );
		}

		if ( cfg.freecam_block_input.value )
		{
			const auto base = cmd->csgo_user_cmd.mutable_base( );
			if ( base )
			{
				base->set_forwardmove( 0.0f );
				base->set_leftmove( 0.0f );
				base->set_upmove( 0.0f );

				if ( const auto subticks = base->mutable_subtick_moves( ) )
				{
					subticks->clear( );
				}

				if ( base->has_viewangles( ) )
				{
					if ( const auto va = base->mutable_viewangles( ) )
					{
						va->set_x( this->m_saved_viewangles.x );
						va->set_y( this->m_saved_viewangles.y );
						va->set_z( this->m_saved_viewangles.z );
					}
				}
			}

			const auto input_history_size = cmd->csgo_user_cmd.input_history_size( );
			for ( auto i = 0; i < input_history_size; ++i )
			{
				const auto entry = cmd->csgo_user_cmd.mutable_input_history( i );
				if ( !entry )
				{
					continue;
				}

				if ( entry->has_view_angles( ) )
				{
					if ( const auto angles = entry->mutable_view_angles( ) )
					{
						angles->set_x( this->m_saved_viewangles.x );
						angles->set_y( this->m_saved_viewangles.y );
						angles->set_z( this->m_saved_viewangles.z );
					}
				}
			}

			cmd->buttons.value = 0;
			cmd->buttons.value_changed = 0;
			cmd->buttons.value_scroll = 0;
		}
	}

	void camera::reset( bool restore_view_angles )
	{
		if ( restore_view_angles && this->m_was_freecam_active )
		{
			if ( std::isfinite( this->m_saved_viewangles.x ) && std::isfinite( this->m_saved_viewangles.y ) && std::isfinite( this->m_saved_viewangles.z ) )
			{
				systems::g_input.set_view_angles( this->m_saved_viewangles );
			}
		}
		this->m_was_freecam_active = false;
		this->m_had_mouse_event = false;
		this->m_cmd_buttons = 0;
		s_was_spec_freecam = false;
		s_global_mouse_valid = false;
		s_win_cursor_valid = false;
		this->m_freecam_pos = {};
		this->m_freecam_angles = {};
		s_spec_freecam_angles = {};
		this->m_saved_viewangles = {};
		this->m_cached_fov_sensitivity = -1.0f;
		this->m_cached_scoped = false;
		this->m_cached_target_fov = 0.0f;
		s_was_spec_thirdperson = false;
		s_had_spec_mouse_event = false;
		s_spec_thirdperson_angles = {};
		s_last_spec_pawn = 0;
	}

} // namespace features::misc
