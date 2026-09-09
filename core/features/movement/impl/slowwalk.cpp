#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include "../movement.hpp"

namespace features::movement {

	void slowwalk::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_active_this_tick = false;

		if ( !cmd )
		{
			return;
		}

		const bool is_active = settings::g_movement.slowwalk.value || settings::g_movement.slowwalk.bind.active;
		if ( !is_active )
		{
			return;
		}

		if ( features::combat::g_rage.should_stop( ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn || !local.is_alive )
		{
			return;
		}

		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );
		if ( !( prestate.flags & cstypes::entity_flags::on_ground ) )
		{
			return;
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		// 1. Determine player movement intent
		float forward_input = base->forwardmove( );
		float left_input = base->leftmove( );

		if ( std::fabsf( forward_input ) < 0.001f && std::fabsf( left_input ) < 0.001f )
		{
			if ( cmd->buttons.value & cstypes::command_buttons::in_forward )
				forward_input += 1.0f;
			if ( cmd->buttons.value & cstypes::command_buttons::in_back )
				forward_input -= 1.0f;
			if ( cmd->buttons.value & cstypes::command_buttons::in_moveleft )
				left_input += 1.0f;
			if ( cmd->buttons.value & cstypes::command_buttons::in_moveright )
				left_input -= 1.0f;
		}

		const auto input_len = std::sqrtf( forward_input * forward_input + left_input * left_input );
		if ( input_len < 0.001f )
		{
			return;
		}

		// 2. Adjust for camera vs cmd viewangle differences (e.g., Anti-Aim rotation)
		const auto view_angles = systems::g_input.get_view_angles( );
		const auto cmd_angles = base->viewangles( );
		const auto yaw_delta_rad = cmd_angles
			? ( view_angles.y - cmd_angles->y( ) ) * ( std::numbers::pi_v<float> / 180.0f )
			: 0.0f;

		const auto cos_delta = std::cosf( yaw_delta_rad );
		const auto sin_delta = std::sinf( yaw_delta_rad );

		const auto cmd_fwd = ( forward_input * cos_delta - left_input * sin_delta ) / input_len;
		const auto cmd_left = ( forward_input * sin_delta + left_input * cos_delta ) / input_len;

		// 3. Obtain maximum movement speed & target speed
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		const auto max_speed = movement_services ? memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) ) : 250.0f;
		const auto effective_max_speed = ( max_speed > 0.0f ) ? max_speed : 250.0f;

		const auto target_speed = std::clamp( settings::g_movement.slowwalk_speed.value, 10.0f, effective_max_speed );
		const auto current_speed = prestate.networked_velocity.length_2d( );

		float final_fwd = 0.0f;
		float final_left = 0.0f;

		auto buttons = cmd->buttons.value;
		buttons &= ~static_cast<std::uintptr_t>(
			cstypes::command_buttons::in_forward |
			cstypes::command_buttons::in_back |
			cstypes::command_buttons::in_moveleft |
			cstypes::command_buttons::in_moveright
		);

		if ( current_speed > target_speed + 2.0f )
		{
			// Overspeed braking (e.g. after landing a jump or running): active counter-strafe to target_speed
			const auto wish_x = -prestate.networked_velocity.x / current_speed;
			const auto wish_y = -prestate.networked_velocity.y / current_speed;

			const auto cmd_yaw_rad = cmd_angles
				? cmd_angles->y( ) * ( std::numbers::pi_v<float> / 180.0f )
				: view_angles.y * ( std::numbers::pi_v<float> / 180.0f );

			const auto sy = std::sinf( cmd_yaw_rad );
			const auto cy = std::cosf( cmd_yaw_rad );

			final_fwd = std::clamp( wish_x * cy + wish_y * sy, -1.0f, 1.0f );
			final_left = std::clamp( -( wish_x * sy - wish_y * cy ), -1.0f, 1.0f );

			if ( final_fwd > 0.05f )
				buttons |= cstypes::command_buttons::in_forward;
			else if ( final_fwd < -0.05f )
				buttons |= cstypes::command_buttons::in_back;

			if ( final_left > 0.05f )
				buttons |= cstypes::command_buttons::in_moveleft;
			else if ( final_left < -0.05f )
				buttons |= cstypes::command_buttons::in_moveright;
		}
		else if ( current_speed < target_speed )
		{
			// Accelerate towards target speed
			const auto c_accelerate = CONVAR( "sv_accelerate" );
			const auto sv_accelerate = c_accelerate ? c_accelerate->get<float>( ) : 5.5f;
			const auto surface_friction = ( prestate.surface_friction > 0.0f ) ? prestate.surface_friction : 1.0f;
			const auto accel_rate = sv_accelerate * surface_friction * cstypes::tick_interval;

			const auto delta_v = target_speed - current_speed;
			float wishspeed = target_speed;
			if ( delta_v > 0.0f && accel_rate > 0.0001f )
			{
				wishspeed = std::fmaxf( target_speed, delta_v / accel_rate );
			}
			wishspeed = std::clamp( wishspeed, 0.0f, effective_max_speed );

			const auto speed_ratio = wishspeed / effective_max_speed;
			final_fwd = std::clamp( cmd_fwd * speed_ratio, -1.0f, 1.0f );
			final_left = std::clamp( cmd_left * speed_ratio, -1.0f, 1.0f );

			if ( cmd_fwd > 0.05f )
				buttons |= cstypes::command_buttons::in_forward;
			else if ( cmd_fwd < -0.05f )
				buttons |= cstypes::command_buttons::in_back;

			if ( cmd_left > 0.05f )
				buttons |= cstypes::command_buttons::in_moveleft;
			else if ( cmd_left < -0.05f )
				buttons |= cstypes::command_buttons::in_moveright;
		}
		else
		{
			// At target speed: throttle movement so friction drops velocity slightly back under target_speed
			final_fwd = 0.0f;
			final_left = 0.0f;
		}

		// Always set in_sprint (CS2 walk key) so game plays walking animations and footstep sounds are silent
		buttons |= cstypes::command_buttons::in_sprint;
		cmd->buttons.value = buttons;

		base->set_forwardmove( final_fwd );
		base->set_leftmove( final_left );

		// Subtick moves handling
		const auto subtick_moves = base->mutable_subtick_moves( );
		if ( subtick_moves )
		{
			constexpr auto dir_buttons = static_cast<std::uint64_t>(
				cstypes::command_buttons::in_forward |
				cstypes::command_buttons::in_back |
				cstypes::command_buttons::in_moveleft |
				cstypes::command_buttons::in_moveright
			);
			for ( int i = 0; i < base->subtick_moves_size( ); ++i )
			{
				if ( auto step = base->mutable_subtick_moves( i ) )
				{
					if ( step->button( ) & dir_buttons )
					{
						step->set_button( 0 );
						step->set_pressed( false );
					}
				}
			}

			const auto cur_cmd_fwd = movement_services ? memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flCmdForwardMove"_hash ) ) : prestate.last_movement_impulses.x;
			const auto cur_cmd_left = movement_services ? memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flCmdLeftMove"_hash ) ) : prestate.last_movement_impulses.y;

			const auto step = systems::g_input.acquire_subtick_step( subtick_moves );
			if ( step )
			{
				step->set_button( 0 );
				step->set_pressed( false );
				step->set_when( 0.0f );
				step->set_analog_forward_delta( final_fwd - cur_cmd_fwd );
				step->set_analog_left_delta( final_left - cur_cmd_left );
			}
		}

		this->m_active_this_tick = true;
	}

} // namespace features::movement