#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/systems/systems.hpp>
#include <core/features/features.hpp>
#include <protection/game_addresses.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace features::combat {

    // ============================================================================
    // ANTIAIM OPTIMIZATION
    // ============================================================================

    void misc::antiaim::on_create_move(systems::input::usercmd* cmd) {
        this->m_antiaim_active = false;

        const bool aa_enabled = settings::g_combat.m_antiaim.enabled.value;
        const bool spin_enabled = settings::g_combat.m_antiaim.spinbot.value;

        if ((!aa_enabled && !spin_enabled) ||
            systems::g_local.is_in_cinematic() ||
            systems::g_local.is_in_time_freeze()) {
            this->m_was_spinning = false;
            return;
        }

        // Оптимизированная обработка ручного направления (без лишних записей)
        const bool manual_left = settings::g_combat.m_antiaim.manual_left.value;
        const bool manual_right = settings::g_combat.m_antiaim.manual_right.value && !manual_left;

        this->m_yaw_side = manual_left ? -1 : (manual_right ? 1 : 0);
        if (manual_right && settings::g_combat.m_antiaim.manual_right.bind.active) {
            settings::g_combat.m_antiaim.manual_right.value = false;
            settings::g_combat.m_antiaim.manual_right.bind.active = false;
        }

        const auto local = systems::g_local.get();
        const auto base = cmd->csgo_user_cmd.mutable_base();
        if (!base) return;

        const auto view_angles = systems::g_input.get_view_angles();
        const auto& ctx = g_shared.ctx();

        // Ранние выходы для невалидных состояний
        if ((cmd->buttons.value & cstypes::command_buttons::in_use) != 0) return;

        // Не включать антиаим при стрельбе или атаке холодным/метательным оружием
        const bool is_attacking = (cmd->buttons.value & cstypes::command_buttons::in_attack) != 0;
        const bool is_knife = (ctx.weapon_type == cstypes::weapon_type::knife);
        const bool is_revolver = (ctx.item_def_idx == cstypes::item_definition_index::weapon_r8_revolver);
        const bool is_grenade = (ctx.weapon_type == cstypes::weapon_type::grenade);
        const bool is_secondary_attack = (cmd->buttons.value & cstypes::command_buttons::in_second_attack) != 0;

        if (is_attacking) return;
        if ((is_knife || is_revolver || is_grenade) && is_secondary_attack) return;

        if (ctx.weapon_type == cstypes::weapon_type::grenade) {
            if (memory::read<float>(ctx.weapon + SCHEMA("C_BaseCSGrenade", "m_fThrowTime"_hash)) > 0.0f)
                return;
        }

        const auto move_type = memory::read<int>(local.pawn + SCHEMA("C_BaseEntity", "m_nActualMoveType"_hash));
        if (move_type == cstypes::move_type::ladder || move_type == cstypes::move_type::noclip)
            return;
        if (this->is_near_ladder(local.pawn)) return;

        this->m_original_buttons = cmd->buttons.value;
        this->m_old_angles = view_angles;
        this->m_antiaim_active = true;
        this->m_modified_angles = view_angles;

        // Расчет углов с минимальными аллокациями
        this->m_modified_angles.x = this->get_pitch(cmd, view_angles.x);
        this->m_modified_angles.y = this->get_yaw(cmd, view_angles, local);
        math::helpers::normalize_angles(this->m_modified_angles);

        base->mutable_viewangles()->set_x(this->m_modified_angles.x);
        base->mutable_viewangles()->set_y(this->m_modified_angles.y);
        base->mutable_viewangles()->set_z(this->m_modified_angles.z);

        const auto history_size = cmd->csgo_user_cmd.input_history_size();
        for (auto i = 0; i < history_size; ++i)
        {
            const auto entry = cmd->csgo_user_cmd.mutable_input_history(i);
            if (!entry)
                continue;

            if (const auto angles = entry->mutable_view_angles())
            {
                angles->set_x(this->m_modified_angles.x);
                angles->set_y(this->m_modified_angles.y);
                angles->set_z(this->m_modified_angles.z);
            }
        }

        this->m_should_correct = true;
        this->correct_movement(cmd);
    }

    void misc::antiaim::on_render(xdraw::draw_list& draw_list) const {
        if ((!settings::g_combat.m_antiaim.enabled.value && !settings::g_combat.m_antiaim.spinbot.value) ||
            !settings::g_combat.m_antiaim.direction_indicator.value ||
            !this->m_antiaim_active ||
            !systems::g_frame_data.valid())
            return;

        const auto origin = systems::g_frame_data.origin();
        const auto aa_yaw_rad = this->m_indicator_yaw * (std::numbers::pi_v<float> / 180.0f);

        constexpr auto radius{ 28.0f };
        constexpr auto feet_offset{ -2.0f };
        constexpr auto arc_sweep_deg{ 60.0f };
        constexpr auto arc_segments{ 32 }; // Снижено с 48 для FPS
        constexpr auto max_thickness{ 3.0f };

        const auto& cfg = settings::g_combat.m_antiaim;
        const auto& color = cfg.direction_indicator_color;
        const auto base = math::vector3{ origin.x, origin.y, origin.z + feet_offset };

        const auto half_sweep = (arc_sweep_deg * 0.5f) * (std::numbers::pi_v<float> / 180.0f);
        const auto start_angle = aa_yaw_rad - half_sweep;
        const auto end_angle = aa_yaw_rad + half_sweep;
        const auto angle_step = (end_angle - start_angle) / static_cast<float>(arc_segments);

        // Stack-allocated буфер вместо heap vector
        std::array<math::vector2, arc_segments + 1> pts{};
        int valid_pts = 0;

        for (int i = 0; i <= arc_segments; ++i) {
            const auto angle = start_angle + angle_step * static_cast<float>(i);
            const auto world_pt = math::vector3{
                base.x + std::cosf(angle) * radius,
                base.y + std::sinf(angle) * radius,
                base.z
            };

            const auto sp = systems::g_view.project(world_pt);
            if (!systems::g_view.projection_valid(sp)) continue;

            pts[valid_pts++] = { sp.x, sp.y };
        }

        if (valid_pts < 2) return;

        const auto total = static_cast<float>(valid_pts - 1);
        const auto fade_at = [](float frac) -> float {
            const auto edge = 1.0f - std::fabsf(frac - 0.5f) * 2.0f;
            return edge * edge * edge * (edge * (edge * 6.0f - 15.0f) + 10.0f);
            };

        // Glow pass
        if (cfg.direction_indicator_glow) {
            auto& glow = xdraw::get_glow();
            for (int i = 0; i + 1 < valid_pts; ++i) {
                const auto f0 = fade_at(static_cast<float>(i) / total);
                const auto f1 = fade_at(static_cast<float>(i + 1) / total);
                const auto ga0 = static_cast<std::uint8_t>(static_cast<float>(color.value.a) * cfg.direction_indicator_glow_strength * std::fmaxf(f0, 0.05f));
                const auto ga1 = static_cast<std::uint8_t>(static_cast<float>(color.value.a) * cfg.direction_indicator_glow_strength * std::fmaxf(f1, 0.05f));
                const auto thickness = (max_thickness + 2.0f) * ((f0 + f1) * 0.5f * 0.85f + 0.15f);

                const float seg[]{ pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y };
                const xdraw::color cols[]{
                    { color.value.r, color.value.g, color.value.b, ga0 },
                    { color.value.r, color.value.g, color.value.b, ga1 }
                };
                glow.polyline_gradient(seg, cols, false, thickness);
            }
        }

        // Main line pass
        for (int i = 0; i + 1 < valid_pts; ++i) {
            const auto f0 = fade_at(static_cast<float>(i) / total);
            const auto f1 = fade_at(static_cast<float>(i + 1) / total);
            const auto a0 = static_cast<std::uint8_t>(color.value.a * std::fmaxf(f0, 0.05f));
            const auto a1 = static_cast<std::uint8_t>(color.value.a * std::fmaxf(f1, 0.05f));
            const auto thickness = max_thickness * ((f0 + f1) * 0.5f * 0.85f + 0.15f);

            const float seg[]{ pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y };
            const xdraw::color cols[]{
                { color.value.r, color.value.g, color.value.b, a0 },
                { color.value.r, color.value.g, color.value.b, a1 }
            };
            draw_list.polyline_gradient(seg, cols, false, thickness);
        }
    }

    float misc::antiaim::get_pitch(systems::input::usercmd* cmd, float view_pitch) {
        if (!std::isfinite(view_pitch)) return 0.0f;

        if (!settings::g_combat.m_antiaim.enabled.value)
            return std::clamp(view_pitch, -89.0f, 89.0f);

        switch (settings::g_combat.m_antiaim.pitch) {
        case settings::combat::antiaim::pitch_mode::zero:   return 0.0f;
        case settings::combat::antiaim::pitch_mode::down:   return 89.0f;
        case settings::combat::antiaim::pitch_mode::up:     return -89.0f;
        case settings::combat::antiaim::pitch_mode::custom: return std::clamp(settings::g_combat.m_antiaim.custom_pitch.value, -89.0f, 89.0f);
        default: return 0.0f;
        }
    }

    float misc::antiaim::get_yaw(systems::input::usercmd* cmd, const math::vector3& view_angles, const systems::local::snapshot& local) {
        const auto& prestate = systems::g_prediction.pre();
        const bool on_ground = (prestate.flags & cstypes::entity_flags::on_ground) != 0;

        const auto is_spinbot = settings::g_combat.m_antiaim.spinbot.value;
        const auto is_custom_yaw = (settings::g_combat.m_antiaim.yaw_type == settings::combat::antiaim::yaw_mode::custom);

        constexpr auto base_yaw_offset{ 180.0f };
        auto base_yaw = view_angles.y - base_yaw_offset;

        if (is_spinbot) {
            if (!this->m_was_spinning) {
                this->m_spin_yaw = view_angles.y;
                this->m_was_spinning = true;
            }

            const auto speed = settings::g_combat.m_antiaim.spin_speed.value;
            const auto dir = settings::g_combat.m_antiaim.spin_direction.value;
            const auto step = (dir == settings::combat::antiaim::spin_direction_mode::clockwise ? -speed : speed);

            this->m_spin_yaw = math::helpers::normalize_yaw(this->m_spin_yaw + step);
            base_yaw = this->m_spin_yaw;
        } else {
            this->m_was_spinning = false;

            if (is_custom_yaw) {
                base_yaw = view_angles.y + settings::g_combat.m_antiaim.custom_yaw.value;
            } else {
                const auto local_game_scene_node = memory::read<std::uintptr_t>(local.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
                if (!local_game_scene_node) return view_angles.y;

            const auto local_origin = memory::read<math::vector3>(local_game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
            const auto eye_pos = local_origin + memory::read<math::vector3>(local.pawn + SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash));
            const auto players = systems::g_entities.get_by_type(systems::entities::type::player);

            // Backstab avoidance (оптимизированный цикл)
            if (settings::g_combat.m_antiaim.avoid_backstab.value) {
                constexpr auto backstab_range_sq = 350.0f * 350.0f;
                auto knife_dist = std::numeric_limits<float>::max();
                auto knife_yaw{ 0.0f };
                auto knife_found{ false };

                for (const auto& p : players) {
                    if (!p.ptr || p.ptr == local.controller) continue;
                    if (!memory::read<bool>(p.ptr + SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_hash))) continue;

                    const auto pawn_handle = memory::read<std::uint32_t>(p.ptr + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
                    const auto pawn = systems::g_entities.lookup(pawn_handle);
                    if (!pawn || pawn == local.pawn) continue;
                    if (!local.is_this_other_team(memory::read<int>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash)))) continue;
                    if (memory::read<int>(pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash)) <= 0) continue;

                    const auto enemy_node = memory::read<std::uintptr_t>(pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
                    if (!enemy_node) continue;

                    const auto enemy_origin = memory::read<math::vector3>(enemy_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
                    const auto dx = enemy_origin.x - local_origin.x;
                    const auto dy = enemy_origin.y - local_origin.y;
                    const auto dist_sq = dx * dx + dy * dy;
                    if (dist_sq > backstab_range_sq) continue;

                    const auto ws = memory::read<std::uintptr_t>(pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
                    if (!ws) continue;

                    const auto wh = memory::read<std::uint32_t>(ws + SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
                    const auto weapon = systems::g_entities.lookup(wh);
                    if (!weapon) continue;

                    const auto vdata = memory::read<std::uintptr_t>(weapon + SCHEMA("C_BaseEntity", "m_nSubclassID"_hash) + 0x8);
                    if (!vdata) continue;
                    if (memory::read<std::uint32_t>(vdata + SCHEMA("CCSWeaponBaseVData", "m_WeaponType"_hash)) != cstypes::weapon_type::knife) continue;

                    if (dist_sq < knife_dist) {
                        knife_dist = dist_sq;
                        knife_yaw = std::atan2f(dy, dx) * (180.0f / std::numbers::pi_v<float>);
                        knife_found = true;
                    }
                }

                if (knife_found) {
                    this->m_indicator_yaw = knife_yaw;
                    return knife_yaw;
                }
            }

            // Target selection with threat scoring
            auto best_yaw = base_yaw;
            auto best_threat = std::numeric_limits<float>::max();
            bool target_found = false;

            for (const auto& p : players) {
                if (!p.ptr || p.ptr == local.controller) continue;
                if (!memory::read<bool>(p.ptr + SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_hash))) continue;

                const auto pawn_handle = memory::read<std::uint32_t>(p.ptr + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
                const auto pawn = systems::g_entities.lookup(pawn_handle);
                if (!pawn || pawn == local.pawn) continue;
                if (!local.is_this_other_team(memory::read<int>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash)))) continue;
                if (memory::read<int>(pawn + SCHEMA("C_BaseEntity", "m_iHealth"_hash)) <= 0) continue;
                if (memory::read<bool>(pawn + SCHEMA("C_CSPlayerPawn", "m_bGunGameImmunity"_hash))) continue;

                const auto enemy_node = memory::read<std::uintptr_t>(pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
                if (!enemy_node) continue;

                const auto enemy_origin = memory::read<math::vector3>(enemy_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));
                const auto enemy_eye = enemy_origin + memory::read<math::vector3>(pawn + SCHEMA("C_BaseModelEntity", "m_vecViewOffset"_hash));

                const auto angle_to_enemy = math::helpers::calculate_angle(eye_pos, enemy_eye);
                const auto fov = math::helpers::angle_distance(view_angles, angle_to_enemy);
                const auto distance = eye_pos.distance(enemy_eye);

                auto threat = fov * 4.0f + distance * 0.01f;

                math::vector3 enemy_fwd{};
                math::helpers::angle_vectors_left(memory::read<math::vector3>(pawn + SCHEMA("C_CSPlayerPawn", "m_angEyeAngles"_hash)), &enemy_fwd);
                const auto dir_to_us = (eye_pos - enemy_eye).normalized();
                threat -= std::clamp(enemy_fwd.dot(dir_to_us), -1.0f, 1.0f) * 25.0f;

                if (systems::g_tracing.is_visible(eye_pos, enemy_eye, pawn, local.pawn))
                    threat -= 15.0f;

                if (threat < best_threat) {
                    best_threat = threat;
                    best_yaw = angle_to_enemy.y - base_yaw_offset;
                    target_found = true;
                }
            }

            if (on_ground && target_found) base_yaw = best_yaw;
            }
        }

        auto indicator = base_yaw;
        auto yaw = base_yaw;

        if (!is_spinbot) {
            if (this->m_yaw_side == -1) { indicator -= 90.0f; yaw -= 90.0f; }
            else if (this->m_yaw_side == 1) { indicator += 90.0f; yaw += 90.0f; }
        }

        if (settings::g_combat.m_antiaim.jitters.value) {
            const auto speed_ticks = std::clamp(settings::g_combat.m_antiaim.jitters_speed.value, 1, 5);
            if (++this->m_jitter_ticks >= speed_ticks) {
                this->m_jitter_flip = !this->m_jitter_flip;
                this->m_jitter_ticks = 0;
            }

            const auto delta = settings::g_combat.m_antiaim.jitters_delta.value;
            const auto jitter_val = this->m_jitter_flip ? (delta * 0.5f) : -(delta * 0.5f);
            yaw += jitter_val;
            indicator += jitter_val;
        } else {
            this->m_jitter_ticks = 0;
        }

        this->m_indicator_yaw = indicator;
        if (!is_spinbot && settings::g_combat.m_antiaim.auto_yaw_adjust.value) yaw += 33.0f;

        // Fast normalize without loops
        yaw = std::fmodf(yaw + 180.0f, 360.0f);
        if (yaw < 0.0f) yaw += 360.0f;
        yaw -= 180.0f;

        return yaw;
    }

    void misc::antiaim::correct_movement(systems::input::usercmd* cmd) {
        if (!this->m_should_correct) return;
        this->m_should_correct = false;

        const auto base = cmd->csgo_user_cmd.mutable_base();
        if (!base) return;

        auto forward_move = base->forwardmove();
        auto side_move = base->leftmove();

        if (forward_move == 0.0f && side_move == 0.0f) {
            if (this->m_original_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward)) forward_move += 1.0f;
            if (this->m_original_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_back)) forward_move -= 1.0f;
            if (this->m_original_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveleft)) side_move += 1.0f;
            if (this->m_original_buttons & static_cast<std::uintptr_t>(cstypes::command_buttons::in_moveright)) side_move -= 1.0f;
        }

        if (forward_move == 0.0f && side_move == 0.0f) return;

        const auto delta_yaw = math::helpers::normalize_yaw(this->m_modified_angles.y - this->m_old_angles.y) * (std::numbers::pi_v<float> / 180.0f);
        const auto cos_yaw = std::cosf(delta_yaw);
        const auto sin_yaw = std::sinf(delta_yaw);

        auto corrected_forward = forward_move * cos_yaw + side_move * sin_yaw;
        auto corrected_side = -forward_move * sin_yaw + side_move * cos_yaw;

        const auto max_comp = std::fmaxf(std::fabsf(corrected_forward), std::fabsf(corrected_side));
        if (max_comp > 1.0f) {
            corrected_forward /= max_comp;
            corrected_side /= max_comp;
        }

        base->set_forwardmove(corrected_forward);
        base->set_leftmove(corrected_side);

        if (const auto subtick_moves = base->mutable_subtick_moves()) {
            for (int i = 0; i < base->subtick_moves_size(); ++i) {
                if (const auto step = base->mutable_subtick_moves(i)) {
                    const auto step_fwd = step->analog_forward_delta();
                    const auto step_left = step->analog_left_delta();
                    if (std::fabsf(step_fwd) > 0.0001f || std::fabsf(step_left) > 0.0001f) {
                        const auto corr_step_fwd = step_fwd * cos_yaw + step_left * sin_yaw;
                        const auto corr_step_left = -step_fwd * sin_yaw + step_left * cos_yaw;
                        step->set_analog_forward_delta(corr_step_fwd);
                        step->set_analog_left_delta(corr_step_left);
                    }
                }
            }
        }

        const auto original_buttons = this->m_original_buttons;
        auto buttons = original_buttons;
        buttons &= ~static_cast<std::uintptr_t>(
            cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
            cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright
        );

        if (corrected_forward > 0.001f)
            buttons |= cstypes::command_buttons::in_forward;
        else if (corrected_forward < -0.001f)
            buttons |= cstypes::command_buttons::in_back;

        if (corrected_side > 0.001f)
            buttons |= cstypes::command_buttons::in_moveleft;
        else if (corrected_side < -0.001f)
            buttons |= cstypes::command_buttons::in_moveright;

        cmd->buttons.value = buttons;
        cmd->buttons.value_changed |= (original_buttons ^ buttons);
    }

    bool misc::antiaim::is_near_ladder(std::uintptr_t local_pawn) const {
        return false; // TODO: Implement spatial ladder check
    }

    // ============================================================================
    // DUCKPEEK & QUICKPEEK STABILITY FIXES
    // ============================================================================

    namespace {
        math::vector3 quickpeek_ground_snap(std::uintptr_t skip_pawn, const math::vector3& feet_pos) {
            const auto start = math::vector3{ feet_pos.x, feet_pos.y, feet_pos.z + 64.0f };
            const auto end = math::vector3{ feet_pos.x, feet_pos.y, feet_pos.z - 8192.0f };
            const auto tr = systems::g_tracing.trace(start, end, skip_pawn);
            if (tr.fraction <= 0.0f || tr.fraction >= 0.997f) return feet_pos;
            auto out = tr.position;
            out.z += 1.0f;
            return out;
        }
    }

    void misc::duckpeek::on_create_move(systems::input::usercmd* cmd) {
        this->m_fake_stand_active = false;
        if (!cmd || !settings::g_combat.m_duckpeek.enabled.value) {
            this->m_was_active = false;
            return;
        }

        const auto local = systems::g_local.get();
        if (!local.is_alive || !local.pawn ||
            systems::g_local.is_in_cinematic() || systems::g_local.is_in_time_freeze()) {
            this->m_was_active = false;
            return;
        }

        this->m_was_active = true;
        const bool wants_shot = g_rage.should_release_duck_for_shot();
        const bool wants_reduck = g_rage.duckpeek_wants_reduck();

        if (wants_reduck) {
            cmd->buttons.value |= cstypes::command_buttons::in_duck;
            cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;
        }
        else if (wants_shot) {
            cmd->buttons.value &= ~cstypes::command_buttons::in_duck;
            cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;
            this->m_fake_stand_active = true;

            if (const auto base = cmd->csgo_user_cmd.mutable_base()) {
                if (auto subtick_moves = base->mutable_subtick_moves()) {
                    for (int i = 0; i < subtick_moves->size(); ++i) {
                        if (auto entry = subtick_moves->mutable_at(i)) {
                            if ((entry->button() & cstypes::command_buttons::in_duck) != 0)
                                entry->set_pressed(false);
                        }
                    }
                }
            }
        }
        else {
            cmd->buttons.value |= cstypes::command_buttons::in_duck;
            cmd->buttons.value_changed |= cstypes::command_buttons::in_duck;
        }
    }

    void misc::duckpeek::on_override_view(std::uintptr_t view_setup) {
        (void)view_setup;
        if (!settings::g_combat.m_duckpeek.enabled.value) {
            this->m_was_active = false;
            this->m_fake_stand_active = false;
        }
    }

    void misc::quickpeek::on_create_move(systems::input::usercmd* cmd) {
        if (!settings::g_combat.m_quickpeek.enabled.value) {
            this->reset();
            return;
        }

        const auto local = systems::g_local.get();
        const auto& ctx = g_shared.ctx();
        if (ctx.weapon_type < cstypes::weapon_type::pistol || ctx.weapon_type > cstypes::weapon_type::lmg) {
            this->reset();
            return;
        }

        const auto base = cmd->csgo_user_cmd.mutable_base();
        constexpr auto movement_mask = static_cast<std::uintptr_t>(
            cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
            cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright);
        const auto curr_bits = cmd->buttons.value & movement_mask;

        const auto game_scene_node = memory::read<std::uintptr_t>(local.pawn + SCHEMA("C_BaseEntity", "m_pGameSceneNode"_hash));
        const auto origin = memory::read<math::vector3>(game_scene_node + SCHEMA("CGameSceneNode", "m_vecAbsOrigin"_hash));

        // Инициализация точки возврата
        if (this->m_saved_origin.length_sqr() < 0.001f) {
            this->m_saved_origin = quickpeek_ground_snap(local.pawn, origin);
            this->m_should_retrack = false;
            this->m_fired = false;
            this->m_active = true;
            this->create_particle();
            this->m_prev_movement_bits = curr_bits;
            return;
        }

        this->update_particle();
        const auto distance = (origin - this->m_saved_origin).length_2d();

        // Отмена ретрека при новом движении
        if (this->m_should_retrack && (curr_bits & ~this->m_prev_movement_bits) != 0)
            this->m_should_retrack = false;

        // Логика автоматического возврата
        if (this->m_should_retrack && (systems::g_prediction.pre().flags & cstypes::entity_flags::on_ground)) {
            const auto velocity = memory::read<math::vector3>(local.pawn + SCHEMA("C_BaseEntity", "m_vecAbsVelocity"_hash));
            const auto speed = velocity.length_2d();

            if (distance < 5.0f && speed < 15.0f) {
                this->m_should_retrack = false;
                this->m_fired = false;
            }
            else {
                math::vector3 desired_dir{};
                if (distance < speed * 0.1f && speed > 15.0f) {
                    desired_dir = (velocity * -1.0f).normalized();
                }
                else {
                    desired_dir = (this->m_saved_origin - origin).normalized();
                }

                const auto yaw_diff = std::atan2f(desired_dir.y, desired_dir.x) - base->viewangles()->y() * (std::numbers::pi_v<float> / 180.0f);
                base->set_forwardmove(std::cosf(yaw_diff));
                base->set_leftmove(-std::sinf(yaw_diff));

                auto buttons = cmd->buttons.value;
                buttons &= ~movement_mask;
                if (base->forwardmove() > 0.0f) buttons |= cstypes::command_buttons::in_forward;
                else if (base->forwardmove() < 0.0f) buttons |= cstypes::command_buttons::in_back;
                if (base->leftmove() > 0.0f) buttons |= cstypes::command_buttons::in_moveleft;
                else if (base->leftmove() < 0.0f) buttons |= cstypes::command_buttons::in_moveright;
                cmd->buttons.value = buttons;
            }
        }

        constexpr auto fired_bits = cstypes::command_buttons::in_attack | cstypes::command_buttons::in_second_attack;
        if ((cmd->buttons.value & fired_bits) != 0 && !g_rage.is_cocking_revolver()) {
            this->m_should_retrack = true;
            this->m_fired = true;
        }

        this->m_prev_movement_bits = curr_bits;
    }

    void misc::quickpeek::reset_if_needed() {
        if (!this->m_active) return;
        const auto local = systems::g_local.get();
        if (!local.is_alive || !local.pawn || !settings::g_combat.m_quickpeek.enabled.value)
            this->reset();
    }

    void misc::quickpeek::create_particle() {
        const auto particle_manager = memory::read<std::uintptr_t>(addresses::globals::particle_manager);
        if (!particle_manager) return;

        constexpr auto particle_path{ "particles/embedded/halo.vpcf" };
        if (!this->m_particle_loaded) {
            struct buffer_string {
                std::uint32_t m_unknown1{};
                std::uint32_t m_unknown2{ 0xc00000c8 };
                union { std::uintptr_t m_str_ptr; std::uint8_t data[0xc8]; };
                std::uintptr_t m_unknown3{ 0 };
                std::uintptr_t m_unknown4{ 0 };
            } buffer{};

            memory::call<void>(PATTERN(patterns::init_particle_path_buffer), &buffer, particle_path);
            buffer.m_unknown4 = 'fcpv';
            memory::call<void>(PATTERN(patterns::resource_system_precache), addresses::globals::resource_system, &buffer, "");
            this->m_particle_loaded = true;
        }

        auto effect_index{ invalid_effect_index };
        memory::call<int*>(PATTERN(patterns::particle_create_effect), particle_manager, &effect_index, particle_path, 8, 0ll, 0ll, 0ll, 0);
        this->m_particle_effect = effect_index;
        if (effect_index == invalid_effect_index) return;

        memory::call<bool>(PATTERN(patterns::particle_set_control_point), particle_manager, effect_index, 0, &this->m_saved_origin, 0);
    }

    void misc::quickpeek::update_particle() {
        if (this->m_particle_effect == invalid_effect_index) return;
        const auto particle_manager = memory::read<std::uintptr_t>(addresses::globals::particle_manager);
        if (!particle_manager) return;

        const auto& cfg = settings::g_combat.m_quickpeek;
        const auto& col = this->m_should_retrack ? cfg.retrack_color : cfg.color;
        const auto color = math::vector3{ static_cast<float>(col.value.r), static_cast<float>(col.value.g), static_cast<float>(col.value.b) };

        memory::call<bool>(PATTERN(patterns::particle_set_control_point), particle_manager, this->m_particle_effect, 1, &color, 0);
        memory::call<bool>(PATTERN(patterns::particle_set_control_point), particle_manager, this->m_particle_effect, 0, &this->m_saved_origin, 0);
    }

    void misc::quickpeek::release_particle() {
        if (this->m_particle_effect == invalid_effect_index) return;
        if (const auto pm = memory::read<std::uintptr_t>(addresses::globals::particle_manager))
            memory::call<void>(PATTERN(patterns::particle_destroy_effect), pm, this->m_particle_effect, true, true);
        this->m_particle_effect = invalid_effect_index;
    }

    void misc::quickpeek::reset() {
        this->release_particle();
        this->m_saved_origin = {};
        this->m_should_retrack = false;
        this->m_fired = false;
        this->m_active = false;
        this->m_prev_movement_bits = 0;
    }

    // ============================================================================
    // AUTOSTOP PERFORMANCE TUNING
    // ============================================================================

    void misc::autostop::on_create_move(systems::input::usercmd* cmd) {
        if (!features::combat::g_rage.should_stop()) return;

        const auto local = systems::g_local.get();
        const auto movement_services = memory::read<std::uintptr_t>(local.pawn + SCHEMA("C_BasePlayerPawn", "m_pMovementServices"_hash));
        const auto base = cmd->csgo_user_cmd.mutable_base();
        const auto& prestate = systems::g_prediction.pre();
        const auto& ctx = g_shared.ctx();

        if (!(prestate.flags & cstypes::entity_flags::on_ground)) return;

        auto velocity = prestate.networked_velocity;
        auto speed = velocity.length_2d();
        if (speed <= 1.0f) return;

        const auto sv_friction = CONVAR("sv_friction")->get<float>();
        const auto sv_stopspeed = CONVAR("sv_stopspeed")->get<float>();
        const auto surface_friction = prestate.surface_friction;

        // Предсказание трения
        const auto control = std::fmaxf(speed, sv_stopspeed);
        const auto drop = control * sv_friction * surface_friction * cstypes::tick_interval;
        auto post_friction = std::fmaxf(speed - drop, 0.0f);

        if (post_friction > 0.0f) {
            velocity *= (post_friction / speed);
            speed = post_friction;
        }
        else {
            base->set_forwardmove(0.0f);
            base->set_leftmove(0.0f);
            return;
        }

        if (speed < 2.0f) {
            base->set_forwardmove(0.0f);
            base->set_leftmove(0.0f);
            return;
        }

        auto accel = CONVAR("sv_accelerate")->get<float>();
        const auto accel_base = this->get_effective_accel_base(local.pawn, movement_services, prestate.flags, ctx.weapon_max_speed);

        if (ctx.is_scoped) {
            const auto weapon_ratio = std::fminf(1.0f, ctx.weapon_max_speed / 250.0f);
            const auto v20 = std::fmaxf(250.0f, memory::read<float>(movement_services + SCHEMA("CPlayer_MovementServices", "m_flMaxspeed"_hash)));
            const auto scoped_max = v20 * weapon_ratio * 0.52f;
            if (speed > scoped_max - 5.0f) {
                const auto t = 1.0f - std::fmaxf(0.0f, speed - (scoped_max - 5.0f)) / std::fmaxf(0.01f, 5.0f);
                accel *= std::clamp(t, 0.0f, 1.0f);
            }
        }

        const auto wish_x = -velocity.x / speed;
        const auto wish_y = -velocity.y / speed;
        const auto accel_speed = std::fminf(accel * accel_base * surface_friction * cstypes::tick_interval, speed);

        velocity.x += wish_x * accel_speed;
        velocity.y += wish_y * accel_speed;

        const auto move_mag = std::clamp(speed / ctx.weapon_max_speed, 0.0f, 1.0f);
        const auto yaw_rad = base->viewangles()->y() * (std::numbers::pi_v<float> / 180.0f);
        const auto sy = std::sinf(yaw_rad);
        const auto cy = std::cosf(yaw_rad);

        const auto forward_move = std::clamp((wish_x * cy + wish_y * sy) * move_mag, -1.0f, 1.0f);
        const auto left_move = std::clamp((wish_x * sy - wish_y * cy) * -move_mag, -1.0f, 1.0f);

        base->set_forwardmove(forward_move);
        base->set_leftmove(left_move);

        // Subtick correction for precise stopping
        if (const auto subtick_moves = base->mutable_subtick_moves()) {
            if (const auto step = systems::g_input.acquire_subtick_step(subtick_moves)) {
                step->set_button(0);
                step->set_pressed(false);
                step->set_when(0.0f);
                step->set_analog_forward_delta(forward_move - prestate.last_movement_impulses.x);
                step->set_analog_left_delta(left_move - prestate.last_movement_impulses.y);
            }
        }

        auto buttons = cmd->buttons.value;
        buttons &= ~static_cast<std::uintptr_t>(cstypes::command_buttons::in_forward | cstypes::command_buttons::in_back |
            cstypes::command_buttons::in_moveleft | cstypes::command_buttons::in_moveright);

        if (forward_move > 0.0f) buttons |= cstypes::command_buttons::in_forward;
        else if (forward_move < 0.0f) buttons |= cstypes::command_buttons::in_back;
        if (left_move > 0.0f) buttons |= cstypes::command_buttons::in_moveleft;
        else if (left_move < 0.0f) buttons |= cstypes::command_buttons::in_moveright;

        cmd->buttons.value = buttons;
    }

    float misc::autostop::get_effective_accel_base(std::uintptr_t local_pawn, std::uintptr_t movement_services,
        std::uint32_t flags, float max_weapon_speed) const {
        const auto max_speed_base = memory::read<float>(movement_services + SCHEMA("CPlayer_MovementServices", "m_flMaxspeed"_hash));
        const auto is_ducked = (flags & 4) != 0;
        const auto ducking_state = memory::read<bool>(movement_services + SCHEMA("CPlayer_MovementServices", "m_bDucking"_hash));
        const auto is_scoped = g_shared.ctx().is_scoped;
        const auto is_ducking = is_ducked || ducking_state;

        const auto v19 = std::fmaxf(250.0f, max_speed_base);
        auto friction_scale{ 1.0f };

        if (CONVAR("sv_accelerate_use_weapon_speed")->get<bool>()) {
            const auto weapon_ratio = std::fminf(1.0f, max_weapon_speed / 250.0f);
            if (!is_ducking && !is_scoped) friction_scale = weapon_ratio;
        }

        if (is_ducking) friction_scale = std::fminf(0.34f, friction_scale);
        auto accel_base = v19 * friction_scale;
        if (is_scoped && !is_ducking) accel_base *= 0.52f;

        return accel_base;
    }

} // namespace features::combat