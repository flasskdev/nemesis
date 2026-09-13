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
#include <ctime>
#include <cstdio>

inline void sync_payload() {
    if (auto* block = shared.load(std::memory_order_acquire)) {
        payload = block->payload;
        payload.sub_to[sizeof(payload.sub_to) - 1] = '\0';
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
    sync_payload();
    if (payload.flags & mintaly_session::developer) return "Developer access";
    if (payload.flags & mintaly_session::has_expiry_epoch) {
        const auto remaining = payload.expires_at_unix - mintaly_session::now_unix();
        if (remaining <= 0) return "Subscription expired";
        const auto days = remaining / 86400;
        if (days >= 2) return std::to_string(days) + " days left";
        if (days == 1) return "1 day left";
        const auto hours = (remaining + 3599) / 3600;
        if (hours > 1) return std::to_string(hours) + " hours left";
        if (hours == 1) return "1 hour left";
        return "< 1 hour left";
    }
    if (std::isfinite(payload.days_left) && payload.days_left >= 0.0) {
        double rem_days = payload.days_left;
        if (payload.received_at_unix > 0) {
            const auto elapsed = mintaly_session::now_unix() - payload.received_at_unix;
            if (elapsed > 0) {
                rem_days -= static_cast<double>(elapsed) / 86400.0;
            }
        }
        if (rem_days <= 0.0) return "Subscription expired";
        const auto days = static_cast<int>(std::round(rem_days));
        if (days >= 2) return std::to_string(days) + " days left";
        if (days == 1) return "1 day left";
        const auto hours = static_cast<int>(std::round(rem_days * 24.0));
        if (hours > 1) return std::to_string(hours) + " hours left";
        if (hours == 1) return "1 hour left";
        return "< 1 hour left";
    }
    if (payload.sub_to[0]) {
        int y = 0, m = 0, d = 0, hr = 0, mn = 0, sc = 0;
        bool parsed = false;
        if (std::sscanf(payload.sub_to, "%d-%d-%d", &y, &m, &d) == 3 ||
            std::sscanf(payload.sub_to, "%d/%d/%d", &y, &m, &d) == 3) {
            std::sscanf(payload.sub_to, "%*d%*[-/]%*d%*[-/]%*d%*[ T]%d:%d:%d", &hr, &mn, &sc);
            parsed = true;
        } else if (std::sscanf(payload.sub_to, "%d.%d.%d", &d, &m, &y) == 3) {
            std::sscanf(payload.sub_to, "%*d.%*d.%*d%*[ T]%d:%d:%d", &hr, &mn, &sc);
            parsed = true;
        }
        if (parsed) {
            if (y < 100) y += 2000;
            if (y >= 1970 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
                std::tm t{};
                t.tm_year = y - 1900;
                t.tm_mon = m - 1;
                t.tm_mday = d;
                t.tm_hour = hr;
                t.tm_min = mn;
                t.tm_sec = sc;
                t.tm_isdst = -1;
                const auto epoch = std::mktime(&t);
                if (epoch > 0) {
                    const auto remaining = static_cast<std::int64_t>(epoch) - mintaly_session::now_unix();
                    if (remaining <= 0) return "Subscription expired";
                    const auto days = remaining / 86400;
                    if (days >= 2) return std::to_string(days) + " days left";
                    if (days == 1) return "1 day left";
                    const auto hours = (remaining + 3599) / 3600;
                    if (hours > 1) return std::to_string(hours) + " hours left";
                    if (hours == 1) return "1 hour left";
                    return "< 1 hour left";
                }
            }
        }
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
        sync_payload();

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

        if (std::isfinite(payload.days_left)) {
            double rem_days = payload.days_left;
            if (payload.received_at_unix > 0) {
                const auto elapsed = mintaly_session::now_unix() - payload.received_at_unix;
                if (elapsed > 0) {
                    rem_days -= static_cast<double>(elapsed) / 86400.0;
                }
            }
            if (rem_days <= 0.0 && !payload.sub_to[0]) {
                return access_status::subscription_expired;
            }
        }

        if (payload.sub_to[0]) {
            int y = 0, m = 0, d = 0, hr = 0, mn = 0, sc = 0;
            bool parsed = false;
            if (std::sscanf(payload.sub_to, "%d-%d-%d", &y, &m, &d) == 3 ||
                std::sscanf(payload.sub_to, "%d/%d/%d", &y, &m, &d) == 3) {
                std::sscanf(payload.sub_to, "%*d%*[-/]%*d%*[-/]%*d%*[ T]%d:%d:%d", &hr, &mn, &sc);
                parsed = true;
            } else if (std::sscanf(payload.sub_to, "%d.%d.%d", &d, &m, &y) == 3) {
                std::sscanf(payload.sub_to, "%*d.%*d.%*d%*[ T]%d:%d:%d", &hr, &mn, &sc);
                parsed = true;
            }
            if (parsed) {
                if (y < 100) y += 2000;
                if (y >= 1970 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
                    std::tm t{};
                    t.tm_year = y - 1900;
                    t.tm_mon = m - 1;
                    t.tm_mday = d;
                    t.tm_hour = hr;
                    t.tm_min = mn;
                    t.tm_sec = sc;
                    t.tm_isdst = -1;
                    const auto epoch = std::mktime(&t);
                    if (epoch > 0) {
                        const auto remaining = static_cast<std::int64_t>(epoch) - mintaly_session::now_unix();
                        if (remaining <= 0) {
                            return access_status::subscription_expired;
                        }
                    }
                }
            }
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

inline bool is_expired() {
    return check_access() != access_status::granted;
}

// Connection remains alive until process exit. Do not unmap from a render callback
// or under the loader lock while another thread may be acknowledging startup.
}
