#include <pch/pch.hpp>
#include <utilities/math/math.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../rendering.hpp"
#include "../theme.hpp"
#include <utilities/security/security.hpp>
#include <map>

namespace rendering {

	void widgets::draw( )
	{
		auto& dl = xdraw::get( );

		if ( settings::g_misc.m_watermark.enabled.value )
		{
			this->watermark( dl );
		}

		this->keybinds( xdraw::get( xdraw::layer::top ) );
	}

	void widgets::watermark( xdraw::draw_list& draw_list )
	{
		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto& wm = settings::g_misc.m_watermark;
		const auto framerate = xdraw::framerate( );
		const auto local = systems::g_local.get( );


		// ── time ────────────────────────────────────────────────────────────
		SYSTEMTIME st{};
		GetLocalTime( &st );
		char time_buf[ 8 ]{};
		std::snprintf( time_buf, sizeof( time_buf ), "%02d:%02d", st.wHour, st.wMinute );

		// ── fps ─────────────────────────────────────────────────────────────
		static auto smoothed_fps{ 0.0f };
		if ( smoothed_fps == 0.0f ) smoothed_fps = framerate;
		smoothed_fps += ( framerate - smoothed_fps ) * std::min( 2.0f * xdraw::delta_time( ), 1.0f );
		char fps_val[ 8 ]{};
		std::snprintf( fps_val, sizeof( fps_val ), "%.0f", smoothed_fps );

		// ── ping ────────────────────────────────────────────────────────────
		auto ping{ 0 };
		if ( local.is_alive && local.controller && systems::g_entities.exists( local.controller ) )
			ping = memory::read<std::uint32_t>( local.controller + SCHEMA( "CCSPlayerController", "m_iPing"_hash ) );
		char ping_val[ 8 ]{};
		std::snprintf( ping_val, sizeof( ping_val ), "%d", ping );

		// ── map name (stored reliably from level_initialization hook) ────────
		const bool has_map = wm.show_map.value && !s_map_name.empty( );

		// ── tick rate (measured from server_tick delta over ~2 s of real time) ─
		static auto last_server_tick{ 0 };
		static auto last_curtime{ 0.0f };
		static auto measured_tickrate{ 0 };

		if ( local.controller )
		{
			const auto net_for_tick = addresses::globals::network_client_service;
			const auto tick_state   = net_for_tick ? memory::call_vfunc<std::uintptr_t>( net_for_tick, 23 ) : 0;
			const auto server_tick  = tick_state   ? memory::read<int>( tick_state + 892 ) : 0;
			const auto gv           = memory::read<std::uintptr_t>( addresses::globals::global_vars );
			const auto curtime      = gv ? memory::read<float>( gv + 0x30 ) : 0.0f;

			if ( server_tick > 0 && last_server_tick > 0 && curtime - last_curtime >= 2.0f )
			{
				const auto tick_delta = server_tick - last_server_tick;
				const auto time_delta = curtime - last_curtime;
				if ( tick_delta > 0 && time_delta > 0.5f )
				{
					const auto rate = static_cast<int>( std::round( tick_delta / time_delta ) );
					if ( rate >= 16 && rate <= 256 ) measured_tickrate = rate;
				}
				last_server_tick = server_tick;
				last_curtime     = curtime;
			}
			else if ( last_server_tick == 0 && server_tick > 0 )
			{
				last_server_tick = server_tick;
				last_curtime     = curtime;
			}
		}
		else
		{
			last_server_tick = 0;
			last_curtime     = 0.0f;
			measured_tickrate = 0;
		}

		const bool has_tick = wm.show_tick.value && local.controller && measured_tickrate > 0;
		char tick_val[ 8 ]{};
		if ( has_tick ) std::snprintf( tick_val, sizeof( tick_val ), "%d", measured_tickrate );

		// ── velocity ──────────────────────────────────────────────────────
		const bool has_velocity = wm.show_velocity.value && local.is_alive && local.pawn;
		static auto smoothed_velocity{ 0.0f };
		char vel_val[ 8 ]{};
		if ( has_velocity )
		{
			const auto velocity = memory::read<math::vector3>( local.pawn + SCHEMA( "C_BaseEntity", "m_vecAbsVelocity"_hash ) );
			const auto speed = velocity.length_2d( );
			smoothed_velocity += ( speed - smoothed_velocity ) * std::min( 8.0f * xdraw::delta_time( ), 1.0f );
			std::snprintf( vel_val, sizeof( vel_val ), "%.0f", smoothed_velocity );
		}
		else
		{
			smoothed_velocity = 0.0f;
		}


        xdraw::push_font(g_fonts.inter_medium[fonts::size::petite]);
        struct segment { std::string value, unit; float width; };
        std::vector<segment> segments;
        const auto add = [&](std::string value, std::string unit = {}) {
            const auto width = xdraw::measure_text(value).first + xdraw::measure_text(unit).first + 22.0f;
            segments.push_back({std::move(value), std::move(unit), width});
        };
        const auto available = std::max(100.0f, static_cast<float>(screen_w) - 24.0f);
        if (wm.show_user.value) add(theme::fit_text(g_menu.user_name(), std::min(140.0f, available * 0.25f)));
        if (has_map) add(theme::fit_text(s_map_name, 130.0f));
        if (wm.show_ping.value) add(ping_val, " ms");
        if (has_velocity) add(vel_val, " u/s");
        if (wm.show_fps.value) add(fps_val, " fps");
        if (has_tick) add(tick_val, " tick");
        if (wm.show_time.value) add(time_buf);

        constexpr float height = 34.0f, pad = 7.0f, gap = 5.0f;
        const auto brand_width = 29.0f + xdraw::measure_text("mintaly").first + 14.0f;
        // Wrap on narrow viewports rather than drawing off-screen.
        std::vector<std::vector<segment>> rows(1);
        std::vector<float> widths{brand_width + pad * 2.0f};
        for (auto& item : segments)
        {
            if (widths.back() + gap + item.width > available)
            {
                rows.emplace_back();
                widths.push_back(pad * 2.0f);
            }
            widths.back() += gap + item.width;
            rows.back().push_back(std::move(item));
        }
        for (std::size_t row = 0; row < rows.size(); ++row)
        {
            const auto width = widths[row];
            const auto x = std::max(4.0f, static_cast<float>(screen_w) - width - 12.0f);
            const auto y = 12.0f + row * (height + 5.0f);
            draw_list.rect_filled(x - 2.0f, y + 3.0f, width + 4.0f, height + 2.0f,
                xdraw::color{0,0,0,30}, xdraw::corner_radius{9.0f});
            draw_list.rect_filled(x, y, width, height, tokens::col_card, xdraw::corner_radius{7.0f});
            draw_list.rect(x, y, width, height, tokens::col_border, xdraw::corner_radius{7.0f});
            draw_list.rect_filled_gradient(x + 10.0f, y, width - 20.0f, 1.5f,
                tokens::col_accent.alpha(0), tokens::col_accent.alpha(180),
                tokens::col_accent.alpha(180), tokens::col_accent.alpha(0));
            auto cx = x + pad;
            if (row == 0)
            {
                draw_list.rect_filled(cx, y + 6.0f, 22.0f, 22.0f, tokens::col_elevated, xdraw::corner_radius{5.0f});
                draw_list.rect(cx, y + 6.0f, 22.0f, 22.0f, tokens::col_accent.alpha(110), xdraw::corner_radius{5.0f});
                xdraw::push_font(g_fonts.inter_bold[fonts::size::petite]);
                const auto [mw, mh] = xdraw::measure_text("M");
                draw_list.text(cx + (22.0f - mw) * 0.5f, y + (height - mh) * 0.5f, "M", tokens::col_accent);
                xdraw::pop_font();
                const auto th = xdraw::measure_text("mintaly").second;
                draw_list.text(cx + 29.0f, y + (height - th) * 0.5f, "mintaly", tokens::col_text);
                cx += brand_width;
            }
            for (const auto& item : rows[row])
            {
                draw_list.line(cx + gap * 0.5f, y + 10.0f, cx + gap * 0.5f, y + height - 10.0f, tokens::col_border);
                cx += gap;
                const auto [vw, vh] = xdraw::measure_text(item.value);
                const auto uh = xdraw::measure_text(item.unit).second;
                draw_list.text(cx + 11.0f, y + (height - vh) * 0.5f, item.value, tokens::col_text);
                if (!item.unit.empty())
                    draw_list.text(cx + 11.0f + vw, y + (height - uh) * 0.5f, item.unit, tokens::col_text_dim);
                cx += item.width;
            }
        }
        xdraw::pop_font();
    }

	void widgets::keybinds( xdraw::draw_list& draw_list )
	{
		struct row_anim_t
		{
			animation::fade alpha;
			animation::spring offset_y;
			bool active_this_frame{ false };
		};

		static std::map<std::string, row_anim_t> row_states;
		static animation::fade container_alpha;
		static bool dragging{};
		static float drag_offset_x{}, drag_offset_y{};
		constexpr auto drag_id = xui::fnv1a( "keybind_panel_drag" );
		auto& ui = xui::ctx( );
		auto& input = ui.input;
		auto& cfg = settings::g_misc.m_keybinds;
		const auto menu_open = g_menu.is_open( );
		if ( !cfg.enabled.value )
		{
			dragging = false;
			if ( ui.active_window == drag_id ) ui.active_window = xui::null_id;
			row_states.clear( );
			container_alpha = {};
			return;
		}

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		if ( screen_w <= 0 || screen_h <= 0 ) return;
		constexpr auto header_h{ 34.0f };
		constexpr auto row_h{ 28.0f };
		constexpr auto pad{ 12.0f };
		const auto panel_w = std::min( 320.0f, static_cast<float>( screen_w ) );

		struct bind_entry
		{
			const char* name;
			char value[ 32 ];
			bool has_value_pill;
			xui::bind_mode mode;
			int key;
		};

		bind_entry entries[ 32 ]{};
		auto count{ 0 };

		const auto& ctx = features::combat::g_shared.ctx( );
		const auto has_weapon = ctx.valid && ctx.weapon_type >= cstypes::weapon_type::pistol && ctx.weapon_type <= cstypes::weapon_type::lmg;

		for ( const auto setting : xui::binds::all( ) )
		{
			if ( !setting || setting->bind.key == 0 || !setting->bind.active || count >= 32 )
			{
				continue;
			}

			auto is_rage_group{ false };
			for ( auto i = 0u; i < settings::combat::ragebot::k_group_count; ++i )
			{
				const auto& g = settings::g_combat.m_ragebot.groups[ i ];
				if ( setting == &g.min_damage_override || setting == &g.hitchance_override || setting == &g.force_shot || setting == &g.force_shot_air || setting == &g.body_aim || setting == &g.silent || setting == &g.no_spread )
				{
					is_rage_group = true;
					break;
				}
			}

			if ( is_rage_group )
			{
				if ( !settings::g_combat.m_ragebot.enabled || !has_weapon )
				{
					continue;
				}

				const auto active_group = &settings::g_combat.m_ragebot.get_group( ctx.weapon_type );
				auto is_active{ false };

				for ( auto i = 0u; i < settings::combat::ragebot::k_group_count; ++i )
				{
					const auto& g = settings::g_combat.m_ragebot.groups[ i ];
					if ( &g == active_group )
					{
						if ( setting == &g.min_damage_override || setting == &g.hitchance_override || setting == &g.force_shot || setting == &g.force_shot_air || setting == &g.body_aim )
						{
							is_active = true;
						}
						break;
					}
				}

				if ( !is_active )
				{
					continue;
				}

				auto& e = entries[ count++ ];
				e.name = setting->name.c_str( );
				e.mode = setting->bind.mode;
				e.key = setting->bind.key;

				if ( setting == &active_group->min_damage_override )
				{
					std::snprintf( e.value, sizeof( e.value ), "%d", active_group->min_damage_override_value.value );
					e.has_value_pill = true;
				}
				else if ( setting == &active_group->hitchance_override )
				{
					std::snprintf( e.value, sizeof( e.value ), "%d%%", active_group->hitchance_override_value.value );
					e.has_value_pill = true;
				}
				else
				{
					e.value[ 0 ] = '\0';
					e.has_value_pill = false;
				}
				continue;
			}

			auto is_legit_group{ false };
			for ( auto i = 0u; i < settings::combat::legitbot::k_group_count; ++i )
			{
				const auto& g = settings::g_combat.m_legitbot.groups[ i ];
				if ( setting == &g.aimbot || setting == &g.rcs || setting == &g.standalone_rcs || setting == &g.triggerbot || setting == &g.autowall || setting == &g.visualize_fov || setting == &g.trigger_head_only || setting == &g.give_me_your_seed )
				{
					is_legit_group = true;
					break;
				}
			}

			if ( is_legit_group )
			{
				if ( !settings::g_combat.m_legitbot.enabled.value || !has_weapon )
				{
					continue;
				}

				const auto* active_group = &settings::g_combat.m_legitbot.get_group( ctx.weapon_type );
				auto is_active{ false };

				for ( auto i = 0u; i < settings::combat::legitbot::k_group_count; ++i )
				{
					if ( &settings::g_combat.m_legitbot.groups[ i ] == active_group )
					{
						const auto& g = settings::g_combat.m_legitbot.groups[ i ];
						if ( setting == &g.aimbot || setting == &g.rcs || setting == &g.standalone_rcs || setting == &g.triggerbot || setting == &g.autowall || setting == &g.visualize_fov || setting == &g.trigger_head_only || setting == &g.give_me_your_seed )
						{
							is_active = true;
						}

						if ( is_active && setting == &active_group->give_me_your_seed && !active_group->triggerbot.value )
						{
							is_active = false;
						}
						break;
					}
				}

				if ( !is_active )
				{
					continue;
				}

				auto& e = entries[ count++ ];
				e.name = setting->name.c_str( );
				e.mode = setting->bind.mode;
				e.key = setting->bind.key;
				e.value[ 0 ] = '\0';
				e.has_value_pill = false;
				continue;
			}

			if ( setting == &settings::g_combat.m_antiaim.enabled || setting == &settings::g_combat.m_antiaim.manual_left || setting == &settings::g_combat.m_antiaim.manual_right || setting == &settings::g_combat.m_antiaim.hide_shots || setting == &settings::g_combat.m_antiaim.avoid_backstab || setting == &settings::g_combat.m_antiaim.direction_indicator )
			{
				if ( !settings::g_combat.m_antiaim.enabled.value )
				{
					continue;
				}
			}

			auto& e = entries[ count++ ];
			e.name = setting->name.c_str( );
			e.mode = setting->bind.mode;
				e.key = setting->bind.key;
			e.value[ 0 ] = '\0';
			e.has_value_pill = false;
		}

		if ( count > 0 || menu_open ) container_alpha.fade_in( 0.18f );
		else container_alpha.fade_out( 0.18f );
		container_alpha.update( );

		if ( !menu_open || !input.mouse_down || input.mouse_released )
		{
			dragging = false;
			if ( ui.active_window == drag_id ) ui.active_window = xui::null_id;
		}
		if ( !container_alpha.visible( ) ) return;

		const auto capacity = std::max( 0, static_cast<int>( ( screen_h - header_h - 8.0f ) / row_h ) );
		const auto visible_count = std::min( count, capacity );
		const auto body_rows = std::max( visible_count, menu_open ? 1 : 0 );
		const auto total_h = std::min( header_h + body_rows * row_h + 8.0f, static_cast<float>( screen_h ) );
		const auto max_x = std::max( 0.0f, screen_w - panel_w );
		const auto max_y = std::max( 0.0f, screen_h - total_h );
		const auto normalized = []( float value, float fallback ) {
			return std::isfinite( value ) ? std::clamp( value, 0.0f, 1.0f ) : fallback;
		};
		auto x = std::clamp( normalized( cfg.x.value, 0.015f ) * screen_w, 0.0f, max_x );
		auto y = std::clamp( normalized( cfg.y.value, 0.42f ) * screen_h, 0.0f, max_y );
		const auto header = xui::rect{ x, y, panel_w, std::min( header_h, total_h ) };
		const auto hovered = menu_open && input.in_rect( header ) && !ui.overlay_blocking( );

		if ( hovered && input.mouse_clicked && ui.active_window == xui::null_id &&
			ui.active_slider == xui::null_id && ui.active_resize == xui::null_id &&
			ui.active_text_input == xui::null_id && ui.active_child_scroll == xui::null_id )
		{
			dragging = true;
			drag_offset_x = input.mouse_x - x;
			drag_offset_y = input.mouse_y - y;
			ui.active_window = drag_id;
		}
		if ( dragging )
		{
			x = std::clamp( input.mouse_x - drag_offset_x, 0.0f, max_x );
			y = std::clamp( input.mouse_y - drag_offset_y, 0.0f, max_y );
			cfg.x.value = x / screen_w;
			cfg.y.value = y / screen_h;
			// Do not let the same press activate or drag the menu underneath.
			input.mouse_clicked = false;
			input.mouse_double_clicked = false;
		}

		const auto alpha = container_alpha.alpha( );
		const auto tint = [alpha]( xdraw::color c ) {
			return c.alpha( static_cast<std::uint8_t>( c.a * alpha ) );
		};
		draw_list.rect_filled( x, y + 3.0f, panel_w, total_h, tint( {0, 0, 0, 65} ), xdraw::corner_radius{10.0f} );
		draw_list.rect_filled_blurred( x, y, panel_w, total_h, xdraw::corner_radius{10.0f}, tint( {255, 255, 255, 220} ) );
		draw_list.rect_filled( x, y, panel_w, total_h, tint( tokens::col_card.alpha( 242 ) ), xdraw::corner_radius{10.0f} );
		draw_list.rect( x, y, panel_w, total_h, tint( dragging ? tokens::col_accent : tokens::col_border ), xdraw::corner_radius{10.0f} );
		draw_list.push_clip( x, y, panel_w, total_h );
		draw_list.line( x + pad, y + header_h, x + panel_w - pad, y + header_h, tint( tokens::col_border ) );
		draw_list.circle_filled( x + pad + 3.0f, y + header_h * 0.5f, 3.0f, tint( tokens::col_accent ) );
		const auto title = std::format( "KEYBINDS  {}", count );
		const auto title_h = xdraw::measure_text( title ).second;
		draw_list.text( x + pad + 14.0f, y + ( header_h - title_h ) * 0.5f, title, tint( tokens::col_text ) );
		if ( menu_open )
		{
			for ( int row = 0; row < 2; ++row )
				for ( int col = 0; col < 3; ++col )
					draw_list.circle_filled( x + panel_w - 25.0f + col * 4.0f, y + 14.0f + row * 5.0f,
						1.0f, tint( hovered ? tokens::col_accent : tokens::col_text_dim ) );
		}

		for ( auto& [name, state] : row_states ) state.active_this_frame = false;
		for ( int i = 0; i < visible_count; ++i )
		{
			const auto& e = entries[i];
			const auto row_key = std::format( "{}:{}:{}", e.name, e.key, static_cast<int>( e.mode ) );
			auto& anim = row_states[row_key];
			const auto target_y = header_h + 4.0f + i * row_h;
			if ( anim.alpha.alpha( ) <= 0.01f ) anim.offset_y.snap( target_y );
			anim.active_this_frame = true;
			anim.alpha.fade_in( 0.15f );
			anim.offset_y.set_target( target_y );
			anim.alpha.update( );
			anim.offset_y.update( );
			const auto row_alpha = alpha * anim.alpha.alpha( );
			const auto row_tint = [row_alpha]( xdraw::color c ) {
				return c.alpha( static_cast<std::uint8_t>( c.a * row_alpha ) );
			};
			const auto ry = y + anim.offset_y.value( );
			const auto mode = e.mode == xui::bind_mode::toggle ? "TOGGLE" : e.mode == xui::bind_mode::hold_off ? "OFF" : "HOLD";
			const auto badge = std::format( "{} / {}", xui::vk_name( e.key ), mode );
			const auto [bw, bh] = xdraw::measure_text( badge );
			const auto badge_w = bw + 12.0f;
			const auto badge_x = x + panel_w - pad - badge_w;
			draw_list.rect_filled( badge_x, ry + 4.0f, badge_w, row_h - 8.0f, row_tint( tokens::col_elevated ), xdraw::corner_radius{4.0f} );
			draw_list.text( badge_x + 6.0f, ry + ( row_h - bh ) * 0.5f, badge, row_tint( tokens::col_text_dim ) );
			auto name_right = badge_x - 8.0f;
			if ( e.has_value_pill )
			{
				const auto [vw, vh] = xdraw::measure_text( e.value );
				const auto vx = name_right - vw - 12.0f;
				draw_list.rect_filled( vx, ry + 4.0f, vw + 12.0f, row_h - 8.0f, row_tint( tokens::col_accent.alpha( 28 ) ), xdraw::corner_radius{4.0f} );
				draw_list.text( vx + 6.0f, ry + ( row_h - vh ) * 0.5f, e.value, row_tint( tokens::col_accent ) );
				name_right = vx - 8.0f;
			}
			const auto name = theme::fit_text( e.name, name_right - x - pad );
			const auto nh = xdraw::measure_text( name ).second;
			draw_list.text( x + pad, ry + ( row_h - nh ) * 0.5f, name, row_tint( tokens::col_text ) );
		}
		for ( auto it = row_states.begin( ); it != row_states.end( ); )
		{
			if ( !it->second.active_this_frame ) it = row_states.erase( it );
			else ++it;
		}
		if ( count == 0 && menu_open )
		{
			const auto label = "No active binds. Drag the header to move.";
			const auto text = theme::fit_text( label, panel_w - pad * 2.0f );
			const auto th = xdraw::measure_text( text ).second;
			draw_list.text( x + pad, y + header_h + 4.0f + ( row_h - th ) * 0.5f, text, tint( tokens::col_text_dim ) );
		}
		draw_list.pop_clip( );
	}

} // namespace rendering
