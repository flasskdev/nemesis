#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>

#include "../movement.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

	void jumpbug::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_active_this_tick = false;

		const bool is_bound = ( settings::g_movement.jumpbug.bind.key != 0 );
		const bool is_key_active = is_bound && ( settings::g_movement.jumpbug.bind.active || settings::g_movement.jumpbug.value );
		const bool is_jump_held = ( cmd->buttons.value & cstypes::command_buttons::in_jump ) != 0;
		const bool holding_duck = ( cmd->buttons.value & cstypes::command_buttons::in_duck ) != 0;

		const bool jumpbug_wanted = is_key_active || ( settings::g_movement.jumpbug.value && ( is_jump_held || holding_duck ) );
		if ( !jumpbug_wanted )
		{
			return;
		}

		if ( features::movement::g_edgebug.active_this_tick( ) )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		if ( !local.pawn )
		{
			return;
		}

		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		const auto& prestate = systems::g_prediction.pre( );
		if ( prestate.flags & cstypes::entity_flags::on_ground )
		{
			return;
		}

		if ( prestate.networked_velocity.z > 0.0f )
		{
			return;
		}

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		const auto duck_amount = memory::read<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flDuckAmount"_hash ) );
		const auto mins = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash ) + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ) );
		const auto maxs = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash ) + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ) );

		constexpr float standing_height = 72.0f;
		const float current_height = maxs.z;
		const float duck_hull_diff = std::max( 0.0f, standing_height - current_height );

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
		const auto sv_gravity = CONVAR( "sv_gravity" )->get<float>( );
		const auto sv_standable_normal = CONVAR( "sv_standable_normal" )->get<float>( );
		const auto gravity_scale = memory::read<float>( local.pawn + SCHEMA( "C_BaseEntity", "m_flGravityScale"_hash ) );

		auto velocity = prestate.networked_velocity;
		velocity.z -= ( gravity_scale * sv_gravity * cstypes::tick_interval ) * 0.5f;

		// 1) Test if unducking causes ground contact this tick.
		// When unducking in mid-air, the player's head remains at the same height while feet extend downward by duck_hull_diff.
		math::vector3 unducked_start = prestate.networked_origin;
		unducked_start.z -= duck_hull_diff;

		math::vector3 unducked_end{};
		unducked_end.x = unducked_start.x + velocity.x * cstypes::tick_interval;
		unducked_end.y = unducked_start.y + velocity.y * cstypes::tick_interval;
		unducked_end.z = unducked_start.z + velocity.z * cstypes::tick_interval - 2.0f;

		const math::vector3 standing_maxs{ maxs.x, maxs.y, standing_height };
		auto trace_result = systems::g_tracing.trace_player_bbox( unducked_start, unducked_end, { mins, standing_maxs }, filter, movement_services );

		bool can_jumpbug = false;
		float landing_fraction = 1.0f;

		if ( trace_result.fraction > 0.0f && trace_result.fraction < 1.0f && trace_result.normal.z >= sv_standable_normal )
		{
			can_jumpbug = true;
			landing_fraction = trace_result.fraction;
		}
		else if ( trace_result.fraction == 0.0f || trace_result.all_solid )
		{
			// Ground is already within duck_hull_diff below player's feet
			math::vector3 check_end = prestate.networked_origin;
			check_end.z += velocity.z * cstypes::tick_interval - 2.0f;
			const auto check_result = systems::g_tracing.trace_player_bbox( prestate.networked_origin, check_end, { mins, maxs }, filter, movement_services );
			if ( check_result.fraction < 1.0f && check_result.normal.z >= sv_standable_normal )
			{
				can_jumpbug = true;
				landing_fraction = std::max( 1.0f / 64.0f, check_result.fraction );
			}
			else if ( duck_hull_diff > 0.0f )
			{
				math::vector3 close_end = prestate.networked_origin;
				close_end.z -= ( duck_hull_diff + 4.0f );
				const auto close_result = systems::g_tracing.trace_player_bbox( prestate.networked_origin, close_end, { mins, maxs }, filter, movement_services );
				if ( close_result.fraction < 1.0f && close_result.normal.z >= sv_standable_normal )
				{
					can_jumpbug = true;
					landing_fraction = 1.0f / 64.0f;
				}
			}
		}

		if ( !can_jumpbug )
		{
			const bool should_autoduck = is_key_active || ( prestate.networked_velocity.z <= -350.0f );
			if ( should_autoduck )
			{
				cmd->buttons.value |= cstypes::command_buttons::in_duck;
				cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;
			}
			return;
		}

		// Landing occurs this tick! Execute jumpbug:
		this->m_active_this_tick = true;

		const auto when = std::clamp( std::round( landing_fraction * 64.0f ) / 64.0f, 1.0f / 64.0f, 63.0f / 64.0f );
		this->m_landing_fraction = when;

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		cmd->buttons.value &= ~cstypes::command_buttons::in_duck;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;

		cmd->buttons.value &= ~cstypes::command_buttons::in_jump;
		cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;

		const auto subtick_moves = base->mutable_subtick_moves( );

		if ( const auto duck_up = systems::g_input.acquire_subtick_step( subtick_moves ) )
		{
			duck_up->set_button( cstypes::command_buttons::in_duck );
			duck_up->set_pressed( false );
			duck_up->set_when( 0.0f );
			duck_up->set_analog_forward_delta( 0.0f );
			duck_up->set_analog_left_delta( 0.0f );
		}

		const auto release_when = std::max( 0.0f, when - ( 1.0f / 64.0f ) );
		if ( release_when < when )
		{
			if ( const auto jump_up = systems::g_input.acquire_subtick_step( subtick_moves ) )
			{
				jump_up->set_button( cstypes::command_buttons::in_jump );
				jump_up->set_pressed( false );
				jump_up->set_when( release_when );
				jump_up->set_analog_forward_delta( 0.0f );
				jump_up->set_analog_left_delta( 0.0f );
			}
		}

		if ( const auto jump_down = systems::g_input.acquire_subtick_step( subtick_moves ) )
		{
			jump_down->set_button( cstypes::command_buttons::in_jump );
			jump_down->set_pressed( true );
			jump_down->set_when( when );
			jump_down->set_analog_forward_delta( 0.0f );
			jump_down->set_analog_left_delta( 0.0f );
		}
	}

	float jumpbug::get_impulse_mul( std::uintptr_t local_pawn ) const
	{
		const auto movement_services = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return 0.0f;
		}

		const auto stamina = memory::read<float>( movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_flStamina"_hash ) );

		if (CONVAR ("sv_legacy_jump")->get<bool>( ) )
		{
			if ( stamina <= 0.0f )
			{
				return 1.0f;
			}

			return std::clamp( 1.0f - ( stamina / 100.0f ), 0.0f, 1.0f );
		}

		const auto current_tick = memory::read<int>( memory::read<std::uintptr_t>( addresses::globals::global_vars ) + 0x44 );
		const auto modern_jump = movement_services + SCHEMA( "CCSPlayer_MovementServices", "m_ModernJump"_hash );
		const auto landing_vel_z = memory::read<float>( modern_jump + SCHEMA( "CCSPlayerModernJump", "m_flLastLandedVelocityZ"_hash ) );
		const auto landed_tick = memory::read<std::uint32_t>( modern_jump + SCHEMA( "CCSPlayerModernJump", "m_nLastLandedTick"_hash ) );
		const auto base = std::clamp( ( landing_vel_z * 0.0005f ) + 1.0f, 0.02f, 1.0f );
		const auto ticks_since_landing = static_cast< float >( current_tick - landed_tick );

		auto result = std::clamp( base + ( ticks_since_landing * 0.6f ), 0.0f, 1.0f );

		if ( stamina > 0.0f )
		{
			result *= std::clamp( 1.0f - ( stamina / 100.0f ), 0.0f, 1.0f );
		}

		return result;
	}

} // namespace features::movement