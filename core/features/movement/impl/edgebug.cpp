#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>

#include "../movement.hpp"
#include <protection/game_addresses.hpp>

namespace features::movement {

	namespace {

		struct sim_step_result
		{
			math::vector3 end_pos{};
			math::vector3 final_pos{};
			math::vector3 velocity{};
			float hit_fraction{ 1.0f };
			math::vector3 hit_normal{};
			bool hit_standable{ false };
			bool slid_off_edge{ false };
		};

		[[nodiscard]] sim_step_result simulate_tick_movement(
			const math::vector3& start_pos,
			const math::vector3& start_vel,
			const math::vector3& box_mins,
			const math::vector3& box_maxs,
			const systems::tracing::player_movement_filter& filter,
			std::uintptr_t movement_services,
			float sv_gravity,
			float sv_standable_normal,
			float gravity_scale,
			float dt,
			int mode )
		{
			sim_step_result res{};
			res.end_pos = start_pos;
			res.final_pos = start_pos;
			res.velocity = start_vel;

			// Apply half-gravity before move
			res.velocity.z -= ( gravity_scale * sv_gravity * dt ) * 0.5f;

			const math::vector3 move_target = start_pos + res.velocity * dt;
			const auto sweep_trace = systems::g_tracing.trace_player_bbox(
				start_pos,
				move_target,
				{ box_mins, box_maxs },
				filter,
				movement_services );

			if ( sweep_trace.fraction >= 1.0f )
			{
				// Clean movement in air
				res.end_pos = move_target;
				res.final_pos = move_target;
				res.velocity.z -= ( gravity_scale * sv_gravity * dt ) * 0.5f;
				return res;
			}

			res.hit_fraction = sweep_trace.fraction;
			res.hit_normal = sweep_trace.normal;
			res.end_pos = sweep_trace.end_pos;

			if ( sweep_trace.normal.z < sv_standable_normal )
			{
				// Hit wall or steep plane
				res.final_pos = sweep_trace.end_pos;
				res.velocity.z -= ( gravity_scale * sv_gravity * dt ) * 0.5f;
				return res;
			}

			// Standable ground collision
			res.hit_standable = true;

			// Clip velocity against the plane normal: v_clipped = v - normal * (v . normal)
			const float backoff = res.velocity.x * sweep_trace.normal.x +
				res.velocity.y * sweep_trace.normal.y +
				res.velocity.z * sweep_trace.normal.z;

			math::vector3 clipped_vel{
				res.velocity.x - sweep_trace.normal.x * backoff,
				res.velocity.y - sweep_trace.normal.y * backoff,
				res.velocity.z - sweep_trace.normal.z * backoff
			};

			const float time_left = dt * ( 1.0f - sweep_trace.fraction );
			const math::vector3 slide_target = sweep_trace.end_pos + clipped_vel * time_left;

			const auto slide_trace = systems::g_tracing.trace_player_bbox(
				sweep_trace.end_pos,
				slide_target,
				{ box_mins, box_maxs },
				filter,
				movement_services );

			const math::vector3 slide_final = sweep_trace.end_pos + ( slide_target - sweep_trace.end_pos ) * slide_trace.fraction;
			res.final_pos = slide_final;

			// Half-gravity for the remainder of the tick
			clipped_vel.z -= ( gravity_scale * sv_gravity * time_left ) * 0.5f;
			res.velocity = clipped_vel;

			// CategorizePosition ground check: trace 2.0f units down from slide_final
			math::vector3 ground_check_end = slide_final;
			ground_check_end.z -= 2.0f;

			const auto ground_trace = systems::g_tracing.trace_player_bbox(
				slide_final,
				ground_check_end,
				{ box_mins, box_maxs },
				filter,
				movement_services );

			// If the player's bounding box slid off the surface into empty air:
			if ( ground_trace.fraction >= 1.0f || ground_trace.normal.z < sv_standable_normal )
			{
				// In mode 1 (edge trace), verify there is an actual fall/drop ahead (not just a 2-unit microstep)
				if ( mode == 1 )
				{
					math::vector3 drop_check_end = slide_final;
					drop_check_end.z -= 6.0f;

					const auto drop_trace = systems::g_tracing.trace_player_bbox(
						slide_final,
						drop_check_end,
						{ box_mins, box_maxs },
						filter,
						movement_services );

					if ( drop_trace.fraction < 1.0f && drop_trace.normal.z >= sv_standable_normal )
					{
						return res;
					}
				}

				res.slid_off_edge = true;
			}

			return res;
		}

		[[nodiscard]] bool mode_allows( int mode, bool holding_jump, float vel2d, float vz )
		{
			switch ( mode )
			{
			case 0:
				return true;
			case 1:
				return true;
			case 2:
				return !holding_jump;
			case 3:
				return vel2d > 15.0f;
			case 4:
				return vel2d > 25.0f && vz < -100.0f;
			default:
				return true;
			}
		}

	} // namespace

	void edgebug::on_create_move( systems::input::usercmd* cmd )
	{
		this->m_active_this_tick = false;

		const bool is_bound = ( settings::g_movement.edgebug.bind.key != 0 );
		const bool is_active = is_bound
			? ( settings::g_movement.edgebug.bind.active || settings::g_movement.edgebug.value )
			: settings::g_movement.edgebug.value;

		if ( !is_active )
		{
			return;
		}

		const auto mode = std::clamp( settings::g_movement.edgebug_mode.value, 0, 4 );
		const auto passes_cfg = settings::g_movement.edgebug_passes.value;
		const int max_sim_ticks = ( passes_cfg <= 0 ) ? 64 : std::clamp( passes_cfg, 1, 64 );

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

		const auto holding_jump = ( cmd->buttons.value & cstypes::command_buttons::in_jump ) != 0;
		const auto vel2d = prestate.networked_velocity.length_2d( );

		if ( !mode_allows( mode, holding_jump, vel2d, prestate.networked_velocity.z ) )
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
		const float dt = cstypes::tick_interval;

		constexpr float standing_height = 72.0f;
		constexpr float ducked_height = 54.0f;
		constexpr float duck_hull_delta = standing_height - ducked_height;
		constexpr float duck_speed = 6.0f; // CS2 duck rate in air: 6.0 units/sec
		const float duck_delta_per_tick = duck_speed * dt;

		// Standing base origin (as if player had duck_amount = 0.0f)
		math::vector3 standing_base_origin = prestate.networked_origin;
		standing_base_origin.z -= duck_amount * duck_hull_delta;

		bool found_duck_eb = false;
		int duck_eb_tick = -1;
		float duck_eb_fraction = 1.0f;

		bool found_stand_eb = false;
		int stand_eb_tick = -1;
		float stand_eb_fraction = 1.0f;

		// 1. Simulate trajectory with +DUCK held (duck amount increases each tick)
		{
			math::vector3 sim_base_pos = standing_base_origin;
			math::vector3 sim_vel = prestate.networked_velocity;
			float sim_duck = duck_amount;

			for ( int tick = 0; tick < max_sim_ticks; ++tick )
			{
				sim_duck = std::clamp( sim_duck + duck_delta_per_tick, 0.0f, 1.0f );

				const math::vector3 tick_pos{
					sim_base_pos.x,
					sim_base_pos.y,
					sim_base_pos.z + sim_duck * duck_hull_delta
				};

				const math::vector3 tick_maxs{
					maxs.x,
					maxs.y,
					standing_height - sim_duck * duck_hull_delta
				};

				const auto sim = simulate_tick_movement(
					tick_pos, sim_vel, mins, tick_maxs,
					filter, movement_services,
					sv_gravity, sv_standable_normal, gravity_scale, dt, mode );

				if ( sim.slid_off_edge )
				{
					found_duck_eb = true;
					duck_eb_tick = tick;
					duck_eb_fraction = sim.hit_fraction;
					break;
				}

				if ( sim.hit_standable )
				{
					break;
				}

				sim_base_pos = sim.final_pos;
				sim_base_pos.z -= sim_duck * duck_hull_delta;
				sim_vel = sim.velocity;
			}
		}

		// 2. Simulate trajectory with -DUCK (STAND) held (duck amount decreases each tick)
		{
			math::vector3 sim_base_pos = standing_base_origin;
			math::vector3 sim_vel = prestate.networked_velocity;
			float sim_duck = duck_amount;

			for ( int tick = 0; tick < max_sim_ticks; ++tick )
			{
				sim_duck = std::clamp( sim_duck - duck_delta_per_tick, 0.0f, 1.0f );

				const math::vector3 tick_pos{
					sim_base_pos.x,
					sim_base_pos.y,
					sim_base_pos.z + sim_duck * duck_hull_delta
				};

				const math::vector3 tick_maxs{
					maxs.x,
					maxs.y,
					standing_height - sim_duck * duck_hull_delta
				};

				const auto sim = simulate_tick_movement(
					tick_pos, sim_vel, mins, tick_maxs,
					filter, movement_services,
					sv_gravity, sv_standable_normal, gravity_scale, dt, mode );

				if ( sim.slid_off_edge )
				{
					found_stand_eb = true;
					stand_eb_tick = tick;
					stand_eb_fraction = sim.hit_fraction;
					break;
				}

				if ( sim.hit_standable )
				{
					break;
				}

				sim_base_pos = sim.final_pos;
				sim_base_pos.z -= sim_duck * duck_hull_delta;
				sim_vel = sim.velocity;
			}
		}

		const auto base = cmd->csgo_user_cmd.mutable_base( );
		if ( !base )
		{
			return;
		}

		// Check if Edgebug occurs on THIS tick (tick == 0)
		if ( found_stand_eb && stand_eb_tick == 0 )
		{
			this->m_active_this_tick = true;

			// Maintain standing state (do not duck)
			cmd->buttons.value &= ~cstypes::command_buttons::in_duck;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;

			const auto subtick_moves = base->mutable_subtick_moves( );
			if ( const auto duck_up = systems::g_input.acquire_subtick_step( subtick_moves ) )
			{
				duck_up->set_button( cstypes::command_buttons::in_duck );
				duck_up->set_pressed( false );
				duck_up->set_when( 0.0f );
				duck_up->set_analog_forward_delta( 0.0f );
				duck_up->set_analog_left_delta( 0.0f );
			}

			if ( settings::g_movement.edgebug_include_jump_steps.value )
			{
				const auto when = std::clamp( std::round( stand_eb_fraction * 64.0f ) / 64.0f, 1.0f / 64.0f, 63.0f / 64.0f );
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
			return;
		}

		if ( found_duck_eb && duck_eb_tick == 0 )
		{
			this->m_active_this_tick = true;

			// Maintain ducking state
			cmd->buttons.value |= cstypes::command_buttons::in_duck;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;

			const auto subtick_moves = base->mutable_subtick_moves( );
			if ( const auto duck_down = systems::g_input.acquire_subtick_step( subtick_moves ) )
			{
				duck_down->set_button( cstypes::command_buttons::in_duck );
				duck_down->set_pressed( true );
				duck_down->set_when( 0.0f );
				duck_down->set_analog_forward_delta( 0.0f );
				duck_down->set_analog_left_delta( 0.0f );
			}

			if ( settings::g_movement.edgebug_include_jump_steps.value )
			{
				const auto when = std::clamp( std::round( duck_eb_fraction * 64.0f ) / 64.0f, 1.0f / 64.0f, 63.0f / 64.0f );
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
			return;
		}

		// If edgebug is predicted in future ticks (ticks_ahead > 0):
		// Decide stance now to prepare bounding box for the edge
		if ( found_duck_eb && ( !found_stand_eb || duck_eb_tick <= stand_eb_tick ) )
		{
			// Duck now to hit the edgebug window ahead
			cmd->buttons.value |= cstypes::command_buttons::in_duck;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;

			const auto subtick_moves = base->mutable_subtick_moves( );
			if ( const auto duck_down = systems::g_input.acquire_subtick_step( subtick_moves ) )
			{
				duck_down->set_button( cstypes::command_buttons::in_duck );
				duck_down->set_pressed( true );
				duck_down->set_when( 0.0f );
				duck_down->set_analog_forward_delta( 0.0f );
				duck_down->set_analog_left_delta( 0.0f );
			}
			return;
		}

		if ( found_stand_eb )
		{
			// Stand (unduck) now to hit the edgebug window ahead
			cmd->buttons.value &= ~cstypes::command_buttons::in_duck;
			cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;

			const auto subtick_moves = base->mutable_subtick_moves( );
			if ( const auto duck_up = systems::g_input.acquire_subtick_step( subtick_moves ) )
			{
				duck_up->set_button( cstypes::command_buttons::in_duck );
				duck_up->set_pressed( false );
				duck_up->set_when( 0.0f );
				duck_up->set_analog_forward_delta( 0.0f );
				duck_up->set_analog_left_delta( 0.0f );
			}
			return;
		}

		// If no edgebug is possible and edgebug_include_jump_steps is enabled:
		// Check if a normal landing will occur this tick, and if so, perform a fail-safe jump step to avoid fall damage
		if ( settings::g_movement.edgebug_include_jump_steps.value )
		{
			const float duck_hull_diff = duck_amount * duck_hull_delta;

			math::vector3 unducked_start = prestate.networked_origin;
			unducked_start.z -= duck_hull_diff;

			math::vector3 unducked_end{};
			unducked_end.x = unducked_start.x + prestate.networked_velocity.x * dt;
			unducked_end.y = unducked_start.y + prestate.networked_velocity.y * dt;
			unducked_end.z = unducked_start.z + ( prestate.networked_velocity.z - ( gravity_scale * sv_gravity * dt ) * 0.5f ) * dt - 2.0f;

			const math::vector3 standing_maxs{ maxs.x, maxs.y, standing_height };
			auto trace_result = systems::g_tracing.trace_player_bbox( unducked_start, unducked_end, { mins, standing_maxs }, filter, movement_services );

			bool can_jump_save = false;
			float landing_fraction = 1.0f;

			if ( trace_result.fraction > 0.0f && trace_result.fraction < 1.0f && trace_result.normal.z >= sv_standable_normal )
			{
				can_jump_save = true;
				landing_fraction = trace_result.fraction;
			}
			else if ( trace_result.fraction == 0.0f || trace_result.all_solid )
			{
				math::vector3 check_end = prestate.networked_origin;
				check_end.z += ( prestate.networked_velocity.z - ( gravity_scale * sv_gravity * dt ) * 0.5f ) * dt - 2.0f;
				const auto check_result = systems::g_tracing.trace_player_bbox( prestate.networked_origin, check_end, { mins, maxs }, filter, movement_services );
				if ( check_result.fraction < 1.0f && check_result.normal.z >= sv_standable_normal )
				{
					can_jump_save = true;
					landing_fraction = std::max( 1.0f / 64.0f, check_result.fraction );
				}
				else if ( duck_hull_diff > 0.0f )
				{
					math::vector3 close_end = prestate.networked_origin;
					close_end.z -= ( duck_hull_diff + 4.0f );
					const auto close_result = systems::g_tracing.trace_player_bbox( prestate.networked_origin, close_end, { mins, maxs }, filter, movement_services );
					if ( close_result.fraction < 1.0f && close_result.normal.z >= sv_standable_normal )
					{
						can_jump_save = true;
						landing_fraction = 1.0f / 64.0f;
					}
				}
			}

			if ( can_jump_save )
			{
				this->m_active_this_tick = true;

				const auto when = std::clamp( std::round( landing_fraction * 64.0f ) / 64.0f, 1.0f / 64.0f, 63.0f / 64.0f );

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
				return;
			}
			else
			{
				// If falling fast in air and no edgebug found, autoduck to keep legs tucked
				if ( prestate.networked_velocity.z <= -350.0f )
				{
					cmd->buttons.value |= cstypes::command_buttons::in_duck;
					cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;
				}
			}
		}
	}

	void edgebug::on_render( xdraw::draw_list& draw_list )
	{
		( void )draw_list;

		if ( !settings::g_movement.edgebug.value )
		{
			return;
		}
	}

} // namespace features::movement
