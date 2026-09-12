#pragma once

#include <Windows.h>
#include <atomic>
#include <string_view>
#include <format>

namespace logging {

	namespace console {

		bool initialize( );

		void print_raw( const char* text );

		template <typename... args_t>
		void print( std::string_view fmt, args_t&&... args )
		{
			print_raw( std::vformat( fmt, std::make_format_args( args... ) ).c_str( ) );
		}

		// Dynamic TLS replacement for manual-map compatibility.
		// Using atomic thread ID instead of thread_local bool.
		inline std::atomic<DWORD> emitting_thread_id{ 0 };

		struct emitting_state {
			operator bool( ) const noexcept {
				return emitting_thread_id.load( std::memory_order_relaxed ) == GetCurrentThreadId( );
			}
			emitting_state& operator=( bool val ) noexcept {
				emitting_thread_id.store( val ? GetCurrentThreadId( ) : 0, std::memory_order_relaxed );
				return *this;
			}
		};

		inline emitting_state emitting{};

		inline bool is_emitting( )
		{
			return emitting;
		}

		inline void set_emitting( bool value )
		{
			emitting = value;
		}

	} // namespace console

	namespace popup {

		bool initialize( );
		void show( const char* title, const char* message );

	} // namespace popup

} // namespace logging
