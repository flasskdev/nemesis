#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <utilities/skin_inspect.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/logging/logging.hpp>
#include <protection/game_addresses.hpp>

namespace features::changer {

    // Render thread submits a value snapshot; only the existing game-thread
    // frame callback invokes the engine. No new Panorama vtable assumptions.
    class inspect_preview
    {
    public:
        void set_available(bool available)
        {
            std::lock_guard lock(m_mutex);
            m_available = available;
            if (!available)
            {
                m_pending.reset();
                m_status = "3D unavailable: frame-stage hook is not active.";
            }
        }

        [[nodiscard]] bool request(const skin_inspect::item& item)
        {
            auto command = skin_inspect::command(item);
            std::lock_guard lock(m_mutex);
            expire();
            if (!m_available)
            {
                m_status = "3D unavailable: frame-stage hook is not active.";
                return false;
            }
            if (!command)
            {
                m_status = "Invalid preview parameters; nothing was sent.";
                return false;
            }
            if (!addresses::globals::source2engine_to_client || !PATTERN(patterns::engine_client_cmd))
            {
                m_status = "3D unavailable: engine command interface was not resolved.";
                return false;
            }
            if (m_pending) return false;
            m_pending = pending{std::move(*command), clock::now() + std::chrono::seconds(3), {}};
            m_status = "Preparing native CS2 inspect...";
            return true;
        }

        // Call AFTER menu::update_ui_state restores the game's input mode.
        void on_render_frame(bool menu_open)
        {
            std::lock_guard lock(m_mutex);
            expire();
            if (!m_pending) return;
            if (menu_open)
            {
                m_pending.reset();
                m_status = "Inspect cancelled: menu reopened.";
                return;
            }
            if (!m_pending->ready_at)
                m_pending->ready_at = clock::now() + std::chrono::milliseconds(200);
        }

        void on_frame_stage_notify()
        {
            std::string command;
            {
                std::lock_guard lock(m_mutex);
                expire();
                if (!m_pending || !m_pending->ready_at || clock::now() < *m_pending->ready_at)
                    return;
                command = std::move(m_pending->command);
                m_pending.reset();
                m_status = "Request sent. No preview? Check the CS2 console / inspect log.";
            }
            const auto engine = addresses::globals::source2engine_to_client;
            const auto execute = PATTERN(patterns::engine_client_cmd);
            if (!engine || !execute)
            {
                std::lock_guard lock(m_mutex);
                m_status = "Inspect failed: engine command interface is unavailable.";
                logging::console::print("[skin_inspect] {}", m_status);
                return;
            }
            // Logging the reproducible, local-only command helps diagnose
            // inspect protocol changes. Dispatch is not confirmation of opening.
            logging::console::print("[skin_inspect] {}", command);
            memory::call<void>(execute, engine, 0, command.c_str(), 0x7ffef001);
        }

        void cancel()
        {
            std::lock_guard lock(m_mutex);
            if (m_pending) m_status = "Inspect cancelled by level change / shutdown.";
            m_pending.reset();
        }

        [[nodiscard]] std::string status()
        {
            std::lock_guard lock(m_mutex);
            expire();
            return m_status;
        }

    private:
        using clock = std::chrono::steady_clock;
        struct pending
        {
            std::string command;
            clock::time_point deadline;
            std::optional<clock::time_point> ready_at;
        };

        void expire()
        {
            if (m_pending && clock::now() >= m_pending->deadline)
            {
                m_pending.reset();
                m_status = "Inspect timed out: no game-thread dispatch. Retry from the main menu.";
                logging::console::print("[skin_inspect] {}", m_status);
            }
        }

        std::mutex m_mutex;
        std::optional<pending> m_pending;
        bool m_available{};
        std::string m_status = "Experimental: 3D opens native CS2 inspect, without equipping.";
    };

    inline inspect_preview g_inspect_preview;
}
