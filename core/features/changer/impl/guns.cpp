#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <core/settings.hpp>
#include <utilities/steam/steam.hpp>
#include <protection/game_addresses.hpp>
namespace features::changer {

	void guns::on_frame_stage_notify( )
	{
		this->process_hud_clear( );

		const auto local = systems::g_local.get( );
		if ( !local.is_alive || systems::g_local.is_in_cinematic( ) || !local.pawn || !local.controller )
		{
			return;
		}

		const auto weapon_services = memory::read<std::uintptr_t>( local.pawn + SCHEMA( "C_BasePlayerPawn", "m_pWeaponServices"_hash ) );
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

		auto steam_id = steam::user::get_steam_id( );
		if ( !steam_id && local.controller )
		{
			steam_id = memory::read<std::uint64_t>( local.controller + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
		}
		const auto account_id = static_cast< std::uint32_t >( steam_id & 0xffffffff );
		const auto active_handle = memory::read<std::uint32_t>( weapon_services + SCHEMA( "CPlayer_WeaponServices", "m_hActiveWeapon"_hash ) );
		const auto active_weapon = systems::g_entities.lookup( active_handle );

		if ( this->m_tracked_pawn != local.pawn )
		{
			this->m_applied_weapons.clear( );
			this->m_last_active_handle = 0;
			this->m_tracked_pawn = local.pawn;
		}

		for ( auto i = 0; i < weapons_size; ++i )
		{
			const auto handle = memory::safe_read<std::uint32_t>( weapons_data + i * sizeof( std::uint32_t ) ).value_or( 0 );
			if ( !handle )
			{
				continue;
			}

			const auto weapon = systems::g_entities.lookup( handle );
			if ( !weapon || weapon < 0x10000 )
			{
				continue;
			}

			const auto iv = weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash ) + SCHEMA( "C_AttributeContainer", "m_Item"_hash );
			const auto current_def_index = memory::safe_read<std::uint16_t>( iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) ).value_or( 0 );
			const auto current_def = g_econ_item_system.find_def( static_cast< std::int16_t >( current_def_index ) );

			if ( !current_def || current_def->category != econ_item_system::item_category::gun )
			{
				continue;
			}

			const auto skin_it = settings::g_changer.skins.data.find( current_def_index );
			if ( skin_it == settings::g_changer.skins.data.end( ) )
			{
				continue;
			}

			const auto skin = cosmetic_attributes::normalize(skin_it->second);
			const auto applied_it = this->m_applied_weapons.find( handle );

			if ( applied_it != this->m_applied_weapons.end( ) && applied_it->second.weapon == weapon && applied_it->second.skin == skin )
			{
				continue;
			}

			const auto subclass_ptr = memory::safe_read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 ).value_or( 0 );
			if ( !subclass_ptr )
			{
				continue;
			}

			if (this->apply( weapon, iv, handle, active_handle, local.pawn, &skin, account_id ))
                this->m_applied_weapons[ handle ] = { weapon, skin };
		}

		if ( active_handle != this->m_last_active_handle )
		{
			this->m_last_active_handle = active_handle;

			if ( active_weapon )
			{
				const auto iv = active_weapon + SCHEMA( "C_EconEntity", "m_AttributeManager"_hash ) + SCHEMA( "C_AttributeContainer", "m_Item"_hash );
				const auto def_index = memory::safe_read<std::uint16_t>( iv + SCHEMA( "C_EconItemView", "m_iItemDefinitionIndex"_hash ) ).value_or( 0 );
				const auto def = g_econ_item_system.find_def( static_cast< std::int16_t >( def_index ) );

				if ( def && def->category == econ_item_system::item_category::gun )
				{
					const auto paint_kit_id = memory::safe_read<int>( active_weapon + SCHEMA( "C_EconEntity", "m_nFallbackPaintKit"_hash ) ).value_or( 0 );
					const auto pk = g_econ_item_system.find_paint_kit( paint_kit_id );
					this->update_view_model( local.pawn, pk );
				}
			}
		}
	}

	bool guns::apply( std::uintptr_t weapon, std::uintptr_t iv, std::uint32_t handle, std::uint32_t active_handle, std::uintptr_t pawn, const settings::changer::applied_skin* skin, std::uint32_t account_id )
	{
		this->m_pending_hud_iv = 0;

		memory::write<std::uint64_t>( iv + SCHEMA( "C_EconItemView", "m_iItemID"_hash ), 0xf000000000000010ull );
		memory::write<int>( iv + SCHEMA( "C_EconItemView", "m_iEntityQuality"_hash ), skin->stattrak ? 9 : 0 );

		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iItemIDHigh"_hash ), 0xf0000000 );
		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iItemIDLow"_hash ), 0x10 );
		memory::write<std::uint32_t>( iv + SCHEMA( "C_EconItemView", "m_iAccountID"_hash ), account_id );
		memory::write<bool>( iv + SCHEMA( "C_EconItemView", "m_bInitialized"_hash ), true );

		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackPaintKit"_hash ), skin->paint_kit_id );
		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackSeed"_hash ), skin->seed );
		memory::write<float>( weapon + SCHEMA( "C_EconEntity", "m_flFallbackWear"_hash ), skin->wear );
		memory::write<int>( weapon + SCHEMA( "C_EconEntity", "m_nFallbackStatTrak"_hash ), skin->stattrak ? skin->stattrak_count : -1 );

		if ( changer::cosmetic_attributes::available( ) )
		{
			const auto set = PATTERN( patterns::econ_item_view_set_attribute );
			const auto remove = PATTERN( patterns::econ_item_view_remove_attribute );
			if ( skin->stattrak )
			{
				const auto count_val = std::bit_cast<float>( static_cast<std::int32_t>( skin->stattrak_count ) );
				const auto score_type = std::bit_cast<float>( std::int32_t{ 0 } );
				memory::call<void>( set, iv, "kill eater", count_val );
				memory::call<void>( set, iv, "kill eater score type", score_type );
			}
			else
			{
				memory::call<void>( remove, iv, 80 );
				memory::call<void>( remove, iv, 81 );
			}
		}

		const auto pk = g_econ_item_system.find_paint_kit( skin->paint_kit_id );

		this->rebuild_paint( weapon, handle, active_handle, pawn, pk );
		this->schedule_hud_clear( iv );
		return true;
	}

	void guns::rebuild_paint( std::uintptr_t weapon, std::uint32_t handle, std::uint32_t active_handle, std::uintptr_t pawn, const econ_item_system::paint_kit* pk )
	{
		if ( !weapon || weapon < 0x10000 )
		{
			return;
		}

		const auto subclass_ptr = memory::safe_read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_nSubclassID"_hash ) + 0x8 ).value_or( 0 );
		if ( !subclass_ptr )
		{
			return;
		}

		const auto is_legacy = pk && pk->legacy_model;
		const auto mesh_group = is_legacy ? std::uint64_t{ 2 } : std::uint64_t{ 1 };

		if ( handle == active_handle )
		{
			this->update_view_model( pawn, pk );
		}

		const auto weapon_scene_node = memory::safe_read<std::uintptr_t>( weapon + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
		if ( weapon_scene_node && PATTERN( patterns::weapon_set_mesh_group_mask ) )
		{
			memory::call<void>( PATTERN( patterns::weapon_set_mesh_group_mask ), weapon_scene_node, mesh_group );
		}

		if ( PATTERN( patterns::weapon_update_composite_material ) )
		{
			const auto composite_list = memory::safe_read<std::uintptr_t>( weapon + 0x608 ).value_or( 0 );
			if ( composite_list )
			{
				memory::call<void>( PATTERN( patterns::weapon_update_composite_material ), weapon + 0x608, true );
			}
		}

		memory::call_vfunc<void>( weapon, 10, 1 );

		if ( PATTERN( patterns::weapon_update_skin ) )
		{
			memory::call<void>( PATTERN( patterns::weapon_update_skin ), weapon, true );
		}


	}

	void guns::update_view_model( std::uintptr_t pawn, const econ_item_system::paint_kit* pk )
	{
		const auto view_model = this->find_hud_model_weapon( pawn );
		if ( !view_model )
		{
			return;
		}

		const auto view_model_scene_node = memory::safe_read<std::uintptr_t>( view_model + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
		if ( !view_model_scene_node )
		{
			return;
		}

		const auto is_legacy = pk && pk->legacy_model;
		memory::call<void>( PATTERN( patterns::weapon_set_mesh_group_mask ), view_model_scene_node, is_legacy ? std::uint64_t{ 2 } : std::uint64_t{ 1 } );
	}

	std::uintptr_t guns::find_hud_model_weapon( std::uintptr_t pawn )
	{
		const auto arms_handle = memory::safe_read<std::uint32_t>( pawn + SCHEMA( "C_CSPlayerPawn", "m_hHudModelArms"_hash ) ).value_or( 0 );
		if ( !arms_handle )
		{
			return 0;
		}

		const auto arms = systems::g_entities.lookup( arms_handle );
		if ( !arms )
		{
			return 0;
		}

		const auto arms_scene_node = memory::safe_read<std::uintptr_t>( arms + SCHEMA( "C_BaseEntity", "m_pGameSceneNode"_hash ) ).value_or( 0 );
		if ( !arms_scene_node )
		{
			return 0;
		}

		auto child = memory::safe_read<std::uintptr_t>( arms_scene_node + SCHEMA( "CGameSceneNode", "m_pChild"_hash ) ).value_or( 0 );

		while ( child && child > 0x10000 )
		{
			const auto owner = memory::safe_read<std::uintptr_t>( child + SCHEMA( "CGameSceneNode", "m_pOwner"_hash ) ).value_or( 0 );
			if ( owner && owner > 0x10000 )
			{
				const auto name = systems::g_entities.get_schema_name( owner );
				if ( name && fnv1a::runtime_hash( name ) == "C_CS2HudModelWeapon"_hash )
				{
					return owner;
				}
			}

			child = memory::safe_read<std::uintptr_t>( child + SCHEMA( "CGameSceneNode", "m_pNextSibling"_hash ) ).value_or( 0 );
		}

		return 0;
	}

	void guns::clear_hud_icon( std::uintptr_t iv )
	{
		const auto invalidate = PATTERN( patterns::econ_item_view_invalidate_description );
		if ( iv && invalidate )
		{
			memory::call<void>( invalidate, iv );
		}
	}

	void guns::schedule_hud_clear( std::uintptr_t iv )
	{
		this->clear_hud_icon( iv );
		this->m_pending_hud_iv = 0;
	}

	void guns::process_hud_clear( )
	{
		this->m_pending_hud_iv = 0;
	}

	void guns::reset( )
	{
		this->m_applied_weapons.clear( );
		this->m_last_active_handle = 0;
	}

} // namespace features::changer
