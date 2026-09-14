#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../movement.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

	namespace {
		constexpr float jb_standing_height = 72.0f;
		constexpr float jb_ducked_height = 54.0f;
		constexpr float jb_max_expansion = jb_standing_height - jb_ducked_height; // 18.0f
		constexpr float jb_ground_probe = 2.0f;

		bool jb_finite( const math::vector3& value )
		{
			return std::isfinite( value.x ) && std::isfinite( value.y ) && std::isfinite( value.z );
		}
	} // namespace

	void jumpbug::on_create_move( systems::input::usercmd* cmd, std::uint64_t original_buttons )
	{
		this->m_active_this_tick = false;
		if ( !cmd )
		{
			this->m_fired_last_tick = false;
			return;
		}

		// Handle jump release cleanup on the tick following a successful jumpbug fire
		if ( this->m_fired_last_tick )
		{
			this->m_fired_last_tick = false;
			cmd->buttons.value &= ~cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_jump;
		}

		const auto& config = settings::g_movement.jumpbug;
		const bool is_enabled = config.value || ( config.bind.key != 0 && config.bind.active );
		if ( !is_enabled )
		{
			return;
		}

		const auto local = systems::g_local.get( );
		const auto& pre = systems::g_prediction.pre( );
		if ( !local.pawn || !local.is_alive || !pre.movement_valid || pre.pawn != local.pawn )
		{
			return;
		}

		// If already on ground, jumpbug does not apply
		if ( ( pre.flags & cstypes::entity_flags::on_ground ) != 0 )
		{
			return;
		}

		// Ladders and noclip cannot jumpbug
		const auto move_type = memory::read<std::uint8_t>( local.pawn + SCHEMA( "C_BaseEntity", "m_nActualMoveType"_hash ) );
		if ( move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip )
		{
			return;
		}

		// Only run when falling downwards
		if ( !jb_finite( pre.networked_origin ) || !jb_finite( pre.networked_velocity ) || pre.networked_velocity.z >= 0.0f )
		{
			return;
		}

		const auto movement_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pMovementServices"_hash ) );
		if ( !movement_services )
		{
			return;
		}

		const auto pawn_ptr = memory::read<std::uintptr_t>( movement_services + 56 );
		if ( !pawn_ptr )
		{
			return;
		}

		auto trace_mask = memory::read<std::uint64_t>( pawn_ptr + 0xd48 );
		if ( memory::read<std::uint32_t>( pawn_ptr + 0x3f8 ) & 0x10 )
		{
			trace_mask |= 0x20;
		}

		const auto gravity_var = CONVAR( "sv_gravity" );
		const auto normal_var = CONVAR( "sv_standable_normal" );
		if ( !gravity_var || !normal_var )
		{
			return;
		}

		const float dt = cstypes::tick_interval;
		const float gravity = gravity_var->get<float>( ) * pre.gravity_scale;
		const float standable_normal = normal_var->get<float>( );
		if ( !std::isfinite( dt ) || dt <= 0.0f || !std::isfinite( gravity ) || gravity < 0.0f ||
			!std::isfinite( standable_normal ) || standable_normal <= 0.0f || standable_normal > 1.0f )
		{
			return;
		}

		const auto filter = systems::g_tracing.make_player_movement_filter( local.pawn, trace_mask, 11 );

		// Calculate current hull dimensions and air unduck expansion based on duck_amount
		const float duck_amount = std::isfinite( pre.duck_amount ) ? std::clamp( pre.duck_amount, 0.0f, 1.0f ) : 0.0f;
		const float expansion = jb_max_expansion * duck_amount;
		const float current_height = jb_standing_height - expansion;

		// Unducked hull: head stays at current_height, feet expand down by expansion.
		// Total height: current_height - (-expansion) = 72.0f exactly.
		const systems::tracing::bbox_collision unducked_hull{
			math::vector3{ -16.0f, -16.0f, -expansion },
			math::vector3{ 16.0f, 16.0f, current_height }
		};

		// Current hull: from feet (0.0f) to head (current_height).
		const systems::tracing::bbox_collision current_hull{
			math::vector3{ -16.0f, -16.0f, 0.0f },
			math::vector3{ 16.0f, 16.0f, current_height }
		};

		// Compute velocity and movement displacement for this tick under gravity
		auto move_velocity = pre.networked_velocity;
		move_velocity.z -= gravity * dt * 0.5f;
		const math::vector3 tick_delta = move_velocity * dt;
		if ( !jb_finite( tick_delta ) )
		{
			return;
		}

		const math::vector3 start_pos = pre.networked_origin;
		const math::vector3 end_pos = start_pos + tick_delta;

		// Extend trace end by the 2-unit ground snap probe distance
		math::vector3 probe_end = end_pos;
		probe_end.z -= jb_ground_probe;

		// Trace the unducked hull along the tick trajectory
		const auto trace_unduck = systems::g_tracing.trace_player_bbox( start_pos, probe_end, unducked_hull, filter, movement_services );
		// Trace current hull as fallback for standing landings
		const auto trace_curr = systems::g_tracing.trace_player_bbox( start_pos, probe_end, current_hull, filter, movement_services );

		bool fire = false;
		float fire_when = 0.0f;

		// Check if unducked hull reaches standable ground this tick
		if ( trace_unduck.all_solid || trace_unduck.fraction <= 0.0f )
		{
			// Already touching or inside the ground snap window at tick boundary
			fire = true;
			fire_when = 1.0f / 64.0f;
		}
		else if ( !trace_unduck.all_solid && trace_unduck.fraction < 1.0f && trace_unduck.normal.z >= standable_normal )
		{
			// Reaches ground snap window at fractional tick
			fire = true;
			fire_when = std::clamp( trace_unduck.fraction, 1.0f / 64.0f, 63.0f / 64.0f );
		}
		else if ( !trace_curr.all_solid && trace_curr.fraction < 1.0f && trace_curr.normal.z >= standable_normal )
		{
			// Standing hull landing fallback
			fire = true;
			fire_when = std::clamp( trace_curr.fraction, 1.0f / 64.0f, 63.0f / 64.0f );
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		const auto moves = base ? base->mutable_subtick_moves( ) : nullptr;
		constexpr auto controlled = cstypes::command_buttons::in_duck | cstypes::command_buttons::in_jump;

		if ( fire )
		{
			// Strip any existing duck or jump subtick steps from earlier features to prevent conflicts
			if ( moves )
			{
				for ( int i = 0; i < moves->m_current_size; ++i )
				{
					const auto step = base->mutable_subtick_moves( i );
					if ( step && ( step->button( ) & controlled ) != 0 )
					{
						step->set_button( step->button( ) & ~controlled );
						if ( step->button( ) == 0 )
						{
							step->set_pressed( false );
						}
					}
				}

				// Unduck at fire_when: duck = false
				if ( const auto duck_up = systems::g_input.acquire_subtick_step( moves ) )
				{
					duck_up->set_button( cstypes::command_buttons::in_duck );
					duck_up->set_pressed( false );
					duck_up->set_when( fire_when );
					duck_up->set_analog_forward_delta( 0.0f );
					duck_up->set_analog_left_delta( 0.0f );
				}

				// Jump release step immediately preceding fire_when ensures clean edge-trigger transition
				const float release_when = std::clamp( fire_when - 1.0f / 64.0f, 0.0f, 62.0f / 64.0f );
				if ( release_when < fire_when )
				{
					if ( const auto jump_up = systems::g_input.acquire_subtick_step( moves ) )
					{
						jump_up->set_button( cstypes::command_buttons::in_jump );
						jump_up->set_pressed( false );
						jump_up->set_when( release_when );
						jump_up->set_analog_forward_delta( 0.0f );
						jump_up->set_analog_left_delta( 0.0f );
					}
				}

				// Jump press at fire_when: jump = true
				if ( const auto jump_down = systems::g_input.acquire_subtick_step( moves ) )
				{
					jump_down->set_button( cstypes::command_buttons::in_jump );
					jump_down->set_pressed( true );
					jump_down->set_when( fire_when );
					jump_down->set_analog_forward_delta( 0.0f );
					jump_down->set_analog_left_delta( 0.0f );
				}
			}

			// Final base buttons for this command: duck is released, jump is pressed
			cmd->buttons.value &= ~cstypes::command_buttons::in_duck;
			cmd->buttons.value |= cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed |= controlled;
			cmd->buttons.value_scroll &= ~controlled;

			this->m_fired_last_tick = true;
			this->m_active_this_tick = true;
		}
		else
		{
			// In mid-air and falling down:
			// Strip jump inputs so m_nOldButtons has jump released on previous ticks,
			// and hold duck to ensure maximum unduck expansion cushion when landing.
			if ( moves )
			{
				for ( int i = 0; i < moves->m_current_size; ++i )
				{
					const auto step = base->mutable_subtick_moves( i );
					if ( step && ( step->button( ) & controlled ) != 0 )
					{
						step->set_button( step->button( ) & ~controlled );
						if ( step->button( ) == 0 )
						{
							step->set_pressed( false );
						}
					}
				}

				// Add duck step at beginning of tick
				if ( const auto duck_down = systems::g_input.acquire_subtick_step( moves ) )
				{
					duck_down->set_button( cstypes::command_buttons::in_duck );
					duck_down->set_pressed( true );
					duck_down->set_when( 0.0f );
					duck_down->set_analog_forward_delta( 0.0f );
					duck_down->set_analog_left_delta( 0.0f );
				}
			}

			cmd->buttons.value |= cstypes::command_buttons::in_duck;
			cmd->buttons.value &= ~cstypes::command_buttons::in_jump;
			cmd->buttons.value_changed |= controlled;
			cmd->buttons.value_scroll &= ~controlled;

			this->m_active_this_tick = true;
		}
	}

} // namespace features::movement
