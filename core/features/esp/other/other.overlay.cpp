#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/steam/steam.hpp>
#include <core/rendering/rendering.hpp>
#include <core/rendering/theme.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

namespace features::esp::other {

	namespace detail {

		struct avatar_cache
		{
			struct entry
			{
				Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture{};
				bool attempted{};
			};

			std::unordered_map<std::uintptr_t, entry> m_entries{};

			[[nodiscard]] ID3D11ShaderResourceView* get( std::uintptr_t steam_id )
			{
				auto it = this->m_entries.find( steam_id );
				if ( it != this->m_entries.end( ) )
				{
					return it->second.texture.Get( );
				}

				auto& e = this->m_entries[ steam_id ];
				e.attempted = true;

				const auto image_handle = steam::friends::get_medium_friend_avatar( steam_id );
				if ( image_handle <= 0 )
				{
					return nullptr;
				}

				std::uint32_t w{}, h{};
				if ( !steam::utils::get_image_size( image_handle, &w, &h ) || !w || !h )
				{
					return nullptr;
				}

				std::vector<std::uint8_t> rgba( w * h * 4 );
				if ( !steam::utils::get_image_rgba( image_handle, rgba.data( ), static_cast< int >( rgba.size( ) ) ) )
				{
					return nullptr;
				}

				e.texture = xdraw::create_srv_from_rgba( rgba.data( ), static_cast< int >( w ), static_cast< int >( h ) );
				return e.texture.Get( );
			}

			void clear( )
			{
				this->m_entries.clear( );
			}
		};

		constexpr unsigned char spectator_icon[ 1030 ]
		{
			0x3C, 0x73, 0x76, 0x67, 0x20, 0x77, 0x69, 0x64, 0x74, 0x68, 0x3D, 0x22,
			0x31, 0x32, 0x22, 0x20, 0x68, 0x65, 0x69, 0x67, 0x68, 0x74, 0x3D, 0x22,
			0x31, 0x32, 0x22, 0x20, 0x76, 0x69, 0x65, 0x77, 0x42, 0x6F, 0x78, 0x3D,
			0x22, 0x30, 0x20, 0x30, 0x20, 0x31, 0x32, 0x20, 0x31, 0x32, 0x22, 0x20,
			0x66, 0x69, 0x6C, 0x6C, 0x3D, 0x22, 0x6E, 0x6F, 0x6E, 0x65, 0x22, 0x20,
			0x78, 0x6D, 0x6C, 0x6E, 0x73, 0x3D, 0x22, 0x68, 0x74, 0x74, 0x70, 0x3A,
			0x2F, 0x2F, 0x77, 0x77, 0x77, 0x2E, 0x77, 0x33, 0x2E, 0x6F, 0x72, 0x67,
			0x2F, 0x32, 0x30, 0x30, 0x30, 0x2F, 0x73, 0x76, 0x67, 0x22, 0x3E, 0x0D,
			0x0A, 0x3C, 0x67, 0x20, 0x63, 0x6C, 0x69, 0x70, 0x2D, 0x70, 0x61, 0x74,
			0x68, 0x3D, 0x22, 0x75, 0x72, 0x6C, 0x28, 0x23, 0x63, 0x6C, 0x69, 0x70,
			0x30, 0x5F, 0x31, 0x35, 0x35, 0x5F, 0x32, 0x32, 0x33, 0x29, 0x22, 0x3E,
			0x0D, 0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D,
			0x35, 0x20, 0x36, 0x43, 0x35, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32,
			0x32, 0x20, 0x35, 0x2E, 0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x36, 0x2E,
			0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38,
			0x39, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31, 0x31, 0x43, 0x35, 0x2E,
			0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x36, 0x2E, 0x38, 0x39, 0x34, 0x36,
			0x34, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20, 0x37, 0x20,
			0x36, 0x20, 0x37, 0x43, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20,
			0x37, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x36, 0x2E,
			0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31,
			0x31, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31, 0x31, 0x43, 0x36, 0x2E,
			0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x39, 0x35,
			0x37, 0x20, 0x37, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20,
			0x37, 0x20, 0x36, 0x43, 0x37, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37,
			0x38, 0x20, 0x36, 0x2E, 0x38, 0x39, 0x34, 0x36, 0x34, 0x20, 0x35, 0x2E,
			0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x36, 0x2E, 0x37, 0x30, 0x37, 0x31,
			0x31, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38, 0x39, 0x43, 0x36, 0x2E,
			0x35, 0x31, 0x39, 0x35, 0x37, 0x20, 0x35, 0x2E, 0x31, 0x30, 0x35, 0x33,
			0x36, 0x20, 0x36, 0x2E, 0x32, 0x36, 0x35, 0x32, 0x32, 0x20, 0x35, 0x20,
			0x36, 0x20, 0x35, 0x43, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20,
			0x35, 0x20, 0x35, 0x2E, 0x34, 0x38, 0x30, 0x34, 0x33, 0x20, 0x35, 0x2E,
			0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38,
			0x39, 0x20, 0x35, 0x2E, 0x32, 0x39, 0x32, 0x38, 0x39, 0x43, 0x35, 0x2E,
			0x31, 0x30, 0x35, 0x33, 0x36, 0x20, 0x35, 0x2E, 0x34, 0x38, 0x30, 0x34,
			0x33, 0x20, 0x35, 0x20, 0x35, 0x2E, 0x37, 0x33, 0x34, 0x37, 0x38, 0x20,
			0x35, 0x20, 0x36, 0x5A, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65,
			0x3D, 0x22, 0x23, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73,
			0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61,
			0x70, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74,
			0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69,
			0x6E, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D,
			0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D, 0x37,
			0x2E, 0x35, 0x31, 0x35, 0x20, 0x38, 0x2E, 0x37, 0x33, 0x39, 0x43, 0x37,
			0x2E, 0x30, 0x32, 0x39, 0x32, 0x34, 0x20, 0x38, 0x2E, 0x39, 0x31, 0x34,
			0x32, 0x36, 0x20, 0x36, 0x2E, 0x35, 0x31, 0x36, 0x34, 0x31, 0x20, 0x39,
			0x2E, 0x30, 0x30, 0x32, 0x36, 0x31, 0x20, 0x36, 0x20, 0x39, 0x43, 0x34,
			0x2E, 0x32, 0x20, 0x39, 0x20, 0x32, 0x2E, 0x37, 0x20, 0x38, 0x20, 0x31,
			0x2E, 0x35, 0x20, 0x36, 0x43, 0x32, 0x2E, 0x37, 0x20, 0x34, 0x20, 0x34,
			0x2E, 0x32, 0x20, 0x33, 0x20, 0x36, 0x20, 0x33, 0x43, 0x37, 0x2E, 0x38,
			0x20, 0x33, 0x20, 0x39, 0x2E, 0x33, 0x20, 0x34, 0x20, 0x31, 0x30, 0x2E,
			0x35, 0x20, 0x36, 0x43, 0x31, 0x30, 0x2E, 0x34, 0x35, 0x37, 0x38, 0x20,
			0x36, 0x2E, 0x30, 0x37, 0x30, 0x33, 0x35, 0x20, 0x31, 0x30, 0x2E, 0x34,
			0x31, 0x34, 0x38, 0x20, 0x36, 0x2E, 0x31, 0x34, 0x30, 0x31, 0x39, 0x20,
			0x31, 0x30, 0x2E, 0x33, 0x37, 0x31, 0x20, 0x36, 0x2E, 0x32, 0x30, 0x39,
			0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x3D, 0x22, 0x23,
			0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F,
			0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61, 0x70, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
			0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69, 0x6E, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x70,
			0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D, 0x39, 0x2E, 0x35, 0x20,
			0x38, 0x56, 0x39, 0x2E, 0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
			0x65, 0x3D, 0x22, 0x23, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20,
			0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63,
			0x61, 0x70, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73,
			0x74, 0x72, 0x6F, 0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F,
			0x69, 0x6E, 0x3D, 0x22, 0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E,
			0x0D, 0x0A, 0x3C, 0x70, 0x61, 0x74, 0x68, 0x20, 0x64, 0x3D, 0x22, 0x4D,
			0x39, 0x2E, 0x35, 0x20, 0x31, 0x31, 0x56, 0x31, 0x31, 0x2E, 0x30, 0x30,
			0x35, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B, 0x65, 0x3D, 0x22, 0x23,
			0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F,
			0x6B, 0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x63, 0x61, 0x70, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x20, 0x73, 0x74, 0x72, 0x6F, 0x6B,
			0x65, 0x2D, 0x6C, 0x69, 0x6E, 0x65, 0x6A, 0x6F, 0x69, 0x6E, 0x3D, 0x22,
			0x72, 0x6F, 0x75, 0x6E, 0x64, 0x22, 0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F,
			0x67, 0x3E, 0x0D, 0x0A, 0x3C, 0x64, 0x65, 0x66, 0x73, 0x3E, 0x0D, 0x0A,
			0x3C, 0x63, 0x6C, 0x69, 0x70, 0x50, 0x61, 0x74, 0x68, 0x20, 0x69, 0x64,
			0x3D, 0x22, 0x63, 0x6C, 0x69, 0x70, 0x30, 0x5F, 0x31, 0x35, 0x35, 0x5F,
			0x32, 0x32, 0x33, 0x22, 0x3E, 0x0D, 0x0A, 0x3C, 0x72, 0x65, 0x63, 0x74,
			0x20, 0x77, 0x69, 0x64, 0x74, 0x68, 0x3D, 0x22, 0x31, 0x32, 0x22, 0x20,
			0x68, 0x65, 0x69, 0x67, 0x68, 0x74, 0x3D, 0x22, 0x31, 0x32, 0x22, 0x20,
			0x66, 0x69, 0x6C, 0x6C, 0x3D, 0x22, 0x77, 0x68, 0x69, 0x74, 0x65, 0x22,
			0x2F, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F, 0x63, 0x6C, 0x69, 0x70, 0x50, 0x61,
			0x74, 0x68, 0x3E, 0x0D, 0x0A, 0x3C, 0x2F, 0x64, 0x65, 0x66, 0x73, 0x3E,
			0x0D, 0x0A, 0x3C, 0x2F, 0x73, 0x76, 0x67, 0x3E, 0x0D, 0x0A
		};

	} // namespace detail

	void overlay::on_render( xdraw::draw_list& draw_list )
	{
		if ( !settings::g_esp.m_other.bomb_timer.value )
		{
			return;
		}

		this->add_bomb( draw_list );
	}

	void overlay::add_bomb( xdraw::draw_list& draw_list )
	{
		const auto local = systems::g_local.get( );
		if ( !local.is_valid( ) || !systems::g_entities.exists( local.view_controller( ) ) )
		{
			return;
		}

		const auto planted_c4 = memory::read<std::uintptr_t>( addresses::globals::planted_c4 );
		const auto global_vars = memory::read<std::uintptr_t>( addresses::globals::global_vars );

		if ( !planted_c4 || !global_vars )
		{
			return;
		}

		const auto current_time = memory::read<float>( global_vars + 0x30 );
		const auto blow_time = memory::read<float>( planted_c4 + SCHEMA( "C_PlantedC4", "m_flC4Blow"_hash ) );
		const auto has_exploded = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bHasExploded"_hash ) );
		const auto bomb_defused = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bBombDefused"_hash ) );

		if ( bomb_defused )
		{
			return;
		}

		const auto time_remaining = blow_time - current_time;
		const auto is_exploding = has_exploded || time_remaining <= 0.0f;

		if ( is_exploding && time_remaining < -2.5f )
		{
			return;
		}

		const auto bomb_site = memory::read<int>( planted_c4 + SCHEMA( "C_PlantedC4", "m_nBombSite"_hash ) );
		const auto being_defused = memory::read<bool>( planted_c4 + SCHEMA( "C_PlantedC4", "m_bBeingDefused"_hash ) );
		const auto timer_length = memory::read<float>( planted_c4 + SCHEMA( "C_PlantedC4", "m_flTimerLength"_hash ) );
		const auto defuse_length = memory::read<float>( planted_c4 + SCHEMA( "C_PlantedC4", "m_flDefuseLength"_hash ) );
		const auto defuse_countdown = memory::read<float>( planted_c4 + SCHEMA( "C_PlantedC4", "m_flDefuseCountDown"_hash ) );

		float defuse_time_left = 0.0f;
		bool can_defuse = false;
		bool has_kit = false;
		float defuse_frac = 0.0f;

		if ( being_defused && defuse_length > 0.0f )
		{
			defuse_time_left = std::max( 0.0f, defuse_countdown - current_time );
			can_defuse = ( defuse_countdown <= blow_time );
			has_kit = ( defuse_length <= 5.1f );
			defuse_frac = std::clamp( ( defuse_length - defuse_time_left ) / defuse_length, 0.0f, 1.0f );
		}

		float c4_distance = 0.0f;

		const auto view_pawn = local.view_pawn( );
		const auto health = ( view_pawn && local.is_alive ) ? memory::read<int>( view_pawn + SCHEMA( "C_BaseEntity", "m_iHealth"_hash ) ) : 0;

		const auto calculate_shockwave_damage = [ & ]( ) -> float
		{
			if ( !view_pawn || !local.is_alive )
			{
				return 0.0f;
			}

			const auto c4_scene_node = memory::read<std::uintptr_t>( planted_c4 + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );
			const auto pawn_scene_node = memory::read<std::uintptr_t>( view_pawn + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) );

			if ( !c4_scene_node || !pawn_scene_node )
			{
				return 0.0f;
			}

			const auto c4_origin = memory::read<math::vector3>( c4_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );
			const auto pawn_origin = memory::read<math::vector3>( pawn_scene_node + SCHEMA( "CGameSceneNode", "m_vecAbsOrigin"_hash ) );

			// In Source/CS2, distance is calculated between bomb center and entity torso/center (WorldSpaceCenter)
			const auto view_offset = memory::read<math::vector3>( view_pawn + SCHEMA( "C_BaseModelEntity", "m_vecViewOffset"_hash ) );
			const float target_z_offset = ( view_offset.z > 0.0f ) ? ( view_offset.z * 0.5f ) : 36.0f;

			const math::vector3 c4_center = c4_origin + math::vector3{ 0.0f, 0.0f, 6.0f };
			const math::vector3 pawn_center = pawn_origin + math::vector3{ 0.0f, 0.0f, target_z_offset };

			c4_distance = ( c4_center - pawn_center ).length( );

			// Map-specific base parameters in CS2
			float base_damage = 500.0f;
			float base_radius = 1750.0f;

			std::string map_lower = rendering::g_widgets.s_map_name;
			for ( auto& ch : map_lower )
			{
				ch = static_cast<char>( std::tolower( static_cast<unsigned char>( ch ) ) );
			}

			if ( map_lower.find( "nuke" ) != std::string::npos ||
				 map_lower.find( "vertigo" ) != std::string::npos ||
				 map_lower.find( "ancient" ) != std::string::npos ||
				 map_lower.find( "anubis" ) != std::string::npos )
			{
				base_damage = 650.0f;
				base_radius = 2275.0f;
			}

			if ( c4_distance >= base_radius )
			{
				return 0.0f;
			}

			// Canonical CS2 / CS:GO Gaussian blast falloff:
			// sigma = base_radius / 3.0f; damage = base_damage * exp( -dist^2 / (2 * sigma^2) )
			const float sigma = base_radius / 3.0f;
			const float raw_damage = base_damage * std::exp( -( c4_distance * c4_distance ) / ( 2.0f * sigma * sigma ) );

			// CS2 explosion armor absorption (DMG_BLAST)
			const auto armor = memory::read<int>( view_pawn + SCHEMA( "C_CSPlayerPawn", "m_ArmorValue"_hash ) );
			float damage = raw_damage;
			if ( armor > 0 )
			{
				constexpr auto armor_ratio = 0.5f;
				constexpr auto armor_bonus = 0.5f;

				auto damage_to_hp = raw_damage * armor_ratio;
				auto armor_loss = ( raw_damage - damage_to_hp ) * armor_bonus;

				if ( armor_loss > static_cast< float >( armor ) )
				{
					damage_to_hp = raw_damage - ( static_cast< float >( armor ) / armor_bonus );
				}

				damage = damage_to_hp;
			}

			return std::max( 0.0f, std::floor( damage ) );
		};

		const auto damage = static_cast< int >( calculate_shockwave_damage( ) );
		const auto remaining_hp = std::max( 0, health - damage );

		const auto [screen_w, screen_h] = xdraw::viewport_size( );

		// Luxury Cyber-HUD Capsule Geometry
		constexpr float total_w = 320.0f;
		constexpr float total_h = 60.0f;
		const float x = ( static_cast< float >( screen_w ) - total_w ) * 0.5f;
		constexpr float y = 115.0f;

		const auto card_r = xdraw::corner_radius{ 11.0f };

		// Dynamic alert state colors
		const xdraw::color state_col = being_defused
			? ( can_defuse ? xdraw::color{ 45, 235, 140, 255 } : xdraw::color{ 255, 65, 75, 255 } )
			: ( ( is_exploding || time_remaining < 5.0f )
				? xdraw::color{ 255, 60, 70, 255 }
				: ( ( time_remaining < 10.0f )
					? xdraw::color{ 255, 190, 50, 255 }
					: tokens::col_accent ) );

		const xdraw::color state_border_col = being_defused
			? ( can_defuse ? xdraw::color{ 45, 235, 140, 180 } : xdraw::color{ 255, 65, 75, 190 } )
			: ( ( is_exploding || time_remaining < 5.0f )
				? xdraw::color{ 255, 60, 70, 190 }
				: tokens::col_border.alpha( 120 ) );

		// Dual soft drop-shadow
		draw_list.rect_filled( x - 3.0f, y + 4.0f, total_w + 6.0f, total_h + 3.0f, xdraw::color{ 0, 0, 0, 35 }, xdraw::corner_radius{ 14.0f } );
		draw_list.rect_filled( x - 1.5f, y + 2.0f, total_w + 3.0f, total_h + 1.5f, xdraw::color{ 0, 0, 0, 55 }, xdraw::corner_radius{ 12.0f } );

		// Frosted cyber-glass plate
		draw_list.rect_filled_blurred( x, y, total_w, total_h, card_r, xdraw::color{ 255, 255, 255, 255 } );
		draw_list.rect_filled( x, y, total_w, total_h, tokens::col_dark.alpha( 228 ), card_r );
		draw_list.rect_filled_gradient( x, y, total_w, 20.0f, tokens::col_elevated.alpha( 45 ), tokens::col_elevated.alpha( 45 ), tokens::col_elevated.alpha( 0 ), tokens::col_elevated.alpha( 0 ) );

		// Dynamic outline
		draw_list.rect( x, y, total_w, total_h, state_border_col, card_r, 1.0f );

		// Top hairline neon bloom
		const auto half_w = ( total_w - 24.0f ) * 0.5f;
		draw_list.rect_filled_gradient( x + 12.0f, y, half_w, 1.2f, state_col.alpha( 0 ), state_col.alpha( 180 ), state_col.alpha( 180 ), state_col.alpha( 0 ) );
		draw_list.rect_filled_gradient( x + 12.0f + half_w, y, half_w, 1.2f, state_col.alpha( 180 ), state_col.alpha( 0 ), state_col.alpha( 0 ), state_col.alpha( 180 ) );

		// Left: Tactical Site Inlay Box
		const float emblem_x = x + 10.0f;
		const float emblem_y = y + 9.0f;
		constexpr float emblem_w = 36.0f;
		constexpr float emblem_h = 36.0f;
		const auto emblem_r = xdraw::corner_radius{ 7.0f };

		draw_list.rect_filled( emblem_x, emblem_y, emblem_w, emblem_h, tokens::col_elevated.alpha( 180 ), emblem_r );
		draw_list.rect( emblem_x, emblem_y, emblem_w, emblem_h, tokens::col_border.alpha( 120 ), emblem_r, 1.0f );

		// Left accent edge on emblem
		draw_list.rect_filled( emblem_x + 2.0f, emblem_y + 6.0f, 2.0f, emblem_h - 12.0f, state_col.alpha( 220 ), xdraw::corner_radius{ 1.0f } );

		// Site letter (A or B)
		const auto site_letter = ( bomb_site == 0 ) ? "A" : "B";
		const auto [ sl_w, sl_h ] = xdraw::measure_text( site_letter, rendering::g_fonts.inter_bold[ rendering::fonts::size::big ] );
		draw_list.text( emblem_x + ( emblem_w - sl_w ) * 0.5f + 1.0f, emblem_y + ( emblem_h - sl_h ) * 0.5f - 1.0f, site_letter, state_col, rendering::g_fonts.inter_bold[ rendering::fonts::size::big ] );

		// Middle: Main Countdown Timer & Tactical Subtitle
		const float timer_text_x = emblem_x + emblem_w + 12.0f;

		char timer_buf[ 16 ]{};
		if ( is_exploding )
		{
			std::snprintf( timer_buf, sizeof( timer_buf ), "0.0s" );
		}
		else
		{
			std::snprintf( timer_buf, sizeof( timer_buf ), "%.1fs", time_remaining );
		}

		const auto timer_font = rendering::g_fonts.inter_bold[ rendering::fonts::size::big ];
		const auto timer_col = ( is_exploding || time_remaining < 5.0f )
			? xdraw::color{ 255, 65, 75, 255 }
			: ( ( time_remaining < 10.0f )
				? xdraw::color{ 255, 195, 60, 255 }
				: tokens::col_text );
		draw_list.text( timer_text_x, y + 8.0f, timer_buf, timer_col, timer_font );

		// Subtitle underneath timer
		const auto label_font = rendering::g_fonts.inter_medium[ rendering::fonts::size::petite ];

		if ( being_defused )
		{
			char def_sub[ 48 ]{};
			std::snprintf( def_sub, sizeof( def_sub ), "DEFUSING %.1fs • %s", defuse_time_left, can_defuse ? "CAN DEFUSE" : "TOO LATE" );
			const auto def_sub_col = can_defuse ? xdraw::color{ 55, 230, 145, 230 } : xdraw::color{ 255, 75, 85, 230 };
			draw_list.text( timer_text_x, y + 29.0f, def_sub, def_sub_col, label_font );
		}
		else
		{
			char sub_txt[ 48 ]{};
			if ( c4_distance > 0.0f )
			{
				std::snprintf( sub_txt, sizeof( sub_txt ), "%s • %.0fm", is_exploding ? "EXPLODING" : "BOMB PLANTED", c4_distance * 0.0254f );
			}
			else
			{
				std::snprintf( sub_txt, sizeof( sub_txt ), "%s", is_exploding ? "EXPLODING" : "BOMB PLANTED" );
			}
			draw_list.text( timer_text_x, y + 29.0f, sub_txt, tokens::col_text_dim.alpha( 170 ), label_font );
		}

		// Right: Armor-Aware Health Impact Module
		constexpr float card_w = 82.0f;
		constexpr float card_h = 36.0f;
		const float card_x = x + total_w - 10.0f - card_w;
		const float card_y = y + 9.0f;
		const auto subcard_r = xdraw::corner_radius{ 6.0f };

		if ( local.is_alive && health > 0 )
		{
			if ( damage >= health )
			{
				// FATAL
				draw_list.rect_filled( card_x, card_y, card_w, card_h, xdraw::color{ 255, 55, 65, 38 }, subcard_r );
				draw_list.rect( card_x, card_y, card_w, card_h, xdraw::color{ 255, 55, 65, 130 }, subcard_r, 1.0f );

				const auto fatal_title = "FATAL";
				const auto [ fw, fh ] = xdraw::measure_text( fatal_title, rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );
				draw_list.text( card_x + ( card_w - fw ) * 0.5f, card_y + 4.0f, fatal_title, xdraw::color{ 255, 85, 95, 255 }, rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );

				char dmg_sub[ 16 ]{};
				std::snprintf( dmg_sub, sizeof( dmg_sub ), "-%d HP", damage );
				const auto [ dw, dh ] = xdraw::measure_text( dmg_sub, label_font );
				draw_list.text( card_x + ( card_w - dw ) * 0.5f, card_y + 19.0f, dmg_sub, xdraw::color{ 255, 110, 120, 220 }, label_font );
			}
			else if ( damage > 0 )
			{
				// SURVIVES WITH DAMAGE (Interactive Micro Health Bar)
				draw_list.rect_filled( card_x, card_y, card_w, card_h, xdraw::color{ 255, 180, 45, 30 }, subcard_r );
				draw_list.rect( card_x, card_y, card_w, card_h, xdraw::color{ 255, 180, 45, 110 }, subcard_r, 1.0f );

				char dmg_top[ 16 ]{};
				std::snprintf( dmg_top, sizeof( dmg_top ), "-%d", damage );
				draw_list.text( card_x + 6.0f, card_y + 4.0f, dmg_top, xdraw::color{ 255, 200, 70, 255 }, rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );

				char hp_top[ 16 ]{};
				std::snprintf( hp_top, sizeof( hp_top ), "%d HP", remaining_hp );
				const auto [ hw, hh ] = xdraw::measure_text( hp_top, label_font );
				draw_list.text( card_x + card_w - hw - 6.0f, card_y + 5.0f, hp_top, tokens::col_text.alpha( 220 ), label_font );

				// Micro Health Meter
				const float bar_inner_x = card_x + 6.0f;
				const float bar_inner_y = card_y + 22.0f;
				const float bar_inner_w = card_w - 12.0f;
				constexpr float bar_inner_h = 4.0f;
				const auto bar_inner_r = xdraw::corner_radius{ 2.0f };

				draw_list.rect_filled( bar_inner_x, bar_inner_y, bar_inner_w, bar_inner_h, xdraw::color{ 0, 0, 0, 90 }, bar_inner_r );

				const float hp_pct = std::clamp( static_cast< float >( remaining_hp ) / 100.0f, 0.0f, 1.0f );
				const auto hp_bar_col = ( remaining_hp > 50 )
					? xdraw::color{ 55, 225, 135, 255 }
					: ( ( remaining_hp > 25 )
						? xdraw::color{ 255, 190, 50, 255 }
						: xdraw::color{ 255, 65, 75, 255 } );
				draw_list.rect_filled( bar_inner_x, bar_inner_y, bar_inner_w * hp_pct, bar_inner_h, hp_bar_col, bar_inner_r );
			}
			else
			{
				// SAFE
				draw_list.rect_filled( card_x, card_y, card_w, card_h, xdraw::color{ 45, 215, 135, 30 }, subcard_r );
				draw_list.rect( card_x, card_y, card_w, card_h, xdraw::color{ 45, 215, 135, 110 }, subcard_r, 1.0f );

				const auto safe_title = "SAFE";
				const auto [ sw, sh ] = xdraw::measure_text( safe_title, rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );
				draw_list.text( card_x + ( card_w - sw ) * 0.5f, card_y + 4.0f, safe_title, xdraw::color{ 60, 235, 150, 255 }, rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );

				const auto safe_sub = "0 DMG";
				const auto [ ssw, ssh ] = xdraw::measure_text( safe_sub, label_font );
				draw_list.text( card_x + ( card_w - ssw ) * 0.5f, card_y + 19.0f, safe_sub, tokens::col_text_dim.alpha( 160 ), label_font );
			}
		}
		else
		{
			// Spectator / Dead
			draw_list.rect_filled( card_x, card_y, card_w, card_h, tokens::col_elevated.alpha( 150 ), subcard_r );
			draw_list.rect( card_x, card_y, card_w, card_h, tokens::col_border.alpha( 100 ), subcard_r, 1.0f );

			const auto spec_title = "C4 BLAST";
			const auto [ bw, bh ] = xdraw::measure_text( spec_title, label_font );
			draw_list.text( card_x + ( card_w - bw ) * 0.5f, card_y + 4.0f, spec_title, tokens::col_text_dim.alpha( 180 ), label_font );

			char dmg_sub[ 16 ]{};
			std::snprintf( dmg_sub, sizeof( dmg_sub ), "-%d HP", damage );
			const auto [ dw, dh ] = xdraw::measure_text( dmg_sub, rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );
			draw_list.text( card_x + ( card_w - dw ) * 0.5f, card_y + 18.0f, dmg_sub, tokens::col_text.alpha( 220 ), rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );
		}

		// Bottom Dual Progress Bar Track
		const auto bar_x = x + 10.0f;
		const auto bar_y = y + total_h - 7.0f;
		const auto bar_w = total_w - 20.0f;
		constexpr auto bar_h = 3.5f;
		const auto bar_r = xdraw::corner_radius{ 2.0f };

		// Background rail
		draw_list.rect_filled( bar_x, bar_y, bar_w, bar_h, tokens::col_elevated.alpha( 170 ), bar_r );

		// Detonation Progress Fill
		const float max_timer = ( timer_length > 0.0f ) ? timer_length : 40.0f;
		const float timer_pct = std::clamp( time_remaining / max_timer, 0.0f, 1.0f );
		const float fill_w = bar_w * timer_pct;

		if ( fill_w > 0.0f )
		{
			draw_list.rect_filled( bar_x, bar_y, fill_w, bar_h, state_col, bar_r );
		}

		// Defuse progress overlay & indicator marker
		if ( being_defused && defuse_frac > 0.0f )
		{
			const float defuse_w = bar_w * defuse_frac;
			const xdraw::color defuse_col = can_defuse ? xdraw::color{ 50, 235, 145, 255 } : xdraw::color{ 255, 65, 75, 255 };
			draw_list.rect_filled( bar_x, bar_y, defuse_w, bar_h, defuse_col, bar_r );

			// Threshold tick notch on the timeline
			if ( max_timer > 0.0f )
			{
				const float tick_pct = std::clamp( ( time_remaining - defuse_time_left ) / max_timer, 0.0f, 1.0f );
				const float tick_x = bar_x + bar_w * tick_pct;
				draw_list.rect_filled( tick_x - 1.0f, bar_y - 2.0f, 2.0f, bar_h + 4.0f, defuse_col, xdraw::corner_radius{ 1.0f } );
			}
		}
	}

	void overlay::add_spectators( xdraw::draw_list& /*draw_list*/ )
	{
	}

} // namespace features::esp::other
