#include <pch/pch.hpp>
#include <core/systems/systems.hpp>
#include <core/hooks/hooks.hpp>
#include <core/features/features.hpp>

#include "../rendering.hpp"

namespace rendering {

	bool context::try_bind_ui_assets( )
	{
		if ( this->m_ui_assets_ready )
		{
			return true;
		}

		g_fonts.initialize( );

		this->m_ui_assets_ready = true;
		return true;
	}

	bool context::initialize( IDXGISwapChain* swap_chain )
	{
		if ( this->m_initialized )
		{
			return true;
		}

		if ( FAILED( swap_chain->GetDevice( __uuidof( ID3D11Device ), reinterpret_cast< void** >( &this->m_device ) ) ) )
		{
			return false;
		}

		this->m_device->GetImmediateContext( &this->m_context );

		DXGI_SWAP_CHAIN_DESC desc{};
		swap_chain->GetDesc( &desc );

		this->m_window = desc.OutputWindow;

		this->create_rtv( swap_chain );
		this->setup_zdraw( this->m_window );

		g_menu.initialize_graphics( );
		this->try_bind_ui_assets( );

		this->m_initialized = true;
		return true;
	}

	void context::shutdown( )
	{
		if ( this->m_rtv )
		{
			this->m_rtv->Release( );
			this->m_rtv = nullptr;
		}

		if ( this->m_context )
		{
			this->m_context->Release( );
			this->m_context = nullptr;
		}

		if ( this->m_device )
		{
			this->m_device->Release( );
			this->m_device = nullptr;
		}

		this->m_window = nullptr;
		this->m_initialized = false;
	}

	void context::on_present( IDXGISwapChain* swap_chain )
	{
		if ( !this->m_initialized ) [[unlikely]]
		{
			if ( !this->initialize( swap_chain ) ) [[unlikely]]
			{
				return;
			}
		}

		this->try_bind_ui_assets( );
		features::misc::g_dlight.on_present( );

		if ( !this->m_rtv || this->m_viewport.Width <= 0.0f || this->m_viewport.Height <= 0.0f )
			return;

		features::misc::g_motion_blur.on_present( swap_chain, this->m_device, this->m_context, this->m_rtv, this->m_viewport );

		struct viewport_guard
		{
			ID3D11DeviceContext* context;
			UINT count{ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE };
			D3D11_VIEWPORT saved[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			explicit viewport_guard( ID3D11DeviceContext* value ) : context{ value }
			{
				context->RSGetViewports( &count, saved );
			}
			~viewport_guard( )
			{
				context->RSSetViewports( count, count ? saved : nullptr );
			}
		} restore_viewports{ this->m_context };

		this->m_context->RSSetViewports( 1, &this->m_viewport );
		this->m_context->OMSetRenderTargets( 1, &this->m_rtv, nullptr );

		xdraw::begin_frame( true );
		g_menu.update_ui_state( );
		{
			auto& dl = xdraw::get( xdraw::layer::bottom );

			if ( this->m_ui_assets_ready && !hooks::cheat::is_level_shutting_down( ) && systems::g_local.get( ).is_valid( ) && systems::g_view.has_camera( ) )
			{
				features::misc::g_impacts.on_render_early( dl );
				features::combat::g_misc.antiaim( ).on_render( dl );
				features::movement::g_edgebug.on_render( dl );
				features::esp::item::g_overlay.on_render( dl );
				features::esp::projectile::g_overlay.on_render( dl, xdraw::get( xdraw::layer::middle ) );
				features::esp::player::g_overlay.on_render( dl );
				features::misc::g_projectile_trajectory.on_render( dl );
				features::combat::g_rage.on_render( dl );
				features::combat::g_legit.on_render( dl );
				features::misc::g_impacts.on_render( dl );
				features::misc::g_hud.on_render( dl );
				features::esp::other::g_overlay.on_render( dl );
			}

			g_menu.draw( );

			// Draw after menu input and popups so toggles take effect in this frame.
			if ( this->m_ui_assets_ready )
			{
				g_widgets.draw( );
			}
		}
		xdraw::end_frame( );
	}

	void context::on_resize_buffers( )
	{
		features::misc::g_motion_blur.on_resize_buffers( );

		if ( this->m_rtv )
		{
			this->m_rtv->Release( );
			this->m_rtv = nullptr;
		}
	}

	void context::on_resize_buffers_post( IDXGISwapChain* swap_chain )
	{
		this->create_rtv( swap_chain );
	}

	void context::create_rtv( IDXGISwapChain* swap_chain )
	{
		ID3D11Texture2D* back_buffer{ nullptr };
		if ( SUCCEEDED( swap_chain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &back_buffer ) ) ) )
		{
			this->m_device->CreateRenderTargetView( back_buffer, nullptr, &this->m_rtv );

			D3D11_TEXTURE2D_DESC back_buffer_desc{};
			back_buffer->GetDesc( &back_buffer_desc );

			this->m_viewport = {};
			this->m_viewport.Width = static_cast<float>( back_buffer_desc.Width );
			this->m_viewport.Height = static_cast<float>( back_buffer_desc.Height );
			this->m_viewport.MaxDepth = 1.0f;

			back_buffer->Release( );
		}
	}

	void context::setup_zdraw( HWND window )
	{
		if ( !xdraw::initialize( this->m_device, this->m_context ) )
		{
			return;
		}

		xui::initialize( window );
	}

} // namespace rendering
