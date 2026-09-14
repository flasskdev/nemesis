#include <pch/pch.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/steam/steam.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/cosmetic_attributes.hpp>
#include <external/config.hpp>
#include <protection/game_addresses.hpp>

namespace features::misc {
        namespace {

                [[nodiscard]] std::string controller_name(std::uintptr_t controller)
                {
                        if (!controller)
                        {
                                return {};
                        }

                        const auto name_ptr = memory::safe_read<std::uintptr_t>(
                                controller + SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_hash)).value_or(0);
                        return memory::read_string(name_ptr, 127);
                }

                [[nodiscard]] std::string get_steam_nickname(std::uintptr_t controller = 0)
                {
                        if (const auto* persona = steam::friends::get_persona_name(); persona && *persona)
                        {
                                std::string name(persona);
                                if (!name.empty() && name != "x")
                                {
                                        return name;
                                }
                        }

                        if (controller)
                        {
                                const auto name_ptr = memory::safe_read<std::uintptr_t>(
                                        controller + SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_hash)).value_or(0);
                                auto name = memory::read_string(name_ptr, 127);
                                if (!name.empty() && name != "x")
                                {
                                        return name;
                                }

                                auto raw_name = memory::read_string(
                                        controller + SCHEMA("CBasePlayerController", "m_iszPlayerName"_hash), 127);
                                if (!raw_name.empty() && raw_name != "x")
                                {
                                        return raw_name;
                                }
                        }

                        return {};
                }

                void submit_name_change(const std::string& display_name)
                {
                        if (display_name.empty())
                        {
                                return;
                        }

                        std::string sanitized = display_name;
                        std::erase(sanitized, '"');
                        std::erase(sanitized, '\n');
                        std::erase(sanitized, '\r');
                        std::erase(sanitized, ';');

                        other::s_display_name = display_name;
                        other::s_name_change_pending = true;

                        const auto cmd = std::format("setinfo name \"{}\"", sanitized);
                        memory::call<void>(PATTERN(patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, cmd.c_str(), 0x7ffef001);
                }

                inline bool is_local_player( std::uintptr_t ent, const systems::local::snapshot& local )
                {
                        if ( !ent || !local.is_valid( ) ) return false;
                        if ( ent == local.controller || ent == local.pawn ) return true;

                        const auto ctrl_h = memory::read<std::uint32_t>( ent + SCHEMA( "C_BasePlayerPawn", "m_hController"_hash ) );
                        if ( ctrl_h && ctrl_h != 0xFFFFFFFF && systems::g_entities.lookup( ctrl_h ) == local.controller ) return true;

                        const auto pawn_h = memory::read<std::uint32_t>( ent + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
                        if ( pawn_h && pawn_h != 0xFFFFFFFF && systems::g_entities.lookup( pawn_h ) == local.pawn ) return true;

                        return false;
                }

                inline std::uintptr_t resolve_pawn( std::uintptr_t ent )
                {
                        if ( !ent ) return 0;
                        const auto ctrl_h = memory::read<std::uint32_t>( ent + SCHEMA( "C_BasePlayerPawn", "m_hController"_hash ) );
                        if ( ctrl_h && ctrl_h != 0xFFFFFFFF ) return ent;

                        const auto pawn_h = memory::read<std::uint32_t>( ent + SCHEMA( "CBasePlayerController", "m_hPawn"_hash ) );
                        if ( pawn_h && pawn_h != 0xFFFFFFFF )
                        {
                                const auto pawn = systems::g_entities.lookup( pawn_h );
                                if ( pawn ) return pawn;
                        }
                        return ent;
                }


        } // namespace

        void other::on_round_start()
        {
                features::misc::g_vote_logs.reset();
                this->do_autobuy();
        }

        void other::on_player_death(std::uintptr_t event)
        {
                if (!event)
                {
                        return;
                }

                auto attacker = systems::events::get_controller(reinterpret_cast<void*>(event), "attacker");
                if (!attacker)
                {
                        attacker = systems::events::get_pawn(reinterpret_cast<void*>(event), "attacker");
                }
                auto victim = systems::events::get_controller(reinterpret_cast<void*>(event), "userid");
                if (!victim)
                {
                        victim = systems::events::get_pawn(reinterpret_cast<void*>(event), "userid");
                }

                const auto local = systems::g_local.get();
                if (!local.is_valid() || !attacker || !is_local_player(attacker, local) || is_local_player(victim, local))
                {
                        return;
                }

                auto victim_pawn = systems::events::get_pawn(reinterpret_cast<void*>(event), "userid");
                if (!victim_pawn && victim)
                {
                        victim_pawn = resolve_pawn(victim);
                }

                if (!victim_pawn)
                {
                        return;
                }

                const auto victim_team = memory::read<int>(victim_pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
                if (!local.is_this_other_team(victim_team))
                {
                        return;
                }

                const auto weapon_services = memory::read<std::uintptr_t>(local.pawn + SCHEMA("C_BasePlayerPawn", "m_pWeaponServices"_hash));
                if (!weapon_services)
                {
                        return;
                }

                const auto active_handle = memory::read<std::uint32_t>(weapon_services + SCHEMA("CPlayer_WeaponServices", "m_hActiveWeapon"_hash));
                const auto active_weapon = systems::g_entities.lookup(active_handle);
                if (!active_weapon)
                {
                        return;
                }

                const auto iv = active_weapon + SCHEMA("C_EconEntity", "m_AttributeManager"_hash) + SCHEMA("C_AttributeContainer", "m_Item"_hash);
                const auto def_index = memory::read<std::uint16_t>(iv + SCHEMA("C_EconItemView", "m_iItemDefinitionIndex"_hash));
                const auto def = changer::g_econ_item_system.find_def(static_cast<std::int16_t>(def_index));
                if (!def)
                {
                        return;
                }

                settings::changer::applied_skin* target_skin{ nullptr };
                if (def->category == changer::econ_item_system::item_category::gun)
                {
                        const auto it = settings::g_changer.skins.data.find(def_index);
                        if (it != settings::g_changer.skins.data.end())
                        {
                                target_skin = &it->second;
                        }
                }
                else if (def->category == changer::econ_item_system::item_category::knife)
                {
                        const auto it = settings::g_changer.skins.data.find(def_index);
                        if (it != settings::g_changer.skins.data.end())
                        {
                                target_skin = &it->second;
                        }
                        else
                        {
                                for (auto& [k_def, k_skin] : settings::g_changer.skins.data)
                                {
                                        const auto kdef = changer::g_econ_item_system.find_def(k_def);
                                        if (kdef && kdef->category == changer::econ_item_system::item_category::knife)
                                        {
                                                target_skin = &k_skin;
                                                break;
                                        }
                                }
                        }
                }

                if (target_skin && target_skin->stattrak)
                {
                        target_skin->stattrak_count++;

                        memory::write<int>(active_weapon + SCHEMA("C_EconEntity", "m_nFallbackStatTrak"_hash), target_skin->stattrak_count);

                        if (changer::cosmetic_attributes::available())
                        {
                                const auto set = PATTERN(patterns::econ_item_view_set_attribute);
                                const auto count_val = std::bit_cast<float>(static_cast<std::int32_t>(target_skin->stattrak_count));
                                memory::call<void>(set, iv, "kill eater", count_val);
                        }

                        std::thread([]() {
                                config::registry::save_active();
                        }).detach();
                }

                if (settings::g_misc.m_kill_say.enabled.value && !settings::g_misc.m_kill_say.message.value.empty())
                {
                        std::string text = settings::g_misc.m_kill_say.message.value;
                        std::replace(text.begin(), text.end(), '\n', ' ');
                        std::replace(text.begin(), text.end(), '\r', ' ');
                        std::replace(text.begin(), text.end(), '"', '\'');
                        std::replace(text.begin(), text.end(), ';', ' ');
                        const auto cmd = std::format("say \"{}\"", text);
                        memory::call<void>(PATTERN(patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, cmd.c_str(), 0x7ffef001);
                }
        }

        void other::on_frame_stage_notify()
        {
                if ( settings::g_misc.vote_kick_self.value )
                {
                        settings::g_misc.vote_kick_self.value = false;
                        this->vote_kick_self( );
                }

                this->do_player_alpha_changing();
                this->do_reveal_radar();
                this->do_name_changing();
                this->do_chat_spam();
        }

        void other::do_reveal_radar() const
        {
                if (!settings::g_misc.reveal_radar.value)
                {
                        return;
                }

                const auto local = systems::g_local.get();
                if (!local.is_valid())
                {
                        return;
                }

                const auto spotted_state_offset = SCHEMA("C_CSPlayerPawn", "m_entitySpottedState"_hash);
                const auto spotted_offset = SCHEMA("EntitySpottedState_t", "m_bSpotted"_hash);

                for (const auto& player : systems::g_entities.get_by_type(systems::entities::type::player))
                {
                        const auto controller = player.ptr;
                        if (!controller || !memory::read<bool>(controller + SCHEMA("CCSPlayerController", "m_bPawnIsAlive"_hash)))
                        {
                                continue;
                        }

                        const auto pawn_handle = memory::read<std::uint32_t>(controller + SCHEMA("CBasePlayerController", "m_hPawn"_hash));
                        const auto pawn = systems::g_entities.lookup(pawn_handle);
                        if (!pawn || pawn == local.view_pawn())
                        {
                                continue;
                        }

                        const auto team = memory::read<int>(pawn + SCHEMA("C_BaseEntity", "m_iTeamNum"_hash));
                        if (!local.is_this_other_team(team))
                        {
                                continue;
                        }

                        memory::write<bool>(pawn + spotted_state_offset + spotted_offset, true);
                }
        }

        void other::do_autobuy() const
        {
                if (!settings::g_misc.m_autobuy.enabled)
                {
                        return;
                }

                std::string cmd{};

                switch (settings::g_misc.m_autobuy.primary_weapon)
                {
                case 1: cmd += xs("buy ak47; buy m4a1; "); break;
                case 2: cmd += xs("buy sg556; buy aug; "); break;
                case 3: cmd += xs("buy ssg08; "); break;
                case 4: cmd += xs("buy awp; "); break;
                case 5: cmd += xs("buy g3sg1; buy scar20; "); break;
                }

                if (settings::g_misc.m_autobuy.armor)
                {
                        cmd += xs("buy vesthelm; buy vest; ");
                }

                if (settings::g_misc.m_autobuy.taser)
                {
                        cmd += xs("buy taser; ");
                }

                if (settings::g_misc.m_autobuy.defuser)
                {
                        cmd += xs("buy defuser; ");
                }

                switch (settings::g_misc.m_autobuy.secondary_weapon)
                {
                case 1: cmd += xs("buy elite; "); break;
                case 2: cmd += xs("buy fiveseven; buy tec9; "); break;
                case 3: cmd += xs("buy deagle; "); break;
                case 4: cmd += xs("buy revolver; "); break;
                }

                for (auto i = 0; i < 5; ++i)
                {
                        if (!settings::g_misc.m_autobuy.grenades[i])
                        {
                                continue;
                        }

                        switch (i)
                        {
                        case 0: cmd += xs("buy molotov; buy incgrenade; "); break;
                        case 1: cmd += xs("buy hegrenade; "); break;
                        case 2: cmd += xs("buy smokegrenade; "); break;
                        case 3: cmd += xs("buy flashbang; "); break;
                        case 4: cmd += xs("buy decoy; "); break;
                        }
                }

                if (!cmd.empty())
                {
                        memory::call<void>(PATTERN(patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, cmd.c_str(), 0x7ffef001);
                }
        }

        void other::do_player_alpha_changing()
        {
                const auto local = systems::g_local.get();

                if (!local.pawn || !local.is_alive)
                {
                        if (this->m_is_alpha_changed && local.pawn)
                        {
                                memory::call<void>(PATTERN(patterns::game_event_get_string), local.pawn, 255);
                        }

                        this->m_is_alpha_changed = false;
                        return;
                }

                if (!settings::g_esp.m_local_alpha.enabled.value)
                {
                        if (this->m_is_alpha_changed)
                        {
                                this->m_is_alpha_changed = false;
                                memory::call<void>(PATTERN(patterns::game_event_get_string), local.pawn, 255);
                        }

                        return;
                }

                const auto is_scoped = memory::read<bool>(local.pawn + SCHEMA("C_CSPlayerPawn", "m_bIsScoped"_hash));
                const auto should_apply = !settings::g_esp.m_local_alpha.only_scoped.value || is_scoped;

                if (should_apply)
                {
                        this->m_is_alpha_changed = true;
                        const auto alpha = static_cast<std::uint8_t>(settings::g_esp.m_local_alpha.opacity.value * 255.0f);
                        memory::call<void>(PATTERN(patterns::game_event_get_string), local.pawn, alpha);
                }
                else
                {
                        if (this->m_is_alpha_changed)
                        {
                                this->m_is_alpha_changed = false;
                                memory::call<void>(PATTERN(patterns::game_event_get_string), local.pawn, 255);
                        }
                }
        }

        void other::do_name_changing()
        {
                const auto local = systems::g_local.get();
                const auto& cfg = settings::g_misc.m_name_changer;
                const auto enabled = cfg.clantag.value || cfg.override_name.value || cfg.anim_nickname.value;

                if (!enabled)
                {
                        bool need_restore = this->m_name_changer_active;
                        if (!need_restore && local.controller)
                        {
                                const auto cur_name = controller_name(local.controller);
                                if (cur_name == "x")
                                {
                                        need_restore = true;
                                }
                        }

                        if (need_restore && local.controller)
                        {
                                auto steam_name = get_steam_nickname(local.controller);
                                if (steam_name.empty() || steam_name == "x")
                                {
                                        if (!this->m_original_name.empty() && this->m_original_name != "x")
                                        {
                                                steam_name = this->m_original_name;
                                        }
                                }

                                if (!steam_name.empty() && steam_name != "x" && this->m_last_sent_name != steam_name)
                                {
                                        submit_name_change(steam_name);
                                        this->m_last_sent_name = steam_name;
                                }
                        }

                        this->m_name_changer_active = false;
                        this->m_avatar_overridden = false;
                        this->m_name_changer_controller = 0;
                        this->m_original_name.clear();
                        this->m_original_steam_id = 0;
                        this->m_override_name_was_active = false;
                        return;
                }

                if (!local.controller)
                {
                        return;
                }

                if (this->m_name_changer_controller != local.controller)
                {
                        this->m_avatar_overridden = false;
                }

                const auto steam_name = get_steam_nickname(local.controller);
                if (!steam_name.empty() && steam_name != "x")
                {
                        this->m_original_name = steam_name;
                }

                if (!this->m_name_changer_active)
                {
                        if (this->m_original_name.empty() || this->m_original_name == "x")
                        {
                                this->m_original_name = !steam_name.empty() ? steam_name : "Player";
                        }

                        this->m_name_changer_active = true;
                        this->m_name_changer_controller = local.controller;
                        this->m_last_sent_name.clear();
                }
                else if (this->m_name_changer_controller != local.controller)
                {
                        // Keep the captured real name across map loads, where the controller may be recreated.
                        this->m_name_changer_controller = local.controller;
                        this->m_last_sent_name.clear();
                }

                const bool override_name_active = (cfg.override_name.value || cfg.anim_nickname.value) && !cfg.name.value.empty();
                if (this->m_override_name_was_active && !override_name_active)
                {
                        // Override was toggled off while clantag is still active - force immediate update
                        this->m_last_sent_name.clear();
                }
                this->m_override_name_was_active = override_name_active;

                const auto& configured_name = cfg.name.value;
                const auto& base_name = override_name_active
                        ? configured_name
                        : (!this->m_original_name.empty() && this->m_original_name != "x" ? this->m_original_name : (!steam_name.empty() && steam_name != "x" ? steam_name : "Player"));

                std::string animated_name = base_name;
                if (cfg.anim_nickname.value && !base_name.empty())
                {
                        const auto now = std::chrono::steady_clock::now();
                        static auto last_anim_time = now;
                        static int anim_step = 0;

                        const auto speed = std::clamp(cfg.anim_speed.value, 0.05f, 2.0f);
                        const auto elapsed = std::chrono::duration<float>(now - last_anim_time).count();
                        if (elapsed >= speed)
                        {
                                last_anim_time = now;
                                anim_step++;
                        }

                        const auto name_len = static_cast<int>(base_name.size());
                        const auto type = std::clamp(cfg.anim_type.value, 0, 4);

                        switch (type)
                        {
                        case 0: // Marquee (Scroll)
                        {
                                const auto padded = base_name + " ";
                                const auto total = static_cast<int>(padded.size());
                                if (total > 0)
                                {
                                        const auto offset = (anim_step % total + total) % total;
                                        animated_name = padded.substr(offset) + padded.substr(0, offset);
                                }
                                break;
                        }
                        case 1: // Typewriter
                        {
                                const auto cycle = name_len * 2;
                                if (cycle > 0)
                                {
                                        const auto pos = (anim_step % cycle + cycle) % cycle;
                                        const auto count = pos <= name_len ? pos : (cycle - pos);
                                        animated_name = base_name.substr(0, std::max(1, count));
                                }
                                break;
                        }
                        case 2: // Dancing Wave (Case wave)
                        {
                                animated_name = base_name;
                                for (int i = 0; i < name_len; ++i)
                                {
                                        if (((i + anim_step) % 4) < 2)
                                        {
                                                animated_name[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(animated_name[i])));
                                        }
                                        else
                                        {
                                                animated_name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(animated_name[i])));
                                        }
                                }
                                break;
                        }
                        case 3: // Cyber Glitch / Matrix
                        {
                                animated_name = base_name;
                                static constexpr char glitch_chars[] = "01!@#$%^&*<>~";
                                for (int i = 0; i < name_len; ++i)
                                {
                                        if (((i * 7 + anim_step * 3) % 5) == 0 && base_name[i] != ' ')
                                        {
                                                animated_name[i] = glitch_chars[(i + anim_step) % 13];
                                        }
                                }
                                break;
                        }
                        case 4: // Star Pulse / Brackets
                        {
                                static constexpr const char* frames[] = {
                                        "[ %s ]",
                                        "*[ %s ]*",
                                        "**[ %s ]**",
                                        "> %s <",
                                        ">> %s <<",
                                        "-= %s =-"
                                };
                                constexpr auto frame_count = sizeof(frames) / sizeof(frames[0]);
                                const auto frame_idx = (anim_step % frame_count + frame_count) % frame_count;
                                char buf[160]{};
                                std::snprintf(buf, sizeof(buf), frames[frame_idx], base_name.c_str());
                                animated_name = buf;
                                break;
                        }
                        default:
                                break;
                        }
                }

                std::string display_name = animated_name;
                if (cfg.clantag.value)
                {
                        constexpr std::string_view tag{ "mintalynews.t.me" };
                        constexpr auto ticks_per_step{ 32 }; // 0.25 seconds at CS2's 64-tick interval.
                        constexpr auto phase_count{ static_cast<int>(tag.size() * 2) };

                        const auto global_vars = memory::safe_read<std::uintptr_t>(addresses::globals::global_vars).value_or(0);
                        const auto current_tick = global_vars
                                ? memory::safe_read<int>(global_vars + 0x44).value_or(0)
                                : 0;
                        auto phase = current_tick / ticks_per_step % phase_count;
                        if (phase < 0)
                        {
                                phase += phase_count;
                        }

                        const auto reveal_index = phase <= static_cast<int>(tag.size())
                                ? phase
                                : phase_count - phase;
                        const auto visible_tag = tag.substr(0, static_cast<std::size_t>(reveal_index));
                        if (!visible_tag.empty())
                        {
                                display_name.reserve(base_name.size() + visible_tag.size() + 3);
                                display_name = "[";
                                display_name += visible_tag;
                                display_name += "] ";
                                display_name += base_name;
                        }
                }

                if (display_name == this->m_last_sent_name)
                {
                        return;
                }

                submit_name_change(display_name);
                this->m_last_sent_name = std::move(display_name);
        }

        void other::do_kill_feed_preservation()
        {
                const auto local = systems::g_local.get();

                if (!local.pawn || !local.is_alive) {
                        return;
                }

                const auto hud_element = memory::call<std::uintptr_t>(PATTERN(patterns::find_hud_element), xs("CCSGO_HudDeathNotice"));
                if (!hud_element)
                {
                        return;
                }

                memory::write<float>(hud_element + 0x58, settings::g_misc.preserve_killfeed ? 1000.0f : 1.5f);

                float spawntime = memory::read<float>(local.pawn + SCHEMA("C_CSPlayerPawnBase", "m_flLastSpawnTimeIndex"_hash));
                if (m_last_spawntime != spawntime)
                {
                        const auto clear_death_notices = PATTERN(patterns::hud_death_notice_clear);
                        if (clear_death_notices)
                        {
                                memory::call<void>(clear_death_notices, hud_element - 0x20);
                        }

                        m_last_spawntime = spawntime;
                }
        }

        void other::vote_kick_self()
        {
                const auto local = systems::g_local.get();
                if ( !local.controller )
                {
                        return;
                }

                int local_slot = -1;
                for ( int i = 1; i <= 64; ++i )
                {
                        if ( systems::g_entities.get_by_index( i ) == local.controller )
                        {
                                local_slot = i - 1;
                                break;
                        }
                }

                if ( local_slot < 0 )
                {
                        return;
                }

                const auto cmd = std::format( "callvote kick {}", local_slot );
                memory::call<void>( PATTERN( patterns::engine_client_cmd ), addresses::globals::source2engine_to_client, 0, cmd.c_str(), 0x7ffef001 );
        }

        void other::do_chat_spam()
        {
                const auto& cfg = settings::g_misc.m_chat_spam;

                if (!cfg.enabled.value || cfg.message.value.empty())
                {
                        return;
                }

                const bool send_all  = cfg.targets.values[0];
                const bool send_team = cfg.targets.values[1];

                if (!send_all && !send_team)
                {
                        return;
                }

                using clock = std::chrono::steady_clock;
                static auto last_time = clock::now();

                const auto now = clock::now();
                const auto elapsed = std::chrono::duration<float>(now - last_time).count();

                if (elapsed < cfg.delay.value)
                {
                        return;
                }

                last_time = now;

                std::string text = cfg.message.value;
                std::replace(text.begin(), text.end(), '\n', ' ');
                std::replace(text.begin(), text.end(), '\r', ' ');
                std::replace(text.begin(), text.end(), '"', '\'');
                std::replace(text.begin(), text.end(), ';', ' ');

                if (send_all)
                {
                        const auto cmd = std::format("say \"{}\"", text);
                        memory::call<void>(PATTERN(patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, cmd.c_str(), 0x7ffef001);
                }

                if (send_team)
                {
                        const auto cmd = std::format("say_team \"{}\"", text);
                        memory::call<void>(PATTERN(patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, cmd.c_str(), 0x7ffef001);
                }
        }

} // namespace features::misc