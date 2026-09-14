#include <pch/pch.hpp>
#include <core/settings.hpp>

#include "../../rendering.hpp"
#include "menu.weapons.hpp"

namespace rendering {

	namespace detail {

		constexpr const char* hitbox_names_legit[ ]{ "head", "chest", "stomach", "arms", "legs" };
		using menu_weapons::weapon_sel_legit;

	} // namespace detail

	void menu::draw_legitbot( float group_w ) const
	{
		auto& s = settings::g_combat;
		auto& lb = s.m_legitbot;

		const auto is_custom_wep = (menu_weapons::weapon_sel_legit.weapon_flat_idx >= 0 &&
			menu_weapons::weapon_sel_legit.weapon_flat_idx < static_cast<int>(cstypes::weapons::k_total_weapons));

		auto& wg = is_custom_wep
			? lb.weapons[menu_weapons::weapon_sel_legit.weapon_flat_idx].cfg
			: lb.groups[std::clamp(menu_weapons::weapon_sel_legit.group_idx, 0, 5)];

		const auto sel_scope_id = is_custom_wep
			? ( static_cast< std::uintptr_t >( 0x4C425F57 ) + static_cast< std::uintptr_t >( menu_weapons::weapon_sel_legit.weapon_flat_idx ) )
			: ( static_cast< std::uintptr_t >( 0x4C425F47 ) + static_cast< std::uintptr_t >( std::clamp( menu_weapons::weapon_sel_legit.group_idx, 0, 5 ) ) );

		const auto wx = this->m_x;
		const auto wy = this->m_y;
		const auto content_x = wx + tokens::gap + menu::k_sidebar_w + tokens::gap;
		const auto body_y = wy + tokens::gap + tokens::subtab_bar_h + tokens::gap;
		const auto body_h = this->m_body_h;
		const auto content_w = this->m_w - tokens::gap * 2.0f - menu::k_sidebar_w - tokens::gap;
		const auto col_w = ( content_w - tokens::gap ) * 0.5f;
		const auto right_x = content_x + col_w + tokens::gap;

		constexpr float k_header_h = 22.0f;
		auto draw_col_title = [&]( float x, const char* title ) {
			auto& dl = xui::draw::current( );
			xdraw::push_font( rendering::g_fonts.inter_bold[ rendering::fonts::size::petite ] );
			dl.text( x + 2.0f, body_y + 2.0f, title, tokens::col_text );
			xdraw::pop_font( );
		};

		// LEFT COLUMN: LEGITBOT MAIN
		draw_col_title( content_x, "LEGITBOT MAIN" );
		xui::layout::set_cursor( content_x - wx, body_y + k_header_h - wy );

		if ( xui::begin_child( "##legitbot_main", col_w, body_h - k_header_h, true ) )
		{
			xui::toggle( "Enable Legitbot", lb.enabled );
			xui::layout::spacing( 3.0f );
			menu_weapons::draw_selector( "##lb_weapon_select", menu_weapons::weapon_sel_legit, true );
			if ( is_custom_wep )
			{
				auto& ow = lb.weapons[ menu_weapons::weapon_sel_legit.weapon_flat_idx ];

				if ( !ow.override_group.value )
				{
					bool has_bind = false;
					for ( const auto s : { &ow.cfg.aimbot, &ow.cfg.rcs, &ow.cfg.standalone_rcs, &ow.cfg.triggerbot, &ow.cfg.trigger_head_only, &ow.cfg.give_me_your_seed, &ow.cfg.autowall, &ow.cfg.smoke_check, &ow.cfg.scope_check, &ow.cfg.flash_check, &ow.cfg.ground_check, &ow.cfg.visualize_fov } )
					{
						if ( s->bind.key != 0 ) { has_bind = true; break; }
					}
					if ( !has_bind )
					{
						for ( const auto ptr : { ( void* )&ow.cfg.fov.value, ( void* )&ow.cfg.smooth.value, ( void* )&ow.cfg.rcs_min.value, ( void* )&ow.cfg.rcs_max.value, ( void* )&ow.cfg.trigger_delay.value, ( void* )&ow.cfg.trigger_hitchance.value, ( void* )&ow.cfg.standalone_rcs_strength.value, ( void* )&ow.cfg.standalone_rcs_min.value, ( void* )&ow.cfg.standalone_rcs_max.value, ( void* )&ow.cfg.min_damage.value } )
						{
							if ( auto* sb = xui::slider_binds::find_by_ptr( ptr ) )
							{
								if ( sb->count > 0 ) { has_bind = true; break; }
							}
						}
					}
					if ( has_bind )
					{
						ow.override_group.value = true;
					}
				}

				xui::layout::spacing( 2.0f );
				xui::toggle( "Custom Weapon Settings", ow.override_group );
				if ( !ow.override_group.value )
				{
					xui::layout::spacing( 2.0f );
					if ( xui::button( "Copy Group Settings##lb" ) )
					{
						ow.cfg.copy_values_from( lb.groups[ std::clamp( menu_weapons::weapon_sel_legit.group_idx, 0, 5 ) ] );
						ow.override_group.value = true;
					}
				}
			}

			xui::push_id( sel_scope_id );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Aimbot", wg.aimbot );
			xui::layout::spacing( 3.0f );
			xui::slider_float( "Target FOV", wg.fov, 0.5f, 30.0f, "%.1f°" );
			xui::layout::spacing( 3.0f );
			xui::slider_int( "Smoothness", wg.smooth, 0, 100, "%d" );
			xui::layout::spacing( 4.0f );
			xui::multicombo( "Hitboxes", wg.hitboxes, detail::hitbox_names_legit, 5 );

			xui::layout::spacing( 6.0f );
			xui::toggle( "Draw FOV", wg.visualize_fov );
			if ( xui::begin_popup( "##fov_color_popup", 220.0f ) )
			{
				xui::color_picker( "color##fov", wg.fov_color );
				xui::end_popup( );
			}

			xui::layout::spacing( 6.0f );
			xui::toggle( "Recoil Control (RCS)", wg.rcs );
			if ( xui::begin_popup( "##rcs_popup", 220.0f ) )
			{
				xui::slider_int( "min##rcs", wg.rcs_min, 50, 150, "%d%%" );
				xui::slider_int( "max##rcs", wg.rcs_max, 50, 150, "%d%%" );
				xui::end_popup( );
			}
			xui::pop_id( );

			xui::end_child( );
		}

		const bool is_sniper = is_custom_wep
			? ( menu_weapons::weapon_sel_legit.weapon_flat_idx >= 0 &&
			    menu_weapons::weapon_sel_legit.weapon_flat_idx < static_cast< int >( cstypes::weapons::k_total_weapons ) &&
			    cstypes::weapons::k_weapons[ menu_weapons::weapon_sel_legit.weapon_flat_idx ].group_idx == 4 )
			: ( std::clamp( menu_weapons::weapon_sel_legit.group_idx, 0, 5 ) == 4 );

		// RIGHT COLUMN: TRIGGER & WEAPON ACCURACY
		draw_col_title( right_x, "TRIGGER & ACCURACY" );
		xui::layout::set_cursor( right_x - wx, body_y + k_header_h - wy );

		if ( xui::begin_child( "##legitbot_trigger_accuracy", col_w, body_h - k_header_h, true ) )
		{
			auto draw_animated_item = [&]( const char* name, bool should_show, float item_h, float extra_pad, auto&& render_fn ) {
				auto anim = xui::anim::lerp(
					xui::fnv1a( name ),
					should_show ? 1.0f : 0.0f,
					10.0f,
					should_show ? 1.0f : 0.0f
				);

				if ( anim <= 0.001f )
				{
					anim = 0.0f;
					xui::anim::set( xui::fnv1a( name ), 0.0f );
				}
				else if ( anim >= 0.999f )
				{
					anim = 1.0f;
					xui::anim::set( xui::fnv1a( name ), 1.0f );
				}

				if ( anim > 0.0f )
				{
					auto* win = xui::layout::current_window( );
					auto& st = xui::ctx( ).style;

					if ( win && win->line_h > 0.0f )
					{
						win->cursor_y += win->line_h + st.item_spacing_y;
						win->line_h = 0.0f;
					}

					const auto [start_cx, base_y] = xui::layout::get_cursor( );
					const float scroll_y = win ? win->scroll_y : 0.0f;
					const float win_bx = win ? win->bounds.x : 0.0f;
					const float win_by = win ? win->bounds.y : 0.0f;
					const float screen_x = win_bx + start_cx;
					const float screen_y = win_by + base_y - scroll_y;

					const float full_stride = item_h + extra_pad + st.item_spacing_y;
					const float anim_ease = xui::ease::out_cubic( anim );
					const float current_offset = full_stride * anim_ease;
					const float clip_h = ( item_h + extra_pad ) * anim_ease;
					const float clip_w = win ? win->bounds.w : col_w;

					auto& dl = xui::draw::current( );
					dl.push_clip( screen_x - 10.0f, screen_y - 2.0f, clip_w + 20.0f, clip_h + 4.0f );

					const auto a = std::clamp( anim, 0.0f, 1.0f );
					xui::push_style_color( xui::style_col::text, st.text.alpha( static_cast< std::uint8_t >( st.text.a * a ) ) );
					xui::push_style_color( xui::style_col::text_dim, st.text_dim.alpha( static_cast< std::uint8_t >( st.text_dim.a * a ) ) );
					xui::push_style_color( xui::style_col::accent, st.accent.alpha( static_cast< std::uint8_t >( st.accent.a * a ) ) );
					xui::push_style_color( xui::style_col::checkbox_bg, st.checkbox_bg.alpha( static_cast< std::uint8_t >( st.checkbox_bg.a * a ) ) );
					xui::push_style_color( xui::style_col::checkbox_border, st.checkbox_border.alpha( static_cast< std::uint8_t >( st.checkbox_border.a * a ) ) );
					xui::push_style_color( xui::style_col::slider_track, st.slider_track.alpha( static_cast< std::uint8_t >( st.slider_track.a * a ) ) );
					xui::push_style_color( xui::style_col::slider_fill, st.slider_fill.alpha( static_cast< std::uint8_t >( st.slider_fill.a * a ) ) );
					xui::push_style_color( xui::style_col::combo_bg, st.combo_bg.alpha( static_cast< std::uint8_t >( st.combo_bg.a * a ) ) );
					xui::push_style_color( xui::style_col::combo_border, st.combo_border.alpha( static_cast< std::uint8_t >( st.combo_border.a * a ) ) );

					auto& input = xui::ctx( ).input;
					const auto saved_clicked = input.mouse_clicked;
					const auto saved_down = input.mouse_down;
					if ( !should_show || anim < 0.95f )
					{
						input.mouse_clicked = false;
						input.mouse_down = false;
					}

					render_fn( );

					input.mouse_clicked = saved_clicked;
					input.mouse_down = saved_down;
					xui::pop_style_color( 9 );
					dl.pop_clip( );

					if ( win )
					{
						win->line_h = 0.0f;
					}
					xui::layout::set_cursor( start_cx, base_y + current_offset );
				}
			};

			xui::push_id( sel_scope_id );
			xui::toggle( "Check Smoke", wg.smoke_check );
			xui::layout::spacing( 3.0f );
			draw_animated_item( "lb_scope_check_anim", is_sniper, 24.0f, 3.0f, [ & ]() {
				xui::toggle( "Check Scope", wg.scope_check );
			} );
			xui::toggle( "Flash Check", wg.flash_check );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Only On Ground", wg.ground_check );
			xui::layout::spacing( 8.0f );

			xui::toggle( "Triggerbot", wg.triggerbot );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Trigger Head Only", wg.trigger_head_only );
			xui::layout::spacing( 3.0f );
			xui::slider_int( "Reaction Delay", wg.trigger_delay, 0, 250, "%d ms" );
			xui::layout::spacing( 3.0f );
			xui::slider_int( "Hitchance", wg.trigger_hitchance, 0, 100, "%d%%" );
			xui::layout::spacing( 3.0f );
			xui::toggle( "Seed Prediction", wg.give_me_your_seed );

			xui::layout::spacing( 8.0f );
			xui::toggle( "Standalone RCS", wg.standalone_rcs );
			if ( xui::begin_popup( "##srcs_popup", 220.0f ) )
			{
				xui::slider_int( "strength##srcs", wg.standalone_rcs_strength, 0, 100, "%d%%" );
				xui::slider_int( "min##srcs", wg.standalone_rcs_min, 50, 150, "%d%%" );
				xui::slider_int( "max##srcs", wg.standalone_rcs_max, 50, 150, "%d%%" );
				xui::end_popup( );
			}

			xui::layout::spacing( 6.0f );
			xui::toggle( "Autowall", wg.autowall );
			if ( xui::begin_popup( "##aw_popup", 220.0f ) )
			{
				xui::slider_int( "min damage##aw", wg.min_damage, 1, 125, "%d" );
				xui::end_popup( );
			}
			xui::pop_id( );

			xui::end_child( );
		}
	}

} // namespace rendering