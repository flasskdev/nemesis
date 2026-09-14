#include <pch/pch.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <utilities/steam/steam.hpp>
#include <core/settings.hpp>
#include <external/nlohmann/json.hpp>
#include <core/features/features.hpp>
#include <core/features/changer/skin_sync.hpp>
#include <protection/game_addresses.hpp>

namespace features::misc {



	static constexpr const char* k_setup_script = R"PANORAMA(
(function () {

	if (typeof SClient !== "undefined") {
		SClient = undefined;
	}

	SClient = (function () {
		var handlers = {};
		return {
			register_handler: function (type, callback) { handlers[type] = callback; },
			receive: function (msg) {
				if (msg && handlers[msg.type]) handlers[msg.type](msg);
			}
		};
	})();

	SWeaponManager = (function () {

		function isValid(panel) {
			return panel && panel.IsValid();
		}

		function getScoreboard() {
			var root = $.GetContextPanel();
			if (!isValid(root)) return null;
			var scoreboard = root.id === "Scoreboard" ? root : root.FindChildTraverse("Scoreboard");
			return isValid(scoreboard) ? scoreboard : null;
		}

		function getRow(sb, xuid, account_id) {
			var keys = [xuid, account_id];
			var prefixes = ["player-", "id-", "id-player-", "player_"];
			for (var i = 0; i < keys.length; ++i) {
				if (!keys[i]) continue;
				for (var j = 0; j < prefixes.length; ++j) {
					var row = sb.FindChildTraverse(prefixes[j] + keys[i]);
					if (isValid(row)) return row;
				}
			}
			return null;
		}

		function getLocalRow(sb, xuid, account_id) {
			if (typeof MyPersonaAPI !== "undefined" && typeof MyPersonaAPI.GetXuid === "function") {
				var myXuid = MyPersonaAPI.GetXuid();
				if (myXuid) {
					var r = sb.FindChildTraverse("player-" + myXuid);
					if (isValid(r)) return r;
					r = sb.FindChildTraverse("id-player-" + myXuid);
					if (isValid(r)) return r;
				}
			}

			var row = getRow(sb, xuid, account_id);
			if (isValid(row)) return row;

			var candidates = sb.FindChildrenWithClassTraverse("sb-row--local");
			if (candidates && candidates.length > 0 && isValid(candidates[0])) return candidates[0];

			candidates = sb.FindChildrenWithClassTraverse("is-local-player");
			if (candidates && candidates.length > 0 && isValid(candidates[0])) return candidates[0];

			candidates = sb.FindChildrenWithClassTraverse("local");
			for (var i = 0; candidates && i < candidates.length; ++i) {
				if (isValid(candidates[i]) && candidates[i].id && candidates[i].id.indexOf("player") !== -1)
					return candidates[i];
			}
			return null;
		}

		function getSize(w) {
			var t = w.type, p = w.path;
			if (t === 1) {
				if (p.indexOf("usp_silencer") !== -1) return "42px";
				if (p.indexOf("deagle")       !== -1) return "36px";
				if (p.indexOf("revolver")     !== -1) return "32px";
				if (p.indexOf("tec9")         !== -1) return "36px";
				if (p.indexOf("elite")        !== -1) return "32px";
				return "30px";
			}
			if (t === 2 || t === 3 || t === 4 || t === 5 || t === 6)
				return p.indexOf("mac10") !== -1 ? "30px" : "52px";
			if (t === 9) {
				if (p.indexOf("incgrenade")   !== -1 || p.indexOf("smokegrenade") !== -1) return "14px";
				if (p.indexOf("molotov")      !== -1 || p.indexOf("flashbang")    !== -1) return "16px";
				return "14px";
			}
			if (t === 7)  return "16px";
			if (t === 8)  return "22px";
			if (t === 11) return p.indexOf("healthshot") !== -1 ? "18px" : "14px";
			if (t === 0)  return "46px";
			return "16px";
		}

		function updateWeaponIcon(parent, w, active_path) {
			var id   = "wep_" + w.path.replace(/[^a-zA-Z0-9]/g, "_");
			var img  = parent.FindChildTraverse(id);
			if (!isValid(img)) {
				img = $.CreatePanel("Image", parent, id);
			}

			img.style.verticalAlign = "center";
			img.style.margin        = "0px 1px";
			img.scaling    = "stretch-aspect-preserve";

			var finalPath = w.path;
			if (finalPath.indexOf("file://") !== 0) {
				if (finalPath.indexOf("icons/equipment") === -1)
					finalPath = "icons/equipment/" + finalPath;
				if (finalPath.indexOf(".svg") === -1 && finalPath.indexOf(".vsvg") === -1)
					finalPath += ".svg";
				finalPath = "file://{images}/" + finalPath;
			}
			img.SetImage(finalPath);

			var sz           = getSize(w);
			img.style.height = "16px";
			img.style.width  = sz;
			img.style.opacity = (w.path === active_path) ? "1.0" : "0.35";
			img.style.visibility = "visible";
		}

		function updateNow(xuid, account_id, weapons, active_path) {
			var sb = getScoreboard();
			if (!sb) return;

			var row = getRow(sb, xuid, account_id);
			if (!row) return;

			var nameIcons = row.FindChildTraverse("id-sb-name__nameicons");
			if (!isValid(nameIcons)) return;

			var containerId = "custom-weapons-container-" + xuid;
			var container   = nameIcons.FindChildTraverse(containerId);

			if (!weapons || weapons.length === 0) {
				if (isValid(container)) container.style.visibility = "collapse";
				return;
			}

			if (!isValid(container)) {
				container = $.CreatePanel("Panel", nameIcons, containerId);
				container.AddClass("custom-weapons-container");
				container.style.flowChildren     = "right";
				container.style.height           = "20px";
				container.style.width            = "fit-children";
				container.style.padding          = "1px 4px";
				container.style.verticalAlign    = "center";
				container.style.marginLeft       = "3px";
				container.style.backgroundColor  = "rgba(0,0,0,0.35)";
				container.style.borderRadius     = "3px";
				container.style.border           = "1px solid rgba(255,255,255,0.18)";
			}

			// Scoreboard rows cache their paint commands. Keep panel identities stable:
			// deleting children while Panorama is updating that cache can leave native
			// panel references dangling until the next paint pass.
			var children = container.Children();
			for (var i = 0; i < children.length; ++i) {
				if (isValid(children[i])) children[i].style.visibility = "collapse";
			}

			var primary = [], pistols = [], equip = [];
			weapons.forEach(function (w) {
				if (w.type === 2 || w.type === 3 || w.type === 4 || w.type === 5 || w.type === 6)
					primary.push(w);
				else if (w.type === 1)
					pistols.push(w);
				else
					equip.push(w);
			});

			primary.concat(pistols).concat(equip).forEach(function (w) {
				updateWeaponIcon(container, w, active_path);
			});
			container.style.visibility = "visible";
		}

		return {
			update: function (xuid, account_id, weapons, active_path) {
				updateNow(xuid, account_id, weapons, active_path);
			},

			clear: function () {
				var sb = getScoreboard();
				if (!sb) return;
				var containers = sb.FindChildrenWithClassTraverse("custom-weapons-container");
				for (var i = 0; i < containers.length; ++i) {
					if (isValid(containers[i])) containers[i].style.visibility = "collapse";
				}
			}
		};

	})();

	SIndicatorManager = (function () {
		function isValid(panel) { return panel && panel.IsValid(); }
		function getScoreboard() {
			var root = $.GetContextPanel();
			while (isValid(root)) {
				if (root.id === "Scoreboard") return root;
				var sb = root.FindChildTraverse("Scoreboard");
				if (isValid(sb)) return sb;
				root = typeof root.GetParent === "function" ? root.GetParent() : null;
			}
			return null;
		}
		function getRow(sb, xuid, account_id, name) {
			var keys = [String(xuid || ""), String(account_id || "")];
			var prefixes = ["player-", "id-", "id-player-", "player_"];
			for (var i = 0; i < keys.length; ++i) {
				if (!keys[i] || keys[i] === "0") continue;
				for (var j = 0; j < prefixes.length; ++j) {
					var row = sb.FindChildTraverse(prefixes[j] + keys[i]);
					if (isValid(row)) return row;
				}
			}
			if (typeof MyPersonaAPI !== "undefined" && typeof MyPersonaAPI.GetXuid === "function" &&
				String(MyPersonaAPI.GetXuid()) === keys[0]) {
				var localRows = sb.FindChildrenWithClassTraverse("sb-row--local");
				if (localRows && localRows.length === 1 && isValid(localRows[0])) return localRows[0];
			}
			// Name fallback is allowed only for one unambiguous row.
			if (!name) return null;
			var labels = sb.FindChildrenWithClassTraverse("sb-name__label");
			if (!labels || !labels.length) labels = sb.FindChildrenWithClassTraverse("id-sb-name__label");
			var match = null;
			for (var k = 0; labels && k < labels.length; ++k) {
				var label = labels[k];
				if (!isValid(label) || label.text !== name) continue;
				var parent = label.GetParent();
				while (isValid(parent) && parent !== sb) {
					if (isValid(parent.FindChildTraverse("id-sb-name__nameicons"))) {
						if (match && match !== parent) return null;
						match = parent;
						break;
					}
					parent = parent.GetParent();
				}
			}
			return match;
		}
		function visibleBranch(panel, row) {
			for (var p = panel; isValid(p); p = p.GetParent()) {
				if (p.visible === false || p.style.visibility === "collapse" || p.style.visibility === "hidden") return false;
				if (p === row) return true;
			}
			return false;
		}
		function updateIndicator(xuid, account_id, name, show) {
			var sb = getScoreboard();
			if (!sb) return;
			var row = getRow(sb, xuid, account_id, name);
			if (!row) return;
			// A fixed ID is safe within a row and survives SteamID/name changes.
			var id = "mintaly_user_indicator";
			var badge = row.FindChildTraverse(id);
			if (!show) {
				if (isValid(badge)) badge.style.visibility = "collapse";
				return;
			}
			// The medals column can be collapsed for players without medals.
			// Use name icons instead, without hiding any native UI children.
			var parent = row.FindChildTraverse("id-sb-name__nameicons");
			if (!isValid(parent) || !visibleBranch(parent, row)) parent = row;
			if (isValid(badge) && badge.GetParent() !== parent) {
				if (typeof badge.SetParent !== "function") return;
				badge.SetParent(parent);
			}
			if (!isValid(badge)) {
				badge = $.CreatePanel("Panel", parent, id);
				badge.AddClass("mintaly-indicator-badge");
			}
			badge.visible = true;
			badge.style.verticalAlign = "center";
			badge.style.horizontalAlign = "left";
			badge.style.flowChildren = "none";
			badge.style.height = "18px";
			badge.style.width = "18px";
			badge.style.minWidth = "18px";
			badge.style.margin = "0px 3px";
			badge.style.padding = "0px";
			badge.style.borderRadius = "3px";
			badge.style.backgroundColor = "#0C0C10F2";
			badge.style.border = "1px solid #FFFFFF47";
			badge.style.opacity = "1";
			var label = badge.FindChildTraverse(id + "_txt");
			if (!isValid(label)) label = $.CreatePanel("Label", badge, id + "_txt");
			label.text = "M";
			label.visible = true;
			label.style.color = "#FFFFFF";
			label.style.fontSize = "12px";
			label.style.fontWeight = "bold";
			label.style.fontFamily = "Stratum2";
			label.style.textAlign = "center";
			label.style.verticalAlign = "center";
			label.style.horizontalAlign = "center";
			label.style.margin = "0px";
			label.style.padding = "0px";
			label.style.visibility = "visible";
			badge.style.visibility = "visible";
		}
		return {
			update: updateIndicator,
			clear: function () {
				var sb = getScoreboard();
				if (!sb) return;
				var badges = sb.FindChildrenWithClassTraverse("mintaly-indicator-badge");
				for (var i = 0; badges && i < badges.length; ++i) {
					if (isValid(badges[i])) badges[i].style.visibility = "collapse";
				}
			}
		};
	})();

	SChatManager = (function () {
		function isValid(panel) {
			return panel && panel.IsValid();
		}

		function getPanoramaRoot() {
			var p = $.GetContextPanel();
			if (!isValid(p)) return null;
			while (p && typeof p.GetParent === "function" && p.GetParent()) {
				p = p.GetParent();
			}
			return isValid(p) ? p : null;
		}

		function findFirstLabel(panel) {
			if (!isValid(panel)) return null;
			if (panel.paneltype === "Label") return panel;
			var ch = panel.Children ? panel.Children() : [];
			for (var i = 0; i < ch.length; ++i) {
				var lbl = findFirstLabel(ch[i]);
				if (lbl) return lbl;
			}
			return null;
		}

		function createMintalyBadge(parent, text, isSmallSquare) {
			var id = "mintaly_badge_" + Math.floor(Math.random() * 10000000);
			var badge = $.CreatePanel("Panel", parent, id);
			badge.AddClass("mintaly-chat-badge");
			badge.style.verticalAlign = "center";
			badge.style.horizontalAlign = "left";
			badge.style.borderRadius = "3px";
			badge.style.backgroundColor = "gradient(linear, 0% 0%, 100% 100%, from(#2A3042EE), to(#1E2230EE))";
			badge.style.border = "1px solid rgba(173, 192, 255, 0.45)";
			badge.style.margin = "0px 6px 0px 1px";

			if (isSmallSquare) {
				badge.style.width = "16px";
				badge.style.height = "16px";
				badge.style.flowChildren = "none";
			} else {
				badge.style.height = "18px";
				badge.style.padding = "1px 6px";
				badge.style.flowChildren = "none";
			}

			var lbl = $.CreatePanel("Label", badge, id + "_txt");
			lbl.text = text || "mintaly";
			lbl.style.color = "#ADC0FFFF";
			lbl.style.fontSize = "11px";
			lbl.style.fontWeight = "bold";
			lbl.style.fontFamily = "Stratum2, Arial, sans-serif";
			lbl.style.textAlign = "center";
			lbl.style.verticalAlign = "center";
			lbl.style.horizontalAlign = "center";
			lbl.style.lineHeight = "16px";

			return badge;
		}

		function processVoiceAlert(p) {
			if (!isValid(p)) return;
			if (p.GetAttributeInt("mintaly_done", 0) === 1) return;

			var textLabel = p.FindChildTraverse("Text");
			if (!isValid(textLabel)) {
				textLabel = findFirstLabel(p);
			}
			if (!isValid(textLabel)) return;

			var rawText = textLabel.text || "";
			if (!rawText || rawText.length === 0) return;

			var plain = rawText.replace(/<[^>]*>/g, "").trim();
			if (plain.indexOf("mintaly") !== -1 || rawText.indexOf("mintaly") !== -1) {
				var cleanText = plain.replace(/^\[?\s*mintaly\s*\]?\s*:\s*/i, "");
				cleanText = cleanText.replace(/^\s*:\s*/, "");

				var textColor = "#FFFFFF";
				if (plain.indexOf("missed") !== -1) {
					textColor = "#FF6B8B";
				} else if (plain.indexOf("hit") !== -1) {
					textColor = "#59C9A5";
				} else if (plain.indexOf("vote") !== -1) {
					textColor = "#ADC0FF";
				}

				p.style.flowChildren = "right";
				var badge = createMintalyBadge(p, "mintaly", false);
				p.MoveChildBefore(badge, textLabel);

				textLabel.html = true;
				textLabel.text = "<font color='" + textColor + "'>" + cleanText + "</font>";
				textLabel.style.verticalAlign = "center";
				p.SetAttributeInt("mintaly_done", 1);
				return;
			}
		}

		function traverseVoiceAlerts(panel, depth) {
			if (!isValid(panel) || depth > 5) return;
			if (panel.paneltype === "Label") return;

			if (panel.GetAttributeInt("mintaly_done", 0) === 1) return;

			var lbl = panel.FindChildTraverse("Text");
			if (!isValid(lbl)) lbl = findFirstLabel(panel);

			if (isValid(lbl) && lbl.text) {
				var t = lbl.text;
				if (t.indexOf("mintaly") !== -1 || t.indexOf("Mintaly") !== -1) {
					processVoiceAlert(panel);
					return;
				}
			}

			var children = panel.Children ? panel.Children() : [];
			for (var i = 0; i < children.length; ++i) {
				traverseVoiceAlerts(children[i], depth + 1);
			}
		}

		function processChatLines() {
			var top = getPanoramaRoot();
			if (!isValid(top)) return;

			var voiceHud = top.FindChildTraverse("CCSGO_HudVoiceStatus");
			if (!isValid(voiceHud)) voiceHud = top.FindChildTraverse("HudVoiceStatus");
			if (isValid(voiceHud)) {
				traverseVoiceAlerts(voiceHud, 0);
			}
		}

		function startTimer() {
			processChatLines();
			$.Schedule(0.04, startTimer);
		}
		startTimer();

		return {
			process: function () {
				processChatLines();
			}
		};
	})();

	SClient.register_handler("updateWeapons", function (msg) {
		if (msg && msg.content)
			SWeaponManager.update(msg.content.xuid, msg.content.account_id, msg.content.weapons, msg.content.active_path);
	});

	SClient.register_handler("clearWeapons", function (msg) {
		if (msg && msg.content)
			SWeaponManager.update(msg.content.xuid, msg.content.account_id, [], "");
	});

	SClient.register_handler("updateIndicator", function (msg) {
		if (msg && msg.content)
			SIndicatorManager.update(msg.content.xuid, msg.content.account_id, msg.content.name, msg.content.show);
	});

})();
)PANORAMA";

	void c_ui_engine::run_script (c_ui_panel* panel, const char* script) {
		static constexpr char origin_file[] = "";
		memory::call_vfunc<void> (
			reinterpret_cast<std::uintptr_t>(this), 77,
			panel, script,
			origin_file,
			static_cast<std::uint64_t>(1));
	}

	c_ui_engine* c_panorama_ui_engine::get_ui_engine () {
		return memory::call_vfunc<c_ui_engine*> (
			reinterpret_cast<std::uintptr_t>(this), 13);
	}

	c_ui_panel* scoreboard_weapons::find_hud_panel () const {
		if (!addresses::globals::hud)
			return nullptr;

		const auto hud = memory::safe_read<std::uintptr_t>(addresses::globals::hud).value_or(0);
		if (!hud)
			return nullptr;

		const auto panel = memory::safe_read<c_ui_panel*>(hud + 0x8).value_or(nullptr);
		if (!panel)
			return nullptr;

		const auto vtable = memory::safe_read<std::uintptr_t>(
			reinterpret_cast<std::uintptr_t>(panel));
		return vtable && *vtable ? panel : nullptr;
	}

	void scoreboard_weapons::on_level_change () {
		m_script_injected = false;
		m_cache.clear ();
		m_scoreboard_open = false;
		m_throttle = 0;
		m_init_throttle = 0;
		m_ui_engine = nullptr;
		m_script_panel = nullptr;

		logging::console::print (xs ("[scoreboard_weapons] level change — state reset\n"));
	}

	void scoreboard_weapons::on_frame_stage_notify () {
		if (!m_script_injected) {
			++m_init_throttle;
			if (m_init_throttle % 10 == 0)
				try_initialize ();
		}

		const auto scoreboard_open = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
		if (!scoreboard_open) {
			if (m_scoreboard_open && m_script_injected)
				clear_all();
			m_scoreboard_open = false;
			return;
		}

		if (!m_scoreboard_open) {
			m_scoreboard_open = true;
			m_cache.clear();
			m_throttle = 0;
			try_initialize();
		}

		if (!m_script_injected) {
			return;
		}

		++m_throttle;

		if (m_throttle % 8 != 0)
			return;
		if (m_throttle % 64 == 0)
			m_cache.clear();

		const auto players = systems::g_entities.get_by_type (systems::entities::type::player);

		// Send indicators for all players in scoreboard
		for (const auto& player : players) {
			if (!player.ptr)
				continue;

			send_player_indicator (player.ptr);
		}

		if (settings::g_misc.m_scoreboard_weapons.enabled.value) {
			const auto items = systems::g_entities.get_by_type (systems::entities::type::item);
			for (const auto& player : players) {
				if (!player.ptr)
					continue;

				send_player_weapons (player.ptr, items);
			}
		}
	}

	void scoreboard_weapons::try_initialize () {
		if (m_script_injected)
			return;

		if (!addresses::globals::panorama) {
			logging::console::print (xs ("[scoreboard_weapons] panorama address not ready\n"));
			return;
		}

		auto* panorama = reinterpret_cast<c_panorama_ui_engine*>(addresses::globals::panorama);
		auto* ui_engine = panorama->get_ui_engine ();
		if (!ui_engine) {
			logging::console::print (xs ("[scoreboard_weapons] get_ui_engine returned null\n"));
			return;
		}

		const auto script_panel = find_hud_panel();
		if (!script_panel) {
			logging::console::print (xs ("[scoreboard_weapons] HUD script panel not ready\n"));
			return;
		}

		m_ui_engine = ui_engine;
		m_script_panel = script_panel;

		logging::console::print (xs ("[scoreboard_weapons] injecting HUD setup script (engine={:p} panel={:p})\n"),
			static_cast<void*>(m_ui_engine),
			static_cast<void*>(m_script_panel));

		if (run_script(k_setup_script)) {
			m_script_injected = true;
			logging::console::print (xs ("[scoreboard_weapons] setup script injected\n"));
		}
	}

	bool scoreboard_weapons::run_script (const std::string& script) {
		if (!m_script_injected && script != k_setup_script) {
			try_initialize();
		}

		if (!m_ui_engine || !m_script_panel)
			return false;

		auto* panorama = reinterpret_cast<c_panorama_ui_engine*>(addresses::globals::panorama);
		if (!panorama || panorama->get_ui_engine() != m_ui_engine ||
			find_hud_panel() != m_script_panel) {
			m_script_injected = false;
			m_ui_engine = nullptr;
			m_script_panel = nullptr;
			m_cache.clear();
			return false;
		}

		const auto engine = reinterpret_cast<std::uintptr_t>(m_ui_engine);
		const auto vtable = memory::safe_read<std::uintptr_t>(engine);
		const auto function = vtable ? memory::safe_read<std::uintptr_t>(
			*vtable + 77 * sizeof(std::uintptr_t)) : std::nullopt;
		const auto panorama_begin = addresses::modules::panorama;
		const auto panorama_end = panorama_begin + memory::get_module_size(panorama_begin);
		if (!function || *function < panorama_begin || *function >= panorama_end)
			return false;

		m_ui_engine->run_script (m_script_panel, script.c_str ());
		return true;
	}

	void scoreboard_weapons::send_player_weapons (
		std::uintptr_t controller,
		std::span<const systems::entities::cached> items) {
		if (!controller)
			return;

		constexpr std::uint64_t steam_id_base = 76561197960265728ull;
		const auto steamid = memory::safe_read<std::uint64_t> (
			controller + SCHEMA ("CBasePlayerController", "m_steamID"_hash)).value_or(0);
		if (steamid < steam_id_base)
			return;

		const auto pawn_handle = memory::safe_read<std::uint32_t> (
			controller + SCHEMA ("CBasePlayerController", "m_hPawn"_hash)).value_or(0);
		if (!pawn_handle)
			return;

		const auto pawn = systems::g_entities.lookup (pawn_handle);

		player_weapon_state state {};
		const auto read_weapon = [] (std::uintptr_t weapon) -> std::optional<weapon_entry> {
			if (!weapon)
				return std::nullopt;

			const auto vdata = memory::safe_read<std::uintptr_t> (
				weapon + SCHEMA ("C_BaseEntity", "m_nSubclassID"_hash) + 0x8).value_or(0);
			if (!vdata)
				return std::nullopt;

			const auto name_ptr = memory::safe_read<const char*> (
				vdata + SCHEMA ("CCSWeaponBaseVData", "m_szName"_hash)).value_or(nullptr);
			if (!name_ptr)
				return std::nullopt;

			std::string name {};
			name.reserve(32);
			for (std::size_t i = 0; i < 64; ++i) {
				const auto character = memory::safe_read<char> (
					reinterpret_cast<std::uintptr_t>(name_ptr) + i);
				if (!character)
					return std::nullopt;
				if (*character == '\0')
					break;
				name.push_back(*character);
			}

			if (!name.starts_with(xs ("weapon_")))
				return std::nullopt;
			name.erase(0, 7);

			const auto type = memory::safe_read<std::uint32_t> (
				vdata + SCHEMA ("CCSWeaponBaseVData", "m_WeaponType"_hash));
			if (!type)
				return std::nullopt;

			return weapon_entry {std::move(name), static_cast<int>(*type)};
		};

		if (pawn) {
			const auto health = memory::safe_read<int> (
				pawn + SCHEMA ("C_BaseEntity", "m_iHealth"_hash)).value_or(0);

			if (health > 0) {
				const auto weapon_services = memory::safe_read<std::uintptr_t> (
					pawn + SCHEMA ("C_BasePlayerPawn", "m_pWeaponServices"_hash)).value_or(0);

				if (weapon_services) {
					const auto active_handle = memory::safe_read<std::uint32_t> (
						weapon_services + SCHEMA ("CPlayer_WeaponServices", "m_hActiveWeapon"_hash)).value_or(0);
					const auto active_weapon = active_handle
						? systems::g_entities.lookup (active_handle) : 0;

					if (const auto active = read_weapon(active_weapon))
						state.active_name = active->name;
				}

				// Enemy inventory handles are not reliably populated in m_hMyWeapons.
				// Weapon entities and their networked owner handles are, so mirror the
				// working scoreboard implementation and collect by owner instead.
				for (const auto& item : items) {
					if (!item.ptr)
						continue;

					const auto owner_handle = memory::safe_read<std::uint32_t> (
						item.ptr + SCHEMA ("C_BaseEntity", "m_hOwnerEntity"_hash)).value_or(0);
					if (!owner_handle || systems::g_entities.lookup(owner_handle) != pawn)
						continue;

					if (auto weapon = read_weapon(item.ptr))
						state.weapons.emplace_back(std::move(*weapon));
				}
			}
		}

		if (m_throttle == 8) {
			logging::console::print (xs ("[scoreboard_weapons] xuid={} collected_weapons={}\n"),
				steamid, state.weapons.size());
		}

		// skip if nothing changed
		const auto it = m_cache.find (steamid);
		if (it != m_cache.end () && it->second == state)
			return;

		if (state.weapons.empty ()) {
			if (send_clear(steamid))
				m_cache[steamid] = state;
			return;
		}

		// sort: primaries → pistols → grenades/other  (mirrors JS-side sort)
		auto sort_key = [] (int type) -> int {
			if (type == 2 || type == 3 || type == 4 || type == 5 || type == 6) return 0; // primary
			if (type == 1)                                                      return 1; // pistol
			return 2;                                                                       // grenades / equipment
		};
		std::stable_sort (state.weapons.begin (), state.weapons.end (),
			[&] (const weapon_entry& a, const weapon_entry& b) { return sort_key (a.type) < sort_key (b.type); });

		// build weapons JSON
		std::string weapons_json = "[";
		for (std::size_t i = 0; i < state.weapons.size (); ++i) {
			weapons_json += std::format (R"({{path:"{}",type:{}}})",
				state.weapons [i].name, state.weapons [i].type);
			if (i + 1 < state.weapons.size ())
				weapons_json += ",";
		}
		weapons_json += "]";

		const auto account_id = steamid - steam_id_base;
		const auto script = std::format (
			R"(if(typeof(SClient)!=='undefined'){{SClient.receive({{type:"updateWeapons",content:{{xuid:"{}",account_id:"{}",weapons:{},active_path:"{}"}}}});}})",
			steamid,
			account_id,
			weapons_json,
			state.active_name
		);

		if (run_script(script))
			m_cache[steamid] = std::move(state);
	}

	void scoreboard_weapons::send_player_indicator (std::uintptr_t controller) {
		if (!controller)
			return;

		constexpr std::uint64_t steam_id_base = 76561197960265728ull;
		const auto steamid = memory::safe_read<std::uint64_t> (
			controller + SCHEMA ("CBasePlayerController", "m_steamID"_hash)).value_or(0);

		const bool is_local = memory::safe_read<bool>(
			controller + SCHEMA("CBasePlayerController", "m_bIsLocalPlayerController"_hash)).value_or(false);
		if (is_local && steamid >= steam_id_base) {
			features::changer::g_skin_sync.set_local_steam_id(steamid);
		}

		const bool show = features::changer::g_skin_sync.should_show_indicator (steamid);
		const auto account_id = (steamid >= steam_id_base) ? (steamid - steam_id_base) : steamid;

		std::string player_name{};
		const auto name_ptr = memory::safe_read<std::uintptr_t>(
			controller + SCHEMA("CCSPlayerController", "m_sSanitizedPlayerName"_hash)).value_or(0);
		if (name_ptr) {
			player_name = memory::read_string(name_ptr, 127);
		}
		if (player_name.empty()) {
			player_name = memory::read_string(controller + SCHEMA("CBasePlayerController", "m_iszPlayerName"_hash), 127);
		}

		const nlohmann::json message = {
			{"type", "updateIndicator"},
			{"content", {
				{"xuid", std::to_string(steamid)},
				{"account_id", std::to_string(account_id)},
				{"name", player_name}, {"show", show}
			}}
		};
		// JSON escaping includes line breaks, control characters and Unicode separators.
		const auto script = std::string("if(typeof(SClient)!=='undefined'){SClient.receive(") +
			message.dump(-1, ' ', true, nlohmann::json::error_handler_t::replace) + ");}";

		run_script (script);
	}

	bool scoreboard_weapons::send_clear (std::uint64_t steamid) {
		constexpr std::uint64_t steam_id_base = 76561197960265728ull;
		const auto account_id = steamid >= steam_id_base ? steamid - steam_id_base : steamid;
		const auto script = std::format (
			R"(if(typeof(SClient)!=='undefined'){{SClient.receive({{type:"clearWeapons",content:{{xuid:"{}",account_id:"{}"}}}});}})",
			steamid,
			account_id
		);

		return run_script(script);
	}

	void scoreboard_weapons::clear_all () {
		if (!m_script_injected)
			return;

		(void)run_script(R"(if(typeof(SWeaponManager)!=='undefined'){SWeaponManager.clear();}if(typeof(SIndicatorManager)!=='undefined'){SIndicatorManager.clear();})");
		m_cache.clear ();
	}

} // namespace features::misc
