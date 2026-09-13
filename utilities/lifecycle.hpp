#pragma once
#include <windows.h>
#include <atomic>

namespace lifecycle {

inline std::atomic<bool> g_is_unloading{ false };
inline std::atomic<bool> g_has_shutdown{ false };
inline std::atomic<bool> g_stop_monitor{ false };
inline HMODULE g_module_handle{ nullptr };
inline HANDLE g_monitor_thread{ nullptr };

inline bool is_unloading()
{
    return g_is_unloading.load( std::memory_order_acquire );
}

void print_expired_chat_notification();
void shutdown_all_cheat_systems();
void unload_and_exit( HMODULE module_handle = nullptr );
void request_unload();
void start_subscription_monitor( HMODULE module_handle );
void stop_subscription_monitor();

} // namespace lifecycle
