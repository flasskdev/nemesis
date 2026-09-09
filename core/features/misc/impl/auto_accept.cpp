#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <core/rendering/rendering.hpp>
#include <protection/game_addresses.hpp>
#include "../misc.hpp"

namespace features::misc {

	void auto_accept::reset()
	{
		m_match_detected = false;
		m_accepted = false;
		m_retry_count = 0;
		m_found_time = {};
		m_accepted_time = {};
		m_last_check = {};
	}

	void auto_accept::ensure_initialized()
	{
		if ( m_initialized )
			return;

		m_fn_is_match_waiting = reinterpret_cast<fn_is_match_waiting>(
			PATTERN( patterns::is_match_waiting ) );
		m_fn_set_local_player_ready = reinterpret_cast<fn_set_local_player_ready>(
			PATTERN( patterns::set_local_player_ready ) );

		if ( m_fn_set_local_player_ready )
		{
			const auto fn_addr = reinterpret_cast<std::uintptr_t>( m_fn_set_local_player_ready );
			for ( std::size_t offset = 0x1E; offset < 0x30; ++offset )
			{
				const auto byte = memory::safe_read<std::uint8_t>( fn_addr + offset ).value_or( 0 );
				if ( byte == 0xE8 )
				{
					const auto rel = memory::safe_read<std::int32_t>( fn_addr + offset + 1 ).value_or( 0 );
					if ( rel != 0 )
					{
						m_fn_internal_ready = reinterpret_cast<fn_internal_ready>( fn_addr + offset + 5 + rel );
						break;
					}
				}
			}
		}

		m_initialized = true;

		logging::console::print(
			xs( "[auto_accept] initialized | is_match_waiting: {:#x}, set_local_player_ready: {:#x}, internal_ready: {:#x}\n" ),
			reinterpret_cast<std::uintptr_t>( m_fn_is_match_waiting ),
			reinterpret_cast<std::uintptr_t>( m_fn_set_local_player_ready ),
			reinterpret_cast<std::uintptr_t>( m_fn_internal_ready ) );
	}

	std::uintptr_t auto_accept::get_reservation_ptr()
	{
		if ( !m_fn_is_match_waiting )
			return 0;

		const auto fn = reinterpret_cast<std::uintptr_t>( m_fn_is_match_waiting );
		const auto b0 = memory::safe_read<std::uint8_t>( fn ).value_or( 0 );
		const auto b1 = memory::safe_read<std::uint8_t>( fn + 1 ).value_or( 0 );
		const auto b2 = memory::safe_read<std::uint8_t>( fn + 2 ).value_or( 0 );
		if ( b0 == 0x48 && b1 == 0x8B && b2 == 0x05 )
		{
			const auto rel = memory::safe_read<std::int32_t>( fn + 3 ).value_or( 0 );
			if ( rel != 0 )
			{
				const auto ptr_addr = fn + 7 + rel;
				return memory::safe_read<std::uintptr_t>( ptr_addr ).value_or( 0 );
			}
		}
		return 0;
	}

	bool auto_accept::is_match_waiting_internal()
	{
		// 1. Direct engine query (checks ServerConfirmedReservation != nullptr && state == 3 && accepted == 0)
		if ( m_fn_is_match_waiting && m_fn_is_match_waiting() )
			return true;

		// 2. Reservation state check (state 3 = MatchReady, has_accepted = 0)
		// NOTE: Never check state == 1 (state 1 is SEARCHING in queue, not match found!)
		if ( const auto reservation = this->get_reservation_ptr(); reservation != 0 )
		{
			const auto state = memory::safe_read<std::int32_t>( reservation + 0xA8 ).value_or( 0 );
			const auto accepted = memory::safe_read<std::uint8_t>( reservation + 0xA4 ).value_or( 0 );
			if ( state == 3 && accepted == 0 )
				return true;
		}

		return false;
	}

	void auto_accept::on_panorama_event( const char* event_name )
	{
		if ( !settings::g_misc.auto_accept.value || !event_name )
			return;

		if ( reinterpret_cast<std::uintptr_t>( event_name ) < 0x10000 )
			return;

		__try
		{
			bool has_null = false;
			for ( std::size_t i = 0; i < 256; ++i )
			{
				if ( event_name[ i ] == '\0' )
				{
					has_null = true;
					break;
				}
			}

			if ( !has_null )
				return;

			this->ensure_initialized();

			// Diagnostic logging for relevant Panorama events
			if ( strstr( event_name, "popup" ) || strstr( event_name, "match" ) ||
			     strstr( event_name, "accept" ) || strstr( event_name, "ReadyUp" ) )
			{
				logging::console::print( xs( "[auto_accept] panorama event: {}\n" ), event_name );
			}

			// popup_accept_match_found is fired by CS2 when a competitive/premier/casual match is found
			if ( strcmp( event_name, "popup_accept_match_found" ) == 0 ||
			     strstr( event_name, "accept_match_found" ) != nullptr ||
			     strstr( event_name, "MatchAssistedReadyUp" ) != nullptr ||
			     strstr( event_name, "ReadyUp" ) != nullptr ||
			     strstr( event_name, "csgo_matchmaking_match_found" ) != nullptr )
			{
				if ( !m_accepted )
				{
					const auto now = std::chrono::steady_clock::now();
					m_match_detected = true;
					m_found_time = now;
					m_last_check = now;

					logging::console::print(
						xs( "[auto_accept] MATCH DETECTED via '{}'! Accepting...\n" ),
						event_name );

					// Alert user immediately
					const auto hwnd = rendering::g_context.get_window();
					if ( hwnd && GetForegroundWindow() != hwnd )
					{
						FLASHWINFO fi{};
						fi.cbSize = sizeof( FLASHWINFO );
						fi.hwnd = hwnd;
						fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
						fi.uCount = 3;
						fi.dwTimeout = 0;
						FlashWindowEx( &fi );
						MessageBeep( MB_ICONINFORMATION );
					}

					// Accept immediately
					this->accept_match();

					if ( !this->is_match_waiting_internal() )
					{
						m_accepted = true;
						m_accepted_time = now;
					}
				}
			}
		}
		__except( EXCEPTION_EXECUTE_HANDLER )
		{
			return;
		}
	}

	void auto_accept::run()
	{
		if ( !settings::g_misc.auto_accept.value )
		{
			m_match_detected = false;
			m_accepted = false;
			return;
		}

		this->ensure_initialized();

		const auto now = std::chrono::steady_clock::now();
		const bool waiting = this->is_match_waiting_internal();

		if ( waiting )
		{
			if ( !m_match_detected )
			{
				m_match_detected = true;
				m_accepted = false;
				m_found_time = now;
				m_retry_count = 0;
				m_last_check = now;

				logging::console::print( xs( "[auto_accept] MATCH DETECTED via is_match_waiting! Accepting...\n" ) );

				// Alert user immediately
				const auto hwnd = rendering::g_context.get_window();
				if ( hwnd && GetForegroundWindow() != hwnd )
				{
					FLASHWINFO fi{};
					fi.cbSize = sizeof( FLASHWINFO );
					fi.hwnd = hwnd;
					fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
					fi.uCount = 3;
					fi.dwTimeout = 0;
					FlashWindowEx( &fi );
					MessageBeep( MB_ICONINFORMATION );
				}
			}

			// Accept and retry every ~150ms while match is waiting
			if ( !m_accepted )
			{
				if ( m_retry_count == 0 || ( now - m_last_check ) >= std::chrono::milliseconds( 150 ) )
				{
					m_last_check = now;
					m_retry_count++;
					this->accept_match();
				}

				// If match is no longer waiting, the engine / GC confirmed acceptance!
				if ( !this->is_match_waiting_internal() )
				{
					m_accepted = true;
					m_accepted_time = now;
					logging::console::print( xs( "[auto_accept] match accepted confirmed!\n" ) );
				}
			}
		}
		else
		{
			// No match waiting: if previously accepted, reset after 3s cooldown
			if ( m_accepted )
			{
				if ( now - m_accepted_time > std::chrono::seconds( 3 ) )
				{
					this->reset();
				}
			}
			else if ( m_match_detected )
			{
				if ( now - m_found_time > std::chrono::seconds( 5 ) )
				{
					this->reset();
				}
			}
		}
	}

	void auto_accept::accept_match()
	{
		static bool s_in_accept = false;
		if ( s_in_accept )
			return;
		s_in_accept = true;

		logging::console::print( xs( "[auto_accept] ACCEPTING MATCH...\n" ) );

		bool accepted = false;

		// Method 1: SetLocalPlayerReady(nullptr, "deferred")
		// In CS2, SetLocalPlayerReady ONLY executes the internal ready logic when passed "deferred"!
		// Passing "accept" jumps over the internal ready call and returns false.
		if ( m_fn_set_local_player_ready )
		{
			accepted = m_fn_set_local_player_ready( nullptr, "deferred" );
			logging::console::print(
				xs( "[auto_accept] SetLocalPlayerReady('deferred') returned {}\n" ),
				accepted );

			// Also try "accept" as fallback
			m_fn_set_local_player_ready( nullptr, "accept" );
		}

		// Method 2: Direct internal ready call (calls GC ready with state 2 = Accepted)
		if ( m_fn_internal_ready )
		{
			const auto res = m_fn_internal_ready( nullptr, 2 );
			logging::console::print(
				xs( "[auto_accept] internal_ready(nullptr, 2) returned {}\n" ),
				res );
			if ( res ) accepted = true;
		}

		// IMPORTANT: DO NOT OVERWRITE local ServerConfirmedReservation state (+0xA8 or +0xA4)!
		// Overwriting client memory does NOT accept the match on Valve's server, but it DOES
		// trick Panorama into hiding the accept window, which causes the match to time out!

		s_in_accept = false;
	}

} // namespace features::misc
