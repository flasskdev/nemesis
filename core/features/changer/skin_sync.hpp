#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <core/settings.hpp>

namespace features::changer {

	struct remote_player_skin {
		std::unordered_map<std::int16_t, settings::changer::applied_skin> skins{};
		int music_kit_id{ 0 };
		std::int16_t agent_ct{ 0 };
		std::int16_t agent_t{ 0 };
		std::chrono::steady_clock::time_point last_updated{};
	};

	class skin_sync {
	public:
		void initialize( );
		void shutdown( );
		void on_frame_stage_notify( );
		void trigger_push( );
		void set_local_steam_id( std::uint64_t steam_id );
		[[nodiscard]] std::uint64_t resolve_local_steam_id( ) const;

		[[nodiscard]] const remote_player_skin* get_remote_skin( std::uint64_t steam_id ) const;
		[[nodiscard]] bool is_cheat_user( std::uint64_t steam_id ) const;
		[[nodiscard]] bool should_show_indicator( std::uint64_t steam_id ) const;
		[[nodiscard]] int get_remote_music_kit( std::uint64_t steam_id ) const;

		// Inverted test mode: when true, indicator shows for players who DO NOT use cheat
		std::atomic<bool> m_test_inversion{ false };

		// Bot sync test mode: when true, bots mirror synced cosmetics from server/local
		std::atomic<bool> m_bot_sync_test{ false };

	private:
		void worker_loop( );
		bool perform_push( );
		void perform_pull( );
		void perform_users_update( );
		void capture_local_snapshot(std::uint64_t steam_id);

		mutable std::shared_mutex m_mutex{};
		std::unordered_map<std::uint64_t, remote_player_skin> m_cache{};
		std::unordered_set<std::uint64_t> m_cheat_users{};
		mutable remote_player_skin m_bot_preview{};

		std::atomic<bool> m_running{ false };
		std::atomic<bool> m_push_pending{ true };
		std::atomic<bool> m_initialized{ false };
		std::atomic<std::uint64_t> m_last_local_steam_id{ 0 };
		std::string m_local_payload{}; // Protected by m_mutex.
		std::uint64_t m_payload_steam_id{};
		std::chrono::steady_clock::time_point m_last_snapshot_time{}; // Producer thread only.
		std::chrono::steady_clock::time_point m_last_push_time{};
		std::chrono::steady_clock::time_point m_last_pull_time{};
		std::chrono::steady_clock::time_point m_last_users_time{};

		std::vector<std::uint64_t> m_pending_query_ids{};
		std::mutex m_query_mutex{};
	};

	inline skin_sync g_skin_sync{};

} // namespace features::changer
