#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <cstring>

// Transport only. Local IPC is not a signed licence or a security boundary.
namespace mintaly_session {
constexpr std::uint32_t magic = 0x4D544C59;
constexpr std::uint32_t version = 1;
constexpr std::uint32_t developer = 1;
constexpr std::uint32_t subscription_active = 2;
constexpr std::uint32_t has_expiry_epoch = 4;
enum Phase : LONG { created = 0, attached = 1, initialized = 2, failed = 3 };

#pragma pack(push, 8)
struct Payload {
    std::uint64_t user_id;
    std::int64_t received_at_unix;
    std::int64_t expires_at_unix; // Only populated from explicit API Unix seconds.
    double days_left;            // API value, never converted into a made-up expiry.
    std::uint32_t flags;
    std::uint32_t reserved;
    char sub_to[96];             // Original API string; no implicit time-zone conversion.
};
struct Shared {
    std::uint32_t signature;
    std::uint32_t protocol;
    std::uint32_t bytes;
    std::uint32_t target_pid;
    std::uint64_t target_created;
    Payload payload;
    volatile LONG phase;
    volatile LONG frame_completed;
    char error[512];
};
#pragma pack(pop)
static_assert(sizeof(Payload) == 136);
static_assert(sizeof(Shared) == 680);
static_assert(offsetof(Shared, phase) == 160);

inline std::int64_t now_unix() {
    FILETIME f{};
    GetSystemTimeAsFileTime(&f);
    ULARGE_INTEGER t{};
    t.LowPart = f.dwLowDateTime;
    t.HighPart = f.dwHighDateTime;
    return static_cast<std::int64_t>(t.QuadPart / 10000000ULL - 11644473600ULL);
}
inline bool creation_time(HANDLE process, std::uint64_t& value) {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(process, &c, &e, &k, &u)) return false;
    value = (static_cast<std::uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
    return true;
}
inline bool object_name(DWORD pid, std::uint64_t created_at, wchar_t (&name)[128]) {
    return swprintf_s(name, L"Local\\Mintaly.Session.v1.%lu.%016llX", pid,
        static_cast<unsigned long long>(created_at)) > 0;
}
inline LONG read_phase(Shared* block) {
    return InterlockedCompareExchange(&block->phase, 0, 0);
}
inline bool read_frame(Shared* block) {
    return InterlockedCompareExchange(&block->frame_completed, 0, 0) == 1;
}
}
