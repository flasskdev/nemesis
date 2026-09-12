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
			bool valid{ true };
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

			// Starting inside solid geometry is not an edge contact.
			if ( sweep_trace.all_solid || !std::isfinite( sweep_trace.fraction ) ||
				sweep_trace.fraction <= 0.0f || sweep_trace.fraction > 1.0f )
			{
				res.valid = false;
				return res;
			}

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

			if ( slide_trace.all_solid || !std::isfinite( slide_trace.fraction ) ||
				slide_trace.fraction < 0.0f || slide_trace.fraction > 1.0f )
			{
				res.valid = false;
				return res;
			}

			const math::vector3 slide_final = sweep_trace.end_pos + ( slide_target - sweep_trace.end_pos ) * slide_trace.fraction;
			res.final_pos = slide_final;

			// FinishGravity uses the full frame interval, not the remaining slide time.
			clipped_vel.z -= ( gravity_scale * sv_gravity * dt ) * 0.5f;
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

			if ( ground_trace.all_solid || !std::isfinite( ground_trace.fraction ) ||
				ground_trace.fraction < 0.0f || ground_trace.fraction > 1.0f )
			{
				res.valid = false;
				return res;
			}

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

					if ( drop_trace.all_solid || !std::isfinite( drop_trace.fraction ) ||
						( drop_trace.fraction < 1.0f && drop_trace.normal.z >= sv_standable_normal ) )
					{
						return res;
					}
				}

				res.slid_off_edge = true;
			}

			return res;
		}

		[[nodiscard]] bool apply_stance( systems::input::usercmd* cmd, bool duck,
			std::optional<float> jump_fraction = std::nullopt )
		{
			const auto base = cmd->csgo_user_cmd.mutable_base( );
			const auto moves = base ? base->mutable_subtick_moves( ) : nullptr;
			if ( !moves || ( jump_fraction && !std::isfinite( *jump_fraction ) ) )
			{
				return false;
			}

			const int old_size = moves->m_current_size;
			const int count = jump_fraction ? 3 : 2;
			std::array<proto::subtick_move_step*, 3> added{};
			for ( int i = 0; i < count; ++i )
			{
				added[i] = systems::g_input.acquire_subtick_step( moves );
				if ( !added[i] )
				{
					moves->m_current_size = old_size;
					return false;
				}
				*added[i] = {};
			}

			constexpr auto controlled = cstypes::command_buttons::in_duck |
				cstypes::command_buttons::in_jump;
			for ( int i = 0; i < old_size; ++i )
			{
				const auto step = base->mutable_subtick_moves( i );
				if ( step && ( step->button( ) & controlled ) )
				{
					// Keep analog/view deltas and any unrelated button bits intact.
					step->set_button( step->button( ) & ~controlled );
					if ( step->button( ) == 0 )
					{
						step->set_pressed( false );
					}
				}
			}

			added[0]->set_button( cstypes::command_buttons::in_jump );
			added[0]->set_pressed( false );
			added[0]->set_when( 0.0f );
			added[1]->set_button( cstypes::command_buttons::in_duck );
			added[1]->set_pressed( duck );
			added[1]->set_when( 0.0f );
			if ( jump_fraction )
			{
				// Fractions are within this tick, not multiples of the server tick interval.
				const float when = std::clamp( *jump_fraction,
					std::nextafter( 0.0f, 1.0f ), std::nextafter( 1.0f, 0.0f ) );
				added[2]->set_button( cstypes::command_buttons::in_jump );
				added[2]->set_pressed( true );
				added[2]->set_when( when );
			}

			cmd->buttons.value &= ~controlled;
			if ( duck )
			{
				cmd->buttons.value |= cstypes::command_buttons::in_duck;
			}
			if ( jump_fraction )
			{
				cmd->buttons.value |= cstypes::command_buttons::in_jump;
			}
			cmd->buttons.value_changed |= controlled;
			cmd->buttons.value_scroll &= ~controlled;
			return true;
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

	void jumpbug::on_create_move( systems::input::usercmd* cmd, std::uint64_t original_buttons )
	{
		this->m_active_this_tick = false;
		if ( !cmd )
		{
			return;
		}

		const bool is_bound = ( settings::g_movement.jumpbug.bind.key != 0 );
		const bool is_active = is_bound
			? ( settings::g_movement.jumpbug.bind.active || settings::g_movement.jumpbug.value )
			: settings::g_movement.jumpbug.value;

		if ( !is_active )
		{
			return;
		}

		const auto mode = std::clamp( settings::g_movement.jumpbug_mode.value, 0, 4 );
		const auto passes_cfg = settings::g_movement.jumpbug_passes.value;
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

		const auto holding_jump = ( original_buttons & cstypes::command_buttons::in_jump ) != 0;
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

				if ( !sim.valid || sim.hit_standable )
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

				if ( !sim.valid || sim.hit_standable )
				{
					break;
				}

				sim_base_pos = sim.final_pos;
				sim_base_pos.z -= sim_duck * duck_hull_delta;
				sim_vel = sim.velocity;
			}
		}

		const bool include_jump = settings::g_movement.jumpbug_include_jump_steps.value;
		if ( found_stand_eb && stand_eb_tick == 0 )
		{
			this->m_active_this_tick = apply_stance( cmd, false,
				include_jump ? std::optional<float>{ stand_eb_fraction } : std::nullopt );
			return;
		}
		if ( found_duck_eb && duck_eb_tick == 0 )
		{
			this->m_active_this_tick = apply_stance( cmd, true,
				include_jump ? std::optional<float>{ duck_eb_fraction } : std::nullopt );
			return;
		}
		if ( found_duck_eb && ( !found_stand_eb || duck_eb_tick <= stand_eb_tick ) )
		{
			this->m_active_this_tick = apply_stance( cmd, true );
			return;
		}
		if ( found_stand_eb )
		{
			this->m_active_this_tick = apply_stance( cmd, false );
			return;
		}

		if ( !include_jump )
		{
			return;
		}

		// Optional landing jump. A trace hit is NOT proof of avoiding fall damage.
		// Do not extend this sweep by the 2-unit ground probe: its fraction is time.
		math::vector3 unducked_start = prestate.networked_origin;
		unducked_start.z -= duck_amount * duck_hull_delta;
		auto velocity = prestate.networked_velocity;
		velocity.z -= ( gravity_scale * sv_gravity * dt ) * 0.5f;
		const auto unducked_end = unducked_start + velocity * dt;
		const math::vector3 standing_maxs{ maxs.x, maxs.y, standing_height };
		const auto landing = systems::g_tracing.trace_player_bbox(
			unducked_start, unducked_end, { mins, standing_maxs }, filter, movement_services );
		if ( !landing.all_solid && std::isfinite( landing.fraction ) &&
			landing.fraction > 0.0f && landing.fraction < 1.0f &&
			landing.normal.z >= sv_standable_normal )
		{
			this->m_active_this_tick = apply_stance( cmd, false, landing.fraction );
			return;
		}
		if ( prestate.networked_velocity.z <= -350.0f )
		{
			this->m_active_this_tick = apply_stance( cmd, true );
		}
	}

} // namespace features::movement
