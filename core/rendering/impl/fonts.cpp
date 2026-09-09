#include <pch/pch.hpp>
#include <core/resources/fonts/verdana.hpp>
#include "../rendering.hpp"

namespace rendering {

	void fonts::initialize( )
	{
		this->load_family( this->inter_medium, std::as_bytes( std::span{ resources::fonts::verdana::bold } ), { 13.0f, 16.0f, 20.0f } );
		this->load_family( this->inter_bold, std::as_bytes( std::span{ resources::fonts::verdana::bold } ), { 13.0f, 16.0f, 20.0f } );
		this->load_family( this->smallest_pixel7, std::as_bytes( std::span{ resources::fonts::verdana::bold } ), { 10.0f, 11.5f, 14.0f } );
	}

	void fonts::load_family( family_t& family, std::span<const std::byte> data, const std::array<float, static_cast< std::size_t >( size::count )>& sizes )
	{
		for ( auto i = 0ull; i < sizes.size( ); ++i )
		{
			family.sizes[ i ] = xdraw::load_font( data, sizes[ i ] );
		}
	}

} // namespace rendering
