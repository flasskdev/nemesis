#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/rendering/theme.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>

#include "../misc.hpp"

namespace features::misc {

	namespace {

		inline std::string read_protobuf_string( std::uintptr_t field_addr )
		{
			const auto str_obj = memory::safe_read<std::uintptr_t>( field_addr ).value_or( 0 );
			if ( !str_obj )
				return "";

			const auto str_ptr = str_obj & ~3ull;
			if ( !str_ptr )
				return "";

			const auto capacity = memory::safe_read<std::size_t>( str_ptr + 0x18 ).value_or( 0 );
			const auto size = memory::safe_read<std::size_t>( str_ptr + 0x10 ).value_or( 0 );

			if ( size == 0 || size > 512 )
				return "";

			if ( capacity > 15 )
			{
				const auto heap_ptr = memory::safe_read<std::uintptr_t>( str_ptr ).value_or( 0 );
				if ( !heap_ptr )
					return "";
				return memory::read_string( heap_ptr, static_cast<std::uint32_t>( size ) );
			}
			else
			{
				return memory::read_string( str_ptr, static_cast<std::uint32_t>( size ) );
			}
		}

		inline std::string get_player_name_by_slot( int slot )
		{
			if ( slot < 0 || slot >= 64 )
				return "";

			auto controller = systems::g_entities.get_by_index( slot + 1 );
			if ( !controller )
				controller = systems::g_entities.get_by_index( slot );

			if ( controller )
			{
				const auto name_ptr = memory::read<std::uintptr_t>( controller + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) );
				if ( name_ptr )
				{
					auto name = memory::read_string( name_ptr, 127 );
					if ( !name.empty( ) )
						return name;
				}
			}

			for ( const auto& player : systems::g_entities.get_by_type( systems::entities::type::player ) )
			{
				if ( player.ptr && ( player.index == slot + 1 || player.index == slot ) )
				{
					const auto name_ptr = memory::read<std::uintptr_t>( player.ptr + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) );
					if ( name_ptr )
					{
						auto name = memory::read_string( name_ptr, 127 );
						if ( !name.empty( ) )
							return name;
					}
				}
			}

			return "";
		}

		inline std::string format_map_name( std::string map_name )
		{
			const auto first_valid = map_name.find_first_not_of( " \t\r\n\"'" );
			if ( first_valid == std::string::npos )
				return "";
			const auto last_valid = map_name.find_last_not_of( " \t\r\n\"'" );
			map_name = map_name.substr( first_valid, last_valid - first_valid + 1 );

			const auto slash_pos = map_name.find_last_of( "/\\" );
			if ( slash_pos != std::string::npos )
				map_name = map_name.substr( slash_pos + 1 );

			if ( map_name.starts_with( "#SFUI_Map_" ) )
				map_name.erase( 0, 10 );
			else if ( map_name.starts_with( "#sfui_map_" ) )
				map_name.erase( 0, 10 );
			else if ( map_name.starts_with( "#" ) )
				map_name.erase( 0, 1 );

			if ( map_name.ends_with( ".vpk" ) )
				map_name.resize( map_name.size( ) - 4 );
			else if ( map_name.ends_with( ".bsp" ) )
				map_name.resize( map_name.size( ) - 4 );

			if ( map_name.empty( ) )
				return "";

			const auto first_underscore = map_name.find( '_' );
			if ( first_underscore != std::string::npos && first_underscore + 1 < map_name.size( ) )
				map_name = map_name.substr( first_underscore + 1 );

			std::string result;
			bool new_word = true;
			for ( const char c : map_name )
			{
				if ( c == '_' || c == '-' )
				{
					if ( !result.empty( ) && result.back( ) != ' ' )
						result += ' ';
					new_word = true;
				}
				else
				{
					if ( new_word )
					{
						result += static_cast<char>( std::toupper( static_cast<unsigned char>( c ) ) );
						new_word = false;
					}
					else
					{
						result += static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
					}
				}
			}

			return result.empty( ) ? map_name : result;
		}

	} // namespace

	void vote_logs::on_vote_start( std::uintptr_t msg )
	{
		if ( !msg )
			return;

		std::unique_lock lock( this->m_mtx );

		this->m_vote_in_progress = true;
		this->m_yes_votes = 0;
		this->m_no_votes = 0;
		this->m_voted_players.clear( );
		this->m_voted_names.clear( );

		if ( !settings::g_misc.m_impacts.vote_log.value )
			return;

		const auto caller_slot = memory::safe_read<int>( msg + 0x6c ).value_or( -1 );
		std::string caller_name = "server";

		if ( caller_slot != 99 && caller_slot != 0x63 && caller_slot >= 0 && caller_slot < 64 )
		{
			const auto name = get_player_name_by_slot( caller_slot );
			if ( !name.empty( ) )
				caller_name = name;
		}

		const auto disp_str = read_protobuf_string( msg + 0x48 );
		const auto details_str = read_protobuf_string( msg + 0x50 );

		std::string reason = "unknown";
		if ( disp_str == "kick" )
		{
			const auto target_slot = memory::safe_read<int>( msg + 0x70 ).value_or( -1 );
			std::string target_name{};
			if ( target_slot >= 0 && target_slot < 64 )
			{
				target_name = get_player_name_by_slot( target_slot );
			}
			if ( target_name.empty( ) && !details_str.empty( ) )
			{
				target_name = details_str;
			}

			if ( !target_name.empty( ) )
				reason = "kick " + target_name;
			else
				reason = "kick";
		}
		else if ( disp_str == "surrender" || disp_str == "#SFUI_vote_surrender" )
		{
			reason = "surrender";
		}
		else if ( disp_str == "starttimeout" || disp_str == "timeout" || disp_str == "#SFUI_vote_timeout" )
		{
			reason = "timeout";
		}
		else if ( disp_str == "pausematch" || disp_str == "#SFUI_vote_pause_match" )
		{
			reason = "pause match";
		}
		else if ( disp_str == "unpausematch" || disp_str == "#SFUI_vote_unpause_match" )
		{
			reason = "unpause match";
		}
		else if ( disp_str == "loadbackup" )
		{
			reason = "load backup";
		}
		else if ( disp_str == "restartgame" || disp_str == "#SFUI_vote_restart_game" )
		{
			reason = "restart game";
		}
		else if ( disp_str == "changelevel" || disp_str == "#SFUI_vote_changelevel" ||
		          disp_str == "nextlevel" || disp_str == "#SFUI_vote_nextlevel" ||
		          disp_str.find( "changelevel" ) != std::string::npos ||
		          disp_str.find( "changemap" ) != std::string::npos )
		{
			const auto map_name = format_map_name( details_str );
			if ( !map_name.empty( ) )
				reason = "change map to " + map_name;
			else
				reason = "change map";
		}
		else if ( !disp_str.empty( ) )
		{
			reason = disp_str;
			if ( reason.starts_with( "#SFUI_vote_" ) )
				reason.erase( 0, 11 );
			else if ( reason.starts_with( "#Panorama_Vote_" ) )
				reason.erase( 0, 15 );
			std::ranges::replace( reason, '_', ' ' );
		}

		const auto r = tokens::col_accent.r;
		const auto g = tokens::col_accent.g;
		const auto b = tokens::col_accent.b;

		const auto formatted = std::format(
			"<font color='#{:02X}{:02X}{:02X}'>mintaly</font> <font color='#888888'>:</font> <font color='#38BDF8'>{} start vote for {}</font>",
			r, g, b, caller_name, reason
		);

		detail::chat_print_raw( formatted.c_str( ) );
	}

	void vote_logs::on_vote_cast( std::uintptr_t event )
	{
		if ( !event )
			return;

		if ( !settings::g_misc.m_impacts.vote_log.value )
			return;

		const auto vote_option = memory::call<int>( PATTERN( patterns::game_event_get_int ), event, "vote_option", -1 );
		if ( vote_option != 0 && vote_option != 1 )
			return;

		std::unique_lock lock( this->m_mtx );

		auto voter_slot = memory::call<int>( PATTERN( patterns::game_event_get_int ), event, "entityid", -1 );
		if ( voter_slot == -1 )
			voter_slot = memory::call<int>( PATTERN( patterns::game_event_get_int ), event, "id", -1 );
		if ( voter_slot == -1 )
			voter_slot = memory::call<int>( PATTERN( patterns::game_event_get_int ), event, "userid", -1 );

		std::uintptr_t controller{};
		if ( voter_slot >= 0 && voter_slot < 64 )
			controller = systems::g_entities.get_by_index( voter_slot + 1 );
		if ( !controller && voter_slot >= 0 )
			controller = systems::g_entities.get_by_index( voter_slot );

		if ( !controller )
		{
			const auto entityid_key = cstypes::event_hash{ 0, "entityid" };
			controller = memory::call<std::uintptr_t>( PATTERN( patterns::game_event_get_controller ), event, &entityid_key );
		}
		if ( !controller )
		{
			const auto userid_key = cstypes::event_hash{ 0, "userid" };
			controller = memory::call<std::uintptr_t>( PATTERN( patterns::game_event_get_controller ), event, &userid_key );
		}

		std::string player_name{};
		if ( controller )
		{
			const auto name_ptr = memory::read<std::uintptr_t>( controller + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) );
			if ( name_ptr )
			{
				player_name = memory::read_string( name_ptr, 127 );
			}
		}

		if ( player_name.empty( ) && voter_slot >= 0 )
		{
			player_name = get_player_name_by_slot( voter_slot );
		}

		if ( player_name.empty( ) )
			player_name = "player";

		if ( controller )
		{
			if ( this->m_voted_players.contains( controller ) )
				return;
			this->m_voted_players.insert( controller );
		}
		else if ( player_name != "player" )
		{
			if ( this->m_voted_names.contains( player_name ) )
				return;
			this->m_voted_names.insert( player_name );
		}

		const auto r = tokens::col_accent.r;
		const auto g = tokens::col_accent.g;
		const auto b = tokens::col_accent.b;

		if ( vote_option == 0 )
		{
			this->m_yes_votes++;
			const auto formatted = std::format(
				"<font color='#{:02X}{:02X}{:02X}'>mintaly</font> <font color='#888888'>:</font> {} <font color='#4ADE80'>vote yes</font>",
				r, g, b, player_name
			);
			detail::chat_print_raw( formatted.c_str( ) );
		}
		else
		{
			this->m_no_votes++;
			const auto formatted = std::format(
				"<font color='#{:02X}{:02X}{:02X}'>mintaly</font> <font color='#888888'>:</font> {} <font color='#FF5C80'>vote no</font>",
				r, g, b, player_name
			);
			detail::chat_print_raw( formatted.c_str( ) );
		}
	}

	void vote_logs::on_vote_pass( std::uintptr_t msg )
	{
		this->on_vote_finish( true );
	}

	void vote_logs::on_vote_failed( std::uintptr_t msg )
	{
		this->on_vote_finish( false );
	}

	void vote_logs::on_vote_failed_event( std::uintptr_t event )
	{
		this->on_vote_finish( false );
	}

	void vote_logs::on_vote_finish( bool passed )
	{
		std::unique_lock lock( this->m_mtx );

		if ( !this->m_vote_in_progress && this->m_yes_votes == 0 && this->m_no_votes == 0 )
		{
			return;
		}

		if ( settings::g_misc.m_impacts.vote_log.value )
		{
			const auto r = tokens::col_accent.r;
			const auto g = tokens::col_accent.g;
			const auto b = tokens::col_accent.b;

			const auto formatted = std::format(
				"<font color='#{:02X}{:02X}{:02X}'>mintaly</font> <font color='#888888'>:</font> <font color='#38BDF8'>vote results</font> <font color='#4ADE80'>{} yes</font>, <font color='#FF5C80'>{} no</font>",
				r, g, b, this->m_yes_votes, this->m_no_votes
			);

			detail::chat_print_raw( formatted.c_str( ) );
		}

		this->m_vote_in_progress = false;
		this->m_yes_votes = 0;
		this->m_no_votes = 0;
		this->m_voted_players.clear( );
		this->m_voted_names.clear( );
	}

	void vote_logs::reset( )
	{
		std::unique_lock lock( this->m_mtx );
		this->m_vote_in_progress = false;
		this->m_yes_votes = 0;
		this->m_no_votes = 0;
		this->m_voted_players.clear( );
		this->m_voted_names.clear( );
	}

} // namespace features::misc
