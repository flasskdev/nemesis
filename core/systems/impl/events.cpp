#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
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
			&patterns::particle_set_entity_binding
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

		// Independent of pending rage shots: a missed R8 prediction must not
		// also make the actual weapon_fire event invisible to diagnostics.
		if ( !register_listener( xs( "weapon_fire" ), [ ]( void* raw_event )
			{
				if ( !raw_event || !settings::g_misc.m_impacts.console_log.value )
				{
					return;
				}
				const auto event = reinterpret_cast<std::uintptr_t>( raw_event );
				const auto userid_key = cstypes::event_hash{ 0, "userid" };
				const auto controller = memory::call<std::uintptr_t>( PATTERN( patterns::game_event_get_controller ), event, &userid_key );
				const auto local = systems::g_local.get( );
				if ( !local.controller || controller != local.controller )
				{
					return;
				}
				const auto weapon = memory::call<const char*>( PATTERN( patterns::game_event_get_string ), event, "weapon", "" );
				if ( weapon && ( std::strcmp( weapon, "revolver" ) == 0 || std::strcmp( weapon, "weapon_revolver" ) == 0 ) )
				{
					logging::console::print( xs( "[r8:event] weapon_fire received (independent of rage hit/miss records)" ) );
				}
			} ) )
		{
			// Optional diagnostics must not prevent the remaining listeners from
			// being registered if this event is unavailable.
			logging::console::print( xs( "[r8:event] weapon_fire listener unavailable" ) );
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

} // namespace systems