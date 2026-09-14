#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::changer {

	std::string g_last_applied_model{};

namespace {
	bool apply_model_safe (std::uintptr_t pawn, const char* model)
	{
		struct buffer_string {
			std::uint32_t m_unknown1 {};
			std::uint32_t m_unknown2 { 0xc00000c8 };
			union { std::uintptr_t m_str_ptr; std::uint8_t data[ 0xc8 ]; };
			std::uintptr_t m_unknown3 {};
			std::uintptr_t m_unknown4 {};
		} buffer;

		const auto init_path_buffer = PATTERN (patterns::init_particle_path_buffer);
		const auto precache_resource = PATTERN (patterns::resource_system_precache);
		const auto set_model = PATTERN (patterns::set_player_model);

		g_last_applied_model = model;

		__try
		{
			if ( init_path_buffer && precache_resource && addresses::globals::resource_system )
			{
				memory::call<void>( init_path_buffer, &buffer, model );
				buffer.m_unknown4 = 'ldmv';
				memory::call<void>( precache_resource, addresses::globals::resource_system, &buffer, "" );
			}

			memory::call<void>( set_model, pawn, model );
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return false;
		}

		return true;
	}

	// MSVC rejects __try in any frame that also needs C++ unwinding (C2712), and
	// on_frame_stage_notify holds std::string / std::vector locals -- so the
	// guarded call gets its own frame, where every local is trivially
	// destructible. Resolve the pattern on the caller's side, not in here.
	bool set_model_guarded (std::uintptr_t set_model, std::uintptr_t pawn, const char* model)
	{
		__try
		{
			memory::call<void>( set_model, pawn, model );
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			return false;
		}

		return true;
	}

	struct remote_agent_state {
		std::int16_t def_index{ 0 };
		std::uintptr_t model_handle{ 0 };
		int team{ 0 };
	};
	static std::unordered_map<std::uintptr_t, remote_agent_state> s_remote_agents;
}

	void agents::on_frame_stage_notify( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || systems::g_local.is_in_cinematic( ) || !local.pawn )
		{
			return;
		}

		const auto local_ctrl = local.controller;
		const auto local_pawn = local.pawn;

		if ( true )
		{
			const auto team = memory::read<int>( local_pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			const auto selected_def_index = ( team == 3 ) ? settings::g_changer.agents.ct_def : ( team == 2 ) ? settings::g_changer.agents.t_def : static_cast< std::int16_t >( 0 );

			const econ_item_system::item_def* selected{ nullptr };
			if ( selected_def_index != 0 )
			{
				selected = g_econ_item_system.find_def( selected_def_index );
			}

			// custom player model (agent) ?
			std::string model_path;
			{
				const auto& custom_agents = settings::g_changer.custom_agents;
				const auto custom_idx = ( team == 3 ) ? custom_agents.selected_ct
				                     : ( team == 2 ) ? custom_agents.selected_t : -1;

				if ( custom_idx >= 0 && custom_idx < static_cast< int >( custom_agents.entries.size( ) ) )
				{
					const auto& entry = custom_agents.entries[ custom_idx ];
					if ( ( entry.team == team || entry.team == 0 ) && !entry.model_path.empty( ) )
						model_path = entry.model_path;
				}
			}

			if ( model_path.empty( ) && selected )
				model_path = selected->model_player;

			if ( this->m_tracked_pawn != local_pawn )
			{
				this->m_original_model.clear( );
				this->m_applied_model.clear( );
				this->m_overridden = false;
				this->m_applied_handle = 0;
				this->m_applied_def = 0;
				this->m_tracked_team = 0;
				this->m_tracked_pawn = local_pawn;
			}

			const auto game_scene_node = memory::read<std::uintptr_t>( local_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( game_scene_node )
			{
				const auto model_state = game_scene_node + SCHEMA( "CSkeletonInstance", "m_modelState"_hash );
				const auto cur_hModel = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );

				if ( cur_hModel != 0 )
				{
					if ( model_path.empty( ) )
					{
						if ( this->m_overridden && !this->m_original_model.empty( ) )
						{
							memory::call<void>( PATTERN( patterns::set_player_model ), local_pawn, this->m_original_model.c_str( ) );
							this->cycle_weapon_owners( local_pawn );
							this->m_applied_handle = 0;
							this->m_applied_def = 0;
							this->m_applied_model.clear( );
							this->m_tracked_team = 0;
							this->m_overridden = false;
						}
					}
					else
					{
						if ( !this->m_overridden && this->m_original_model.empty( ) )
						{
							const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
							if ( model_name_ptr )
							{
								this->m_original_model = memory::read_string( model_name_ptr );
							}
						}

						const auto current_handle = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
						const auto selection_matches = ( this->m_applied_def == ( selected ? selected->def_index : 0 ) ) && this->m_applied_model == model_path;
						const auto team_matches = ( this->m_tracked_team == team );
						const auto handle_matches = ( this->m_applied_handle != 0 && current_handle == this->m_applied_handle );

						if ( !( this->m_overridden && selection_matches && team_matches && handle_matches ) )
						{
							if ( this->m_overridden && !team_matches )
							{
								this->m_original_model.clear( );
								this->m_applied_model.clear( );
								this->m_overridden = false;

								const auto model_name_ptr = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_ModelName"_hash ) );
								if ( model_name_ptr )
								{
									this->m_original_model = memory::read_string( model_name_ptr );
								}
							}

							if ( model_path.size( ) >= 7 && model_path.substr( model_path.size( ) - 7 ) == ".vmdl_c" )
								model_path = model_path.substr( 0, model_path.size( ) - 2 );

							auto to_lower = []( unsigned char c ) { return ( c >= 'A' && c <= 'Z' ) ? static_cast< char >( c + 32 ) : static_cast< char >( c ); };
							auto icontains = [&]( const std::string& s, const char* needle, std::size_t nlen )
							{
								if ( s.size( ) < nlen ) return false;
								for ( std::size_t i = 0; i + nlen <= s.size( ); ++i )
								{
									bool ok = true;
									for ( std::size_t j = 0; j < nlen; ++j )
										if ( to_lower( static_cast< unsigned char >( s[ i + j ] ) ) != to_lower( static_cast< unsigned char >( needle[ j ] ) ) )
										{ ok = false; break; }
									if ( ok ) return true;
								}
								return false;
							};

							const bool bad =
								icontains( model_path, "_arm", 4 ) ||
								icontains( model_path, "arms", 4 ) ||
								icontains( model_path, "viewmodel", 8 ) ||
								icontains( model_path, "/arm.", 5 ) ||
								icontains( model_path, "\\arm.", 5 );

							if ( bad )
							{
								this->m_applied_model.clear( );
								auto& ca = settings::g_changer.custom_agents;
								if ( team == 3 ) ca.selected_ct = -1; else ca.selected_t = -1;
							}
							else
							{
								if ( model_path.size( ) >= 5 && model_path.compare( model_path.size( ) - 5, 5, ".vmdl" ) == 0 )
								{
									apply_model_safe( local_pawn, model_path.c_str( ) );
								}
								else
								{
									const auto set_model = PATTERN( patterns::set_player_model );
									set_model_guarded( set_model, local_pawn, model_path.c_str( ) );
								}

								const auto collision = local_pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash );
								memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ), math::vector3( -16.0f, -16.0f, 0.0f ) );
								memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ), math::vector3( 16.0f, 16.0f, 72.0f ) );

								this->cycle_weapon_owners( local_pawn );

								this->m_applied_handle = memory::read<std::uintptr_t>( model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
								this->m_applied_def = selected ? selected->def_index : 0;
								this->m_applied_model = model_path;
								this->m_tracked_team = team;
								this->m_overridden = true;
							}
						}
					}
				}
			}
		}

		// Apply synced agents for remote players
		const auto all_players = systems::g_entities.get_by_type( systems::entities::type::player );
		for ( const auto& p : all_players )
		{
			const auto ctrl = p.ptr;
			if ( !ctrl || ctrl == local_ctrl )
			{
				continue;
			}

			const auto sid = memory::read<std::uint64_t>( ctrl + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
			constexpr std::uint64_t steam_id_base = 76561197960265728ull;
			if ( sid < steam_id_base && !g_skin_sync.m_bot_sync_test.load( ) )
			{
				continue;
			}

			const auto remote_skin_data = g_skin_sync.get_remote_skin( sid );
			if ( !remote_skin_data )
			{
				continue;
			}

			const auto pawn_handle = memory::read<std::uint32_t>( ctrl + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			if ( !pawn_handle )
			{
				continue;
			}

			const auto pawn = systems::g_entities.lookup( pawn_handle );
			if ( !pawn || pawn < 0x10000 )
			{
				continue;
			}

			const auto remote_team = memory::read<int>( pawn + SCHEMA( "C_BaseEntity", "m_iTeamNum"_hash ) );
			if ( remote_team != 2 && remote_team != 3 )
			{
				continue;
			}

			const auto remote_agent_def = ( remote_team == 3 ) ? remote_skin_data->agent_ct : remote_skin_data->agent_t;
			if ( !remote_agent_def )
			{
				continue;
			}

			const auto agent_item = g_econ_item_system.find_def( remote_agent_def );
			if ( !agent_item || agent_item->category != econ_item_system::item_category::agent || agent_item->model_player.empty( ) )
			{
				continue;
			}

			// Reject any custom disk path models (must be standard game character model, no drive letters or traversals)
			const std::string& raw_model = agent_item->model_player;
			if ( raw_model.find( ":" ) != std::string::npos || raw_model.find( ".." ) != std::string::npos ||
				 raw_model.starts_with( "/" ) || raw_model.starts_with( "\\" ) )
			{
				continue;
			}

			std::string remote_model = agent_item->model_player;
			if ( remote_model.size( ) >= 7 && remote_model.substr( remote_model.size( ) - 7 ) == ".vmdl_c" )
				remote_model = remote_model.substr( 0, remote_model.size( ) - 2 );

			const auto remote_gsn = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			if ( !remote_gsn )
			{
				continue;
			}

			const auto remote_model_state = remote_gsn + SCHEMA( "CSkeletonInstance", "m_modelState"_hash );
			const auto remote_model_handle = memory::read<std::uintptr_t>( remote_model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
			if ( !remote_model_handle )
			{
				continue;
			}

			const auto it = s_remote_agents.find( pawn );
			if ( it != s_remote_agents.end( ) && it->second.def_index == remote_agent_def && it->second.team == remote_team && it->second.model_handle == remote_model_handle )
			{
				continue;
			}

			if ( remote_model.size( ) >= 5 && remote_model.compare( remote_model.size( ) - 5, 5, ".vmdl" ) == 0 )
			{
				apply_model_safe( pawn, remote_model.c_str( ) );
			}
			else
			{
				const auto set_model = PATTERN( patterns::set_player_model );
				set_model_guarded( set_model, pawn, remote_model.c_str( ) );
			}

			const auto collision = pawn + SCHEMA( "C_BaseModelEntity", "m_Collision"_hash );
			memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMins"_hash ), math::vector3( -16.0f, -16.0f, 0.0f ) );
			memory::write<math::vector3>( collision + SCHEMA( "CCollisionProperty", "m_vecMaxs"_hash ), math::vector3( 16.0f, 16.0f, 72.0f ) );

			this->cycle_weapon_owners( pawn );

			const auto new_handle = memory::read<std::uintptr_t>( remote_model_state + SCHEMA( "CModelState", "m_hModel"_hash ) );
			s_remote_agents[ pawn ] = { remote_agent_def, new_handle, remote_team };
		}
	}

	void agents::cycle_weapon_owners( std::uintptr_t pawn )
	{
		const auto weapon_services = memory::read<std::uintptr_t>( pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
		if ( !weapon_services )
		{
			return;
		}

		const auto weapons_base = weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hMyWeapons"_hash );
		const auto weapons_size = memory::read<int>( weapons_base );
		const auto weapons_data = memory::read<std::uintptr_t>( weapons_base + 0x8 );

		if ( !weapons_data || weapons_size <= 0 )
		{
			return;
		}

		for ( auto i = 0; i < weapons_size; ++i )
		{
			const auto handle = memory::read<std::uint32_t>( weapons_data + i * sizeof( std::uint32_t ) );
			const auto weapon = systems::g_entities.lookup( handle );

			if ( !weapon )
			{
				continue;
			}

			const auto owner_off = SCHEMA( "C_BaseEntity", "m_hOwnerEntity"_hash );
			const auto saved_owner = memory::read<std::uint32_t>( weapon + owner_off );

			memory::write<std::uint32_t>( weapon + owner_off, 0xffffffff );
			memory::write<std::uint32_t>( weapon + owner_off, saved_owner );
		}
	}

	void agents::reset( )
	{
		this->m_original_model.clear( );
		this->m_applied_model.clear( );
		this->m_tracked_pawn = 0;
		this->m_applied_handle = 0;
		this->m_applied_def = 0;
		this->m_overridden = false;
		this->m_tracked_team = 0;
		s_remote_agents.clear( );
	}

} // namespace features::changer