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

		void submit_name_change(const std::string& display_name)
		{
			if (display_name.empty())
			{
				return;
			}

			other::s_display_name = display_name;
			other::s_name_change_pending = true;
			memory::call<void>(PATTERN(patterns::engine_client_cmd), addresses::globals::source2engine_to_client, 0, xs("setinfo name x"), 0x7ffef001);
			other::s_name_change_pending = false;
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

		const auto attacker_key = cstypes::event_hash{ 0, "attacker" };
		const auto userid_key = cstypes::event_hash{ 0, "userid" };

		const auto attacker = memory::call<std::uintptr_t>(PATTERN(patterns::game_event_get_controller), event, &attacker_key);
		const auto victim = memory::call<std::uintptr_t>(PATTERN(patterns::game_event_get_controller), event, &userid_key);

		const auto local = systems::g_local.get();
		if (!local.is_valid() || !attacker || attacker != local.controller || victim == local.controller)
		{
			return;
		}

		const auto victim_pawn = memory::call<std::uintptr_t>(PATTERN(patterns::game_event_get_pawn), event, &userid_key);
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

			config::registry::save_active();
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
		const auto enabled = cfg.clantag.value || cfg.override_name.value;

		if (!enabled)
		{
			if (this->m_name_changer_active && local.controller && !this->m_original_name.empty())
			{
				submit_name_change(this->m_original_name);
			}

			this->m_name_changer_active = false;
			this->m_avatar_overridden = false;
			this->m_name_changer_controller = 0;
			this->m_original_name.clear();
			this->m_original_steam_id = 0;
			this->m_last_sent_name.clear();
			other::s_display_name.clear();
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

		if (!this->m_name_changer_active)
		{
			this->m_original_name = controller_name(local.controller);
			if (this->m_original_name.empty())
			{
				this->m_original_name = xs("x");
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

		const auto& configured_name = cfg.name.value;
		const auto& base_name = cfg.override_name.value && !configured_name.empty()
			? configured_name
			: this->m_original_name;

		std::string display_name = base_name;
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