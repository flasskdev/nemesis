#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::movement {

	void quickstop::on_create_move( systems::input::usercmd* cmd ) const
	{
		const bool is_bound = ( settings::g_movement.quickstop.bind.key != 0 );
		const bool is_key_active = is_bound && settings::g_movement.quickstop.bind.active;
		const bool is_setting_active = is_key_active || settings::g_movement.quickstop.value;

		if ( !is_setting_active || settings::g_movement.edgejump.value )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );
		if ( !( prestate.flags & cstypes::entity_flags::on_ground ) )
		{
			return;
		}

		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		const auto game_scene_node = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
		if ( !game_scene_node )
		{
			return;
		}

		const auto origin = memory::read<math::vector3>( game_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		const auto collision = local.pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash );
		const auto mins = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
		const auto maxs = memory::read<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );

		auto trace_mask{ 0ull };
		{
			const auto pawn_ptr = memory::read<std::uintptr_t>( movement_services + 56 );
			trace_mask = memory::read<std::uintptr_t>( pawn_ptr + 0xd48 );

			if ( !pawn_ptr || ( memory::read<std::uint32_t>( pawn_ptr + 0x3f8 ) & 0x10 ) )
			{
				trace_mask |= 0x20;
			}
		}

		const auto filter = systems::g_tracing.make_player_movement_filter( local.pawn, trace_mask, 11 );
		const auto sv_standable_normal = CONVAR( "sv_standable_normal" )->get<float>( );

		const auto velocity = prestate.networked_velocity;
		const auto speed = velocity.length_2d( );

		const auto check_edge = [ & ]( int ticks_ahead ) -> bool
		{
			auto predicted = origin;
			predicted.x += velocity.x * cstypes::tick_interval * static_cast< float >( ticks_ahead );
			predicted.y += velocity.y * cstypes::tick_interval * static_cast< float >( ticks_ahead );

			const auto start = math::vector3{ predicted.x, predicted.y, predicted.z + 2.0f };
			const auto end = math::vector3{ predicted.x, predicted.y, predicted.z - 4.0f };

			const auto result = systems::g_tracing.trace_player_bbox( start, end, { mins, maxs }, filter, movement_services );
			return result.fraction >= 1.0f || result.normal.z < sv_standable_normal;
		};

		const auto at_edge = check_edge( 1 );
		auto approaching_edge = false;

		const auto sv_friction = CONVAR( "sv_friction" )->get<float>( );
		const auto sv_stopspeed = CONVAR( "sv_stopspeed" )->get<float>( );
		const auto sv_accelerate = CONVAR( "sv_accelerate" )->get<float>( );
		const auto max_weapon_speed = combat::g_shared.ctx( ).valid ? combat::g_shared.ctx( ).weapon_max_speed : 250.0f;

		// Calculate maximum stopping capability per tick
		const auto friction_drop = std::fmaxf( speed, sv_stopspeed ) * sv_friction * prestate.surface_friction * cstypes::tick_interval;
		const auto accel_drop = sv_accelerate * max_weapon_speed * prestate.surface_friction * cstypes::tick_interval;
		const auto total_decel_per_tick = std::fmaxf( friction_drop + accel_drop, 1.0f );
		const auto ticks_needed = std::max( 2, static_cast< int >( std::ceilf( speed / total_decel_per_tick ) ) );

		if ( !at_edge && speed > 1.0f )
		{
			for ( auto i = 2; i <= ticks_needed + 2; ++i )
			{
				if ( check_edge( i ) )
				{
					approaching_edge = true;
					break;
				}
			}
		}

		// Activate full stop if keybind is held, or if at/approaching edge with setting enabled
		const bool should_stop = is_key_active || at_edge || approaching_edge;
		if ( !should_stop )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		const auto view_yaw = base->viewangles( )->y( );
		const auto view_yaw_rad = view_yaw * ( std::numbers::pi_v<float> / 180.0f );

		// If player is at the edge and trying to step off it:
		if ( at_edge )
		{
			const auto user_fwd = base->forwardmove( );
			const auto user_left = base->leftmove( );
			const auto wish_x = std::cosf( view_yaw_rad ) * user_fwd - std::sinf( view_yaw_rad ) * user_left;
			const auto wish_y = std::sinf( view_yaw_rad ) * user_fwd + std::cosf( view_yaw_rad ) * user_left;
			const auto wish_len = std::sqrtf( wish_x * wish_x + wish_y * wish_y );

			if ( wish_len > 0.001f )
			{
				auto test_origin = origin;
				test_origin.x += ( wish_x / wish_len ) * 6.0f;
				test_origin.y += ( wish_y / wish_len ) * 6.0f;

				const auto start = math::vector3{ test_origin.x, test_origin.y, origin.z + 2.0f };
				const auto end = math::vector3{ test_origin.x, test_origin.y, origin.z - 4.0f };

				const auto result = systems::g_tracing.trace_player_bbox( start, end, { mins, maxs }, filter, movement_services );
				if ( result.fraction >= 1.0f || result.normal.z < sv_standable_normal )
				{
					// Moving towards the drop: completely zero user input so player cannot walk off
					base->set_forwardmove( 0.0f );
					base->set_leftmove( 0.0f );
					cmd->buttons.value &= ~static_cast< std::uintptr_t >(
						cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
						cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );
				}
			}
		}

		// FULL INSTANT STOP: Apply maximum counter-strafe
		if ( speed > 0.5f )
		{
			// Wishdir is strictly opposite to 2D velocity vector
			const auto wish_norm_x = -velocity.x / speed;
			const auto wish_norm_y = -velocity.y / speed;

			// Rotate wishdir into view-angle space
			const auto cy = std::cosf( view_yaw_rad );
			const auto sy = std::sinf( view_yaw_rad );

			const auto forward_component = wish_norm_x * cy + wish_norm_y * sy;
			const auto left_component = -( wish_norm_x * sy - wish_norm_y * cy );

			// Maximum counter-impulse: full 1.0 magnitude unless remaining speed is within 1 tick's deceleration
			float magnitude = 1.0f;
			if ( speed < accel_drop && accel_drop > 0.001f )
			{
				magnitude = std::clamp( speed / accel_drop, 0.05f, 1.0f );
			}

			const auto forward_move = std::clamp( forward_component * magnitude, -1.0f, 1.0f );
			const auto left_move = std::clamp( left_component * magnitude, -1.0f, 1.0f );

			base->set_forwardmove( forward_move );
			base->set_leftmove( left_move );

			// Set exact counter-strafe buttons
			auto buttons = cmd->buttons.value;
			buttons &= ~static_cast< std::uintptr_t >(
				cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
				cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );

			if ( forward_move > 0.05f )
				buttons |= cstypes::command_buttons::in_forward;
			else if ( forward_move < -0.05f )
				buttons |= cstypes::command_buttons::in_back;

			if ( left_move > 0.05f )
				buttons |= cstypes::command_buttons::in_moveleft;
			else if ( left_move < -0.05f )
				buttons |= cstypes::command_buttons::in_moveright;

			cmd->buttons.value = buttons;

			// Emit subtick move step so engine executes counter-force immediately on subtick 0.0f
			const auto subtick_moves = base->mutable_subtick_moves( );
			if ( subtick_moves )
			{
				if ( const auto step = systems::g_input.acquire_subtick_step( subtick_moves ) )
				{
					step->set_button( 0 );
					step->set_pressed( false );
					step->set_when( 0.0f );
					step->set_analog_forward_delta( forward_move - prestate.last_movement_impulses.x );
					step->set_analog_left_delta( left_move - prestate.last_movement_impulses.y );
				}
			}
		}
		else
		{
			// Speed is negligible (already completely stopped) -> zero out all movement
			base->set_forwardmove( 0.0f );
			base->set_leftmove( 0.0f );

			cmd->buttons.value &= ~static_cast< std::uintptr_t >(
				cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
				cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright );

			const auto subtick_moves = base->mutable_subtick_moves( );
			if ( subtick_moves )
			{
				if ( const auto step = systems::g_input.acquire_subtick_step( subtick_moves ) )
				{
					step->set_button( 0 );
					step->set_pressed( false );
					step->set_when( 0.0f );
					step->set_analog_forward_delta( 0.0f - prestate.last_movement_impulses.x );
					step->set_analog_left_delta( 0.0f - prestate.last_movement_impulses.y );
				}
			}
		}
	}

} // namespace features::movement
