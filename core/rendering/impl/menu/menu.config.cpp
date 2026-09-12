#include <pch/pch.hpp>
#include <utilities/math/math.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../../rendering.hpp"

namespace rendering {

	namespace detail {

		std::string name_buf{};
		std::vector<std::wstring> config_list{};
		auto selected{ -1 };
		auto needs_refresh{ true };
		auto confirm_save{ false };
		auto confirm_delete{ false };
		auto confirm_reset{ false };
		auto confirm_timer{ 0.0f };
		auto confirm_reset_timer{ 0.0f };

		auto reset_popup_open{ false };
		auto reset_target_idx{ -1 };
		std::string reset_target_name{};
		float reset_popup_x{ 0.0f };
		float reset_popup_y{ 0.0f };

		static inline void wide_to_utf8( const std::wstring& wide, char* out, int out_size )
		{
			WideCharToMultiByte( CP_UTF8, 0, wide.c_str( ), -1, out, out_size, nullptr, nullptr );
		}

		static inline std::wstring utf8_to_wide( const std::string& utf8 )
		{
			wchar_t buf[ 128 ]{};
			MultiByteToWideChar( CP_UTF8, 0, utf8.c_str( ), -1, buf, 128 );
			return buf;
		}

		static inline bool config_matches_search( const std::wstring& wname )
		{
			if ( detail::name_buf.empty( ) )
			{
				return true;
			}

			const auto wsearch = utf8_to_wide( detail::name_buf );

			// If a config is selected and name_buf is its exact name, keep all configs visible
			if ( detail::selected >= 0 && detail::selected < static_cast< int >( detail::config_list.size( ) ) )
			{
				if ( _wcsicmp( detail::config_list[ detail::selected ].c_str( ), wsearch.c_str( ) ) == 0 )
				{
					return true;
				}
			}

			std::wstring lower_name = wname;
			std::wstring lower_search = wsearch;

			for ( auto& wc : lower_name )
			{
				wc = static_cast< wchar_t >( towlower( wc ) );
			}

			for ( auto& wc : lower_search )
			{
				wc = static_cast< wchar_t >( towlower( wc ) );
			}

			return lower_name.find( lower_search ) != std::wstring::npos;
		}

		static inline std::string selected_name( )
		{
			if ( detail::selected < 0 || detail::selected >= static_cast< int >( detail::config_list.size( ) ) )
			{
				return {};
			}

			char narrow[ 128 ]{};
			wide_to_utf8( detail::config_list[ detail::selected ], narrow, sizeof( narrow ) );
			return narrow;
		}

		static inline bool config_exists( const std::string& name )
		{
			if ( name.empty( ) )
			{
				return false;
			}
			const auto wname = utf8_to_wide( name );
			for ( const auto& cfg : detail::config_list )
			{
				if ( _wcsicmp( cfg.c_str( ), wname.c_str( ) ) == 0 )
				{
					return true;
				}
			}
			return false;
		}

		static inline void reset_defaults( )
		{
			auto& reg = config::detail::get_registry( );

			for ( auto& f : reg.fields )
			{
				char key_str[ 12 ];
				std::snprintf( key_str, sizeof( key_str ), "%08x", f.key );

				auto def = reg.defaults.find( key_str );
				if ( def != reg.defaults.end( ) )
				{
					config::serial::json_to_field( *def, f );
				}
			}

			settings::finalize_binds( );
			settings::g_world.update_active( rendering::g_widgets.s_map_name );
		}

	} // namespace detail

	void menu::draw_config( float group_w )
	{
		( void )group_w;

		if ( detail::needs_refresh )
		{
			detail::config_list = config::registry::list( );
			detail::needs_refresh = false;

			if ( detail::selected >= static_cast< int >( detail::config_list.size( ) ) )
			{
				detail::selected = -1;
			}
		}

		const auto dt = xdraw::delta_time( );

		if ( detail::confirm_save || detail::confirm_delete )
		{
			detail::confirm_timer += dt;

			if ( detail::confirm_timer > 3.5f )
			{
				detail::confirm_save = false;
				detail::confirm_delete = false;
			}
		}

		if ( detail::confirm_reset )
		{
			detail::confirm_reset_timer += dt;

			if ( detail::confirm_reset_timer > 3.5f )
			{
				detail::confirm_reset = false;
			}
		}

		auto& dl = xui::draw::current( );
		const auto& s = xui::ctx( ).style;
		const auto& input = xui::ctx( ).input;

		xui::layout::set_cursor( this->m_body_x - this->m_x, this->m_body_y - this->m_y );

		if ( !xui::begin_child( "##cfg_panel", this->m_body_w, this->m_body_h, false ) )
		{
			return;
		}

		xui::text( "CONFIG PROFILES", tokens::col_accent );
		xui::layout::spacing( 4.0f );
		xui::layout::separator( );
		xui::layout::spacing( 8.0f );

		xui::text_input( "##cfg_name", detail::name_buf, 64, "config name / search..." );

		constexpr auto btn_h{ 28.0f };
		const auto [ avail_w, avail_h ] = xui::layout::avail( );
		const auto list_h = std::max( 80.0f, avail_h - btn_h - s.item_spacing_y );

		constexpr auto popup_w{ 120.0f };
		constexpr auto popup_h{ 28.0f };
		const auto popup_active = detail::reset_popup_open && detail::reset_target_idx >= 0;
		const auto popup_test_rect = xui::rect{ detail::reset_popup_x, detail::reset_popup_y, popup_w, popup_h };
		const auto popup_hovered = popup_active && input.in_rect( popup_test_rect );

		xui::layout::spacing( 6.0f );
		if ( xui::begin_child( "##cfg_list", avail_w, list_h, true, false ) )
		{
			const auto row_w = xui::layout::avail( ).first;
			constexpr auto row_h{ 28.0f };
			auto visible_rows{ 0 };

			for ( auto i = 0; i < static_cast< int >( detail::config_list.size( ) ); ++i )
			{
				const auto& wname = detail::config_list[ i ];

				if ( !detail::config_matches_search( wname ) )
				{
					continue;
				}

				char narrow[ 128 ]{};
				detail::wide_to_utf8( wname, narrow, sizeof( narrow ) );

				const auto row = xui::layout::item( row_w, row_h );
				const auto is_selected = ( detail::selected == i );
				const auto is_hovered = input.in_rect( row );

				if ( is_hovered && !popup_hovered && input.mouse_clicked && !xui::ctx( ).overlay_blocking( ) )
				{
					detail::selected = i;
					detail::name_buf = narrow;
					detail::confirm_save = false;
					detail::confirm_delete = false;
					detail::reset_popup_open = false;
					config::registry::load( wname );
					features::changer::g_guns.reset( );
					features::changer::g_knives.reset( );
					features::changer::g_gloves.reset( );
					features::changer::g_music.reset( );
					settings::finalize_binds( );
					settings::g_world.update_active( rendering::g_widgets.s_map_name );
				}
				else if ( is_hovered && !popup_hovered && input.rmb_clicked && !xui::ctx( ).overlay_blocking( ) )
				{
					detail::selected = i;
					detail::name_buf = narrow;
					detail::reset_target_idx = i;
					detail::reset_target_name = narrow;
					detail::reset_popup_open = true;
					detail::confirm_reset = false;
					detail::confirm_reset_timer = 0.0f;
					detail::reset_popup_x = input.mouse_x;
					detail::reset_popup_y = input.mouse_y;
				}

				const auto hover_anim = xui::anim::lerp( xui::fnv1a( "cfgrow" ) + i, is_hovered ? 1.0f : 0.0f, 14.0f );
				const auto sel_anim = xui::anim::lerp( xui::fnv1a( "cfgsel" ) + i, is_selected ? 1.0f : 0.0f, 10.0f );

				if ( sel_anim > 0.01f )
				{
					dl.rect_filled( row.x, row.y, row.w, row.h, tokens::col_accent.alpha( static_cast< std::uint8_t >( 70.0f * sel_anim ) ), xdraw::corner_radius{ 6.0f } );
				}
				else if ( hover_anim > 0.01f )
				{
					dl.rect_filled( row.x, row.y, row.w, row.h, tokens::col_elevated.alpha( static_cast< std::uint8_t >( 255.0f * hover_anim * 0.5f ) ), xdraw::corner_radius{ 6.0f } );
				}

				const auto [ tw, th ] = xdraw::measure_text( narrow );
				const auto text_col = is_selected
					? xui::lerp( tokens::col_text, tokens::col_accent, sel_anim )
					: xui::lerp( tokens::col_text_dim, tokens::col_text, hover_anim );

				dl.text( row.x + 10.0f, row.y + ( row.h - th ) * 0.5f, narrow, text_col );

				visible_rows++;
			}

			if ( visible_rows == 0 )
			{
				const auto row = xui::layout::item( row_w, row_h );
				dl.text( row.x + 10.0f, row.y + 6.0f, detail::config_list.empty( ) ? "no configs found" : "no matches", tokens::col_text_dim );
			}

			xui::end_child( );
		}

		// 3 buttons: save, delete, refresh
		const auto btn_w = ( avail_w - s.item_spacing_x * 2.0f ) / 3.0f;
		const auto has_selection = detail::selected >= 0 && detail::selected < static_cast< int >( detail::config_list.size( ) );
		const auto save_name = detail::name_buf.empty( ) ? detail::selected_name( ) : detail::name_buf;
		const auto can_save = !save_name.empty( );
		const auto already_exists = detail::config_exists( save_name );

		// Button 1: Save
		std::string save_label;
		if ( detail::confirm_save )
		{
			save_label = already_exists ? "overwrite?" : "confirm save?";
		}
		else
		{
			save_label = "save";
		}

		if ( xui::button( save_label, btn_w, btn_h ) && can_save )
		{
			if ( !detail::confirm_save )
			{
				detail::confirm_save = true;
				detail::confirm_delete = false;
				detail::confirm_timer = 0.0f;
			}
			else
			{
				config::registry::save( detail::utf8_to_wide( save_name ) );
				detail::needs_refresh = true;
				detail::confirm_save = false;
			}
		}

		xui::layout::same_line( );

		// Button 2: Delete
		std::string delete_label;
		if ( detail::confirm_delete )
		{
			delete_label = "confirm delete?";
		}
		else
		{
			delete_label = "delete";
		}

		if ( xui::button( delete_label, btn_w, btn_h ) && has_selection )
		{
			if ( !detail::confirm_delete )
			{
				detail::confirm_delete = true;
				detail::confirm_save = false;
				detail::confirm_timer = 0.0f;
			}
			else
			{
				config::registry::remove( detail::config_list[ detail::selected ] );
				detail::selected = -1;
				detail::name_buf.clear( );
				detail::needs_refresh = true;
				detail::confirm_delete = false;
			}
		}

		xui::layout::same_line( );

		// Button 3: Refresh
		if ( xui::button( "refresh", btn_w, btn_h ) )
		{
			detail::needs_refresh = true;
			detail::confirm_save = false;
			detail::confirm_delete = false;
			detail::reset_popup_open = false;
		}

		// Reset popup (context menu on right-click of config row)
		if ( popup_active && detail::reset_target_idx < static_cast< int >( detail::config_list.size( ) ) )
		{
			const auto px = std::clamp( detail::reset_popup_x, this->m_body_x + 10.0f, this->m_body_x + this->m_body_w - popup_w - 10.0f );
			const auto py = std::clamp( detail::reset_popup_y, this->m_body_y + 10.0f, this->m_body_y + this->m_body_h - popup_h - 10.0f );
			const auto popup_rect = xui::rect{ px, py, popup_w, popup_h };

			auto& top_dl = xdraw::get( xdraw::layer::top );

			const auto clicked_any = input.mouse_clicked || input.rmb_clicked;
			const auto is_hovered = input.in_rect( popup_rect );

			if ( clicked_any && !is_hovered )
			{
				detail::reset_popup_open = false;
				detail::confirm_reset = false;
			}
			else
			{
				// Soft shadow
				top_dl.rect_filled( px + 2.0f, py + 2.0f, popup_w, popup_h, xdraw::color{ 0, 0, 0, 120 }, xdraw::corner_radius{ 6.0f } );
				// Blurred glass
				top_dl.rect_filled_blurred( px, py, popup_w, popup_h, xdraw::corner_radius{ 6.0f } );
				// Card body
				top_dl.rect_filled( px, py, popup_w, popup_h, tokens::col_card.alpha( 245 ), xdraw::corner_radius{ 6.0f } );
				// Border
				const auto is_danger = is_hovered || detail::confirm_reset;
				const auto border_col = is_danger ? xdraw::color{ 255, 75, 85, 200 } : tokens::col_border.alpha( 180 );
				top_dl.rect( px, py, popup_w, popup_h, border_col, xdraw::corner_radius{ 6.0f } );

				// Hover effect
				if ( is_hovered )
				{
					const auto fill_col = detail::confirm_reset ? xdraw::color{ 255, 75, 85, 60 } : xdraw::color{ 255, 75, 85, 45 };
					top_dl.rect_filled( px + 2.0f, py + 2.0f, popup_w - 4.0f, popup_h - 4.0f, fill_col, xdraw::corner_radius{ 4.0f } );
				}

				const auto btn_text = detail::confirm_reset ? "confirm reset?" : "reset config";
				const auto text_col = is_danger ? xdraw::color{ 255, 85, 95 } : tokens::col_text;
				const auto [ tw, th ] = xdraw::measure_text( btn_text );
				top_dl.text( px + ( popup_w - tw ) * 0.5f, py + ( popup_h - th ) * 0.5f, btn_text, text_col );

				if ( is_hovered && input.mouse_clicked )
				{
					if ( !detail::confirm_reset )
					{
						detail::confirm_reset = true;
						detail::confirm_reset_timer = 0.0f;
					}
					else
					{
						detail::reset_defaults( );
						config::registry::save( detail::config_list[ detail::reset_target_idx ] );
						detail::reset_popup_open = false;
						detail::confirm_reset = false;
						detail::needs_refresh = true;
					}
				}
			}
		}

		xui::end_child( );
	}

} // namespace rendering
