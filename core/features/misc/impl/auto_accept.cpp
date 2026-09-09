#include <pch/pch.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <core/rendering/rendering.hpp>
#include <protection/game_addresses.hpp>
#include "../misc.hpp"

namespace features::misc {

	void auto_accept::reset()
	{
		m_match_detected = false;
		m_accepted = false;
		m_found_time = {};
		m_last_check = {};
	}

	void auto_accept::run()
	{
		if (!settings::g_misc.auto_accept.value)
		{
			m_match_detected = false;
			m_accepted = false;
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now - m_last_check < std::chrono::milliseconds(100))
			return;
		m_last_check = now;

		// Check if the accept popup is showing via Panorama panel tree
		bool waiting = false;

		if (addresses::globals::panorama)
		{
			auto* panorama = reinterpret_cast<c_panorama_ui_engine*>(addresses::globals::panorama);
			if (panorama)
			{
				auto* ui_engine = panorama->get_ui_engine();
				if (ui_engine && ui_engine->m_panels_array)
				{
					// Search for popup_accept_match panel in the panel list
					for (int i = 0; i < ui_engine->m_panel_count; ++i)
					{
						const auto& pd = ui_engine->m_panels_array[i];
						if (!pd.m_panel || !pd.m_visible)
							continue;

						auto* panel = pd.m_panel;
						if (!panel->m_panel_name)
							continue;

						// The match accept popup panel
						if (strcmp(panel->m_panel_name, "PopupAcceptMatch") == 0 ||
							strcmp(panel->m_panel_name, "popup_accept_match") == 0 ||
							strcmp(panel->m_panel_name, "MatchAccept") == 0)
						{
							waiting = true;
							break;
						}
					}
				}
			}
		}

		// Fallback: try native functions if patterns resolved
		if (!waiting)
		{
			if (!m_initialized)
			{
				m_fn_is_match_waiting = reinterpret_cast<fn_is_match_waiting>(
					PATTERN(patterns::is_match_waiting));
				m_fn_get_ready_time = reinterpret_cast<fn_get_ready_time>(
					PATTERN(patterns::get_ready_time_remaining));
				m_initialized = true;
			}

			if (m_fn_get_ready_time)
			{
				const auto remaining = m_fn_get_ready_time(nullptr);
				if (remaining > 0)
					waiting = true;
			}

			if (!waiting && m_fn_is_match_waiting)
			{
				if (m_fn_is_match_waiting())
					waiting = true;
			}
		}

		if (waiting)
		{
			if (!m_match_detected)
			{
				m_match_detected = true;
				m_found_time = now;
				logging::console::print(xs("[auto_accept] match found! accepting in 0.5s...\n"));
			}

			constexpr auto k_delay = 0.5f;
			const auto elapsed = std::chrono::duration<float>(now - m_found_time).count();

			if (elapsed >= k_delay && !m_accepted)
			{
				this->accept_match();
				m_accepted = true;
			}
		}
		else
		{
			m_match_detected = false;
			m_accepted = false;
		}
	}

	void auto_accept::accept_match()
	{
		logging::console::print(xs("[auto_accept] accepting match!\n"));

		// Dispatch Panorama MatchAssistedAccept event through the UI engine
		if (addresses::globals::panorama)
		{
			auto* panorama = reinterpret_cast<c_panorama_ui_engine*>(addresses::globals::panorama);
			if (panorama)
			{
				auto* ui_engine = panorama->get_ui_engine();
				if (ui_engine && addresses::globals::hud)
				{
					const auto hud = memory::safe_read<std::uintptr_t>(addresses::globals::hud).value_or(0);
					if (hud)
					{
						const auto panel = memory::safe_read<c_ui_panel*>(hud + 0x8).value_or(nullptr);
						if (panel)
						{
							const auto vtable = memory::safe_read<std::uintptr_t>(
								reinterpret_cast<std::uintptr_t>(panel));
							if (vtable && *vtable)
							{
								// Try multiple event names that different CS2 versions use
								ui_engine->run_script(panel, "$.DispatchEvent('MatchAssistedAccept');");
								ui_engine->run_script(panel, "$.DispatchEvent('CSGOReadyUpForMatch');");
								
								logging::console::print(xs("[auto_accept] dispatched Panorama events\n"));
							}
						}
					}
				}
			}
		}

		// Alert user
		const auto hwnd = rendering::g_context.get_window();
		if (hwnd && GetForegroundWindow() != hwnd)
		{
			FLASHWINFO fi{};
			fi.cbSize = sizeof(FLASHWINFO);
			fi.hwnd = hwnd;
			fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
			fi.uCount = 5;
			fi.dwTimeout = 0;
			FlashWindowEx(&fi);
			MessageBeep(MB_ICONINFORMATION);
		}
	}

} // namespace features::misc
