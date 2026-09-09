#include <pch/pch.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../../rendering.hpp"
#include "../../map_images.hpp"

namespace rendering {

	namespace {
		static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> s_map_srvs[ 11 ]{};
		static bool s_map_srvs_initialized = false;

		void ensure_map_textures( )
		{
			if ( s_map_srvs_initialized )
				return;

			for ( std::size_t i = 0; i < rendering::map_assets::k_all_map_images.size( ); ++i )
			{
				s_map_srvs[ i ] = xdraw::load_texture( rendering::map_assets::k_all_map_images[ i ] );
			}

			s_map_srvs_initialized = true;
		}
	}

	void menu::draw_world( float group_w, int subtab ) const
	{
		auto& w = settings::g_world;
		auto& item = settings::g_esp.m_item;
		auto& proj = settings::g_esp.m_projectile;
		auto& other = settings::g_esp.m_other;

		static int s_selected_map = 0;
		static float s_card_hover_anim[ 11 ]{};
		static std::string s_last_live_map{};

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + tokens::sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto content_w = this->m_w - tokens::gap * 2.0f - tokens::sidebar_w - tokens::gap;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		subtab = std::clamp( subtab, 0, 2 );

		constexpr const char* display_types[ ]{ "text", "icon", "text + icon" };
		constexpr const char* cham_material_names[ ]{
			"liquid", "metallic", "matte", "flat", "bloom", "outlines", "glow", "electric", "distortion", "hologram", "pearl",
			"liquid (iz)", "matte (iz)", "flat (iz)", "bloom (iz)", "outlines (iz)", "glow (iz)", "distortion (iz)", "hologram (iz)",
			"outline glow", "outline glow (iz)"
		};
		constexpr auto cham_material_count = static_cast< int >( settings::esp::cham_ids::count );

		auto draw_chams_layer = [ & ]( const char* label, const char* popup_id, settings::esp::chams_layer& layer )
		{
			xui::checkbox( label, layer.enabled );
			if ( xui::begin_popup( popup_id, 220.0f ) )
			{
				const bool is_outline = settings::esp::is_outline_material( layer.material.value );
				const bool is_glow_outline = ( layer.material.value == settings::esp::cham_ids::outline_glow ||
				                               layer.material.value == settings::esp::cham_ids::outline_glow_ignorez );

				const auto prev_mat = layer.material.value;
				if ( xui::combo( "material", layer.material.value, cham_material_names, cham_material_count ) )
				{
					if ( ( layer.material.value == settings::esp::cham_ids::outline_glow || layer.material.value == settings::esp::cham_ids::outline_glow_ignorez ) &&
					     ( prev_mat != settings::esp::cham_ids::outline_glow && prev_mat != settings::esp::cham_ids::outline_glow_ignorez ) )
					{
						layer.filled.value = true;
					}
				}
				xui::color_picker( "color", layer.color, 0.0f, true, is_outline ? &layer.filled.value : nullptr );
				if ( is_outline )
				{
					xui::checkbox( "filled", layer.filled );
				}
				if ( is_glow_outline )
				{
					xui::layout::spacing( 5.0f );
					xui::layout::separator( );
					auto& cfg = layer.glow;
					char buf[ 64 ]{};
					xui::text( "glow settings", tokens::col_accent );

					std::snprintf( buf, sizeof( buf ), "intensity##%s", popup_id );
					xui::slider_float( buf, cfg.intensity, 1.0f, 50.0f, "%.1f" );

					std::snprintf( buf, sizeof( buf ), "thickness##%s", popup_id );
					xui::slider_float( buf, cfg.thickness, 0.5f, 10.0f, "%.1f" );

					std::snprintf( buf, sizeof( buf ), "softness##%s", popup_id );
					xui::slider_float( buf, cfg.softness, 0.2f, 4.0f, "%.2f" );

					std::snprintf( buf, sizeof( buf ), "opacity##%s", popup_id );
					xui::slider_float( buf, cfg.opacity, 0.0f, 1.0f, "%.2f" );

					std::snprintf( buf, sizeof( buf ), "inner spread##%s", popup_id );
					xui::slider_float( buf, cfg.inner_spread, 0.0f, 1.0f, "%.2f" );

					std::snprintf( buf, sizeof( buf ), "pulse speed##%s", popup_id );
					xui::slider_float( buf, cfg.pulse_speed, 0.0f, 5.0f, "%.1f" );
				}
				xui::end_popup( );
			}
		};

		// SUBTAB 0: ITEMS & PROJECTILES & BOMB
		if ( subtab == 0 )
		{
			// Left Column: Items ESP
			xui::layout::set_cursor( content_x - wx, body_y - wy );
			if ( xui::begin_child( "##esp_items", col_w, this->m_body_h, true ) )
			{
				static int item_group{};
				xui::combo( "group##item_sel", item_group, settings::esp::item::k_group_names, settings::esp::item::k_group_count );

				xui::layout::separator( );

				xui::toggle( "item esp", item.m_overlay.group_toggle( item_group ) );
				if ( xui::begin_popup( "##ie_grp_cfg", 220.0f ) )
				{
					auto& g = item.m_overlay.groups[ item_group ];

					char id_d[ 32 ]{}, id_m[ 32 ]{}, id_t[ 32 ]{}, id_i[ 32 ]{};
					std::snprintf( id_d, sizeof( id_d ), "display##ie%d", item_group );
					std::snprintf( id_m, sizeof( id_m ), "max dist##ie%d", item_group );
					std::snprintf( id_t, sizeof( id_t ), "text color##ie%d", item_group );
					std::snprintf( id_i, sizeof( id_i ), "icon color##ie%d", item_group );

					xui::combo( id_d, g.display.value, display_types, 3 );
					xui::slider_float( id_m, g.max_distance, 1.0f, 200.0f, "%.0fm" );
					xui::color_picker( id_t, g.text_color );
					xui::color_picker( id_i, g.icon_color );
					xui::end_popup( );
				}

				xui::toggle( "item chams", item.m_chams.group_toggle( item_group ) );
				if ( xui::begin_popup( "##ic_grp_cfg", 220.0f ) )
				{
					auto& g = item.m_chams.groups[ item_group ];

					char id_p[ 48 ]{}, id_pp[ 48 ]{}, id_s[ 48 ]{}, id_sp[ 48 ]{};
					std::snprintf( id_p, sizeof( id_p ), "primary##ic%d", item_group );
					std::snprintf( id_pp, sizeof( id_pp ), "##ic_p%d", item_group );
					std::snprintf( id_s, sizeof( id_s ), "secondary##ic%d", item_group );
					std::snprintf( id_sp, sizeof( id_sp ), "##ic_s%d", item_group );

					draw_chams_layer( id_p, id_pp, g.primary );
					draw_chams_layer( id_s, id_sp, g.secondary );
					xui::end_popup( );
				}

				xui::toggle( "item glow", item.m_glow.group_toggle( item_group ) );
				if ( xui::begin_popup( "##ig_grp_cfg", 220.0f ) )
				{
					char id[ 32 ]{};
					std::snprintf( id, sizeof( id ), "color##ig%d", item_group );
					xui::color_picker( id, item.m_glow.groups[ item_group ].color );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			// Right Column: Projectiles & Bomb Timer
			xui::layout::set_cursor( right_x - wx, body_y - wy );
			if ( xui::begin_child( "##esp_projectiles", col_w, this->m_body_h, true ) )
			{
				static auto proj_group{ 0 };
				xui::combo( "group##proj_sel", proj_group, settings::esp::projectile::k_group_names, settings::esp::projectile::k_group_count );

				xui::layout::separator( );

				const auto is_inferno = ( proj_group == 5 );

				xui::toggle( is_inferno ? "inferno esp" : "projectile esp", proj.m_overlay.group_toggle( proj_group ) );

				if ( !is_inferno )
				{
					if ( xui::begin_popup( "##pe_grp_cfg", 220.0f ) )
					{
						auto& g = proj.m_overlay.groups[ proj_group ];

						char id_d[ 32 ]{}, id_m[ 32 ]{}, id_t[ 32 ]{}, id_i[ 32 ]{};
						std::snprintf( id_d, sizeof( id_d ), "display##pe%d", proj_group );
						std::snprintf( id_m, sizeof( id_m ), "max dist##pe%d", proj_group );
						std::snprintf( id_t, sizeof( id_t ), "text color##pe%d", proj_group );
						std::snprintf( id_i, sizeof( id_i ), "icon color##pe%d", proj_group );

						xui::combo( id_d, g.display.value, display_types, 3 );
						xui::slider_float( id_m, g.max_distance, 1.0f, 200.0f, "%.0fm" );
						xui::color_picker( id_t, g.text_color );
						xui::color_picker( id_i, g.icon_color );
						xui::end_popup( );
					}
				}
				else
				{
					if ( xui::begin_popup( "##inferno_cfg", 220.0f ) )
					{
						xui::color_picker( "fill color##inf", proj.m_overlay.m_infernos.fill_color );
						xui::color_picker( "outline color##inf", proj.m_overlay.m_infernos.outline_color );
						xui::slider_float( "outline thickness##inf", proj.m_overlay.m_infernos.outline_thickness, 0.5f, 5.0f, "%.1f" );
						xui::checkbox( "glow##inf", proj.m_overlay.m_infernos.glow );
						xui::slider_float( "glow strength##inf", proj.m_overlay.m_infernos.glow_strength, 0.1f, 1.0f, "%.2f" );
						xui::end_popup( );
					}
				}

				const auto indicator_id = proj_group == 0 ? 0 : proj_group == 3 ? 1 : proj_group == 5 ? 2 : -1;
				if ( indicator_id >= 0 )
				{
					xui::toggle( is_inferno ? "indicator" : "landing indicator", proj.m_overlay.m_indicator.get_group( indicator_id ).enabled );
					if ( xui::begin_popup( "##ind_grp_cfg", 220.0f ) )
					{
						auto& g = proj.m_overlay.m_indicator.get_group( indicator_id );

						char id_a[ 32 ]{}, id_i[ 32 ]{}, id_b[ 32 ]{}, id_g[ 32 ]{}, id_gs[ 32 ]{};
						std::snprintf( id_a, sizeof( id_a ), "arc color##ind%d", indicator_id );
						std::snprintf( id_i, sizeof( id_i ), "icon color##ind%d", indicator_id );
						std::snprintf( id_b, sizeof( id_b ), "background##ind%d", indicator_id );
						std::snprintf( id_g, sizeof( id_g ), "glow##ind%d", indicator_id );
						std::snprintf( id_gs, sizeof( id_gs ), "glow strength##ind%d", indicator_id );

						xui::color_picker( id_a, g.arc_color );
						xui::color_picker( id_i, g.icon_color );
						xui::color_picker( id_b, g.background_color );
						xui::checkbox( id_g, g.glow );
						xui::slider_float( id_gs, g.glow_strength, 0.1f, 1.0f, "%.2f" );
						xui::end_popup( );
					}
				}

				xui::layout::spacing( 4.0f );
				xui::layout::separator( );
				xui::layout::spacing( 4.0f );

				xui::toggle( "bomb timer", other.bomb_timer );

				xui::end_child( );
			}
		}

		// SUBTABS 1 & 2: NEVERLOSE-STYLE PER-MAP WORLD VISUALS & EFFECTS
		if ( subtab == 1 || subtab == 2 )
		{
			ensure_map_textures( );

			constexpr int k_num_maps = static_cast<int>( settings::world::map_max );
			const auto body_h = this->m_body_h;

			// Exact 2-column layout matching the reference screenshot:
			// Left column: MAP SELECTION (~48%)
			// Right column: EFFECTS (~52%)
			constexpr float col_gap = 16.0f;
			const float left_col_w = std::floorf( ( content_w - col_gap ) * 0.48f );
			const float right_col_w = content_w - left_col_w - col_gap;
			const float right_col_x = content_x + left_col_w + col_gap;

			// Detect current live map in CS2
			int live_map_idx = -1;
			if ( !rendering::g_widgets.s_map_name.empty( ) )
			{
				std::string cur = rendering::g_widgets.s_map_name;
				for ( char& c : cur ) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

				for ( int i = 1; i < k_num_maps; ++i )
				{
					const char* id = settings::world::k_map_entries[ i ].id_name;
					std::string id_str = id;
					for ( char& c : id_str ) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

					if ( cur.find( id_str ) != std::string::npos ||
					     ( id_str.size( ) > 3 && cur.find( id_str.substr( 3 ) ) != std::string::npos ) )
					{
						live_map_idx = i;
						break;
					}
				}
			}

			// Automatically select live map when entering a CS2 match
			if ( !rendering::g_widgets.s_map_name.empty( ) && rendering::g_widgets.s_map_name != s_last_live_map )
			{
				s_last_live_map = rendering::g_widgets.s_map_name;
				if ( live_map_idx > 0 )
				{
					s_selected_map = live_map_idx;
					if ( !w.presets[ live_map_idx ]->override_map.value )
					{
						settings::world::copy_scene( w.presets[ live_map_idx ]->m_scene, w.m_global.m_scene );
						settings::world::copy_weather( w.presets[ live_map_idx ]->m_weather, w.m_global.m_weather );
						w.presets[ live_map_idx ]->override_map.value = true;
					}
				}
			}
			else if ( rendering::g_widgets.s_map_name.empty( ) )
			{
				s_last_live_map.clear( );
			}

			// Screenshot map order: Global, Mirage, Nuke, Dust II, Inferno, Overpass, Vertigo, Office, Ancient, Anubis, Italy
			constexpr int k_map_display_order[ 11 ] = {
				settings::world::map_global,
				settings::world::map_mirage,
				settings::world::map_nuke,
				settings::world::map_dust2,
				settings::world::map_inferno,
				settings::world::map_overpass,
				settings::world::map_vertigo,
				settings::world::map_office,
				settings::world::map_ancient,
				settings::world::map_anubis,
				settings::world::map_italy
			};

			constexpr float k_header_h = 22.0f;
			auto draw_col_title = [&]( float x, const char* title ) {
				auto& dl = xui::draw::current( );
				xdraw::push_font( rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );
				dl.text( x + 2.0f, body_y + 2.0f, title, tokens::col_text );
				xdraw::pop_font( );
			};

			// ==========================================
			// 1. LEFT COLUMN: MAP SELECTION
			// ==========================================
			draw_col_title( content_x, "MAP SELECTION" );
			xui::layout::set_cursor( content_x - wx, body_y + k_header_h - wy );
			if ( xui::begin_child( "##map_selection_child", left_col_w, body_h - k_header_h, true ) )
			{
				const auto& input = xui::ctx( ).input;
				const auto dt = xdraw::delta_time( );
				const bool can_interact = !xui::ctx( ).overlay_blocking( );
				auto& dl = xui::draw::current( );

				const auto time_ms = static_cast<float>( GetTickCount64( ) % 100000 );
				const float live_pulse = 0.65f + 0.35f * std::sin( time_ms * 0.005f );

				const auto [avail_w, avail_h] = xui::layout::avail( );
				const float card_w = avail_w;
				constexpr float card_h = 38.0f;

				const auto child_win = xui::layout::current_window( );
				const auto cb = child_win ? child_win->bounds : xui::rect{};

				for ( int order_i = 0; order_i < k_num_maps; ++order_i )
				{
					const int map_idx = k_map_display_order[ order_i ];
					const auto& entry = settings::world::k_map_entries[ map_idx ];
					const auto card_rect = xui::layout::item( card_w, card_h );

					const bool is_in_view = ( card_rect.y + card_rect.h >= cb.y && card_rect.y <= cb.y + cb.h );
					const bool is_sel = ( s_selected_map == map_idx );
					const bool is_hover = can_interact && is_in_view && input.in_rect( card_rect );
					const bool is_live = ( live_map_idx == map_idx );

					// Handle card selection
					if ( is_hover && input.mouse_clicked )
					{
						s_selected_map = map_idx;
						if ( map_idx > 0 && !w.presets[ map_idx ]->override_map.value )
						{
							settings::world::copy_scene( w.presets[ map_idx ]->m_scene, w.m_global.m_scene );
							settings::world::copy_weather( w.presets[ map_idx ]->m_weather, w.m_global.m_weather );
							w.presets[ map_idx ]->override_map.value = true;
						}
					}

					if ( !is_in_view )
						continue;

					const float target_hover = is_sel ? 1.0f : ( is_hover ? 0.7f : 0.0f );
					s_card_hover_anim[ map_idx ] += ( target_hover - s_card_hover_anim[ map_idx ] ) * std::min( 16.0f * dt, 1.0f );

					// 1. Base dark card fill
					const auto base_bg = is_sel
						? xdraw::color{ 20, 24, 30, 235 }
						: ( is_hover ? xdraw::color{ 22, 26, 33, 200 } : xdraw::color{ 15, 18, 23, 175 } );
					dl.rect_filled( card_rect.x, card_rect.y, card_rect.w, card_rect.h, base_bg, xdraw::corner_radius{ 6.0f } );

					// 2. Map photo preview on the right portion with smooth horizontal fade
					if ( s_map_srvs[ map_idx ].Get( ) != nullptr )
					{
						const float img_w = card_rect.w * 0.62f;
						const float img_x = card_rect.x + card_rect.w - img_w;

						dl.push_clip( card_rect.x, card_rect.y, card_rect.w, card_rect.h );

						const auto img_tint = is_sel
							? xdraw::color{ 220, 225, 235, 175 }
							: ( is_hover ? xdraw::color{ 200, 210, 220, 140 } : xdraw::color{ 150, 160, 170, 105 } );
						dl.image( img_x, card_rect.y, img_w, card_rect.h, s_map_srvs[ map_idx ].Get( ), xdraw::corner_radius{ 0.0f, 6.0f, 6.0f, 0.0f }, img_tint );

						// Smooth gradient fade from solid dark card base to transparent
						const auto fade_col_left = base_bg;
						const auto fade_col_right = xdraw::color{ base_bg.r, base_bg.g, base_bg.b, 40 };
						dl.rect_filled_gradient( img_x, card_rect.y, img_w, card_rect.h, fade_col_left, fade_col_right, fade_col_right, fade_col_left );

						dl.pop_clip( );
					}

					// 3. Display Name (left aligned)
					const char* name = ( map_idx == 0 ? "Global" : entry.display_name );
					xdraw::push_font( rendering::g_fonts.inter_medium[ rendering::fonts::size::normal ] );
					const auto [tw, th] = xdraw::measure_text( name );
					const auto text_col = is_sel
						? xdraw::color{ 245, 250, 255, 255 }
						: ( is_hover ? xdraw::color{ 220, 228, 238, 240 } : xdraw::color{ 160, 172, 185, 215 } );
					dl.text( card_rect.x + 12.0f, card_rect.y + ( card_rect.h - th ) * 0.5f, name, text_col );
					xdraw::pop_font( );

					// 4. Live map indicator (pulsing green dot if active match)
					if ( is_live )
					{
						const auto dot_alpha = static_cast<std::uint8_t>( 255 * live_pulse );
						dl.circle_filled( card_rect.x + 12.0f + tw + 8.0f, card_rect.y + card_rect.h * 0.5f, 3.0f, xdraw::color{ 46, 213, 115, dot_alpha } );
					}

					// 5. Three dots (...) on the far right
					const float dots_cx = card_rect.x + card_rect.w - 18.0f;
					const float dots_cy = card_rect.y + card_rect.h * 0.5f;
					const auto dots_col = is_sel
						? xdraw::color{ 200, 212, 225, 220 }
						: ( is_hover ? xdraw::color{ 170, 182, 195, 185 } : xdraw::color{ 115, 128, 142, 140 } );
					for ( int d = -1; d <= 1; ++d )
					{
						dl.circle_filled( dots_cx + d * 4.0f, dots_cy, 1.2f, dots_col, 8 );
					}

					// 6. Card border outline
					if ( is_sel )
					{
						// Crisp light outline exactly as in the reference screenshot
						dl.rect( card_rect.x, card_rect.y, card_rect.w, card_rect.h, xdraw::color{ 125, 140, 165, 210 }, xdraw::corner_radius{ 6.0f }, 1.0f );
					}
					else if ( is_hover )
					{
						dl.rect( card_rect.x, card_rect.y, card_rect.w, card_rect.h, xdraw::color{ 255, 255, 255, 30 }, xdraw::corner_radius{ 6.0f }, 1.0f );
					}
					else
					{
						dl.rect( card_rect.x, card_rect.y, card_rect.w, card_rect.h, xdraw::color{ 255, 255, 255, 8 }, xdraw::corner_radius{ 6.0f }, 1.0f );
					}

					xui::layout::spacing( 2.0f );
				}

				xui::end_child( );
			}

			// Active preset to edit
			s_selected_map = std::clamp( s_selected_map, 0, k_num_maps - 1 );
			auto* editing = w.presets[ s_selected_map ];
			auto& scene = editing->m_scene;
			auto& weather = editing->m_weather;

			// ==========================================
			// 2. RIGHT COLUMN: EFFECTS
			// ==========================================
			draw_col_title( right_col_x, "EFFECTS" );
			xui::layout::set_cursor( right_col_x - wx, body_y + k_header_h - wy );
			if ( xui::begin_child( "##effects_child", right_col_w, body_h - k_header_h, true ) )
			{

				// 1. Nightmode (with world color swatch)
				xui::toggle( "Nightmode", scene.world_setting );
				if ( xui::begin_popup( "##nightmode_popup", 220.0f, &scene.world_color.value ) )
				{
					xui::color_picker( "color##world", scene.world_color );
					xui::slider_float( "intensity##world", scene.ambient_intensity, 0.0f, 2.0f, "%.2f" );
					xui::end_popup( );
				}

				// 2. Fullbright (with lighting color swatch)
				xui::toggle( "Fullbright", scene.lighting );
				if ( xui::begin_popup( "##fullbright_popup", 220.0f, &scene.lighting_color.value ) )
				{
					xui::slider_float( "intensity##light", scene.lighting_intensity, 0.0f, 2.0f, "%.2f" );
					xui::color_picker( "color##light", scene.lighting_color );
					xui::end_popup( );
				}

				// 3. Override Sky (with sky color swatch)
				xui::toggle( "Override Sky", scene.skybox.custom_skybox );
				if ( xui::begin_popup( "##skybox_popup", 220.0f, &scene.skybox.skybox_color.value ) )
				{
					const auto& skyboxes = features::world::g_scene.get_skyboxes( );
					if ( !skyboxes.empty( ) )
					{
						std::vector<const char*> names;
						names.reserve( skyboxes.size( ) );
						for ( const auto& skybox : skyboxes )
						{
							names.push_back( skybox.display_name.c_str( ) );
						}

						scene.skybox.selected_skybox.value = std::clamp(
							scene.skybox.selected_skybox.value, 0,
							static_cast<int>( skyboxes.size( ) ) - 1 );
						xui::combo(
							"skybox", scene.skybox.selected_skybox.value,
							names.data( ), static_cast<int>( names.size( ) ) );
					}
					xui::checkbox( "custom colors", scene.skybox.custom_color );
					xui::color_picker( "sky color", scene.skybox.skybox_color );
					xui::color_picker( "cloud color", scene.skybox.cloud_color );
					xui::color_picker( "sun color", scene.skybox.sun_color );
					xui::end_popup( );
				}

				// 4. Override Sunlight (with sunlight ambient swatch)
				xui::toggle( "Override Sunlight", scene.ambient );
				if ( xui::begin_popup( "##sunlight_popup", 220.0f, &scene.ambient_color.value ) )
				{
					xui::color_picker( "sunlight color", scene.ambient_color );
					xui::slider_float( "intensity##sun", scene.ambient_intensity, 0.0f, 3.0f, "%.2f" );
					xui::slider_float( "pitch##sun", scene.lighting_rotation.value.x, -1.0f, 1.0f, "%.2f" );
					xui::slider_float( "yaw##sun", scene.lighting_rotation.value.y, -1.0f, 1.0f, "%.2f" );
					xui::end_popup( );
				}

				// 5. Override Fog (with fog color swatch)
				xui::toggle( "Override Fog", weather.fog_enabled );
				if ( xui::begin_popup( "##fog_popup", 220.0f, &weather.fog_color.value ) )
				{
					xui::slider_float( "density##fog", weather.fog_density, 0.0f, 1.0f, "%.2f" );
					xui::slider_float( "anisotropy##fog", weather.fog_anisotropy, 0.0f, 1.0f, "%.2f" );
					xui::slider_float( "draw distance##fog", weather.fog_draw_distance, 500.0f, 20000.0f, "%.0f" );
					xui::color_picker( "color##fog", weather.fog_color );
					xui::end_popup( );
				}

				// 6. Override DoF (with ... popup)
				xui::toggle( "Override DoF", scene.dof );
				if ( xui::begin_popup( "##dof_popup", 220.0f ) )
				{
					xui::slider_float( "near blurry", scene.dof_near_blurry, 0.0f, 50.0f, "%.0f" );
					xui::slider_float( "near crisp", scene.dof_near_crisp, 0.0f, 100.0f, "%.0f" );
					xui::slider_float( "far crisp", scene.dof_far_crisp, 100.0f, 2000.0f, "%.0f" );
					xui::slider_float( "far blurry", scene.dof_far_blurry, 200.0f, 5000.0f, "%.0f" );
					xui::end_popup( );
				}

				// 7. Weather (with > arrow popup)
				xui::toggle( "Weather", weather.enabled );
				if ( xui::begin_popup( "##weather_popup", 220.0f, nullptr, true ) )
				{
					constexpr const char* weather_types[ ]{ "snow", "rain", "stars" };
					xui::combo( "type", weather.type.value, weather_types, 3 );
					xui::color_picker( "color##weather", weather.color );
					xui::checkbox( "wetness", weather.wetness );
					xui::slider_float( "wetness density", weather.wetness_density, 0.0f, 5.0f, "%.1f" );
					xui::slider_float( "wetness speed", weather.wetness_speed, 0.0f, 3.0f, "%.1f" );
					xui::checkbox( "wind", weather.wind );
					xui::slider_float( "wind strength", weather.wind_strength, 0.0f, 5.0f, "%.1f" );
					xui::slider_float( "wind direction", weather.wind_direction, 0.0f, 360.0f, "%.0f" );
					xui::slider_float( "wind turbulence", weather.wind_turbulence, 0.0f, 5.0f, "%.1f" );
					xui::end_popup( );
				}

				// 8. Removals (multi-select combo dropdown on same row)
				{
					static bool s_removals_sel[ 4 ]{};
					s_removals_sel[ 0 ] = settings::g_misc.m_removals.skybox_fog.value;
					s_removals_sel[ 1 ] = settings::g_misc.m_removals.skybox_3d.value;
					s_removals_sel[ 2 ] = settings::g_misc.m_removals.smoke.value;
					s_removals_sel[ 3 ] = ( settings::g_misc.m_removals.flash_alpha.value < 100.0f );
					constexpr const char* removal_names[ ]{ "Fog", "3D Skybox", "Smoke", "Flash" };

					const auto [r_avail_w, _] = xui::layout::avail( );
					constexpr float combo_w = 175.0f;
					const auto rem_row = xui::layout::item( r_avail_w, 26.0f );
					xui::draw::current( ).text( rem_row.x, rem_row.y + 4.0f, "Removals", tokens::col_text );
					const auto w_win = xui::layout::current_window( );
					xui::layout::set_cursor( rem_row.x + r_avail_w - combo_w - w_win->bounds.x, rem_row.y - w_win->bounds.y );
					if ( xui::multicombo( "##removals_combo", s_removals_sel, removal_names, 4, combo_w ) )
					{
						settings::g_misc.m_removals.skybox_fog.value = s_removals_sel[ 0 ];
						settings::g_misc.m_removals.skybox_3d.value = s_removals_sel[ 1 ];
						settings::g_misc.m_removals.smoke.value = s_removals_sel[ 2 ];
						settings::g_misc.m_removals.flash_alpha.value = s_removals_sel[ 3 ] ? 0.0f : 100.0f;
					}
				}

				xui::layout::spacing( 4.0f );

				// 9. Override Bloom
				xui::toggle( "Override Bloom", scene.bloom );
				if ( xui::begin_popup( "##bloom_popup", 220.0f ) )
				{
					xui::slider_float( "value##bloom", scene.bloom_value, 0.0f, 2.0f, "%.2f" );
					xui::end_popup( );
				}

				// 10. Override Gamma
				xui::toggle( "Override Gamma", scene.gamma );
				if ( xui::begin_popup( "##gamma_popup", 220.0f ) )
				{
					xui::slider_float( "value##gamma", scene.gamma_value, 0.5f, 5.0f, "%.1f" );
					xui::end_popup( );
				}

				xui::end_child( );
			}

			// Apply to active world scene & weather in real time!
			w.update_active( rendering::g_widgets.s_map_name );
		}
	}

} // namespace rendering
