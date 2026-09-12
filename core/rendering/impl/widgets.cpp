#include <pch/pch.hpp>
#include <utilities/math/math.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>

#include "../rendering.hpp"
#include "../theme.hpp"
#include "menu/menu.weapons.hpp"
#include <utilities/security/security.hpp>
#include <utilities/steam/steam.hpp>

namespace rendering {

	void widgets::draw( )
	{
		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		if ( screen_w <= 0 || screen_h <= 0 )
			return;

		// HUD widgets must not inherit a menu/popup clip rectangle.
		auto& dl = xdraw::get( xdraw::layer::top );
		dl.push_clip_absolute( 0.0f, 0.0f, static_cast<float>( screen_w ), static_cast<float>( screen_h ) );
		xdraw::push_font( g_fonts.inter_medium[fonts::size::petite] );

		if ( settings::g_misc.m_watermark.enabled.value )
		{
			this->watermark( dl );
		}

		if ( settings::g_misc.m_widgets.keybinds_list.value )
		{
			this->keybinds( dl );
		}
		else
		{
			this->m_keybinds_hovered = false;
			this->m_keybinds_dragging = false;
		}

		if ( settings::g_misc.m_widgets.spectator_list.value )
		{
			this->spectators( dl );
		}
		else
		{
			this->m_spectators_hovered = false;
			this->m_spectators_dragging = false;
		}

		xdraw::pop_font( );
		dl.pop_clip( );
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
        enum class wm_icon_type : int {
            user,
            map,
            ping,
            velocity,
            fps,
            tick,
            time
        };
        struct segment { wm_icon_type icon; std::string value, unit; float width; };
        std::vector<segment> segments;
        const auto add = [&](wm_icon_type icon, std::string value, std::string unit = {}) {
            constexpr float icon_area_w = 14.0f + 6.0f;
            const auto width = icon_area_w + xdraw::measure_text(value).first + xdraw::measure_text(unit).first + 18.0f;
            segments.push_back({icon, std::move(value), std::move(unit), width});
        };
        const auto available = std::max(100.0f, static_cast<float>(screen_w) - 24.0f);
        if (wm.show_user.value) add(wm_icon_type::user, theme::fit_text(g_menu.user_name(), std::min(140.0f, available * 0.25f)));
        if (has_map) add(wm_icon_type::map, theme::fit_text(s_map_name, 130.0f));
        if (wm.show_ping.value) add(wm_icon_type::ping, ping_val, " ms");
        if (has_velocity) add(wm_icon_type::velocity, vel_val, " u/s");
        if (wm.show_fps.value) add(wm_icon_type::fps, fps_val, " fps");
        if (has_tick) add(wm_icon_type::tick, tick_val, " tick");
        if (wm.show_time.value) add(wm_icon_type::time, time_buf);

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
        const auto master_opacity = std::clamp( wm.opacity.value, 0.0f, 100.0f ) / 100.0f;
        const auto tint = [master_opacity]( xdraw::color c ) -> xdraw::color {
            return c.alpha( static_cast<std::uint8_t>( c.a * master_opacity ) );
        };

        // Position enum: 0=top-left, 1=top-center, 2=top-right, 3=bottom-left, 4=bottom-center, 5=bottom-right
        const auto pos = std::clamp( wm.position.value, 0, 5 );
        const bool is_bottom = pos >= 3;
        const bool is_left   = pos == 0 || pos == 3;
        const bool is_center = pos == 1 || pos == 4;

        auto draw_wm_icon = [&]( wm_icon_type type, float ix, float iy, xdraw::color col ) {
            switch ( type )
            {
            case wm_icon_type::user:
            {
                draw_list.circle( ix, iy - 2.8f, 2.5f, col, 1.3f );
                std::vector<float> p{
                    ix - 4.5f, iy + 4.2f,
                    ix - 3.2f, iy + 1.8f,
                    ix,        iy + 1.0f,
                    ix + 3.2f, iy + 1.8f,
                    ix + 4.5f, iy + 4.2f
                };
                draw_list.polyline( p, col, false, 1.3f );
                break;
            }
            case wm_icon_type::map:
            {
                draw_list.circle( ix, iy - 1.8f, 3.2f, col, 1.3f );
                draw_list.circle_filled( ix, iy - 1.8f, 1.2f, col );
                draw_list.line( ix - 2.4f, iy + 0.2f, ix, iy + 4.4f, col, 1.3f );
                draw_list.line( ix + 2.4f, iy + 0.2f, ix, iy + 4.4f, col, 1.3f );
                break;
            }
            case wm_icon_type::ping:
            {
                draw_list.line( ix - 3.5f, iy + 3.8f, ix - 3.5f, iy + 1.2f, col, 1.5f );
                draw_list.line( ix,        iy + 3.8f, ix,        iy - 1.2f, col, 1.5f );
                draw_list.line( ix + 3.5f, iy + 3.8f, ix + 3.5f, iy - 3.8f, col, 1.5f );
                break;
            }
            case wm_icon_type::velocity:
            {
                draw_list.line( ix - 4.5f, iy - 2.6f, ix + 2.2f, iy - 2.6f, col, 1.4f );
                draw_list.line( ix - 2.5f, iy,        ix + 4.5f, iy,        col, 1.4f );
                draw_list.line( ix - 4.5f, iy + 2.6f, ix + 1.2f, iy + 2.6f, col, 1.4f );
                break;
            }
            case wm_icon_type::fps:
            {
                std::vector<float> p{
                    ix + 0.6f, iy - 4.8f,
                    ix - 2.8f, iy + 0.2f,
                    ix - 0.2f, iy + 0.2f,
                    ix - 1.4f, iy + 4.8f,
                    ix + 3.2f, iy - 0.8f,
                    ix + 0.8f, iy - 0.8f,
                    ix + 1.8f, iy - 4.8f
                };
                draw_list.polyline( p, col, false, 1.4f );
                break;
            }
            case wm_icon_type::tick:
            {
                std::vector<float> p{
                    ix - 4.8f, iy + 0.8f,
                    ix - 2.2f, iy + 0.8f,
                    ix - 0.8f, iy - 3.8f,
                    ix + 0.8f, iy + 3.8f,
                    ix + 2.2f, iy + 0.8f,
                    ix + 4.8f, iy + 0.8f
                };
                draw_list.polyline( p, col, false, 1.4f );
                break;
            }
            case wm_icon_type::time:
            {
                draw_list.circle( ix, iy, 4.4f, col, 1.3f );
                draw_list.line( ix, iy, ix,        iy - 2.5f, col, 1.3f );
                draw_list.line( ix, iy, ix + 2.2f, iy + 0.6f, col, 1.3f );
                draw_list.circle_filled( ix, iy, 1.0f, col );
                break;
            }
            }
        };

        for (std::size_t row = 0; row < rows.size(); ++row)
        {
            const auto w = widths[row];

            // X anchor
            float x;
            if (is_left)
                x = 12.0f;
            else if (is_center)
                x = (static_cast<float>(screen_w) - w) * 0.5f;
            else // right
                x = std::max(4.0f, static_cast<float>(screen_w) - w - 12.0f);

            // Y anchor
            float y;
            if (is_bottom)
                y = static_cast<float>(screen_h) - 12.0f - static_cast<float>(rows.size() - row) * (height + 5.0f) + 5.0f;
            else
                y = 12.0f + row * (height + 5.0f);

            draw_list.rect_filled(x - 2.0f, y + 3.0f, w + 4.0f, height + 2.0f,
                tint({0,0,0,30}), xdraw::corner_radius{9.0f});
            draw_list.rect_filled(x, y, w, height, tint(tokens::col_card), xdraw::corner_radius{7.0f});
            draw_list.rect(x, y, w, height, tint(tokens::col_border), xdraw::corner_radius{7.0f});
            const auto accent_x = x + 10.0f;
            const auto accent_w = std::max(0.0f, w - 20.0f);
            const auto half_w = accent_w * 0.5f;
            const auto edge = tint(tokens::col_accent.alpha(0));
            const auto center = tint(tokens::col_accent.alpha(180));
            draw_list.rect_filled_gradient(accent_x, y, half_w, 1.5f,
                edge, center, center, edge);
            draw_list.rect_filled_gradient(accent_x + half_w, y, half_w, 1.5f,
                center, edge, edge, center);
            auto cx = x + pad;
            if (row == 0)
            {
                draw_list.rect_filled(cx, y + 6.0f, 22.0f, 22.0f, tint(tokens::col_elevated), xdraw::corner_radius{5.0f});
                draw_list.rect(cx, y + 6.0f, 22.0f, 22.0f, tint(tokens::col_accent.alpha(110)), xdraw::corner_radius{5.0f});
                xdraw::push_font(g_fonts.inter_bold[fonts::size::petite]);
                const auto [mw, mh] = xdraw::measure_text("M");
                draw_list.text(cx + (22.0f - mw) * 0.5f, y + (height - mh) * 0.5f, "M", tint(tokens::col_accent));
                xdraw::pop_font();
                const auto th = xdraw::measure_text("mintaly").second;
                draw_list.text(cx + 29.0f, y + (height - th) * 0.5f, "mintaly", tint(tokens::col_text));
                cx += brand_width;
            }
            for (const auto& item : rows[row])
            {
                cx += gap;
                const auto item_cy = y + height * 0.5f;
                const auto icon_x = cx + 8.0f;
                draw_wm_icon( item.icon, icon_x, item_cy, tint(tokens::col_accent) );

                const auto text_x = icon_x + 11.0f;
                const auto [vw, vh] = xdraw::measure_text(item.value);
                const auto uh = xdraw::measure_text(item.unit).second;
                draw_list.text(text_x, y + (height - vh) * 0.5f, item.value, tint(tokens::col_text));
                if (!item.unit.empty())
                    draw_list.text(text_x + vw, y + (height - uh) * 0.5f, item.unit, tint(tokens::col_text_dim));

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
			float last_w{ 0.0f };
		};

		static std::map<std::string, row_anim_t> row_states;
		static animation::fade container_alpha;
		static animation::spring smoothed_base_y;

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto& s = xui::ctx( ).style;

		constexpr auto margin{ 10.0f };

		constexpr std::size_t k_max_entries = 64;
		struct bind_entry
		{
			char name[ 64 ]{};
			char value[ 32 ]{};
			char key[ 16 ]{};
			bool has_value_pill{ false };
			xui::bind_mode mode{ xui::bind_mode::toggle };
		};

		const auto format_key_name = []( int vk, char* out, std::size_t out_sz ) {
			if ( !out || out_sz == 0 )
				return;
			out[ 0 ] = '\0';
			if ( vk == 0 )
				return;
			const auto raw = xui::vk_name( vk );
			if ( !raw || raw[ 0 ] == '\0' || std::strcmp( raw, "none" ) == 0 )
				return;
			std::size_t i = 0;
			for ( ; raw[ i ] != '\0' && i + 1 < out_sz; ++i )
			{
				out[ i ] = static_cast< char >( std::toupper( static_cast< unsigned char >( raw[ i ] ) ) );
			}
			out[ i ] = '\0';
		};

		bind_entry entries[ k_max_entries ]{};
		auto count{ 0 };

		const auto& ctx = features::combat::g_shared.ctx( );
		const auto has_weapon = ctx.valid && ctx.weapon_type >= cstypes::weapon_type::pistol && ctx.weapon_type <= cstypes::weapon_type::lmg;

		static const settings::combat::ragebot::weapon_group* s_last_rage_group = &settings::g_combat.m_ragebot.groups[ 2 ];
		static const settings::combat::legitbot::weapon_group* s_last_legit_group = &settings::g_combat.m_legitbot.groups[ 2 ];

		if ( has_weapon )
		{
			s_last_rage_group = &settings::g_combat.m_ragebot.get_group( ctx.weapon_type, ctx.item_def_idx );
			s_last_legit_group = &settings::g_combat.m_legitbot.get_group( ctx.weapon_type, ctx.item_def_idx );
		}
		else if ( g_menu.is_open( ) )
		{
			const auto is_custom_rage = ( menu_weapons::weapon_sel_rage.weapon_flat_idx >= 0 &&
				menu_weapons::weapon_sel_rage.weapon_flat_idx < static_cast< int >( cstypes::weapons::k_total_weapons ) );
			if ( is_custom_rage && settings::g_combat.m_ragebot.weapons[ menu_weapons::weapon_sel_rage.weapon_flat_idx ].override_group.value )
				s_last_rage_group = &settings::g_combat.m_ragebot.weapons[ menu_weapons::weapon_sel_rage.weapon_flat_idx ].cfg;
			else
				s_last_rage_group = &settings::g_combat.m_ragebot.groups[ std::clamp( menu_weapons::weapon_sel_rage.group_idx, 0, 5 ) ];

			const auto is_custom_legit = ( menu_weapons::weapon_sel_legit.weapon_flat_idx >= 0 &&
				menu_weapons::weapon_sel_legit.weapon_flat_idx < static_cast< int >( cstypes::weapons::k_total_weapons ) );
			if ( is_custom_legit && settings::g_combat.m_legitbot.weapons[ menu_weapons::weapon_sel_legit.weapon_flat_idx ].override_group.value )
				s_last_legit_group = &settings::g_combat.m_legitbot.weapons[ menu_weapons::weapon_sel_legit.weapon_flat_idx ].cfg;
			else
				s_last_legit_group = &settings::g_combat.m_legitbot.groups[ std::clamp( menu_weapons::weapon_sel_legit.group_idx, 0, 5 ) ];
		}

		const auto active_rage_group = s_last_rage_group ? s_last_rage_group : &settings::g_combat.m_ragebot.groups[ 2 ];
		const auto active_legit_group = s_last_legit_group ? s_last_legit_group : &settings::g_combat.m_legitbot.groups[ 2 ];

		const auto is_in_rage_group = []( const xui::setting* s, const settings::combat::ragebot::weapon_group& wg ) {
			return s == &wg.min_damage_override || s == &wg.hitchance_override ||
				s == &wg.force_shot || s == &wg.force_shot_air || s == &wg.body_aim ||
				s == &wg.silent || s == &wg.no_spread || s == &wg.autostop ||
				s == &wg.dynamic_pointscale || s == &wg.debug_multipoints;
		};

		const auto is_in_legit_group = []( const xui::setting* s, const settings::combat::legitbot::weapon_group& wg ) {
			return s == &wg.aimbot || s == &wg.rcs || s == &wg.standalone_rcs ||
				s == &wg.triggerbot || s == &wg.trigger_head_only || s == &wg.give_me_your_seed ||
				s == &wg.autowall || s == &wg.smoke_check || s == &wg.scope_check ||
				s == &wg.flash_check || s == &wg.ground_check || s == &wg.visualize_fov;
		};

		for ( const auto setting : xui::binds::all( ) )
		{
			if ( !setting || setting->bind.key == 0 || !setting->bind.active || count >= k_max_entries )
			{
				continue;
			}

			auto is_rage_group{ false };
			for ( auto i = 0u; i < settings::combat::ragebot::k_group_count; ++i )
			{
				if ( is_in_rage_group( setting, settings::g_combat.m_ragebot.groups[ i ] ) )
				{
					is_rage_group = true;
					break;
				}
			}
			if ( !is_rage_group )
			{
				for ( auto i = 0u; i < settings::combat::ragebot::k_weapon_count; ++i )
				{
					if ( is_in_rage_group( setting, settings::g_combat.m_ragebot.weapons[ i ].cfg ) )
					{
						is_rage_group = true;
						break;
					}
				}
			}

			if ( is_rage_group )
			{
				if ( !settings::g_combat.m_ragebot.enabled )
				{
					continue;
				}

				if ( ctx.valid )
				{
					if ( !is_in_rage_group( setting, *active_rage_group ) )
					{
						continue;
					}
				}

				std::string clean_name = setting->name;
				if ( clean_name.empty( ) || clean_name == "enabled" )
				{
					if ( !setting->category.empty( ) )
						clean_name = setting->category;
					else
						clean_name = "ragebot";
				}

				bool duplicate = false;
				for ( auto j = 0; j < count; ++j )
				{
					if ( std::strcmp( entries[ j ].name, clean_name.c_str( ) ) == 0 )
					{
						duplicate = true;
						break;
					}
				}
				if ( duplicate )
				{
					continue;
				}

				auto& e = entries[ count++ ];
				format_key_name( setting->bind.key, e.key, sizeof( e.key ) );
				std::strncpy( e.name, clean_name.c_str( ), sizeof( e.name ) - 1 );
				e.name[ sizeof( e.name ) - 1 ] = '\0';
				e.mode = setting->bind.mode;

				const auto matched_group = is_in_rage_group( setting, *active_rage_group ) ? active_rage_group : &settings::g_combat.m_ragebot.groups[ 2 ];

				if ( setting == &matched_group->min_damage_override )
				{
					std::snprintf( e.value, sizeof( e.value ), "%d", matched_group->min_damage_override_value.value );
					e.has_value_pill = true;
				}
				else if ( setting == &matched_group->hitchance_override )
				{
					std::snprintf( e.value, sizeof( e.value ), "%d%%", matched_group->hitchance_override_value.value );
					e.has_value_pill = true;
				}
				else
				{
					e.has_value_pill = false;
					switch ( e.mode )
					{
					case xui::bind_mode::toggle:
						std::snprintf( e.value, sizeof( e.value ), "toggle" );
						break;
					case xui::bind_mode::hold_on:
						std::snprintf( e.value, sizeof( e.value ), "hold" );
						break;
					case xui::bind_mode::hold_off:
						std::snprintf( e.value, sizeof( e.value ), "release" );
						break;
					default:
						std::snprintf( e.value, sizeof( e.value ), "on" );
						break;
					}
				}
				continue;
			}

			auto is_legit_group{ false };
			for ( auto i = 0u; i < settings::combat::legitbot::k_group_count; ++i )
			{
				if ( is_in_legit_group( setting, settings::g_combat.m_legitbot.groups[ i ] ) )
				{
					is_legit_group = true;
					break;
				}
			}
			if ( !is_legit_group )
			{
				for ( auto i = 0u; i < settings::combat::legitbot::k_weapon_count; ++i )
				{
					if ( is_in_legit_group( setting, settings::g_combat.m_legitbot.weapons[ i ].cfg ) )
					{
						is_legit_group = true;
						break;
					}
				}
			}

			if ( is_legit_group )
			{
				if ( !settings::g_combat.m_legitbot.enabled.value )
				{
					continue;
				}

				if ( ctx.valid )
				{
					if ( !is_in_legit_group( setting, *active_legit_group ) )
					{
						continue;
					}
				}

				if ( setting == &active_legit_group->give_me_your_seed && !active_legit_group->triggerbot.value )
				{
					continue;
				}

				std::string clean_name = setting->name;
				if ( clean_name.empty( ) || clean_name == "enabled" )
				{
					if ( !setting->category.empty( ) )
						clean_name = setting->category;
					else
						clean_name = "legitbot";
				}

				bool duplicate = false;
				for ( auto j = 0; j < count; ++j )
				{
					if ( std::strcmp( entries[ j ].name, clean_name.c_str( ) ) == 0 )
					{
						duplicate = true;
						break;
					}
				}
				if ( duplicate )
				{
					continue;
				}

				auto& e = entries[ count++ ];
				format_key_name( setting->bind.key, e.key, sizeof( e.key ) );
				std::strncpy( e.name, clean_name.c_str( ), sizeof( e.name ) - 1 );
				e.name[ sizeof( e.name ) - 1 ] = '\0';
				e.mode = setting->bind.mode;
				e.has_value_pill = false;

				switch ( e.mode )
				{
				case xui::bind_mode::toggle:
					std::snprintf( e.value, sizeof( e.value ), "toggle" );
					break;
				case xui::bind_mode::hold_on:
					std::snprintf( e.value, sizeof( e.value ), "hold" );
					break;
				case xui::bind_mode::hold_off:
					std::snprintf( e.value, sizeof( e.value ), "release" );
					break;
				default:
					std::snprintf( e.value, sizeof( e.value ), "on" );
					break;
				}
				continue;
			}

			if ( setting == &settings::g_combat.m_antiaim.manual_left || setting == &settings::g_combat.m_antiaim.manual_right || setting == &settings::g_combat.m_antiaim.hide_shots || setting == &settings::g_combat.m_antiaim.avoid_backstab || setting == &settings::g_combat.m_antiaim.direction_indicator )
			{
				if ( !settings::g_combat.m_antiaim.enabled.value )
				{
					continue;
				}
			}

			std::string clean_name = setting->name;
			if ( clean_name.empty( ) || clean_name == "enabled" )
			{
				if ( !setting->category.empty( ) )
					clean_name = setting->category;
				else
					clean_name = "unnamed";
			}

			bool duplicate = false;
			for ( auto j = 0; j < count; ++j )
			{
				if ( std::strcmp( entries[ j ].name, clean_name.c_str( ) ) == 0 )
				{
					duplicate = true;
					break;
				}
			}
			if ( duplicate )
			{
				continue;
			}

			auto& e = entries[ count++ ];
			format_key_name( setting->bind.key, e.key, sizeof( e.key ) );
			std::strncpy( e.name, clean_name.c_str( ), sizeof( e.name ) - 1 );
			e.name[ sizeof( e.name ) - 1 ] = '\0';
			e.mode = setting->bind.mode;
			e.has_value_pill = false;

			switch ( e.mode )
			{
			case xui::bind_mode::toggle:
				std::snprintf( e.value, sizeof( e.value ), "toggle" );
				break;
			case xui::bind_mode::hold_on:
				std::snprintf( e.value, sizeof( e.value ), "hold" );
				break;
			case xui::bind_mode::hold_off:
				std::snprintf( e.value, sizeof( e.value ), "release" );
				break;
			default:
				std::snprintf( e.value, sizeof( e.value ), "on" );
				break;
			}
		}

		for ( const auto entry : xui::slider_binds::all( ) )
		{
			if ( !entry )
				continue;

			auto is_rage_group{ false };
			for ( auto i = 0u; i < settings::combat::ragebot::k_group_count; ++i )
			{
				const auto& g = settings::g_combat.m_ragebot.groups[ i ];
				if ( entry->ptr == &g.hitchance.value || entry->ptr == &g.min_damage.value || entry->ptr == &g.max_fov.value || entry->ptr == &g.pointscale.value )
				{
					is_rage_group = true;
					break;
				}
			}
			if ( !is_rage_group )
			{
				for ( auto i = 0u; i < settings::combat::ragebot::k_weapon_count; ++i )
				{
					const auto& w = settings::g_combat.m_ragebot.weapons[ i ].cfg;
					if ( entry->ptr == &w.hitchance.value || entry->ptr == &w.min_damage.value || entry->ptr == &w.max_fov.value || entry->ptr == &w.pointscale.value )
					{
						is_rage_group = true;
						break;
					}
				}
			}

			if ( is_rage_group )
			{
				if ( !settings::g_combat.m_ragebot.enabled )
				{
					continue;
				}

				if ( ctx.valid )
				{
					if ( entry->ptr != &active_rage_group->hitchance.value && entry->ptr != &active_rage_group->min_damage.value && entry->ptr != &active_rage_group->max_fov.value && entry->ptr != &active_rage_group->pointscale.value )
					{
						continue;
					}
				}
			}

			for ( std::size_t i = 0; i < entry->count; ++i )
			{
				const auto& b = entry->binds[ i ];
				if ( b.key == 0 || !b.active || count >= k_max_entries )
					continue;

				const auto hash_pos = entry->label.find( "##" );
				const auto name_len = ( hash_pos != std::string::npos ) ? hash_pos : entry->label.size( );
				std::string clean_name = entry->label.substr( 0, name_len );

				bool duplicate = false;
				for ( auto j = 0; j < count; ++j )
				{
					if ( std::strcmp( entries[ j ].name, clean_name.c_str( ) ) == 0 )
					{
						duplicate = true;
						break;
					}
				}
				if ( duplicate )
					continue;

				auto& e = entries[ count++ ];
				format_key_name( b.key, e.key, sizeof( e.key ) );
				std::strncpy( e.name, clean_name.c_str( ), sizeof( e.name ) - 1 );
				e.name[ sizeof( e.name ) - 1 ] = '\0';
				e.mode = b.mode;

				if ( entry->is_integral )
				{
					std::snprintf( e.value, sizeof( e.value ), "%d", static_cast< int >( std::roundf( b.value ) ) );
				}
				else
				{
					std::snprintf( e.value, sizeof( e.value ), "%.1f", b.value );
				}
				e.has_value_pill = true;
			}
		}

		// Keep the header visible as a preview while the menu is open.
		if ( count > 0 || g_menu.is_open( ) )
			container_alpha.fade_in( 0.2f );
		else
			container_alpha.fade_out( 0.2f );

		container_alpha.update( );
		if ( !container_alpha.visible( ) )
		{
			this->m_keybinds_hovered = false;
			this->m_keybinds_dragging = false;
			return;
		}

		const auto master_alpha = container_alpha.alpha( );
		const auto master_u8 = static_cast< std::uint8_t >( 255.0f * master_alpha );

		constexpr auto header_h{ 30.0f };
		constexpr auto row_h{ 24.0f };
		const auto [header_tw, header_th] = xdraw::measure_text( "Keybinds" );

		float max_w = 200.0f;
		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			const auto [nw, nh] = xdraw::measure_text( e.name );
			const auto [vw, vh] = xdraw::measure_text( e.value );
			const bool has_key = ( e.key[ 0 ] != '\0' );
			const auto [kw, kh] = has_key ? xdraw::measure_text( e.key ) : std::pair{ 0.0f, 0.0f };
			const float key_w = has_key ? ( kw + 10.0f + 4.0f ) : 0.0f;
			const float row_w = 15.0f + 8.0f + nw + 18.0f + ( vw + 10.0f ) + key_w + 12.0f;
			if ( row_w > max_w )
			{
				max_w = row_w;
			}
		}

		const auto body_h = ( count > 0 )
			? ( static_cast< float >( count ) * row_h + 6.0f )
			: ( g_menu.is_open( ) ? 28.0f : 0.0f );
		const auto total_h = header_h + body_h;

		auto& widgets_cfg = settings::g_misc.m_widgets;
		const auto max_screen_x = std::max( 0.0f, static_cast< float >( screen_w ) - max_w );
		const auto max_screen_y = std::max( 0.0f, static_cast< float >( screen_h ) - total_h );

		float current_x = widgets_cfg.keybinds_x.value;
		float current_y = widgets_cfg.keybinds_y.value;
		const bool has_custom_pos = ( current_x >= 0.0f && current_y >= 0.0f );

		const auto target_base_y = ( static_cast< float >( screen_h ) * 0.5f ) - ( total_h * 0.5f );
		if ( !has_custom_pos )
		{
			smoothed_base_y.set_target( target_base_y );
			smoothed_base_y.update( );
			current_x = margin;
			current_y = smoothed_base_y.value( );
		}
		else
		{
			smoothed_base_y.snap( current_y );
			current_x = std::clamp( current_x, 0.0f, max_screen_x );
			current_y = std::clamp( current_y, 0.0f, max_screen_y );
		}

		// Drag handling when menu is open
		static bool s_last_mouse_down{ false };
		static bool s_is_dragging{ false };
		static float s_drag_offset_x{ 0.0f };
		static float s_drag_offset_y{ 0.0f };

		const auto& input = xui::ctx( ).input;
		const bool lbutton_phys = ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0;
		const bool mouse_down = input.mouse_down && lbutton_phys;
		const bool mouse_clicked = mouse_down && !s_last_mouse_down;
		const bool menu_open = g_menu.is_open( );

		const xui::rect widget_rect{ current_x, current_y, max_w, total_h };
		const bool hovered = menu_open && widget_rect.contains( input.mouse_x, input.mouse_y );

		this->m_keybinds_hovered = hovered;

		if ( !menu_open || !mouse_down )
		{
			s_is_dragging = false;
		}

		const bool can_start_drag = menu_open
			&& !this->m_spectators_dragging
			&& !xui::ctx( ).overlay_blocking( )
			&& xui::ctx( ).active_window == xui::null_id
			&& xui::ctx( ).active_slider == xui::null_id
			&& xui::ctx( ).active_resize == xui::null_id
			&& xui::ctx( ).active_text_input == xui::null_id
			&& xui::ctx( ).active_child_scroll == xui::null_id;

		if ( can_start_drag && hovered && mouse_clicked )
		{
			s_is_dragging = true;
			s_drag_offset_x = input.mouse_x - current_x;
			s_drag_offset_y = input.mouse_y - current_y;
		}

		if ( s_is_dragging )
		{
			current_x = std::clamp( input.mouse_x - s_drag_offset_x, 0.0f, max_screen_x );
			current_y = std::clamp( input.mouse_y - s_drag_offset_y, 0.0f, max_screen_y );

			widgets_cfg.keybinds_x = current_x;
			widgets_cfg.keybinds_y = current_y;
		}

		this->m_keybinds_dragging = s_is_dragging;
		s_last_mouse_down = mouse_down;

		const auto x = current_x;
		const auto base_ry = current_y;

		// Soft modern drop shadow
		draw_list.rect_filled( x - 2.0f, base_ry + 3.0f, max_w + 4.0f, total_h + 3.0f, xdraw::color{ 0, 0, 0, static_cast< std::uint8_t >( 35.0f * master_alpha ) }, xdraw::corner_radius{ 11.0f } );
		draw_list.rect_filled( x - 1.0f, base_ry + 1.5f, max_w + 2.0f, total_h + 1.5f, xdraw::color{ 0, 0, 0, static_cast< std::uint8_t >( 55.0f * master_alpha ) }, xdraw::corner_radius{ 10.0f } );

		// Floating glass capsule container
		const auto card_r = xdraw::corner_radius{ 9.0f };
		draw_list.rect_filled_blurred( x, base_ry, max_w, total_h, card_r, xdraw::color{ 255, 255, 255, master_u8 } );
		draw_list.rect_filled( x, base_ry, max_w, total_h, tokens::col_dark.alpha( static_cast< std::uint8_t >( 225.0f * master_alpha ) ), card_r );

		const auto border_col = ( menu_open && ( hovered || s_is_dragging ) )
			? s.accent.alpha( static_cast< std::uint8_t >( ( s_is_dragging ? 220.0f : 140.0f ) * master_alpha ) )
			: tokens::col_border.alpha( static_cast< std::uint8_t >( 115.0f * master_alpha ) );
		draw_list.rect( x, base_ry, max_w, total_h, border_col, card_r, 1.0f );

		// Top subtle ambient neon reflection line
		const auto half_w = ( max_w - 24.0f ) * 0.5f;
		draw_list.rect_filled_gradient(
			x + 12.0f, base_ry, half_w, 1.2f,
			s.accent.alpha( 0 ),
			s.accent.alpha( static_cast< std::uint8_t >( 170.0f * master_alpha ) ),
			s.accent.alpha( static_cast< std::uint8_t >( 170.0f * master_alpha ) ),
			s.accent.alpha( 0 )
		);
		draw_list.rect_filled_gradient(
			x + 12.0f + half_w, base_ry, half_w, 1.2f,
			s.accent.alpha( static_cast< std::uint8_t >( 170.0f * master_alpha ) ),
			s.accent.alpha( 0 ),
			s.accent.alpha( 0 ),
			s.accent.alpha( static_cast< std::uint8_t >( 170.0f * master_alpha ) )
		);

		// Modern glowing brand indicator on left of header
		const auto dot_cx = x + 16.0f;
		const auto dot_cy = base_ry + header_h * 0.5f;
		draw_list.circle_filled( dot_cx, dot_cy, 5.0f, s.accent.alpha( static_cast< std::uint8_t >( 45.0f * master_alpha ) ) );
		draw_list.circle_filled( dot_cx, dot_cy, 2.5f, s.accent.alpha( static_cast< std::uint8_t >( 230.0f * master_alpha ) ) );
		draw_list.circle_filled( dot_cx, dot_cy, 1.0f, xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 240.0f * master_alpha ) } );

		// Header title
		const auto title_x = dot_cx + 9.0f;
		const auto title_y = base_ry + ( header_h - header_th ) * 0.5f - 0.5f;
		draw_list.text( title_x, title_y, "Keybinds", tokens::col_text.alpha( static_cast< std::uint8_t >( 245.0f * master_alpha ) ), g_fonts.inter_bold[ fonts::size::petite ] );

		// Header count capsule pill on right
		if ( count > 0 )
		{
			char count_str[ 16 ]{};
			std::snprintf( count_str, sizeof( count_str ), "%d", count );
			const auto [ cw, ch ] = xdraw::measure_text( count_str );
			const auto count_pill_w = cw + 10.0f;
			const auto count_pill_h = 16.0f;
			const auto cpx = x + max_w - count_pill_w - 10.0f;
			const auto cpy = base_ry + ( header_h - count_pill_h ) * 0.5f;

			draw_list.rect_filled( cpx, cpy, count_pill_w, count_pill_h, s.accent.alpha( static_cast< std::uint8_t >( 25.0f * master_alpha ) ), xdraw::corner_radius{ 8.0f } );
			draw_list.rect( cpx, cpy, count_pill_w, count_pill_h, s.accent.alpha( static_cast< std::uint8_t >( 85.0f * master_alpha ) ), xdraw::corner_radius{ 8.0f }, 1.0f );
			draw_list.text( cpx + 5.0f, cpy + ( count_pill_h - ch ) * 0.5f - 0.5f, count_str, s.accent.alpha( static_cast< std::uint8_t >( 255.0f * master_alpha ) ) );
		}

		// Divider below header with smooth edge fadeout
		if ( total_h > header_h )
		{
			const auto div_y = base_ry + header_h;
			const auto div_w = max_w - 20.0f;
			const auto div_half = div_w * 0.5f;
			const auto div_col = tokens::col_border.alpha( static_cast< std::uint8_t >( 90.0f * master_alpha ) );
			const auto div_clear = tokens::col_border.alpha( 0 );
			draw_list.rect_filled_gradient( x + 10.0f, div_y, div_half, 1.0f, div_clear, div_col, div_col, div_clear );
			draw_list.rect_filled_gradient( x + 10.0f + div_half, div_y, div_half, 1.0f, div_col, div_clear, div_clear, div_col );
		}

		// Rows
		if ( count == 0 && g_menu.is_open( ) )
		{
			const auto empty_text = "No active binds";
			const auto [ ew, eh ] = xdraw::measure_text( empty_text );
			draw_list.text( x + ( max_w - ew ) * 0.5f, base_ry + header_h + ( 28.0f - eh ) * 0.5f, empty_text, tokens::col_text_dim.alpha( static_cast< std::uint8_t >( 135.0f * master_alpha ) ) );
		}
		else
		{
			float current_offset_y = header_h + 3.0f;
			for ( auto i = 0; i < count; ++i )
			{
				const auto& e = entries[ i ];
				const auto row_y = base_ry + current_offset_y;
				const auto [ nw, nh ] = xdraw::measure_text( e.name );
				const auto [ vw, vh ] = xdraw::measure_text( e.value );

				// Subtle hover/row background
				draw_list.rect_filled( x + 6.0f, row_y, max_w - 12.0f, row_h - 2.0f, tokens::col_card.alpha( static_cast< std::uint8_t >( 45.0f * master_alpha ) ), xdraw::corner_radius{ 5.0f } );

				// Status indicator pip on left
				const auto pip_x = x + 15.0f;
				const auto pip_y = row_y + ( row_h - 2.0f ) * 0.5f;
				if ( e.mode == xui::bind_mode::hold_off )
				{
					draw_list.circle_filled( pip_x, pip_y, 1.8f, tokens::col_text_dim.alpha( static_cast< std::uint8_t >( 100.0f * master_alpha ) ) );
				}
				else
				{
					draw_list.circle_filled( pip_x, pip_y, 3.2f, s.accent.alpha( static_cast< std::uint8_t >( 45.0f * master_alpha ) ) );
					draw_list.circle_filled( pip_x, pip_y, 1.8f, s.accent.alpha( static_cast< std::uint8_t >( 240.0f * master_alpha ) ) );
				}

				// Name text on left
				draw_list.text( pip_x + 8.0f, row_y + ( ( row_h - 2.0f ) - nh ) * 0.5f - 0.5f, e.name, tokens::col_text.alpha( static_cast< std::uint8_t >( 235.0f * master_alpha ) ) );

				// Badges on right: Keycap badge on far right, mode badge immediately to its left
				const auto badge_h = 16.0f;
				const auto badge_r = xdraw::corner_radius{ 4.0f };
				const auto by = row_y + ( ( row_h - 2.0f ) - badge_h ) * 0.5f;

				const bool has_key = ( e.key[ 0 ] != '\0' );
				const auto [kw, kh] = has_key ? xdraw::measure_text( e.key ) : std::pair{ 0.0f, 0.0f };
				float current_right_x = x + max_w - 10.0f;

				if ( has_key )
				{
					const auto key_w = kw + 10.0f;
					const auto kx = current_right_x - key_w;
					draw_list.rect_filled( kx, by, key_w, badge_h, tokens::col_elevated.alpha( static_cast< std::uint8_t >( 190.0f * master_alpha ) ), badge_r );
					draw_list.rect( kx, by, key_w, badge_h, tokens::col_border.alpha( static_cast< std::uint8_t >( 120.0f * master_alpha ) ), badge_r, 1.0f );
					draw_list.text( kx + 5.0f, by + ( badge_h - kh ) * 0.5f - 0.5f, e.key, tokens::col_text.alpha( static_cast< std::uint8_t >( 240.0f * master_alpha ) ) );
					current_right_x = kx - 4.0f;
				}

				const auto badge_w = vw + 10.0f;
				const auto bx = current_right_x - badge_w;

				if ( e.has_value_pill )
				{
					draw_list.rect_filled( bx, by, badge_w, badge_h, s.accent.alpha( static_cast< std::uint8_t >( 35.0f * master_alpha ) ), badge_r );
					draw_list.rect( bx, by, badge_w, badge_h, s.accent.alpha( static_cast< std::uint8_t >( 110.0f * master_alpha ) ), badge_r, 1.0f );
					draw_list.text( bx + 5.0f, by + ( badge_h - vh ) * 0.5f - 0.5f, e.value, s.accent.alpha( static_cast< std::uint8_t >( 255.0f * master_alpha ) ) );
				}
				else if ( e.mode == xui::bind_mode::hold_off )
				{
					draw_list.rect_filled( bx, by, badge_w, badge_h, tokens::col_card.alpha( static_cast< std::uint8_t >( 160.0f * master_alpha ) ), badge_r );
					draw_list.rect( bx, by, badge_w, badge_h, tokens::col_border.alpha( static_cast< std::uint8_t >( 80.0f * master_alpha ) ), badge_r, 1.0f );
					draw_list.text( bx + 5.0f, by + ( badge_h - vh ) * 0.5f - 0.5f, e.value, tokens::col_text_dim.alpha( static_cast< std::uint8_t >( 170.0f * master_alpha ) ) );
				}
				else
				{
					draw_list.rect_filled( bx, by, badge_w, badge_h, s.accent.alpha( static_cast< std::uint8_t >( 28.0f * master_alpha ) ), badge_r );
					draw_list.rect( bx, by, badge_w, badge_h, s.accent.alpha( static_cast< std::uint8_t >( 95.0f * master_alpha ) ), badge_r, 1.0f );
					draw_list.text( bx + 5.0f, by + ( badge_h - vh ) * 0.5f - 0.5f, e.value, s.accent.alpha( static_cast< std::uint8_t >( 255.0f * master_alpha ) ) );
				}

				current_offset_y += row_h;
			}
		}
	}

	void widgets::spectators( xdraw::draw_list& draw_list )
	{
		struct avatar_cache
		{
			struct entry
			{
				Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture{};
				bool attempted{};
			};

			std::unordered_map<std::uintptr_t, entry> m_entries{};

			[[nodiscard]] ID3D11ShaderResourceView* get( std::uintptr_t steam_id )
			{
				auto it = this->m_entries.find( steam_id );
				if ( it != this->m_entries.end( ) )
				{
					return it->second.texture.Get( );
				}

				auto& e = this->m_entries[ steam_id ];
				e.attempted = true;

				const auto image_handle = steam::friends::get_medium_friend_avatar( steam_id );
				if ( image_handle <= 0 )
				{
					return nullptr;
				}

				std::uint32_t w{}, h{};
				if ( !steam::utils::get_image_size( image_handle, &w, &h ) || !w || !h )
				{
					return nullptr;
				}

				std::vector<std::uint8_t> rgba( w * h * 4 );
				if ( !steam::utils::get_image_rgba( image_handle, rgba.data( ), static_cast< int >( rgba.size( ) ) ) )
				{
					return nullptr;
				}

				e.texture = xdraw::create_srv_from_rgba( rgba.data( ), static_cast< int >( w ), static_cast< int >( h ) );
				return e.texture.Get( );
			}

			void clear( )
			{
				this->m_entries.clear( );
			}
		};

		static avatar_cache avatars{};

		struct row_anim_t
		{
			animation::fade alpha;
			animation::spring offset_y;
			bool active_this_frame{ false };
		};

		static std::map<std::string, row_anim_t> row_states;
		static animation::fade container_alpha;
		static animation::spring smoothed_base_y;

		const auto [screen_w, screen_h] = xdraw::viewport_size( );
		const auto& s = xui::ctx( ).style;

		constexpr auto margin{ 10.0f };

		struct spectator_entry
		{
			char name[ 128 ];
			std::uintptr_t steam_id;
		};

		spectator_entry entries[ 32 ]{};
		auto count{ 0 };

		const auto local = systems::g_local.get( );
		const auto game_rules = memory::read<std::uintptr_t>( addresses::globals::game_rules );
		const bool game_in_progress = game_rules && memory::read<int>( game_rules + SCHEMA( "C_CSGameRules", "m_gamePhase"_hash ) ) < 4;

		if ( local.is_valid( ) && systems::g_entities.exists( local.view_controller( ) ) && game_in_progress )
		{
			const auto local_controller = local.controller;
			const auto view_controller = local.view_controller( );
			const auto view_pawn = local.view_pawn( );
			if ( view_pawn )
			{
				for ( const auto& player : systems::g_entities.get_by_type( systems::entities::type::player ) )
				{
					if ( player.ptr == view_controller || player.ptr == local_controller || count >= 32 )
					{
						continue;
					}

					if ( memory::read<bool>( player.ptr + SCHEMA( "CCSPlayerController", "m_bPawnIsAlive"_hash ) ) )
					{
						continue;
					}

					const auto obs_pawn_handle = memory::read<std::uint32_t>( player.ptr + SCHEMA( "CCSPlayerController", "m_hObserverPawn"_hash ) );
					if ( !obs_pawn_handle || obs_pawn_handle == 0xffffffff )
					{
						continue;
					}

					const auto obs_pawn = systems::g_entities.lookup( obs_pawn_handle );
					if ( !obs_pawn )
					{
						continue;
					}

					const auto observer_services = memory::safe_read<std::uintptr_t>( obs_pawn + SCHEMA( "C_BasePlayerPawn", "m_pObserverServices"_hash ) ).value_or( 0 );
					if ( !observer_services || ( observer_services >> 48 ) != 0 )
					{
						continue;
					}

					const auto observer_target_handle = memory::safe_read<std::uint32_t>( observer_services + SCHEMA( "CPlayer_ObserverServices", "m_hObserverTarget"_hash ) ).value_or( 0 );
					if ( !observer_target_handle )
					{
						continue;
					}

					const auto observer_target = systems::g_entities.lookup( observer_target_handle );
					if ( observer_target != view_pawn )
					{
						continue;
					}

					const auto name_ptr = memory::read<std::uintptr_t>( player.ptr + SCHEMA( "CCSPlayerController", "m_sSanitizedPlayerName"_hash ) );
					if ( !name_ptr )
					{
						continue;
					}

					auto name = memory::read_string( name_ptr, 127 );
					std::ranges::transform( name, name.begin( ), [ ]( unsigned char c ) { return std::tolower( c ); } );

					auto& e = entries[ count++ ];
					strncpy_s( e.name, name.c_str( ), sizeof( e.name ) - 1 );
					e.name[ sizeof( e.name ) - 1 ] = '\0';
					e.steam_id = memory::read<std::uintptr_t>( player.ptr + SCHEMA( "CBasePlayerController", "m_steamID"_hash ) );
				}
			}
		}

		if ( count > 0 || g_menu.is_open( ) )
			container_alpha.fade_in( 0.2f );
		else
			container_alpha.fade_out( 0.2f );

		container_alpha.update( );
		if ( !container_alpha.visible( ) )
		{
			this->m_spectators_hovered = false;
			this->m_spectators_dragging = false;
			return;
		}

		const auto master_alpha = container_alpha.alpha( );
		const auto master_u8 = static_cast< std::uint8_t >( 255.0f * master_alpha );

		constexpr auto header_h{ 30.0f };
		constexpr auto row_h{ 25.0f };
		const auto [header_tw, header_th] = xdraw::measure_text( "Spectators" );

		float max_w = 195.0f;
		for ( auto i = 0; i < count; ++i )
		{
			const auto& e = entries[ i ];
			const auto [nw, nh] = xdraw::measure_text( e.name );
			const float row_w = 14.0f + 18.0f + 8.0f + nw + 16.0f + 36.0f + 14.0f;
			if ( row_w > max_w )
			{
				max_w = row_w;
			}
		}

		const auto body_h = ( count > 0 )
			? ( static_cast< float >( count ) * row_h + 6.0f )
			: ( g_menu.is_open( ) ? 28.0f : 0.0f );
		const auto total_h = header_h + body_h;

		auto& widgets_cfg = settings::g_misc.m_widgets;
		const auto max_screen_x = std::max( 0.0f, static_cast< float >( screen_w ) - max_w );
		const auto max_screen_y = std::max( 0.0f, static_cast< float >( screen_h ) - total_h );

		float current_x = widgets_cfg.spectator_x.value;
		float current_y = widgets_cfg.spectator_y.value;
		const bool has_custom_pos = ( current_x >= 0.0f && current_y >= 0.0f );

		const auto target_base_y = ( static_cast< float >( screen_h ) * 0.5f ) - ( total_h * 0.5f );
		if ( !has_custom_pos )
		{
			smoothed_base_y.set_target( target_base_y );
			smoothed_base_y.update( );
			current_x = std::max( 0.0f, static_cast< float >( screen_w ) - max_w - margin );
			current_y = smoothed_base_y.value( );
		}
		else
		{
			smoothed_base_y.snap( current_y );
			current_x = std::clamp( current_x, 0.0f, max_screen_x );
			current_y = std::clamp( current_y, 0.0f, max_screen_y );
		}

		// Drag handling when menu is open
		static bool s_last_mouse_down{ false };
		static bool s_is_dragging{ false };
		static float s_drag_offset_x{ 0.0f };
		static float s_drag_offset_y{ 0.0f };

		const auto& input = xui::ctx( ).input;
		const bool lbutton_phys = ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) != 0;
		const bool mouse_down = input.mouse_down && lbutton_phys;
		const bool mouse_clicked = mouse_down && !s_last_mouse_down;
		const bool menu_open = g_menu.is_open( );

		const xui::rect widget_rect{ current_x, current_y, max_w, total_h };
		const bool hovered = menu_open && widget_rect.contains( input.mouse_x, input.mouse_y );

		this->m_spectators_hovered = hovered;

		if ( !menu_open || !mouse_down )
		{
			s_is_dragging = false;
		}

		const bool can_start_drag = menu_open
			&& !this->m_keybinds_dragging
			&& !xui::ctx( ).overlay_blocking( )
			&& xui::ctx( ).active_window == xui::null_id
			&& xui::ctx( ).active_slider == xui::null_id
			&& xui::ctx( ).active_resize == xui::null_id
			&& xui::ctx( ).active_text_input == xui::null_id
			&& xui::ctx( ).active_child_scroll == xui::null_id;

		if ( can_start_drag && hovered && mouse_clicked )
		{
			s_is_dragging = true;
			s_drag_offset_x = input.mouse_x - current_x;
			s_drag_offset_y = input.mouse_y - current_y;
		}

		if ( s_is_dragging )
		{
			current_x = std::clamp( input.mouse_x - s_drag_offset_x, 0.0f, max_screen_x );
			current_y = std::clamp( input.mouse_y - s_drag_offset_y, 0.0f, max_screen_y );

			widgets_cfg.spectator_x = current_x;
			widgets_cfg.spectator_y = current_y;
		}

		this->m_spectators_dragging = s_is_dragging;
		s_last_mouse_down = mouse_down;

		const auto x = current_x;
		const auto base_ry = current_y;

		// Soft modern drop shadow
		draw_list.rect_filled( x - 2.0f, base_ry + 3.0f, max_w + 4.0f, total_h + 3.0f, xdraw::color{ 0, 0, 0, static_cast< std::uint8_t >( 35.0f * master_alpha ) }, xdraw::corner_radius{ 11.0f } );
		draw_list.rect_filled( x - 1.0f, base_ry + 1.5f, max_w + 2.0f, total_h + 1.5f, xdraw::color{ 0, 0, 0, static_cast< std::uint8_t >( 55.0f * master_alpha ) }, xdraw::corner_radius{ 10.0f } );

		// Floating glass capsule container
		const auto card_r = xdraw::corner_radius{ 9.0f };
		draw_list.rect_filled_blurred( x, base_ry, max_w, total_h, card_r, xdraw::color{ 255, 255, 255, master_u8 } );
		draw_list.rect_filled( x, base_ry, max_w, total_h, tokens::col_dark.alpha( static_cast< std::uint8_t >( 225.0f * master_alpha ) ), card_r );

		const auto border_col = ( menu_open && ( hovered || s_is_dragging ) )
			? s.accent.alpha( static_cast< std::uint8_t >( ( s_is_dragging ? 220.0f : 140.0f ) * master_alpha ) )
			: tokens::col_border.alpha( static_cast< std::uint8_t >( 115.0f * master_alpha ) );
		draw_list.rect( x, base_ry, max_w, total_h, border_col, card_r, 1.0f );

		// Top subtle ambient neon reflection line
		const auto half_w = ( max_w - 24.0f ) * 0.5f;
		draw_list.rect_filled_gradient(
			x + 12.0f, base_ry, half_w, 1.2f,
			s.accent.alpha( 0 ),
			s.accent.alpha( static_cast< std::uint8_t >( 170.0f * master_alpha ) ),
			s.accent.alpha( static_cast< std::uint8_t >( 170.0f * master_alpha ) ),
			s.accent.alpha( 0 )
		);
		draw_list.rect_filled_gradient(
			x + 12.0f + half_w, base_ry, half_w, 1.2f,
			s.accent.alpha( static_cast< std::uint8_t >( 170.0f * master_alpha ) ),
			s.accent.alpha( 0 ),
			s.accent.alpha( 0 ),
			s.accent.alpha( static_cast< std::uint8_t >( 170.0f * master_alpha ) )
		);

		// Modern glowing brand indicator on left of header
		const auto dot_cx = x + 16.0f;
		const auto dot_cy = base_ry + header_h * 0.5f;
		draw_list.circle_filled( dot_cx, dot_cy, 5.0f, s.accent.alpha( static_cast< std::uint8_t >( 45.0f * master_alpha ) ) );
		draw_list.circle_filled( dot_cx, dot_cy, 2.5f, s.accent.alpha( static_cast< std::uint8_t >( 230.0f * master_alpha ) ) );
		draw_list.circle_filled( dot_cx, dot_cy, 1.0f, xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 240.0f * master_alpha ) } );

		// Header title
		const auto title_x = dot_cx + 9.0f;
		const auto title_y = base_ry + ( header_h - header_th ) * 0.5f - 0.5f;
		draw_list.text( title_x, title_y, "Spectators", tokens::col_text.alpha( static_cast< std::uint8_t >( 245.0f * master_alpha ) ), g_fonts.inter_bold[ fonts::size::petite ] );

		// Header count capsule pill on right
		if ( count > 0 )
		{
			char count_str[ 16 ]{};
			std::snprintf( count_str, sizeof( count_str ), "%d", count );
			const auto [ cw, ch ] = xdraw::measure_text( count_str );
			const auto count_pill_w = cw + 10.0f;
			const auto count_pill_h = 16.0f;
			const auto cpx = x + max_w - count_pill_w - 10.0f;
			const auto cpy = base_ry + ( header_h - count_pill_h ) * 0.5f;

			draw_list.rect_filled( cpx, cpy, count_pill_w, count_pill_h, s.accent.alpha( static_cast< std::uint8_t >( 25.0f * master_alpha ) ), xdraw::corner_radius{ 8.0f } );
			draw_list.rect( cpx, cpy, count_pill_w, count_pill_h, s.accent.alpha( static_cast< std::uint8_t >( 85.0f * master_alpha ) ), xdraw::corner_radius{ 8.0f }, 1.0f );
			draw_list.text( cpx + 5.0f, cpy + ( count_pill_h - ch ) * 0.5f - 0.5f, count_str, s.accent.alpha( static_cast< std::uint8_t >( 255.0f * master_alpha ) ) );
		}

		// Divider below header with smooth edge fadeout
		if ( total_h > header_h )
		{
			const auto div_y = base_ry + header_h;
			const auto div_w = max_w - 20.0f;
			const auto div_half = div_w * 0.5f;
			const auto div_col = tokens::col_border.alpha( static_cast< std::uint8_t >( 90.0f * master_alpha ) );
			const auto div_clear = tokens::col_border.alpha( 0 );
			draw_list.rect_filled_gradient( x + 10.0f, div_y, div_half, 1.0f, div_clear, div_col, div_col, div_clear );
			draw_list.rect_filled_gradient( x + 10.0f + div_half, div_y, div_half, 1.0f, div_col, div_clear, div_clear, div_col );
		}

		// Rows
		if ( count == 0 && g_menu.is_open( ) )
		{
			const auto empty_text = "No spectators";
			const auto [ ew, eh ] = xdraw::measure_text( empty_text );
			draw_list.text( x + ( max_w - ew ) * 0.5f, base_ry + header_h + ( 28.0f - eh ) * 0.5f, empty_text, tokens::col_text_dim.alpha( static_cast< std::uint8_t >( 135.0f * master_alpha ) ) );
		}
		else
		{
			float current_offset_y = header_h + 3.0f;
			for ( auto i = 0; i < count; ++i )
			{
				const auto& e = entries[ i ];
				const auto row_y = base_ry + current_offset_y;
				const auto [ nw, nh ] = xdraw::measure_text( e.name );
				const auto avatar_tex = avatars.get( e.steam_id );
				constexpr auto av_size = 18.0f;
				const auto av_x = x + 10.0f;
				const auto av_y = row_y + ( ( row_h - 2.0f ) - av_size ) * 0.5f;

				// Subtle row background
				draw_list.rect_filled( x + 6.0f, row_y, max_w - 12.0f, row_h - 2.0f, tokens::col_card.alpha( static_cast< std::uint8_t >( 45.0f * master_alpha ) ), xdraw::corner_radius{ 5.0f } );

				// Circular avatar
				if ( avatar_tex )
				{
					draw_list.image( av_x, av_y, av_size, av_size, avatar_tex, xdraw::corner_radius{ av_size * 0.5f }, xdraw::color{ 255, 255, 255, static_cast< std::uint8_t >( 255.0f * master_alpha ) } );
				}
				else
				{
					// Sleek circular avatar placeholder
					const auto cx_av = av_x + av_size * 0.5f;
					const auto cy_av = av_y + av_size * 0.5f;
					draw_list.circle_filled( cx_av, cy_av, av_size * 0.5f, tokens::col_elevated.alpha( static_cast< std::uint8_t >( 210.0f * master_alpha ) ) );
					draw_list.circle( cx_av, cy_av, av_size * 0.5f, tokens::col_border.alpha( static_cast< std::uint8_t >( 110.0f * master_alpha ) ), 1.0f );
					draw_list.circle_filled( cx_av, cy_av - 2.2f, 2.6f, s.accent.alpha( static_cast< std::uint8_t >( 190.0f * master_alpha ) ) );
					draw_list.circle_filled( cx_av, cy_av + 4.8f, 3.8f, s.accent.alpha( static_cast< std::uint8_t >( 140.0f * master_alpha ) ) );
				}

				// Name text
				draw_list.text( av_x + av_size + 8.0f, row_y + ( ( row_h - 2.0f ) - nh ) * 0.5f - 0.5f, e.name, tokens::col_text.alpha( static_cast< std::uint8_t >( 235.0f * master_alpha ) ) );

				// SPEC badge on right
				const auto badge_h = 16.0f;
				const auto badge_w = 34.0f;
				const auto bx = x + max_w - badge_w - 10.0f;
				const auto by = row_y + ( ( row_h - 2.0f ) - badge_h ) * 0.5f;
				const auto badge_r = xdraw::corner_radius{ 4.0f };

				draw_list.rect_filled( bx, by, badge_w, badge_h, s.accent.alpha( static_cast< std::uint8_t >( 25.0f * master_alpha ) ), badge_r );
				draw_list.rect( bx, by, badge_w, badge_h, s.accent.alpha( static_cast< std::uint8_t >( 85.0f * master_alpha ) ), badge_r, 1.0f );
				const auto [ sw, sh ] = xdraw::measure_text( "SPEC" );
				draw_list.text( bx + ( badge_w - sw ) * 0.5f, by + ( badge_h - sh ) * 0.5f - 0.5f, "SPEC", s.accent.alpha( static_cast< std::uint8_t >( 245.0f * master_alpha ) ) );

				current_offset_y += row_h;
			}
		}
	}

} // namespace rendering
