#include <pch/pch.hpp>
#include "../security.hpp"

namespace security::regions {

	namespace detail {

		std::vector<region> regions{};

	} // namespace detail

	void add( std::uintptr_t base, std::size_t size )
	{
		detail::regions.push_back( { base, size } );
	}

	void add_module( HMODULE module )
	{
		if ( !module )
			return;

		const auto base = reinterpret_cast< std::uintptr_t >( module );
		std::size_t image_size = 0;

		__try
		{
			const auto dos = reinterpret_cast< const IMAGE_DOS_HEADER* >( base );
			if ( dos->e_magic == IMAGE_DOS_SIGNATURE )
			{
				const auto nt = reinterpret_cast< const IMAGE_NT_HEADERS64* >( base + dos->e_lfanew );
				if ( nt->Signature == IMAGE_NT_SIGNATURE )
				{
					image_size = nt->OptionalHeader.SizeOfImage;
				}
			}
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
			image_size = 0;
		}

		if ( image_size == 0 )
		{
			std::size_t total_size = 0;
			std::uintptr_t current = base;
			MEMORY_BASIC_INFORMATION mbi{};
			while ( VirtualQuery( reinterpret_cast< const void* >( current ), &mbi, sizeof( mbi ) ) )
			{
				if ( reinterpret_cast< std::uintptr_t >( mbi.AllocationBase ) != base || mbi.State == MEM_FREE )
					break;

				total_size += mbi.RegionSize;
				current += mbi.RegionSize;
			}
			image_size = total_size ? total_size : 0x2000000;
		}

		add( base, image_size );
	}

	bool is_protected( const void* address, std::size_t size )
	{
		const auto addr = reinterpret_cast< std::uintptr_t >( address );
		const auto end = addr + size;

		for ( const auto& region : detail::regions )
		{
			const auto region_end = region.base + region.size;

			if ( addr < region_end && end > region.base )
			{
				return true;
			}
		}

		return false;
	}

} // namespace security::regions