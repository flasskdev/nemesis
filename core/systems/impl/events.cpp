#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

#include "../systems.hpp"

namespace systems {

	bool events::initialize( )
	{
		// Resolve hot-path function addresses on the existing initialization
		// thread, before listeners can receive the first shot/hurt event.
		// This only resolves addresses; it does not call game/audio functions.
		const protection::addresses::address_t* const hot_patterns[] =
		{
			&patterns::game_event_get_controller,
			&patterns::game_event_get_float,
			&patterns::game_event_get_int,
			&patterns::game_event_get_pawn,
			&patterns::game_event_get_string,
			&patterns::base_fire_guns_get_inaccuracy,
			&patterns::find_hud_element,
			&patterns::set_voice_data,
			&patterns::print_hud_chat,
			&patterns::play_sound,
			&patterns::init_particle_path_buffer,
			&patterns::resource_system_precache,
			&patterns::particle_create_effect,
			&patterns::particle_set_control_point,
			&patterns::particle_set_entity_binding,
			&patterns::econ_item_view_set_attribute,
			&patterns::engine_client_cmd
		};

		const auto warmup_started = std::chrono::steady_clock::now( );
		std::size_t resolved{};
		for ( const auto* pattern : hot_patterns )
		{
			if ( memory::resolve_pattern_cached( pattern->data.data ) )
			{
				++resolved;
			}
		}

		const auto warmup_ms = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now( ) - warmup_started ).count( );
		diag::writef( diag::level::info,
			"event signature warmup: %zu/%zu resolved in %.2f ms",
			resolved, sizeof( hot_patterns ) / sizeof( hot_patterns[ 0 ] ), warmup_ms );

		if ( !register_listener( xs( "bullet_impact" ), [ ]( void* event ) { features::misc::g_impacts.on_bullet_impact( reinterpret_cast< std::uintptr_t >( event ) ); } ) )
		{
			return false;
		}

		if ( !register_listener( xs( "player_hurt" ), [ ]( void* event ) { features::misc::g_impacts.on_player_hurt( reinterpret_cast< std::uintptr_t >( event ) ); } ) )
		{
			return false;
		}

		if ( !register_listener( xs( "round_start" ), [ ]( void* event ) { features::misc::g_other.on_round_start( ); } ) )
		{
			return false;
		}

		if ( !register_listener( xs( "player_death" ), [ ]( void* event ) { features::misc::g_other.on_player_death( reinterpret_cast< std::uintptr_t >( event ) ); } ) )
		{
			return false;
		}

		if ( !register_listener( xs( "vote_cast" ), [ ]( void* event ) { features::misc::g_vote_logs.on_vote_cast( reinterpret_cast< std::uintptr_t >( event ) ); } ) )
		{
			return false;
		}

		if ( !register_listener( xs( "vote_failed" ), [ ]( void* event ) { features::misc::g_vote_logs.on_vote_failed_event( reinterpret_cast< std::uintptr_t >( event ) ); } ) )
		{
			return false;
		}

		if ( !register_listener( xs( "round_mvp" ), [ ]( void* event ) { features::changer::g_music.on_round_mvp( event ); } ) )
		{
			return false;
		}

		return true;
	}

	void events::shutdown( )
	{
		for ( auto& entry : m_listeners )
		{
			if ( entry->registered )
			{
				memory::call_vfunc<void>( addresses::globals::game_event_manager, 5, &entry->listener );
				entry->registered = false;
			}
		}

		m_listeners.clear( );
	}

	bool events::register_listener( const char* event_name, handler_fn handler )
	{
		if ( !event_name || !handler )
		{
			return false;
		}

		auto current_entry = std::make_unique<entry>( );
		current_entry->handler = handler;
		current_entry->name = event_name;
		current_entry->registered = false;

		current_entry->vtable_data[ 0 ] = nullptr;
		current_entry->vtable_data[ 1 ] = reinterpret_cast< void* >( &fire_event );
		current_entry->vtable_data[ 2 ] = reinterpret_cast< void* >( &get_debug_id );

		current_entry->listener.vtable = current_entry->vtable_data;
		current_entry->listener.debug_id = static_cast< int >( m_listeners.size( ) + 1 );

		const auto success = memory::call_vfunc<bool>( addresses::globals::game_event_manager, 3, &current_entry->listener, event_name, false );
		if ( !success )
		{
			return false;
		}

		current_entry->registered = true;
		m_listeners.push_back( std::move( current_entry ) );

		return true;
	}

	void events::unregister_listener( const char* event_name )
	{
		if ( !event_name )
		{
			return;
		}

		for ( auto it = m_listeners.begin( ); it != m_listeners.end( ); ++it )
		{
			if ( std::strcmp( ( *it )->name, event_name ) == 0 && ( *it )->registered )
			{
				memory::call_vfunc<void>( addresses::globals::game_event_manager, 5, &( *it )->listener );
				( *it )->registered = false;
				m_listeners.erase( it );
				return;
			}
		}
	}

	void* __fastcall events::fire_event( void* self, void* event )
	{
		const auto current_listener = reinterpret_cast< listener* >( self );

		for ( const auto& entry : m_listeners )
		{
			if ( entry->listener.debug_id == current_listener->debug_id && entry->handler )
			{
				entry->handler( event );
				break;
			}
		}

		return nullptr;
	}

	int __fastcall events::get_debug_id( void* self )
	{
		const auto current_listener = reinterpret_cast< listener* >( self );
		return current_listener->debug_id;
	}

	std::uintptr_t events::get_controller( void* event, const char* key_name )
	{
		if ( !event || !key_name )
		{
			return 0;
		}

		auto ent_to_controller = []( std::uintptr_t ent ) -> std::uintptr_t
		{
			if ( !ent ) return 0;
			// 1. If ent has m_hPawn, it is already a controller!
			const auto pawn_handle = memory::read<std::uint32_t>( ent + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			if ( pawn_handle && pawn_handle != 0xFFFFFFFF )
			{
				return ent;
			}
			// 2. If ent has m_hController, it is a pawn: resolve its controller
			const auto ctrl_handle = memory::read<std::uint32_t>( ent + SCHEMA( "C_BasePlayerPawn", "m_hController"_hash ) );
			if ( ctrl_handle && ctrl_handle != 0xFFFFFFFF )
			{
				const auto ctrl = systems::g_entities.lookup( ctrl_handle );
				if ( ctrl ) return ctrl;
			}
			return ent;
		};

		const auto vtable = *reinterpret_cast<void***>( event );

		// 1. Direct virtual call to event vtable[16] (0x80 / 8 = GetPlayerController)
		if ( vtable && vtable[ 16 ] )
		{
			using fn_t = std::uintptr_t( __fastcall* )( void*, const void* );
			const auto fn = reinterpret_cast<fn_t>( vtable[ 16 ] );
			const auto key = cstypes::event_hash{ key_name };
			const auto ent = fn( event, &key );
			if ( ent )
			{
				return ent_to_controller( ent );
			}
		}

		// 2. Pattern call (game_event_get_controller, vtable[16])
		const auto pat_fn = PATTERN( patterns::game_event_get_controller );
		if ( pat_fn )
		{
			const auto key = cstypes::event_hash{ key_name };
			const auto ent = memory::call<std::uintptr_t>( pat_fn, event, &key );
			if ( ent )
			{
				return ent_to_controller( ent );
			}
		}

		// 3. Fallback: virtual call to event vtable[17] (GetPlayerPawn), then resolve controller
		if ( vtable && vtable[ 17 ] )
		{
			using fn_t = std::uintptr_t( __fastcall* )( void*, const void* );
			const auto fn = reinterpret_cast<fn_t>( vtable[ 17 ] );
			const auto key = cstypes::event_hash{ key_name };
			const auto ent = fn( event, &key );
			if ( ent )
			{
				return ent_to_controller( ent );
			}
		}

		// 4. Fallback: integer lookup (e.g. "userid", "attacker", "entityid", "id")
		const auto pat_get_int = PATTERN( patterns::game_event_get_int );
		if ( pat_get_int )
		{
			const auto id = memory::call<int>( pat_get_int, event, key_name, -1 );
			if ( id >= 0 )
			{
				// Check as 0-based slot (0..64)
				if ( id < 64 )
				{
					auto ctrl = systems::g_entities.get_by_index( id + 1 );
					if ( ctrl )
					{
						return ent_to_controller( ctrl );
					}
					ctrl = systems::g_entities.get_by_index( id );
					if ( ctrl )
					{
						return ent_to_controller( ctrl );
					}
				}

				// Check as entity handle
				const auto ent = systems::g_entities.lookup( static_cast<std::uint32_t>( id ) );
				if ( ent )
				{
					return ent_to_controller( ent );
				}
			}
		}

		return 0;
	}

	std::uintptr_t events::get_pawn( void* event, const char* key_name )
	{
		if ( !event || !key_name )
		{
			return 0;
		}

		auto ent_to_pawn = []( std::uintptr_t ent ) -> std::uintptr_t
		{
			if ( !ent ) return 0;
			// 1. If ent has m_hController, it's ALREADY a pawn!
			const auto ctrl_handle = memory::read<std::uint32_t>( ent + SCHEMA( "C_BasePlayerPawn", "m_hController"_hash ) );
			if ( ctrl_handle && ctrl_handle != 0xFFFFFFFF )
			{
				return ent;
			}
			// 2. If ent has m_hPawn, it's a controller; resolve the pawn!
			const auto pawn_handle = memory::read<std::uint32_t>( ent + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
			if ( pawn_handle && pawn_handle != 0xFFFFFFFF )
			{
				const auto pawn = systems::g_entities.lookup( pawn_handle );
				if ( pawn ) return pawn;
			}
			return ent;
		};

		const auto vtable = *reinterpret_cast<void***>( event );

		// 1. Direct virtual call to event vtable[17] (0x88 / 8 = GetPlayerPawn)
		if ( vtable && vtable[ 17 ] )
		{
			using fn_t = std::uintptr_t( __fastcall* )( void*, const void* );
			const auto fn = reinterpret_cast<fn_t>( vtable[ 17 ] );
			const auto key = cstypes::event_hash{ key_name };
			const auto ent = fn( event, &key );
			if ( ent )
			{
				return ent_to_pawn( ent );
			}
		}

		// 2. Direct pattern fallback for game_event_get_pawn
		const auto pat_pawn = PATTERN( patterns::game_event_get_pawn );
		if ( pat_pawn )
		{
			const auto key = cstypes::event_hash{ key_name };
			const auto ent = memory::call<std::uintptr_t>( pat_pawn, event, &key );
			if ( ent )
			{
				return ent_to_pawn( ent );
			}
		}

		// 3. Fallback: resolve via get_controller
		const auto ctrl = get_controller( event, key_name );
		if ( ctrl )
		{
			return ent_to_pawn( ctrl );
		}

		return 0;
	}

} // namespace systems