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

		bool is_game_window_focused( )
		{
			const auto fg = GetForegroundWindow( );
			if ( !fg )
			{
				return false;
			}

			DWORD pid = 0;
			GetWindowThreadProcessId( fg, &pid );
			return pid == GetCurrentProcessId( );
		}

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
				s_get_relative_mouse_state( &r_dx, &r_dy );
				if ( std::fabsf( r_dx ) > 0.0001f || std::fabsf( r_dy ) > 0.0001f )
				{
					out_dx = r_dx;
					out_dy = r_dy;
					return;
				}
			}

			// 2. Secondary: SDL_GetGlobalMouseState
			if ( s_get_global_mouse_state )
			{
				float gx = 0.0f, gy = 0.0f;
				s_get_global_mouse_state( &gx, &gy );
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
						out_dx = g_dx;
						out_dy = g_dy;
						return;
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
						out_dx = w_dx;
						out_dy = w_dy;
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
		const auto local = systems::g_local.get( );
		if ( !systems::g_local.is_in_cinematic( ) )
		{
			const auto target_pawn = local.view_pawn( );
			const bool allow_thirdperson = local.is_alive || ( settings::g_misc.m_camera.spectator_thirdperson.value && local.observer_pawn != 0 );

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

		const auto ratio = CONVAR ("zoom_sensitivity_ratio")->get<float>( );
		const auto desired = ratio * ( target_fov / 90.0f );

		this->m_cached_fov_sensitivity = desired;
		memory::write<float>( player_pawn + SCHEMA( "C_BasePlayerPawn", "m_flFOVSensitivityAdjust"_hash ), desired );
	}

	void camera::do_thirdperson( std::uintptr_t view_setup, std::uintptr_t target_pawn ) const
	{
		const auto& cfg = settings::g_misc.m_camera;
		const auto local = systems::g_local.get( );

		// If spectating another player, manage observer mode for thirdperson model rendering
		if ( !local.is_alive && local.observer_pawn )
		{
			const auto local_player_controller = memory::read<std::uintptr_t>( addresses::globals::local_player_controller );
			if ( local_player_controller )
			{
				const auto obs_pawn_handle = memory::read<std::uint32_t>( local_player_controller + SCHEMA( "CCSPlayerController", "m_hObserverPawn"_hash ) );
				const auto obs_pawn = systems::g_entities.lookup( obs_pawn_handle );
				if ( obs_pawn )
				{
					const auto obs_services = memory::read<std::uintptr_t>( obs_pawn + SCHEMA( "C_BasePlayerPawn", "m_pObserverServices"_hash ) );
					if ( obs_services )
					{
						const auto current_mode = memory::read<std::uint8_t>( obs_services + SCHEMA( "CPlayer_ObserverServices", "m_iObserverMode"_hash ) );
						if ( !cfg.thirdperson.value )
						{
							if ( current_mode == 5 )
							{
								memory::write<std::uint8_t>( obs_services + SCHEMA( "CPlayer_ObserverServices", "m_iObserverMode"_hash ), 4 );
							}
							return;
						}
						else if ( current_mode == 4 )
						{
							memory::write<std::uint8_t>( obs_services + SCHEMA( "CPlayer_ObserverServices", "m_iObserverMode"_hash ), 5 );
						}
					}
				}
			}
		}

		if ( !cfg.thirdperson.value )
		{
			return;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( target_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		math::vector3 eye_position = memory::read<math::vector3>( view_setup + 0x4a0 );
		if ( game_scene_node )
		{
			const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
			const auto view_offset = memory::read<math::vector3>( target_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
			if ( origin.length_sqr( ) > 0.0f )
			{
				eye_position = origin + view_offset;
			}
		}

		const auto view_angles = local.is_alive ? systems::g_input.get_view_angles( ) : memory::read<math::vector3>( view_setup + 0x4b8 );

		math::vector3 forward{};
		{
			math::helpers::angle_vectors_left( view_angles, &forward );
		}

		const float distance = cfg.thirdperson_distance.value;
		const float hull_size = cfg.thirdperson_hull_size.value;

		auto camera_position = eye_position - forward * distance;
		const auto hull = math::vector3{ -hull_size, -hull_size, -hull_size };
		const auto result = systems::g_tracing.trace_hull( eye_position, camera_position, hull, hull, target_pawn );

		if ( result.fraction < 1.0f )
		{
			const auto world = systems::g_entities.get_by_index( 0 );
			if ( result.hit_entity == world )
			{
				camera_position = eye_position + ( camera_position - eye_position ) * result.fraction;
			}
		}

		memory::write<math::vector3>( view_setup + 0x4a0, camera_position );
	}

	void camera::do_fov_change( std::uintptr_t view_setup, std::uintptr_t target_pawn ) const
	{
		const auto& cfg = settings::g_misc.m_camera;
		if ( !cfg.change_fov.value )
		{
			return;
		}

		const auto is_scoped = memory::read<bool>( target_pawn + SCHEMA( "C_CSPlayerPawn", "m_bIsScoped"_hash ) );
		const auto target_fov = ( is_scoped && cfg.scoped_fov_override.value ) ? cfg.scoped_fov.value : cfg.fov.value;

		memory::write<float>( view_setup + k_fov_offset, target_fov );

		this->update_fov_sensitivity( target_pawn );
	}

	void camera::do_aspect_ratio_change( std::uintptr_t view_setup )
	{
		const auto& cfg = settings::g_misc.m_camera;

		if ( cfg.change_aspect_ratio.value )
		{
			const auto base_fov = memory::read<float>( view_setup + k_fov_offset );
			const auto flags = memory::read<std::uint8_t>( view_setup + k_view_flags_offset );

			// Explicit aspect bypasses the game's native 4:3-based FOV conversion.
			memory::write<float>( view_setup + k_fov_offset, scale_horizontal_fov( base_fov, cfg.aspect_ratio ) );
			memory::write<float>( view_setup + k_aspect_ratio_offset, cfg.aspect_ratio );
			memory::write<std::uint8_t>( view_setup + k_view_flags_offset, flags | k_explicit_aspect_ratio_flag );
		}
		else
		{
			const auto flags = memory::read<std::uint8_t>( view_setup + k_view_flags_offset );
			memory::write<std::uint8_t>( view_setup + k_view_flags_offset,
				flags & static_cast<std::uint8_t>( ~k_explicit_aspect_ratio_flag ) );
		}
	}

	bool camera::do_freecam( std::uintptr_t view_setup )
	{
		const auto& cfg = settings::g_misc.m_camera;
		if ( !cfg.freecam.value )
		{
			if ( this->m_was_freecam_active )
			{
				if ( cfg.freecam_block_input.value )
				{
					systems::g_input.set_view_angles( this->m_saved_viewangles );
				}

				const auto local = systems::g_local.get( );
				if ( !local.is_alive && local.observer_pawn && !settings::g_misc.m_camera.spectator_thirdperson.value )
				{
					const auto local_player_controller = memory::read<std::uintptr_t>( addresses::globals::local_player_controller );
					if ( local_player_controller )
					{
						const auto obs_pawn_handle = memory::read<std::uint32_t>( local_player_controller + SCHEMA( "CCSPlayerController", "m_hObserverPawn"_hash ) );
						const auto obs_pawn = systems::g_entities.lookup( obs_pawn_handle );
						if ( obs_pawn )
						{
							const auto obs_services = memory::read<std::uintptr_t>( obs_pawn + SCHEMA( "C_BasePlayerPawn", "m_pObserverServices"_hash ) );
							if ( obs_services )
							{
								const auto current_mode = memory::read<std::uint8_t>( obs_services + SCHEMA( "CPlayer_ObserverServices", "m_iObserverMode"_hash ) );
								if ( current_mode == 5 )
								{
									memory::write<std::uint8_t>( obs_services + SCHEMA( "CPlayer_ObserverServices", "m_iObserverMode"_hash ), 4 );
								}
							}
						}
					}
				}

				this->m_was_freecam_active = false;
				s_was_spec_freecam = false;
				s_global_mouse_valid = false;
				s_win_cursor_valid = false;
			}
			return false;
		}

		const auto now = std::chrono::steady_clock::now( );
		if ( !this->m_was_freecam_active )
		{
			this->m_freecam_pos = memory::read<math::vector3>( view_setup + 0x4a0 );
			this->m_saved_viewangles = systems::g_input.get_view_angles( );
			this->m_last_override_time = now;
			this->m_was_freecam_active = true;
			s_was_spec_freecam = false;
			s_global_mouse_valid = false;
			s_win_cursor_valid = false;
			s_spec_freecam_angles = memory::read<math::vector3>( view_setup + 0x4b8 );
		}

		const float dt = std::clamp( std::chrono::duration<float>( now - this->m_last_override_time ).count( ), 0.0f, 0.1f );
		this->m_last_override_time = now;

		const auto local = systems::g_local.get( );

		// In spectator mode, switch to observer mode 5 so the full player model is rendered
		if ( !local.is_alive && local.observer_pawn )
		{
			const auto local_player_controller = memory::read<std::uintptr_t>( addresses::globals::local_player_controller );
			if ( local_player_controller )
			{
				const auto obs_pawn_handle = memory::read<std::uint32_t>( local_player_controller + SCHEMA( "CCSPlayerController", "m_hObserverPawn"_hash ) );
				const auto obs_pawn = systems::g_entities.lookup( obs_pawn_handle );
				if ( obs_pawn )
				{
					const auto obs_services = memory::read<std::uintptr_t>( obs_pawn + SCHEMA( "C_BasePlayerPawn", "m_pObserverServices"_hash ) );
					if ( obs_services )
					{
						const auto current_mode = memory::read<std::uint8_t>( obs_services + SCHEMA( "CPlayer_ObserverServices", "m_iObserverMode"_hash ) );
						if ( current_mode == 4 )
						{
							memory::write<std::uint8_t>( obs_services + SCHEMA( "CPlayer_ObserverServices", "m_iObserverMode"_hash ), 5 );
						}
					}
				}
			}
		}

		math::vector3 view_angles{};
		if ( local.is_alive )
		{
			s_was_spec_freecam = false;
			s_global_mouse_valid = false;
			s_win_cursor_valid = false;
			view_angles = systems::g_input.get_view_angles( );
		}
		else
		{
			if ( !s_was_spec_freecam )
			{
				s_spec_freecam_angles = memory::read<math::vector3>( view_setup + 0x4b8 );
				s_was_spec_freecam = true;
				s_global_mouse_valid = false;
				s_win_cursor_valid = false;
			}

			float dx = 0.0f, dy = 0.0f;
			query_mouse_delta( dx, dy );

			const bool can_rotate = !rendering::g_menu.is_open( ) && is_game_window_focused( );
			if ( can_rotate )
			{
				if ( std::fabsf( dx ) > 0.0001f || std::fabsf( dy ) > 0.0001f )
				{
					float sens = 1.0f;
					if ( const auto cvar = CONVAR( "sensitivity" ) )
					{
						sens = cvar->get< float >( );
					}
					sens = std::max( sens, 0.001f );

					float m_pitch = 0.022f;
					if ( const auto cvar = CONVAR( "m_pitch" ) )
					{
						m_pitch = cvar->get< float >( );
					}
					float m_yaw = 0.022f;
					if ( const auto cvar = CONVAR( "m_yaw" ) )
					{
						m_yaw = cvar->get< float >( );
					}

					s_spec_freecam_angles.x = std::clamp( s_spec_freecam_angles.x + dy * m_pitch * sens, -89.0f, 89.0f );
					s_spec_freecam_angles.y = math::helpers::normalize_yaw( s_spec_freecam_angles.y - dx * m_yaw * sens );
					s_spec_freecam_angles.z = 0.0f;
				}
			}
			else
			{
				s_global_mouse_valid = false;
				s_win_cursor_valid = false;
			}

			view_angles = s_spec_freecam_angles;
		}

		const bool can_move = !rendering::g_menu.is_open( ) && is_game_window_focused( );
		if ( can_move && dt > 0.0f )
		{
			math::vector3 forward{}, left{}, up{};
			math::helpers::angle_vectors_left( view_angles, &forward, &left, &up );

			math::vector3 move_dir{};
			if ( ( GetAsyncKeyState( 'W' ) & 0x8000 ) != 0 ) move_dir += forward;
			if ( ( GetAsyncKeyState( 'S' ) & 0x8000 ) != 0 ) move_dir -= forward;
			if ( ( GetAsyncKeyState( 'A' ) & 0x8000 ) != 0 ) move_dir -= left;
			if ( ( GetAsyncKeyState( 'D' ) & 0x8000 ) != 0 ) move_dir += left;
			if ( ( GetAsyncKeyState( VK_SPACE ) & 0x8000 ) != 0 ) move_dir.z += 1.0f;
			if ( ( ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) != 0 ) || ( ( GetAsyncKeyState( 'C' ) & 0x8000 ) != 0 ) ) move_dir.z -= 1.0f;

			float speed = cfg.freecam_speed.value;
			if ( ( GetAsyncKeyState( VK_SHIFT ) & 0x8000 ) != 0 )
			{
				speed *= 2.5f;
			}
			else if ( ( GetAsyncKeyState( VK_MENU ) & 0x8000 ) != 0 )
			{
				speed *= 0.3f;
			}

			if ( move_dir.length_sqr( ) > 0.0001f )
			{
				move_dir = move_dir.normalized( );
				this->m_freecam_pos += move_dir * ( speed * dt );
			}
		}

		memory::write<math::vector3>( view_setup + 0x4a0, this->m_freecam_pos );
		memory::write<math::vector3>( view_setup + 0x4b8, view_angles );
		return true;
	}

	void camera::on_create_move( systems::input::usercmd* cmd )
	{
		const auto& cfg = settings::g_misc.m_camera;
		if ( !cfg.freecam.value || !cfg.freecam_block_input.value )
		{
			return;
		}

		if ( !this->m_was_freecam_active )
		{
			this->m_saved_viewangles = systems::g_input.get_view_angles( );
		}

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

			if ( const auto va = base->mutable_viewangles( ) )
			{
				va->set_x( this->m_saved_viewangles.x );
				va->set_y( this->m_saved_viewangles.y );
				va->set_z( this->m_saved_viewangles.z );
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

			if ( const auto angles = entry->mutable_view_angles( ) )
			{
				angles->set_x( this->m_saved_viewangles.x );
				angles->set_y( this->m_saved_viewangles.y );
				angles->set_z( this->m_saved_viewangles.z );
			}
		}

		cmd->buttons.value = 0;
		cmd->buttons.value_changed = 0;
		cmd->buttons.value_scroll = 0;
	}

	void camera::reset( )
	{
		if ( this->m_was_freecam_active && settings::g_misc.m_camera.freecam_block_input.value )
		{
			systems::g_input.set_view_angles( this->m_saved_viewangles );
		}
		this->m_was_freecam_active = false;
		s_was_spec_freecam = false;
		s_global_mouse_valid = false;
		s_win_cursor_valid = false;
		this->m_freecam_pos = {};
		s_spec_freecam_angles = {};
		this->m_saved_viewangles = {};
		this->m_cached_fov_sensitivity = -1.0f;
		this->m_cached_scoped = false;
		this->m_cached_target_fov = 0.0f;
	}

} // namespace features::misc
