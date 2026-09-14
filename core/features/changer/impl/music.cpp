#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::changer {

namespace {
	std::atomic<int> s_last_mvp_kit_id{ 0 };
	std::chrono::steady_clock::time_point s_last_mvp_kit_time{};
}

	void music::on_frame_stage_notify( )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_alive || !local.controller )
		{
			return;
		}

		const auto controller = local.controller;

		const auto inv_services_offset = SCHEMA( "CCSPlayerController", "m_pInventoryServices"_hash );
		const auto inventory_services = inv_services_offset ? memory::read<std::uintptr_t>( controller + inv_services_offset ) : 0;

		const auto music_id_offset = SCHEMA( "CCSPlayerController_InventoryServices", "m_unMusicID"_hash );
		const auto kit_id_offset = SCHEMA( "CCSPlayerController", "m_iMusicKitID"_hash );
		const auto mvp_no_music_offset = SCHEMA( "CCSPlayerController", "m_bMvpNoMusic"_hash );
		const auto kit_mvps_offset = SCHEMA( "CCSPlayerController", "m_iMusicKitMVPs"_hash );

		if ( this->m_last_controller != controller )
		{
			this->m_captured = false;
			this->m_last_controller = controller;
		}

		if ( !this->m_captured )
		{
			if ( inventory_services && music_id_offset )
			{
				this->m_original_music = memory::read<std::uint16_t>( inventory_services + music_id_offset );
			}
			if ( kit_id_offset )
			{
				this->m_original_music_kit_id = memory::read<std::int32_t>( controller + kit_id_offset );
			}
			if ( mvp_no_music_offset )
			{
				this->m_original_mvp_no_music = memory::read<bool>( controller + mvp_no_music_offset );
			}
			if ( kit_mvps_offset )
			{
				this->m_original_music_kit_mvps = memory::read<std::int32_t>( controller + kit_mvps_offset );
			}

			this->m_captured = true;
		}

		const auto target_id = static_cast< std::uint16_t >( settings::g_changer.music.id );
		if ( target_id > 0 )
		{
			if ( inventory_services && music_id_offset )
			{
				const auto current_music = memory::read<std::uint16_t>( inventory_services + music_id_offset );
				if ( current_music != target_id )
				{
					memory::write<std::uint16_t>( inventory_services + music_id_offset, target_id );
				}
			}

			if ( kit_id_offset )
			{
				const auto current_kit = memory::read<std::int32_t>( controller + kit_id_offset );
				if ( current_kit != static_cast< std::int32_t >( target_id ) )
				{
					memory::write<std::int32_t>( controller + kit_id_offset, static_cast< std::int32_t >( target_id ) );
				}
			}

			if ( mvp_no_music_offset )
			{
				if ( memory::read<bool>( controller + mvp_no_music_offset ) )
				{
					memory::write<bool>( controller + mvp_no_music_offset, false );
				}
			}

			if ( kit_mvps_offset )
			{
				const auto current_mvps = memory::read<std::int32_t>( controller + kit_mvps_offset );
				if ( current_mvps < 1 )
				{
					memory::write<std::int32_t>( controller + kit_mvps_offset, 1 );
				}
			}
		}
		else if ( this->m_captured )
		{
			if ( inventory_services && music_id_offset )
			{
				const auto current_music = memory::read<std::uint16_t>( inventory_services + music_id_offset );
				if ( current_music != this->m_original_music )
				{
					memory::write<std::uint16_t>( inventory_services + music_id_offset, this->m_original_music );
				}
			}

			if ( kit_id_offset )
			{
				const auto current_kit = memory::read<std::int32_t>( controller + kit_id_offset );
				if ( current_kit != this->m_original_music_kit_id )
				{
					memory::write<std::int32_t>( controller + kit_id_offset, this->m_original_music_kit_id );
				}
			}

			if ( mvp_no_music_offset )
			{
				memory::write<bool>( controller + mvp_no_music_offset, this->m_original_mvp_no_music );
			}

			if ( kit_mvps_offset )
			{
				memory::write<std::int32_t>( controller + kit_mvps_offset, this->m_original_music_kit_mvps );
			}
		}
	}

	void music::reset( )
	{
		if ( this->m_captured && this->m_last_controller )
		{
			const auto inv_services_offset = SCHEMA( "CCSPlayerController", "m_pInventoryServices"_hash );
			if ( inv_services_offset )
			{
				const auto inventory_services = memory::read<std::uintptr_t>( this->m_last_controller + inv_services_offset );
				if ( inventory_services )
				{
					const auto music_id_offset = SCHEMA( "CCSPlayerController_InventoryServices", "m_unMusicID"_hash );
					if ( music_id_offset )
					{
						memory::write<std::uint16_t>( inventory_services + music_id_offset, this->m_original_music );
					}
				}
			}

			const auto kit_id_offset = SCHEMA( "CCSPlayerController", "m_iMusicKitID"_hash );
			if ( kit_id_offset )
			{
				memory::write<std::int32_t>( this->m_last_controller + kit_id_offset, this->m_original_music_kit_id );
			}

			const auto mvp_no_music_offset = SCHEMA( "CCSPlayerController", "m_bMvpNoMusic"_hash );
			if ( mvp_no_music_offset )
			{
				memory::write<bool>( this->m_last_controller + mvp_no_music_offset, this->m_original_mvp_no_music );
			}

			const auto kit_mvps_offset = SCHEMA( "CCSPlayerController", "m_iMusicKitMVPs"_hash );
			if ( kit_mvps_offset )
			{
				memory::write<std::int32_t>( this->m_last_controller + kit_mvps_offset, this->m_original_music_kit_mvps );
			}
		}

		this->m_original_music = 0;
		this->m_original_music_kit_id = 0;
		this->m_original_mvp_no_music = false;
		this->m_original_music_kit_mvps = 0;
		this->m_captured = false;
		this->m_last_controller = 0;
		this->m_local_won_last_mvp = false;
	}

	void music::on_round_mvp( void* event )
	{
		if ( !event )
		{
			return;
		}

		const auto mvp_controller = systems::events::get_controller( event, "userid" );
		const auto local_controller = systems::g_local.get( ).controller;

		int target_id = 0;

		if ( mvp_controller && local_controller && mvp_controller == local_controller )
		{
			this->m_local_won_last_mvp = true;
			this->m_last_mvp_time = std::chrono::steady_clock::now( );
			target_id = settings::g_changer.music.id;
		}
		else if ( mvp_controller )
		{
			this->m_local_won_last_mvp = false;
			const auto steam_id = memory::read<std::uint64_t>( mvp_controller + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
			if ( steam_id )
			{
				target_id = g_skin_sync.get_remote_music_kit( steam_id );
			}
		}

		if ( target_id > 0 && mvp_controller )
		{
			s_last_mvp_kit_id = target_id;
			s_last_mvp_kit_time = std::chrono::steady_clock::now( );

			const auto kit_id_offset = SCHEMA( "CCSPlayerController", "m_iMusicKitID"_hash );
			if ( kit_id_offset )
			{
				memory::write<std::int32_t>( mvp_controller + kit_id_offset, static_cast< std::int32_t >( target_id ) );
			}

			const auto mvp_no_music_offset = SCHEMA( "CCSPlayerController", "m_bMvpNoMusic"_hash );
			if ( mvp_no_music_offset )
			{
				memory::write<bool>( mvp_controller + mvp_no_music_offset, false );
			}

			const auto inv_services_offset = SCHEMA( "CCSPlayerController", "m_pInventoryServices"_hash );
			const auto inv_services = inv_services_offset ? memory::read<std::uintptr_t>( mvp_controller + inv_services_offset ) : 0;
			const auto music_id_offset = SCHEMA( "CCSPlayerController_InventoryServices", "m_unMusicID"_hash );
			if ( inv_services && music_id_offset )
			{
				memory::write<std::uint16_t>( inv_services + music_id_offset, static_cast< std::uint16_t >( target_id ) );
			}
		}
	}

	bool music::is_local_mvp( ) const
	{
		if ( !this->m_local_won_last_mvp )
		{
			return false;
		}

		const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::steady_clock::now( ) - this->m_last_mvp_time ).count( );

		return elapsed <= 20;
	}

	int get_current_mvp_kit_id( )
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::steady_clock::now( ) - s_last_mvp_kit_time ).count( );
		if ( elapsed <= 25 )
		{
			return s_last_mvp_kit_id.load( );
		}
		return 0;
	}

} // namespace features::changer
