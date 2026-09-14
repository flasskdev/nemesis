#include <pch/pch.hpp>
#include "sync_http.hpp"

#include "skin_sync.hpp"
#include "changer.hpp"
#include <core/features/features.hpp>
#include <utilities/steam/steam.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <external/nlohmann/json.hpp>

namespace features::changer {

	static void log_sync_rejection(const char* action, const detail::sync_http_result& response)
	{
		std::string request_id = "unavailable";
		std::string error_code = "unavailable";
		const auto reply = nlohmann::json::parse(response.body, nullptr, false);
		const auto safe_field = [&](const char* key) -> std::string {
			if (!reply.is_object() || !reply.contains(key) || !reply[key].is_string()) return "unavailable";
			const auto text = reply[key].get<std::string>();
			if (text.empty() || text.size() > 64) return "unavailable";
			for (const auto c : text) {
				if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return "unavailable";
			}
			return text;
		};
		request_id = safe_field("request_id");
		error_code = safe_field("error_code");
		diag::writef(diag::level::warning, "[skin-sync] action=%s rejected http=%lu win32=%lu request_id=%s error_code=%s",
			action, response.status, response.error, request_id.c_str(), error_code.c_str());
	}

	void skin_sync::initialize( )
	{
		if ( this->m_initialized.exchange( true ) )
		{
			return;
		}

		this->m_running = true;
		this->m_push_pending = true;

		const auto worker = CreateThread( nullptr, 0, []( LPVOID param ) -> DWORD {
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
		if (!worker) {
			diag::writef(diag::level::error, "[skin-sync] CreateThread failed win32=%lu", GetLastError());
			this->m_running = false;
			this->m_initialized = false;
			return;
		}
		CloseHandle(worker);
		diag::write(diag::level::info, "[skin-sync] worker started build=sync-fix-20260914");
	}

	void skin_sync::shutdown( )
	{
		this->m_running = false;
	}

	void skin_sync::trigger_push( )
	{
		this->m_push_pending = true;
	}

	void skin_sync::capture_local_snapshot(std::uint64_t steam_id)
	{
		remote_player_skin snapshot{};
		snapshot.skins = settings::g_changer.skins.data;
		snapshot.music_kit_id = settings::g_changer.music.id;
		snapshot.last_updated = std::chrono::steady_clock::now();
		const auto official_agent = [](std::int16_t id, int custom) -> std::int16_t {
			if (custom >= 0 || id <= 0) return 0;
			const auto def = g_econ_item_system.find_def(id);
			return def && def->category == econ_item_system::item_category::agent && !def->model_player.empty() ? id : 0;
		};
		snapshot.agent_ct = official_agent(settings::g_changer.agents.ct_def, settings::g_changer.custom_agents.selected_ct);
		snapshot.agent_t = official_agent(settings::g_changer.agents.t_def, settings::g_changer.custom_agents.selected_t);
		nlohmann::json skins = nlohmann::json::object();
		for (const auto& [def, skin] : snapshot.skins) {
			skins[std::to_string(def)] = {
				{"p", skin.paint_kit_id}, {"w", skin.wear}, {"s", skin.seed},
				{"t", skin.stattrak}, {"c", skin.stattrak_count}
			};
		}
		const nlohmann::json payload = {
			{"action", "skin_sync_push"}, {"steam_id", std::to_string(steam_id)},
			{"skin_data", skins}, {"music_kit_id", snapshot.music_kit_id},
			{"agent_ct", snapshot.agent_ct}, {"agent_t", snapshot.agent_t}
		};
		const auto encoded = payload.dump();
		std::unique_lock lock(this->m_mutex);
		this->m_bot_preview = std::move(snapshot);
		if (encoded != this->m_local_payload) {
			this->m_local_payload = encoded;
			this->m_payload_steam_id = steam_id;
			this->m_push_pending = true;
		}
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
			diag::writef(diag::level::info, "[skin-sync] local steam_id=%llu", static_cast<unsigned long long>(steam_id));
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
			this->set_local_steam_id(local_steam_id);
			try {
				const auto now = std::chrono::steady_clock::now();
				if (now - this->m_last_snapshot_time >= std::chrono::milliseconds(250)) {
					this->capture_local_snapshot(local_steam_id);
					this->m_last_snapshot_time = now;
				}
			} catch (const std::exception&) {
				diag::write(diag::level::warning, "[skin-sync] snapshot serialization failed");
			}
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
		// Valid until the next get_remote_skin call on this thread.
		thread_local remote_player_skin copy;

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
						copy = it->second;
						return &copy;
					}
				}
				copy = this->m_bot_preview;
				return &copy;
			}
			return nullptr;
		}

		const auto it = this->m_cache.find( steam_id );
		if ( it != this->m_cache.end( ) )
		{
			copy = it->second;
			return &copy;
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

	void skin_sync::worker_loop()
	{
		auto next_push = std::chrono::steady_clock::now();
		unsigned failures = 0;
		while (this->m_running.load()) {
			const auto now = std::chrono::steady_clock::now();
			const bool heartbeat_due = now - this->m_last_push_time >= std::chrono::seconds(10);
			if (now >= next_push && (this->m_push_pending.load() || heartbeat_due)) {
				if (this->perform_push()) {
					failures = 0;
					this->m_last_push_time = std::chrono::steady_clock::now();
					next_push = this->m_last_push_time + std::chrono::seconds(1);
				} else {
					if (failures < 5) ++failures;
					next_push = std::chrono::steady_clock::now() + std::chrono::seconds(1u << failures);
				}
			}
			try {
				if (now - this->m_last_pull_time >= std::chrono::seconds(2)) {
					this->perform_pull();
					this->m_last_pull_time = std::chrono::steady_clock::now();
				}
				if (now - this->m_last_users_time >= std::chrono::seconds(3)) {
					this->perform_users_update();
					this->m_last_users_time = std::chrono::steady_clock::now();
				}
			} catch (const std::exception&) {
				diag::write(diag::level::warning, "[skin-sync] remote update failed");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
	}

	bool skin_sync::perform_push()
	{
		try {
			std::string payload;
			std::uint64_t steam_id = 0;
			{
				std::shared_lock lock(this->m_mutex);
				payload = this->m_local_payload;
				steam_id = this->m_payload_steam_id;
			}
			if (payload.empty()) {
				diag::write(diag::level::warning, "[skin-sync] push waiting for in-game snapshot and SteamID");
				return false;
			}
			const auto response = detail::http_post_json(payload);
			if (!response.ok()) { log_sync_rejection("push", response); return false; }
			const auto reply = nlohmann::json::parse(response.body, nullptr, false);
			const bool accepted = reply.is_object() && reply.contains("success") &&
				reply["success"].is_boolean() && reply["success"].get<bool>() &&
				reply.contains("steam_id") && reply["steam_id"].is_string() &&
				reply["steam_id"].get<std::string>() == std::to_string(steam_id);
			if (!accepted) {
				log_sync_rejection("push-ack", response);
				diag::writef(diag::level::warning, "[skin-sync] push invalid acknowledgement http=%lu bytes=%zu", response.status, response.body.size());
				return false;
			}
			std::unique_lock lock(this->m_mutex);
			// A late acknowledgement must not clear a newer edit or account change.
			if (payload == this->m_local_payload && steam_id == this->m_payload_steam_id)
				this->m_push_pending = false;
			this->m_cheat_users.insert(steam_id);
			diag::writef(diag::level::info, "[skin-sync] push confirmed steam_id=%llu bytes=%zu",
				static_cast<unsigned long long>(steam_id), payload.size());
			return true;
		} catch (const std::exception&) {
			diag::write(diag::level::warning, "[skin-sync] push exception");
			return false;
		}
	}

	void skin_sync::perform_pull( )
	{
		std::vector<std::uint64_t> ids_to_query{};
		{
			std::lock_guard lock( this->m_query_mutex );
			ids_to_query = std::move( this->m_pending_query_ids );
			this->m_pending_query_ids.clear( );
			if (ids_to_query.size() > 256) {
				this->m_pending_query_ids.assign(ids_to_query.begin() + 256, ids_to_query.end());
				ids_to_query.resize(256);
			}
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

			const auto response = detail::http_post_json(j.dump());
			if (!response.ok())
			{
				log_sync_rejection("pull", response);
				return;
			}

			const auto resp = nlohmann::json::parse( response.body, nullptr, false );
			if ( !resp.is_object() || !resp.value("success", false) )
			{
				return;
			}

			if (!resp.contains("users")) {
				diag::write(diag::level::warning, "[skin-sync] pull response has no users field");
				return;
			}
			// Compatibility with the old API, which encoded an empty map as [].
			if (resp["users"].is_array() && resp["users"].empty()) return;
			if (!resp["users"].is_object()) {
				diag::write(diag::level::warning, "[skin-sync] pull users must be an object");
				return;
			}

			std::unique_lock lock( this->m_mutex );
			for ( const auto& [id_str, user_data] : resp[ "users" ].items( ) )
			{
				try
				{
					const auto sid = std::stoull(id_str);
					if (std::to_string(sid) != id_str || std::find(ids_to_query.begin(), ids_to_query.end(), sid) == ids_to_query.end() || !user_data.is_object()) continue;
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
							catch (const std::exception&) { diag::write(diag::level::warning, "[skin-sync] invalid remote response"); }
						}
					}

					if (!this->m_cache.contains(sid))
						diag::writef(diag::level::info, "[skin-sync] remote profile steam_id=%llu skins=%zu", static_cast<unsigned long long>(sid), player.skins.size());
					this->m_cache[ sid ] = std::move( player );
					this->m_cheat_users.insert( sid );
				}
				catch (const std::exception&) { diag::write(diag::level::warning, "[skin-sync] invalid remote response"); }
			}
		}
		catch (const std::exception&) { diag::write(diag::level::warning, "[skin-sync] invalid remote response"); }
	}

	void skin_sync::perform_users_update( )
	{
		try
		{
			nlohmann::json j;
			j[ "action" ] = "skin_sync_users";

			const auto response = detail::http_post_json(j.dump());
			if (!response.ok())
			{
				log_sync_rejection("users", response);
				return;
			}

			const auto resp = nlohmann::json::parse( response.body, nullptr, false );
			if ( !resp.is_object() || !resp.value("success", false) )
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
						catch (const std::exception&) { diag::write(diag::level::warning, "[skin-sync] invalid remote response"); }
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
		catch (const std::exception&) { diag::write(diag::level::warning, "[skin-sync] invalid remote response"); }
	}

} // namespace features::changer
