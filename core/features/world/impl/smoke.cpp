#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>

#include "../world.hpp"

// was a deadend.

namespace features::world {

	void smoke::on_map( std::uintptr_t token, std::size_t size, std::uintptr_t buf_ptr )
	{
		if ( size != 2480 || !token || !buf_ptr )
		{
			return;
		}

		this->m_token = token;
		this->m_buf = buf_ptr;
	}

	void smoke::on_unmap( std::uintptr_t token )
	{
		const auto buf = this->m_buf;
		if ( !buf || token != this->m_token )
		{
			return;
		}

		if ( settings::g_misc.m_removals.smoke.value )
		{
			const auto count = *reinterpret_cast< std::uint32_t* >( buf + 2464 );
			if ( count > 0 && count <= 16 )
			{
				for ( auto i = 0u; i < count; ++i )
				{
					auto opacity = reinterpret_cast< float* >( buf + 1280 + 16 * static_cast< std::size_t >( i ) );
					opacity[ 0 ] = 0.0f;
				}
			}
		}

		this->m_buf = 0;
		this->m_token = 0;
	}

	void smoke::on_frame_stage_notify( )
	{
		if ( !settings::g_misc.m_smoke_and_fire_color.custom_smoke.value )
		{
			return;
		}

		static auto smoke_col_offset = SCHEMA( "C_SmokeGrenadeProjectile", "m_vSmokeColor"_hash );
		if ( !smoke_col_offset )
		{
			smoke_col_offset = SCHEMA( "C_SmokeGrenadeProjectile", "m_vSmokeColor"_hash );
			if ( !smoke_col_offset )
			{
				const auto det_offset = SCHEMA( "C_SmokeGrenadeProjectile", "m_vSmokeDetonationPos"_hash );
				if ( det_offset >= 12 )
				{
					smoke_col_offset = det_offset - 12;
				}
			}
		}

		if ( !smoke_col_offset )
		{
			return;
		}

		const auto& col = settings::g_misc.m_smoke_and_fire_color.smoke_color.value;

		for ( const auto& p : systems::g_entities.get_by_type( systems::entities::type::projectile ) )
		{
			if ( p.schema_hash != "C_SmokeGrenadeProjectile"_hash || !p.ptr )
			{
				continue;
			}

			const auto cur = memory::read<math::vector3>( p.ptr + smoke_col_offset );
			math::vector3 target_col;
			if ( cur.x > 1.5f || cur.y > 1.5f || cur.z > 1.5f )
			{
				target_col = math::vector3{ static_cast<float>( col.r ), static_cast<float>( col.g ), static_cast<float>( col.b ) };
			}
			else
			{
				target_col = math::vector3{ static_cast<float>( col.r ) / 255.0f, static_cast<float>( col.g ) / 255.0f, static_cast<float>( col.b ) / 255.0f };
			}

			memory::write<math::vector3>( p.ptr + smoke_col_offset, target_col );
		}
	}

} // namespace features::world