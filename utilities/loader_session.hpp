#pragma once
#include "mintaly_session.hpp"
#include <atomic>
#include <string>
#include <cmath>

namespace loader_session {
inline HANDLE mapping{};
inline std::atomic<mintaly_session::Shared*> shared{nullptr};
inline mintaly_session::Payload payload{};
inline std::atomic<bool> available{false};

// Called on the initialization thread, never from DllMain/entry.
inline bool connect() {
    std::uint64_t born{};
    wchar_t name[128]{};
    if (!mintaly_session::creation_time(GetCurrentProcess(), born) ||
        !mintaly_session::object_name(GetCurrentProcessId(), born, name)) return false;
    HANDLE h = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!h) return false;
    auto* block = static_cast<mintaly_session::Shared*>(MapViewOfFile(
        h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(mintaly_session::Shared)));
    if (!block) { CloseHandle(h); return false; }
    if (block->signature != mintaly_session::magic ||
        block->protocol != mintaly_session::version ||
        block->bytes != sizeof(mintaly_session::Shared) ||
        block->target_pid != GetCurrentProcessId() || block->target_created != born ||
        mintaly_session::read_phase(block) != mintaly_session::created) {
        UnmapViewOfFile(block); CloseHandle(h); return false;
    }
    mapping = h;
    payload = block->payload;
    payload.sub_to[sizeof(payload.sub_to) - 1] = '\0';
    shared.store(block, std::memory_order_release);
    available.store(true, std::memory_order_release);
    InterlockedExchange(&block->phase, mintaly_session::attached);
    return true;
}
inline void fail(const char* message) {
    if (auto* block = shared.load(std::memory_order_acquire)) {
        strncpy_s(block->error, message ? message : "DLL initialization failed", _TRUNCATE);
        InterlockedExchange(&block->phase, mintaly_session::failed);
    }
}
inline void ready() {
    if (auto* block = shared.load(std::memory_order_acquire))
        InterlockedCompareExchange(&block->phase, mintaly_session::initialized,
            mintaly_session::attached);
}
inline void rendered() {
    if (auto* block = shared.load(std::memory_order_acquire)) {
        if (mintaly_session::read_phase(block) == mintaly_session::initialized)
            InterlockedCompareExchange(&block->frame_completed, 1, 0);
    }
}
inline std::string subscription_text() {
    if (!available.load(std::memory_order_acquire)) {
#if defined( DEV )
        return "Developer build";
#else
        return "Subscription unavailable";
#endif
    }
    if (payload.flags & mintaly_session::developer) return "Developer access";
    if (payload.flags & mintaly_session::has_expiry_epoch) {
        const auto remaining = payload.expires_at_unix - mintaly_session::now_unix();
        if (remaining <= 0) return "Subscription expired";
        const auto hours = (remaining + 3599) / 3600;
        return "Subscription: " + std::to_string(hours / 24) + "d " +
            std::to_string(hours % 24) + "h";
    }
    if (payload.sub_to[0]) return std::string("Until ") + payload.sub_to;
    if (std::isfinite(payload.days_left) && payload.days_left >= 0) {
        char text[80]{};
        snprintf(text, sizeof(text), "Subscription: %.1f days (at login)", payload.days_left);
        return text;
    }
    return "Active subscription";
}

enum class access_status {
    granted,
    no_session,
    no_subscription_or_dev,
    subscription_expired
};

inline access_status check_access() {
    if (available.load(std::memory_order_acquire)) {
        if (payload.flags & mintaly_session::developer) {
            return access_status::granted;
        }

        if (!(payload.flags & mintaly_session::subscription_active)) {
            return access_status::no_subscription_or_dev;
        }

        if (payload.flags & mintaly_session::has_expiry_epoch) {
            if (payload.expires_at_unix <= mintaly_session::now_unix()) {
                return access_status::subscription_expired;
            }
        }

        if (std::isfinite(payload.days_left) && payload.days_left <= 0.0 && !payload.sub_to[0]) {
            return access_status::subscription_expired;
        }

        return access_status::granted;
    }

#if defined( DEV )
    return access_status::granted;
#else
    return access_status::no_session;
#endif
}

inline bool has_access() {
    return check_access() == access_status::granted;
}

// Connection remains alive until process exit. Do not unmap from a render callback
// or under the loader lock while another thread may be acknowledging startup.
}
