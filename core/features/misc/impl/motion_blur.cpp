#include <pch/pch.hpp>
#include <d3d11.h>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/settings.hpp>
#include <core/systems/systems.hpp>
#include <core/rendering/rendering.hpp>
#include "../misc.hpp"
#include "../motion_blur_shaders.hpp"

namespace features::misc {

	namespace {
		struct alignas(16) motion_blur_cb
		{
			float motion_vector[ 2 ]{ 0.0f, 0.0f };
			float center_protection{ 0.2f };
			float strength{ 1.0f };
			int   sample_count{ 12 };
			float aspect_ratio{ 1.777f };
			float screen_res[ 2 ]{ 1920.0f, 1080.0f };
		};

		static_assert( sizeof( motion_blur_cb ) % 16 == 0, "Constant buffer must be 16-byte aligned" );
	}

	void motion_blur::reset( )
	{
		this->m_has_last_angles = false;
		this->m_last_angles = {};
		this->m_smoothed_velocity = { 0.0f, 0.0f };
		this->m_last_time = {};
	}

	void motion_blur::release_resources( )
	{
		if ( this->m_scene_srv )
		{
			this->m_scene_srv->Release( );
			this->m_scene_srv = nullptr;
		}

		if ( this->m_scene_texture )
		{
			this->m_scene_texture->Release( );
			this->m_scene_texture = nullptr;
		}

		if ( this->m_constant_buffer )
		{
			this->m_constant_buffer->Release( );
			this->m_constant_buffer = nullptr;
		}

		if ( this->m_sampler_state )
		{
			this->m_sampler_state->Release( );
			this->m_sampler_state = nullptr;
		}

		if ( this->m_vertex_shader )
		{
			this->m_vertex_shader->Release( );
			this->m_vertex_shader = nullptr;
		}

		if ( this->m_pixel_shader )
		{
			this->m_pixel_shader->Release( );
			this->m_pixel_shader = nullptr;
		}

		if ( this->m_blend_state )
		{
			this->m_blend_state->Release( );
			this->m_blend_state = nullptr;
		}

		if ( this->m_rasterizer_state )
		{
			this->m_rasterizer_state->Release( );
			this->m_rasterizer_state = nullptr;
		}

		if ( this->m_depth_stencil_state )
		{
			this->m_depth_stencil_state->Release( );
			this->m_depth_stencil_state = nullptr;
		}

		this->m_texture_width = 0;
		this->m_texture_height = 0;
		this->m_texture_format = DXGI_FORMAT_UNKNOWN;
		this->m_initialized = false;
	}

	void motion_blur::on_resize_buffers( )
	{
		if ( this->m_scene_srv )
		{
			this->m_scene_srv->Release( );
			this->m_scene_srv = nullptr;
		}

		if ( this->m_scene_texture )
		{
			this->m_scene_texture->Release( );
			this->m_scene_texture = nullptr;
		}

		this->m_texture_width = 0;
		this->m_texture_height = 0;
		this->m_texture_format = DXGI_FORMAT_UNKNOWN;
	}

	bool motion_blur::ensure_resources( ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format )
	{
		if ( !device || width == 0 || height == 0 )
			return false;

		// Recreate texture and SRV if dimensions or format changed
		if ( !this->m_scene_texture || this->m_texture_width != width || this->m_texture_height != height || this->m_texture_format != format )
		{
			if ( this->m_scene_srv )
			{
				this->m_scene_srv->Release( );
				this->m_scene_srv = nullptr;
			}

			if ( this->m_scene_texture )
			{
				this->m_scene_texture->Release( );
				this->m_scene_texture = nullptr;
			}

			D3D11_TEXTURE2D_DESC tex_desc{};
			tex_desc.Width = width;
			tex_desc.Height = height;
			tex_desc.MipLevels = 1;
			tex_desc.ArraySize = 1;
			tex_desc.Format = format;
			tex_desc.SampleDesc.Count = 1;
			tex_desc.SampleDesc.Quality = 0;
			tex_desc.Usage = D3D11_USAGE_DEFAULT;
			tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			tex_desc.CPUAccessFlags = 0;
			tex_desc.MiscFlags = 0;

			if ( FAILED( device->CreateTexture2D( &tex_desc, nullptr, &this->m_scene_texture ) ) )
				return false;

			// Passing nullptr uses default format and view parameters for the texture
			if ( FAILED( device->CreateShaderResourceView( this->m_scene_texture, nullptr, &this->m_scene_srv ) ) )
			{
				this->m_scene_texture->Release( );
				this->m_scene_texture = nullptr;
				return false;
			}

			this->m_texture_width = width;
			this->m_texture_height = height;
			this->m_texture_format = format;
		}

		if ( this->m_initialized )
			return true;

		// 1. Shaders
		if ( FAILED( device->CreateVertexShader( shaders::g_motion_blur_vs, sizeof( shaders::g_motion_blur_vs ), nullptr, &this->m_vertex_shader ) ) )
			return false;

		if ( FAILED( device->CreatePixelShader( shaders::g_motion_blur_ps, sizeof( shaders::g_motion_blur_ps ), nullptr, &this->m_pixel_shader ) ) )
			return false;

		// 2. Constant buffer
		D3D11_BUFFER_DESC cb_desc{};
		cb_desc.ByteWidth = sizeof( motion_blur_cb );
		cb_desc.Usage = D3D11_USAGE_DYNAMIC;
		cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		if ( FAILED( device->CreateBuffer( &cb_desc, nullptr, &this->m_constant_buffer ) ) )
			return false;

		// 3. Linear clamp sampler
		D3D11_SAMPLER_DESC samp_desc{};
		samp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samp_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samp_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samp_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		samp_desc.MinLOD = 0;
		samp_desc.MaxLOD = D3D11_FLOAT32_MAX;

		if ( FAILED( device->CreateSamplerState( &samp_desc, &this->m_sampler_state ) ) )
			return false;

		// 4. Blend state (opaque overwrite)
		D3D11_BLEND_DESC blend_desc{};
		blend_desc.RenderTarget[ 0 ].BlendEnable = FALSE;
		blend_desc.RenderTarget[ 0 ].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		if ( FAILED( device->CreateBlendState( &blend_desc, &this->m_blend_state ) ) )
			return false;

		// 5. Rasterizer state (cull none, solid)
		D3D11_RASTERIZER_DESC rast_desc{};
		rast_desc.FillMode = D3D11_FILL_SOLID;
		rast_desc.CullMode = D3D11_CULL_NONE;
		rast_desc.DepthClipEnable = FALSE;

		if ( FAILED( device->CreateRasterizerState( &rast_desc, &this->m_rasterizer_state ) ) )
			return false;

		// 6. Depth-stencil state (disabled)
		D3D11_DEPTH_STENCIL_DESC ds_desc{};
		ds_desc.DepthEnable = FALSE;
		ds_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		ds_desc.StencilEnable = FALSE;

		if ( FAILED( device->CreateDepthStencilState( &ds_desc, &this->m_depth_stencil_state ) ) )
			return false;

		this->m_initialized = true;
		return true;
	}

	void motion_blur::on_present( IDXGISwapChain* swap_chain, ID3D11Device* device, ID3D11DeviceContext* context, ID3D11RenderTargetView* rtv, const D3D11_VIEWPORT& viewport )
	{
		if ( !settings::g_misc.m_motion_blur.enabled.value )
		{
			this->reset( );
			return;
		}

		if ( !device || !context || !rtv || !swap_chain )
			return;

		if ( !addresses::globals::csgo_input || !systems::g_local.get( ).is_valid( ) || !systems::g_view.has_camera( ) )
		{
			this->reset( );
			return;
		}

		const auto current_angles = systems::g_input.get_view_angles( );
		if ( !std::isfinite( current_angles.x ) || !std::isfinite( current_angles.y ) )
		{
			this->reset( );
			return;
		}

		const auto now = std::chrono::steady_clock::now();

		if ( !this->m_has_last_angles )
		{
			this->m_last_angles = current_angles;
			this->m_last_time = now;
			this->m_has_last_angles = true;
			this->m_smoothed_velocity = { 0.0f, 0.0f };
			return;
		}

		float dt = std::chrono::duration<float>( now - this->m_last_time ).count();
		this->m_last_time = now;

		if ( dt <= 0.0001f || dt > 0.2f )
		{
			this->m_last_angles = current_angles;
			return;
		}

		// Calculate angle delta
		math::vector3 delta = current_angles - this->m_last_angles;
		this->m_last_angles = current_angles;

		// Normalize angles to [-180, 180]
		math::helpers::normalize_angles( delta );

		// Angular velocity in degrees/sec
		// Screen motion: turning right (negative delta yaw) moves scene left (negative UV x)
		// Looking up (negative delta pitch) moves scene down (positive UV y)
		float target_vx = delta.y / dt;
		float target_vy = -delta.x / dt;

		// Movement blur (optional linear velocity contribution)
		if ( settings::g_misc.m_motion_blur.movement_blur.value )
		{
			const auto pawn = systems::g_local.get( ).pawn;
			if ( pawn )
			{
				const auto vel = memory::safe_read<math::vector3>( pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) ).value_or( math::vector3{} );
				const float speed_z = vel.z;
				// Falling/jumping velocity adds slight vertical blur
				target_vy += speed_z * 0.015f;
			}
		}

		// Exponential smoothing for responsive yet buttery smooth cinematic response and decay
		const float smoothness = std::clamp( settings::g_misc.m_motion_blur.smoothness.value, 1.0f, 30.0f );
		const float alpha = 1.0f - std::exp( -smoothness * dt );
		this->m_smoothed_velocity.x += ( target_vx - this->m_smoothed_velocity.x ) * alpha;
		this->m_smoothed_velocity.y += ( target_vy - this->m_smoothed_velocity.y ) * alpha;

		// Convert smoothed velocity to screen-space UV offset
		constexpr float k_base_scale = 0.00060f;
		const float strength = std::clamp( settings::g_misc.m_motion_blur.strength.value, 0.05f, 5.0f );

		math::vector2 screen_vel{
			this->m_smoothed_velocity.x * k_base_scale,
			this->m_smoothed_velocity.y * k_base_scale
		};

		// Limit maximum blur length to prevent extreme screen streaking on instant flicks
		constexpr float k_max_blur = 0.080f;
		const float vel_len = std::hypot( screen_vel.x, screen_vel.y );
		if ( vel_len > k_max_blur )
		{
			screen_vel.x *= ( k_max_blur / vel_len );
			screen_vel.y *= ( k_max_blur / vel_len );
		}

		// Deadzone: if camera is stationary, skip post-processing pass completely (0% GPU cost)
		if ( vel_len * strength < 0.00005f )
			return;

		// Acquire swap chain back buffer
		ID3D11Texture2D* back_buffer{ nullptr };
		if ( FAILED( swap_chain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &back_buffer ) ) ) )
			return;

		D3D11_TEXTURE2D_DESC bb_desc{};
		back_buffer->GetDesc( &bb_desc );

		if ( !this->ensure_resources( device, bb_desc.Width, bb_desc.Height, bb_desc.Format ) )
		{
			back_buffer->Release( );
			return;
		}

		// Fast DMA copy of current game frame into offscreen staging texture
		context->CopyResource( this->m_scene_texture, back_buffer );
		back_buffer->Release( );

		// Update constant buffer
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if ( SUCCEEDED( context->Map( this->m_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
		{
			auto* cb = static_cast< motion_blur_cb* >( mapped.pData );
			cb->motion_vector[ 0 ] = screen_vel.x;
			cb->motion_vector[ 1 ] = screen_vel.y;
			cb->center_protection = std::clamp( settings::g_misc.m_motion_blur.center_protection.value, 0.0f, 1.0f );
			cb->strength = strength;
			cb->sample_count = std::clamp( settings::g_misc.m_motion_blur.samples.value, 4, 24 );
			cb->aspect_ratio = ( viewport.Height > 0.0f ) ? ( viewport.Width / viewport.Height ) : 1.777f;
			cb->screen_res[ 0 ] = viewport.Width;
			cb->screen_res[ 1 ] = viewport.Height;
			context->Unmap( this->m_constant_buffer, 0 );
		}

		// Setup pipeline for fullscreen post-process pass into backbuffer
		context->RSSetViewports( 1, &viewport );
		context->OMSetRenderTargets( 1, &rtv, nullptr );
		context->OMSetBlendState( this->m_blend_state, nullptr, 0xFFFFFFFF );
		context->OMSetDepthStencilState( this->m_depth_stencil_state, 0 );
		context->RSSetState( this->m_rasterizer_state );

		ID3D11Buffer* null_vb{ nullptr };
		UINT zero{ 0 };
		context->IASetVertexBuffers( 0, 1, &null_vb, &zero, &zero );
		context->IASetInputLayout( nullptr );
		context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		context->VSSetShader( this->m_vertex_shader, nullptr, 0 );
		context->PSSetShader( this->m_pixel_shader, nullptr, 0 );
		context->PSSetConstantBuffers( 0, 1, &this->m_constant_buffer );
		context->PSSetShaderResources( 0, 1, &this->m_scene_srv );
		context->PSSetSamplers( 0, 1, &this->m_sampler_state );

		// Draw fullscreen triangle
		context->Draw( 3, 0 );

		// Unbind shader resource to avoid D3D11 resource hazard warnings
		ID3D11ShaderResourceView* null_srv{ nullptr };
		context->PSSetShaderResources( 0, 1, &null_srv );
	}

} // namespace features::misc
