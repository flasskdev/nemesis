#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
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

		// 1. Determine player movement intent (WASD / analog move)
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

		const auto cmd_fwd = forward_input / input_len;
		const auto cmd_left = left_input / input_len;

		// 2. Obtain weapon / movement max speed & target speed
		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		const auto max_speed = movement_services ? memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flMaxspeed"_hash ) ) : 250.0f;
		const auto effective_max_speed = ( max_speed > 0.0f ) ? max_speed : 250.0f;

		const auto target_speed = std::clamp( settings::g_movement.slowwalk_speed.value, 1.0f, effective_max_speed );
		const auto current_speed = prestate.networked_velocity.length_2d( );

		// 3. Physics: calculate required wishspeed taking ground friction & acceleration into account
		const auto c_accelerate = CONVAR( "sv_accelerate" );
		const auto sv_accelerate = c_accelerate ? c_accelerate->get<float>( ) : 5.5f;

		const auto c_friction = CONVAR( "sv_friction" );
		const auto sv_friction = c_friction ? c_friction->get<float>( ) : 5.2f;

		const auto c_stopspeed = CONVAR( "sv_stopspeed" );
		const auto sv_stopspeed = c_stopspeed ? c_stopspeed->get<float>( ) : 80.0f;

		const auto surface_friction = ( prestate.surface_friction > 0.0f ) ? prestate.surface_friction : 1.0f;
		const auto dt = cstypes::tick_interval;

		// Speed remaining after friction this tick
		const auto control = std::fmaxf( current_speed, sv_stopspeed );
		const auto drop = control * sv_friction * surface_friction * dt;
		const auto speed_after_friction = std::fmaxf( 0.0f, current_speed - drop );

		float final_fwd = 0.0f;
		float final_left = 0.0f;

		if ( current_speed > target_speed + 2.0f )
		{
			// Overspeed braking (e.g. running or landing): active counter-strafe to smoothly and rapidly brake to target_speed
			const auto wish_x = -prestate.networked_velocity.x / current_speed;
			const auto wish_y = -prestate.networked_velocity.y / current_speed;

			const auto view_angles = systems::g_input.get_view_angles( );
			const auto cmd_yaw_rad = view_angles.y * ( std::numbers::pi_v<float> / 180.0f );
			const auto sy = std::sinf( cmd_yaw_rad );
			const auto cy = std::cosf( cmd_yaw_rad );

			const auto brake_fwd = wish_x * cy + wish_y * sy;
			const auto brake_left = -( wish_x * sy - wish_y * cy );

			const auto accel_drop = sv_accelerate * effective_max_speed * surface_friction * dt;
			const auto delta_speed = current_speed - target_speed;
			const auto brake_scale = std::clamp( delta_speed / ( accel_drop > 0.001f ? accel_drop : 1.0f ), 0.05f, 1.0f );

			final_fwd = std::clamp( brake_fwd * brake_scale, -1.0f, 1.0f );
			final_left = std::clamp( brake_left * brake_scale, -1.0f, 1.0f );
		}
		else
		{
			// Accelerate / maintain target_speed against friction
			// Source 2 / CS2 acceleration formula: accelspeed = sv_accelerate * wishspeed * dt * surface_friction
			// In steady state: accelspeed must equal target_speed - speed_after_friction
			const auto needed_accel = target_speed - speed_after_friction;
			const auto accel_rate = sv_accelerate * surface_friction * dt;

			float wishspeed = target_speed;
			if ( needed_accel > 0.0f && accel_rate > 0.0001f )
			{
				wishspeed = std::fmaxf( target_speed, needed_accel / accel_rate );
			}

			wishspeed = std::clamp( wishspeed, 0.0f, effective_max_speed );
			const auto speed_ratio = wishspeed / effective_max_speed;

			final_fwd = std::clamp( cmd_fwd * speed_ratio, -1.0f, 1.0f );
			final_left = std::clamp( cmd_left * speed_ratio, -1.0f, 1.0f );
		}

		// 4. Update command buttons
		constexpr auto move_buttons = cstypes::command_buttons::in_forward |
			cstypes::command_buttons::in_back |
			cstypes::command_buttons::in_moveleft |
			cstypes::command_buttons::in_moveright;

		const auto original_buttons = cmd->buttons.value;
		auto buttons = original_buttons;
		buttons &= ~static_cast<std::uintptr_t>( move_buttons );

		if ( final_fwd > 0.02f )       buttons |= cstypes::command_buttons::in_forward;
		else if ( final_fwd < -0.02f ) buttons |= cstypes::command_buttons::in_back;
		if ( final_left > 0.02f )      buttons |= cstypes::command_buttons::in_moveleft;
		else if ( final_left < -0.02f ) buttons |= cstypes::command_buttons::in_moveright;

		// Set in_sprint (CS2 walk key) for silent footsteps and walking animations when moving at or below walking threshold
		if ( target_speed <= effective_max_speed * 0.52f )
		{
			buttons |= cstypes::command_buttons::in_sprint;
		}

		cmd->buttons.value = buttons;
		cmd->buttons.value_changed |= ( original_buttons ^ buttons );

		base->set_forwardmove( final_fwd );
		base->set_leftmove( final_left );

		// 5. Update subtick moves
		const auto subtick_moves = base->mutable_subtick_moves( );
		if ( subtick_moves )
		{
			const auto cur_cmd_fwd = movement_services ? memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flCmdForwardMove"_hash ) ) : prestate.last_movement_impulses.x;
			const auto cur_cmd_left = movement_services ? memory::read<float>( movement_services + SCHEMA( "CPlayer_MovementServices", "m_flCmdLeftMove"_hash ) ) : prestate.last_movement_impulses.y;

			if ( const auto step = systems::g_input.acquire_subtick_step( subtick_moves ) )
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