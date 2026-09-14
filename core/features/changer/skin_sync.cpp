#include <pch/pch.hpp>
#include <winhttp.h>

#include "skin_sync.hpp"
#include "changer.hpp"
#include <core/features/features.hpp>
#include <utilities/steam/steam.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <external/nlohmann/json.hpp>

namespace features::changer {

	namespace detail {
		static constexpr const wchar_t* k_api_host = L"flasskdev.alwaysdata.net";
		static constexpr const wchar_t* k_api_path = L"/api/v1/index.php";

		struct winhttp_api {
			using fn_WinHttpOpen = HINTERNET( WINAPI* )( LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD );
			using fn_WinHttpSetTimeouts = BOOL( WINAPI* )( HINTERNET, int, int, int, int );
			using fn_WinHttpConnect = HINTERNET( WINAPI* )( HINTERNET, LPCWSTR, INTERNET_PORT, DWORD );
			using fn_WinHttpOpenRequest = HINTERNET( WINAPI* )( HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD );
			using fn_WinHttpSetOption = BOOL( WINAPI* )( HINTERNET, DWORD, LPVOID, DWORD );
			using fn_WinHttpSendRequest = BOOL( WINAPI* )( HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR );
			using fn_WinHttpReceiveResponse = BOOL( WINAPI* )( HINTERNET, LPVOID );
			using fn_WinHttpQueryDataAvailable = BOOL( WINAPI* )( HINTERNET, LPDWORD );
			using fn_WinHttpReadData = BOOL( WINAPI* )( HINTERNET, LPVOID, DWORD, LPDWORD );
			using fn_WinHttpCloseHandle = BOOL( WINAPI* )( HINTERNET );

			fn_WinHttpOpen WinHttpOpen{};
			fn_WinHttpSetTimeouts WinHttpSetTimeouts{};
			fn_WinHttpConnect WinHttpConnect{};
			fn_WinHttpOpenRequest WinHttpOpenRequest{};
			fn_WinHttpSetOption WinHttpSetOption{};
			fn_WinHttpSendRequest WinHttpSendRequest{};
			fn_WinHttpReceiveResponse WinHttpReceiveResponse{};
			fn_WinHttpQueryDataAvailable WinHttpQueryDataAvailable{};
			fn_WinHttpReadData WinHttpReadData{};
			fn_WinHttpCloseHandle WinHttpCloseHandle{};

			bool initialized{ false };

			bool init( )
			{
				if ( initialized )
				{
					return WinHttpOpen != nullptr;
				}

				HMODULE hModule = LoadLibraryA( "winhttp.dll" );
				if ( !hModule )
				{
					return false;
				}

				WinHttpOpen = reinterpret_cast<fn_WinHttpOpen>( GetProcAddress( hModule, "WinHttpOpen" ) );
				WinHttpSetTimeouts = reinterpret_cast<fn_WinHttpSetTimeouts>( GetProcAddress( hModule, "WinHttpSetTimeouts" ) );
				WinHttpConnect = reinterpret_cast<fn_WinHttpConnect>( GetProcAddress( hModule, "WinHttpConnect" ) );
				WinHttpOpenRequest = reinterpret_cast<fn_WinHttpOpenRequest>( GetProcAddress( hModule, "WinHttpOpenRequest" ) );
				WinHttpSetOption = reinterpret_cast<fn_WinHttpSetOption>( GetProcAddress( hModule, "WinHttpSetOption" ) );
				WinHttpSendRequest = reinterpret_cast<fn_WinHttpSendRequest>( GetProcAddress( hModule, "WinHttpSendRequest" ) );
				WinHttpReceiveResponse = reinterpret_cast<fn_WinHttpReceiveResponse>( GetProcAddress( hModule, "WinHttpReceiveResponse" ) );
				WinHttpQueryDataAvailable = reinterpret_cast<fn_WinHttpQueryDataAvailable>( GetProcAddress( hModule, "WinHttpQueryDataAvailable" ) );
				WinHttpReadData = reinterpret_cast<fn_WinHttpReadData>( GetProcAddress( hModule, "WinHttpReadData" ) );
				WinHttpCloseHandle = reinterpret_cast<fn_WinHttpCloseHandle>( GetProcAddress( hModule, "WinHttpCloseHandle" ) );

				initialized = true;
				return WinHttpOpen && WinHttpConnect && WinHttpOpenRequest && WinHttpSendRequest && WinHttpReceiveResponse && WinHttpCloseHandle;
			}
		};

		inline winhttp_api g_winhttp{};

		static std::string http_post_json( const std::string& json_body )
		{
			std::string response{};
			if ( !g_winhttp.init( ) )
			{
				return response;
			}

			HINTERNET hSession = g_winhttp.WinHttpOpen(
				L"MintalySync/1.0",
				WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
				WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS,
				0
			);
			if ( !hSession )
			{
				return response;
			}

			g_winhttp.WinHttpSetTimeouts( hSession, 3000, 4000, 4000, 4000 );

			HINTERNET hConnect = g_winhttp.WinHttpConnect( hSession, k_api_host, INTERNET_DEFAULT_HTTPS_PORT, 0 );
			if ( hConnect )
			{
				HINTERNET hRequest = g_winhttp.WinHttpOpenRequest(
					hConnect,
					L"POST",
					k_api_path,
					NULL,
					WINHTTP_NO_REFERER,
					WINHTTP_DEFAULT_ACCEPT_TYPES,
					WINHTTP_FLAG_SECURE
				);

				if ( hRequest )
				{
					DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
								  SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
								  SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
								  SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
					g_winhttp.WinHttpSetOption( hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof( flags ) );

					std::wstring headers = L"Content-Type: application/json; charset=utf-8\r\nConnection: close\r\n";
					if ( g_winhttp.WinHttpSendRequest(
						hRequest,
						headers.c_str( ),
						static_cast<DWORD>( headers.length( ) ),
						const_cast<char*>( json_body.data( ) ),
						static_cast<DWORD>( json_body.size( ) ),
						static_cast<DWORD>( json_body.size( ) ),
						0
					) )
					{
						if ( g_winhttp.WinHttpReceiveResponse( hRequest, NULL ) )
						{
							DWORD bytes_avail = 0;
							while ( g_winhttp.WinHttpQueryDataAvailable( hRequest, &bytes_avail ) && bytes_avail > 0 )
							{
								std::vector<char> buffer( bytes_avail );
								DWORD bytes_read = 0;
								if ( g_winhttp.WinHttpReadData( hRequest, buffer.data( ), bytes_avail, &bytes_read ) && bytes_read > 0 )
								{
									response.append( buffer.data( ), bytes_read );
								}
							}
						}
					}
					g_winhttp.WinHttpCloseHandle( hRequest );
				}
				g_winhttp.WinHttpCloseHandle( hConnect );
			}
			g_winhttp.WinHttpCloseHandle( hSession );
			return response;
		}
	} // namespace detail

	void skin_sync::initialize( )
	{
		if ( this->m_initialized.exchange( true ) )
		{
			return;
		}

		this->m_running = true;
		this->m_push_pending = true;

		CreateThread( nullptr, 0, []( LPVOID param ) -> DWORD {
			auto* self = static_cast<skin_sync*>( param );
			// Wait 3 seconds so the game finishes any early rendering/window initialization
			for ( int i = 0; i < 30 && self->m_running.load( ); ++i )
			{
				Sleep( 100 );
			}
			if ( self->m_running.load( ) )
			{
				self->worker_loop( );
			}
			return 0;
		}, this, 0, nullptr );
	}

	void skin_sync::shutdown( )
	{
		this->m_running = false;
	}

	void skin_sync::trigger_push( )
	{
		this->m_last_pushed_hash.store( 0, std::memory_order_relaxed );
		this->m_push_pending = true;
	}

	std::uint64_t skin_sync::compute_local_cosmetics_hash( ) const
	{
		std::uint64_t h = 14695981039346656037ull;
		auto mix = [ &h ]( std::uint64_t val ) {
			h ^= val;
			h *= 1099511628211ull;
		};

		mix( static_cast< std::uint64_t >( settings::g_changer.music.id ) );
		mix( static_cast< std::uint64_t >( settings::g_changer.agents.ct_def ) );
		mix( static_cast< std::uint64_t >( settings::g_changer.agents.t_def ) );
		mix( static_cast< std::uint64_t >( settings::g_changer.custom_agents.selected_ct ) );
		mix( static_cast< std::uint64_t >( settings::g_changer.custom_agents.selected_t ) );

		for ( const auto& [def_idx, skin] : settings::g_changer.skins.data )
		{
			mix( static_cast< std::uint64_t >( def_idx ) );
			mix( static_cast< std::uint64_t >( skin.paint_kit_id ) );
			mix( static_cast< std::uint64_t >( skin.seed ) );
			mix( static_cast< std::uint64_t >( skin.stattrak ? ( skin.stattrak_count + 1 ) : 0 ) );
			std::uint32_t wear_bits = 0;
			std::memcpy( &wear_bits, &skin.wear, sizeof( float ) );
			mix( static_cast< std::uint64_t >( wear_bits ) );
		}

		return h;
	}

	void skin_sync::set_local_steam_id( std::uint64_t steam_id )
	{
		constexpr std::uint64_t steam_id_base = 76561197960265728ull;
		if ( steam_id < steam_id_base )
		{
			return;
		}

		const auto prev = this->m_last_local_steam_id.exchange( steam_id );
		if ( prev != steam_id )
		{
			this->m_push_pending = true;
		}
	}

	std::uint64_t skin_sync::resolve_local_steam_id( ) const
	{
		constexpr std::uint64_t steam_id_base = 76561197960265728ull;

		// 1. First priority: local player controller global in game
		if ( addresses::globals::local_player_controller )
		{
			const auto local_ctrl = memory::safe_read<std::uintptr_t>( addresses::globals::local_player_controller ).value_or( 0 );
			if ( local_ctrl )
			{
				const auto sid = memory::safe_read<std::uint64_t>( local_ctrl + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) ).value_or( 0 );
				if ( sid >= steam_id_base )
				{
					return sid;
				}
			}
		}

		// 2. Local systems snapshot controller
		const auto sys_ctrl = systems::g_local.get( ).controller;
		if ( sys_ctrl )
		{
			const auto sid = memory::safe_read<std::uint64_t>( sys_ctrl + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) ).value_or( 0 );
			if ( sid >= steam_id_base )
			{
				return sid;
			}
		}

		// 3. Scan player entities for m_bIsLocalPlayerController
		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );
		for ( const auto& p : players )
		{
			if ( !p.ptr ) continue;
			const auto is_local = memory::safe_read<bool>( p.ptr + SCHEMA( "CBasePlayerController", "m_bIsLocalPlayerController"_hash ) ).value_or( false );
			if ( is_local )
			{
				const auto sid = memory::safe_read<std::uint64_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) ).value_or( 0 );
				if ( sid >= steam_id_base )
				{
					return sid;
				}
			}
		}

		// 4. Steam API fallback (works in main menu)
		const auto steam_api_id = steam::user::get_steam_id( );
		if ( steam_api_id >= steam_id_base )
		{
			return steam_api_id;
		}

		return 0;
	}

	void skin_sync::on_frame_stage_notify( )
	{
		if ( !this->m_initialized.load( ) )
		{
			this->initialize( );
		}

		constexpr std::uint64_t steam_id_base = 76561197960265728ull;
		const auto local_steam_id = this->resolve_local_steam_id( );
		if ( local_steam_id >= steam_id_base )
		{
			const auto prev = this->m_last_local_steam_id.exchange( local_steam_id );
			if ( prev != local_steam_id )
			{
				this->m_push_pending = true;
			}

			// Automatically detect changes to local skins/music/agents (via cfg load, menu edits, or in-game)
			const auto current_hash = this->compute_local_cosmetics_hash( );
			if ( current_hash != this->m_last_pushed_hash.load( std::memory_order_relaxed ) )
			{
				this->m_last_pushed_hash.store( current_hash, std::memory_order_relaxed );
				this->m_push_pending = true;
			}
		}

		// Update local preview snapshot for bots
		{
			std::unique_lock lock( this->m_mutex );
			this->m_bot_preview.skins = settings::g_changer.skins.data;
			this->m_bot_preview.music_kit_id = settings::g_changer.music.id;
			this->m_bot_preview.agent_ct = settings::g_changer.agents.ct_def;
			this->m_bot_preview.agent_t = settings::g_changer.agents.t_def;
			this->m_bot_preview.last_updated = std::chrono::steady_clock::now( );
		}

		// Enqueue all other players' steam IDs for pulling (plus local steam ID to verify server sync)
		const auto players = systems::g_entities.get_by_type( systems::entities::type::player );
		std::vector<std::uint64_t> ids_to_query{};

		if ( local_steam_id >= steam_id_base )
		{
			ids_to_query.push_back( local_steam_id );
		}

		for ( const auto& p : players )
		{
			if ( !p.ptr )
			{
				continue;
			}

			const auto sid = memory::safe_read<std::uint64_t>( p.ptr + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) ).value_or( 0 );
			if ( sid >= steam_id_base && sid != local_steam_id )
			{
				ids_to_query.push_back( sid );
			}
		}

		if ( !ids_to_query.empty( ) )
		{
			std::lock_guard lock( this->m_query_mutex );
			for ( const auto id : ids_to_query )
			{
				if ( std::find( this->m_pending_query_ids.begin( ), this->m_pending_query_ids.end( ), id ) == this->m_pending_query_ids.end( ) )
				{
					this->m_pending_query_ids.push_back( id );
				}
			}
		}
	}

	const remote_player_skin* skin_sync::get_remote_skin( std::uint64_t steam_id ) const
	{
		std::shared_lock lock( this->m_mutex );

		constexpr std::uint64_t steam_id_base = 76561197960265728ull;
		if ( steam_id < steam_id_base )
		{
			if ( this->m_bot_sync_test.load( ) )
			{
				// Return server-pulled profile if available, otherwise fallback to local snapshot
				if ( this->m_last_local_steam_id >= steam_id_base )
				{
					const auto it = this->m_cache.find( this->m_last_local_steam_id );
					if ( it != this->m_cache.end( ) && ( !it->second.skins.empty( ) || it->second.agent_ct > 0 || it->second.agent_t > 0 || it->second.music_kit_id > 0 ) )
					{
						return &it->second;
					}
				}
				return &this->m_bot_preview;
			}
			return nullptr;
		}

		const auto it = this->m_cache.find( steam_id );
		if ( it != this->m_cache.end( ) )
		{
			return &it->second;
		}
		return nullptr;
	}

	bool skin_sync::is_cheat_user( std::uint64_t steam_id ) const
	{
		const auto local_id = steam::user::get_steam_id( );
		if ( ( local_id != 0 && steam_id == local_id ) || ( this->m_last_local_steam_id != 0 && steam_id == this->m_last_local_steam_id ) )
		{
			return true;
		}

		constexpr std::uint64_t steam_id_base = 76561197960265728ull;
		if ( steam_id < steam_id_base )
		{
			return this->m_bot_sync_test.load( );
		}

		std::shared_lock lock( this->m_mutex );
		return this->m_cheat_users.contains( steam_id ) || this->m_cache.contains( steam_id );
	}

	bool skin_sync::should_show_indicator( std::uint64_t steam_id ) const
	{
		const auto local_id = steam::user::get_steam_id( );
		if ( ( local_id != 0 && steam_id == local_id ) || ( this->m_last_local_steam_id != 0 && steam_id == this->m_last_local_steam_id ) )
		{
			return !this->m_test_inversion.load( );
		}

		constexpr std::uint64_t steam_id_base = 76561197960265728ull;
		if ( steam_id < steam_id_base )
		{
			return this->m_bot_sync_test.load( );
		}

		const bool is_cheat = this->is_cheat_user( steam_id );
		if ( this->m_test_inversion.load( ) )
		{
			return !is_cheat;
		}
		return is_cheat;
	}

	int skin_sync::get_remote_music_kit( std::uint64_t steam_id ) const
	{
		const auto skin = this->get_remote_skin( steam_id );
		return skin ? skin->music_kit_id : 0;
	}

	void skin_sync::worker_loop( )
	{
		while ( this->m_running.load( ) )
		{
			const auto now = std::chrono::steady_clock::now( );

			// Push local skins if pending or every 10 seconds
			if ( this->m_push_pending.load( ) ||
				 std::chrono::duration_cast<std::chrono::seconds>( now - this->m_last_push_time ).count( ) >= 10 )
			{
				this->perform_push( );
				this->m_last_push_time = std::chrono::steady_clock::now( );
			}

			// Pull remote skins every 2 seconds
			if ( std::chrono::duration_cast<std::chrono::seconds>( now - this->m_last_pull_time ).count( ) >= 2 )
			{
				this->perform_pull( );
				this->m_last_pull_time = std::chrono::steady_clock::now( );
			}

			// Refresh active users list every 3 seconds
			if ( std::chrono::duration_cast<std::chrono::seconds>( now - this->m_last_users_time ).count( ) >= 3 )
			{
				this->perform_users_update( );
				this->m_last_users_time = std::chrono::steady_clock::now( );
			}

			std::this_thread::sleep_for( std::chrono::milliseconds( 250 ) );
		}
	}

	void skin_sync::perform_push( )
	{
		constexpr std::uint64_t steam_id_base = 76561197960265728ull;
		auto steam_id = this->m_last_local_steam_id.load( );
		if ( steam_id < steam_id_base )
		{
			steam_id = steam::user::get_steam_id( );
			if ( steam_id >= steam_id_base )
			{
				this->m_last_local_steam_id.store( steam_id );
			}
		}

		if ( steam_id < steam_id_base )
		{
			return;
		}

		try
		{
			// Explicit protection against sending custom agents:
			// Custom agents (external .vmdl files from disk) must NEVER be sent to the database/network,
			// only standard official CS2 agents from g_econ_item_system are allowed.
			std::int16_t ct_agent = 0;
			std::int16_t t_agent = 0;

			const auto& ca = settings::g_changer.custom_agents;
			if ( ca.selected_ct < 0 && settings::g_changer.agents.ct_def > 0 )
			{
				const auto def = g_econ_item_system.find_def( settings::g_changer.agents.ct_def );
				if ( def && def->category == econ_item_system::item_category::agent && !def->model_player.empty( ) )
				{
					ct_agent = settings::g_changer.agents.ct_def;
				}
			}

			if ( ca.selected_t < 0 && settings::g_changer.agents.t_def > 0 )
			{
				const auto def = g_econ_item_system.find_def( settings::g_changer.agents.t_def );
				if ( def && def->category == econ_item_system::item_category::agent && !def->model_player.empty( ) )
				{
					t_agent = settings::g_changer.agents.t_def;
				}
			}

			nlohmann::json j;
			j[ "action" ] = "skin_sync_push";
			j[ "steam_id" ] = std::to_string( steam_id );
			j[ "skin_data" ] = settings::g_changer.skins.serialize( );
			j[ "music_kit_id" ] = settings::g_changer.music.id;
			j[ "agent_ct" ] = ct_agent;
			j[ "agent_t" ] = t_agent;

			const auto resp_str = detail::http_post_json( j.dump( ) );
			if ( !resp_str.empty( ) )
			{
				const auto resp = nlohmann::json::parse( resp_str, nullptr, false );
				if ( !resp.is_discarded( ) && resp.value( "success", false ) )
				{
					this->m_push_pending = false;
					std::unique_lock lock( this->m_mutex );
					this->m_cheat_users.insert( steam_id );
				}
			}
		}
		catch ( ... ) {}
	}

	void skin_sync::perform_pull( )
	{
		std::vector<std::uint64_t> ids_to_query{};
		{
			std::lock_guard lock( this->m_query_mutex );
			ids_to_query = std::move( this->m_pending_query_ids );
			this->m_pending_query_ids.clear( );
		}

		if ( ids_to_query.empty( ) )
		{
			return;
		}

		try
		{
			nlohmann::json j;
			j[ "action" ] = "skin_sync_pull";
			auto id_array = nlohmann::json::array( );
			for ( const auto id : ids_to_query )
			{
				id_array.push_back( std::to_string( id ) );
			}
			j[ "steam_ids" ] = id_array;

			const auto resp_str = detail::http_post_json( j.dump( ) );
			if ( resp_str.empty( ) )
			{
				return;
			}

			const auto resp = nlohmann::json::parse( resp_str, nullptr, false );
			if ( resp.is_discarded( ) || !resp.value( "success", false ) )
			{
				return;
			}

			if ( !resp.contains( "users" ) || !resp[ "users" ].is_object( ) )
			{
				return;
			}

			std::unique_lock lock( this->m_mutex );
			for ( const auto& [id_str, user_data] : resp[ "users" ].items( ) )
			{
				try
				{
					const auto sid = std::stoull( id_str );
					remote_player_skin player{};
					player.music_kit_id = user_data.value( "music_kit_id", 0 );
					player.last_updated = std::chrono::steady_clock::now( );

					// Verify that received agent IDs correspond strictly to official standard game agents
					const auto raw_ct = static_cast<std::int16_t>( user_data.value( "agent_ct", 0 ) );
					const auto raw_t = static_cast<std::int16_t>( user_data.value( "agent_t", 0 ) );

					if ( raw_ct > 0 )
					{
						const auto def = g_econ_item_system.find_def( raw_ct );
						if ( def && def->category == econ_item_system::item_category::agent && !def->model_player.empty( ) )
						{
							player.agent_ct = raw_ct;
						}
					}

					if ( raw_t > 0 )
					{
						const auto def = g_econ_item_system.find_def( raw_t );
						if ( def && def->category == econ_item_system::item_category::agent && !def->model_player.empty( ) )
						{
							player.agent_t = raw_t;
						}
					}

					if ( user_data.contains( "skins" ) && user_data[ "skins" ].is_object( ) )
					{
						for ( const auto& [def_str, skin_json] : user_data[ "skins" ].items( ) )
						{
							try
							{
								const auto def = static_cast<std::int16_t>( std::stoi( def_str ) );
								settings::changer::applied_skin s{};
								s.paint_kit_id = skin_json.value( "p", 0 );
								s.wear = skin_json.value( "w", 0.01f );
								s.seed = skin_json.value( "s", 0 );
								s.stattrak = skin_json.value( "t", false );
								s.stattrak_count = skin_json.value( "c", 0 );
								player.skins[ def ] = s;
							}
							catch ( ... ) {}
						}
					}

					this->m_cache[ sid ] = std::move( player );
					this->m_cheat_users.insert( sid );
				}
				catch ( ... ) {}
			}
		}
		catch ( ... ) {}
	}

	void skin_sync::perform_users_update( )
	{
		try
		{
			nlohmann::json j;
			j[ "action" ] = "skin_sync_users";

			const auto resp_str = detail::http_post_json( j.dump( ) );
			if ( resp_str.empty( ) )
			{
				return;
			}

			const auto resp = nlohmann::json::parse( resp_str, nullptr, false );
			if ( resp.is_discarded( ) || !resp.value( "success", false ) )
			{
				return;
			}

			if ( resp.contains( "users" ) && resp[ "users" ].is_array( ) )
			{
				constexpr std::uint64_t steam_id_base = 76561197960265728ull;
				const auto local_id = this->m_last_local_steam_id.load( );
				std::vector<std::uint64_t> new_users_to_pull{};

				{
					std::unique_lock lock( this->m_mutex );
					this->m_cheat_users.clear( );
					for ( const auto& user_id_val : resp[ "users" ] )
					{
						try
						{
							std::uint64_t sid = 0;
							if ( user_id_val.is_string( ) )
							{
								sid = std::stoull( user_id_val.get<std::string>( ) );
							}
							else if ( user_id_val.is_number( ) )
							{
								sid = user_id_val.get<std::uint64_t>( );
							}
							if ( sid >= steam_id_base )
							{
								this->m_cheat_users.insert( sid );
								if ( sid != local_id && !this->m_cache.contains( sid ) )
								{
									new_users_to_pull.push_back( sid );
								}
							}
						}
						catch ( ... ) {}
					}
				}

				if ( !new_users_to_pull.empty( ) )
				{
					std::lock_guard lock( this->m_query_mutex );
					for ( const auto id : new_users_to_pull )
					{
						if ( std::find( this->m_pending_query_ids.begin( ), this->m_pending_query_ids.end( ), id ) == this->m_pending_query_ids.end( ) )
						{
							this->m_pending_query_ids.push_back( id );
						}
					}
				}
			}
		}
		catch ( ... ) {}
	}

} // namespace features::changer
