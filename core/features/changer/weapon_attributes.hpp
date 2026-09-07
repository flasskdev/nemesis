#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <protection/game_addresses.hpp>

namespace features::changer::weapon_attributes {

	inline constexpr std::array<std::uint16_t, 2> stattrak_indices{ 80, 81 };
	inline constexpr std::array<const char*, 2> stattrak_names
	{
		"kill eater",
		"kill eater score type"
	};

	struct stattrak_state
	{
		std::array<std::uint32_t, 2> bits{};
		std::array<bool, 2> present{};
	};

	[[nodiscard]] inline bool capture( std::uintptr_t item_view, stattrak_state& state )
	{
		state = {};
		const auto list_offset = SCHEMA( "C_EconItemView", "m_AttributeList"_hash );
		const auto vector_offset = SCHEMA( "CAttributeList", "m_Attributes"_hash );
		const auto definition_offset = SCHEMA( "CEconItemAttribute", "m_iAttributeDefinitionIndex"_hash );
		const auto value_offset = SCHEMA( "CEconItemAttribute", "m_flValue"_hash );
		if ( !item_view || !list_offset || !vector_offset || !definition_offset || !value_offset )
		{
			return false;
		}

		// Same CUtlVector / CEconItemAttribute layout used by gloves.cpp.
		constexpr std::uintptr_t attribute_stride{ 0x48 };
		const auto vector = item_view + list_offset + vector_offset;
		const auto count = memory::safe_read<int>( vector );
		const auto data = memory::safe_read<std::uintptr_t>( vector + 0x8 );
		if ( !count || !data || *count < 0 || *count > 16384 || ( *count && !*data ) )
		{
			return false;
		}

		for ( auto i = 0; i < *count; ++i )
		{
			const auto attribute = *data + static_cast<std::uintptr_t>( i ) * attribute_stride;
			const auto definition = memory::safe_read<std::uint16_t>( attribute + definition_offset );
			if ( !definition )
			{
				return false;
			}
			for ( std::size_t slot = 0; slot < stattrak_indices.size( ); ++slot )
			{
				if ( *definition != stattrak_indices[ slot ] )
				{
					continue;
				}
				const auto value = memory::safe_read<std::uint32_t>( attribute + value_offset );
				if ( !value )
				{
					return false;
				}
				state.bits[ slot ] = *value;
				state.present[ slot ] = true;
			}
		}
		return true;
	}

	[[nodiscard]] inline bool restore( std::uintptr_t item_view, const stattrak_state& state )
	{
		const auto set_attribute = PATTERN( patterns::econ_item_view_set_attribute );
		const auto remove_attribute = PATTERN( patterns::econ_item_view_remove_attribute );
		if ( !item_view || !set_attribute || !remove_attribute )
		{
			return false;
		}
		for ( std::size_t slot = 0; slot < stattrak_indices.size( ); ++slot )
		{
			if ( state.present[ slot ] )
			{
				memory::call<void>( set_attribute, item_view, stattrak_names[ slot ], std::bit_cast<float>( state.bits[ slot ] ) );
			}
			else
			{
				memory::call<void>( remove_attribute, item_view, static_cast<int>( stattrak_indices[ slot ] ) );
			}
		}
		return true;
	}

	[[nodiscard]] inline bool apply( std::uintptr_t item_view, bool enabled )
	{
		stattrak_state desired{};
		if ( enabled )
		{
			desired.bits = { static_cast<std::uint32_t>( settings::changer::stattrak_max ), 0u };
			desired.present = { true, true };
		}
		if ( !restore( item_view, desired ) )
		{
			return false;
		}
		stattrak_state actual{};
		return capture( item_view, actual ) && actual.present == desired.present && actual.bits == desired.bits;
	}

} // namespace features::changer::weapon_attributes
