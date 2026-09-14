#include <pch/pch.hpp>

#include <cstdio>

#include <utilities/logging/logging.hpp>
#include <utilities/addresses/addresses.hpp>
#include <utilities/security/security.hpp>
#include <utilities/memory/memory.hpp>
#include <utilities/threadpool/threadpool.hpp>
#include <utilities/steam/steam.hpp>

#include <core/hooks/hooks.hpp>
#include <core/systems/systems.hpp>
#include <core/settings.hpp>
#include <core/features/features.hpp>
#include <core/rendering/rendering.hpp>
#include <core/rendering/theme.hpp>
#include <core/features/misc/misc.hpp>

#include <utilities/diag.hpp>
#include <utilities/loader_session.hpp>
#include <utilities/lifecycle.hpp>

namespace {

	PRUNTIME_FUNCTION g_registered_function_table{};
	std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> g_previous_exception_filter{};
	PVOID g_vectored_exception_handler{};
	std::atomic<bool> g_is_attached{ false };

	HMODULE resolve_self_module( HMODULE candidate = nullptr )
	{
		if ( candidate )
		{
			MEMORY_BASIC_INFORMATION mbi{};
			if ( VirtualQuery( candidate, &mbi, sizeof( mbi ) ) && mbi.State == MEM_COMMIT )
			{
				return static_cast<HMODULE>( mbi.AllocationBase );
			}
		}

		MEMORY_BASIC_INFORMATION mbi{};
		if ( VirtualQuery( reinterpret_cast<const void*>( &resolve_self_module ), &mbi, sizeof( mbi ) ) && mbi.AllocationBase )
		{
			return static_cast<HMODULE>( mbi.AllocationBase );
		}

		return candidate;
	}

	void register_exception_table( HMODULE module_base )
	{
		if ( !module_base )
			return;

		const auto base = reinterpret_cast<std::uintptr_t>( module_base );
		__try
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>( base );
			if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
				return;

			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>( base + dos->e_lfanew );
			if ( nt->Signature != IMAGE_NT_SIGNATURE )
				return;

			const auto& pdata = nt->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXCEPTION ];
			if ( pdata.VirtualAddress && pdata.Size )
			{
				auto* function_table = reinterpret_cast<PRUNTIME_FUNCTION>( base + pdata.VirtualAddress );
				const DWORD entry_count = pdata.Size / sizeof( RUNTIME_FUNCTION );
				if ( entry_count > 0 )
				{
					if ( RtlAddFunctionTable( function_table, entry_count, static_cast<DWORD64>( base ) ) )
					{
						g_registered_function_table = function_table;
					}
				}
			}
		}
		__except ( EXCEPTION_EXECUTE_HANDLER )
		{
		}
	}

	void unregister_exception_table( )
	{
		if ( g_registered_function_table )
		{
			RtlDeleteFunctionTable( g_registered_function_table );
			g_registered_function_table = nullptr;
		}
	}

	LONG WINAPI diag_unhandled_exception_filter( EXCEPTION_POINTERS* info );

#if defined( DEV )
	hooking::jmp g_minidump_hook{};
	hooking::jmp g_terminate_process_hook{};

	BOOL WINAPI diag_minidump_write_detour(
		HANDLE process,
		DWORD process_id,
		HANDLE file,
		unsigned long dump_type,
		diag::minidump_exception_information* exception,
		void* user_stream,
		void* callback )
	{
		const auto original =
			g_minidump_hook.original<diag::minidump_write_fn>( );
		if ( !diag::g_writing_minidump &&
			exception &&
			!exception->client_pointers &&
			exception->exception_pointers )
		{
			diag::record_crash(
				exception->exception_pointers,
				"game fatal handler (Source 2 caught the exception)" );
		}

		return original
			? original(
				process,
				process_id,
				file,
				dump_type,
				exception,
				user_stream,
				callback )
			: FALSE;
	}

	bool install_game_crash_capture( )
	{
		if ( !diag::g_minidump_write )
		{
			return false;
		}

		if ( !hooking::manager::create( {
				{
					&g_minidump_hook,
					reinterpret_cast<void*>( diag_minidump_write_detour ),
					"dbghelp!MiniDumpWriteDump",
					reinterpret_cast<std::uintptr_t>( diag::g_minidump_write )
				}
			} ) )
		{
			diag::write(
				diag::level::warning,
				"failed to hook the Source 2 minidump path; "
				"only self/unhandled crashes will be captured" );
			return false;
		}

		diag::write(
			diag::level::info,
			"Source 2 fatal minidump path hooked" );
		return true;
	}

	BOOL WINAPI diag_terminate_process_detour(
		HANDLE process,
		UINT exit_code )
	{
		const auto original =
			g_terminate_process_hook.original<decltype( &TerminateProcess )>( );

		if ( GetProcessId( process ) == GetCurrentProcessId( ) )
		{
			char stage[ 96 ]{};
			_snprintf_s(
				stage,
				sizeof( stage ),
				_TRUNCATE,
				"TerminateProcess requested for CS2; exit_code=0x%08X",
				exit_code );
			diag::capture_snapshot( stage );
		}

		return original ? original( process, exit_code ) : FALSE;
	}

	bool install_termination_capture( )
	{
		const auto kernel32 = GetModuleHandleW( L"kernel32.dll" );
		const auto terminate_process = kernel32
			? GetProcAddress( kernel32, "TerminateProcess" )
			: nullptr;
		if ( !terminate_process ||
			!hooking::manager::create( {
				{
					&g_terminate_process_hook,
					reinterpret_cast<void*>( diag_terminate_process_detour ),
					"kernel32!TerminateProcess",
					reinterpret_cast<std::uintptr_t>( terminate_process )
				}
			} ) )
		{
			diag::write(
				diag::level::warning,
				"failed to hook forced process termination" );
			return false;
		}

		diag::write(
			diag::level::info,
			"forced process termination capture hooked" );
		return true;
	}
#endif

	LONG CALLBACK diag_vectored_exception_filter( EXCEPTION_POINTERS* info )
	{
		if ( !info || !info->ExceptionRecord ||
			!diag::is_serious_exception( info->ExceptionRecord->ExceptionCode ) )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		if ( diag::probe_active( ) )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		// Safety catch: client.dll collect_attached_entities null dereference of m_pGameSceneNode (access violation reading 0x40)
		if ( info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
			info->ExceptionRecord->NumberParameters > 1 &&
			info->ExceptionRecord->ExceptionInformation[ 1 ] == 0x40 &&
			info->ContextRecord &&
			info->ContextRecord->Rax == 0 )
		{
			const auto* ip = reinterpret_cast<const std::uint8_t*>( info->ExceptionRecord->ExceptionAddress );
			if ( ip && !IsBadReadPtr( ip, 4 ) &&
				ip[ 0 ] == 0x48 && ip[ 1 ] == 0x8B && ip[ 2 ] == 0x48 && ip[ 3 ] == 0x40 )
			{
				info->ContextRecord->Rcx = 0;
				info->ContextRecord->Rip += 4;
				return EXCEPTION_CONTINUE_EXECUTION;
			}
		}

		// The host or Steam may replace the single process-wide last-chance
		// filter after injection. Re-arm it at first chance and preserve the
		// displaced handler so the host still receives the crash after us.
		const auto displaced_filter =
			SetUnhandledExceptionFilter( diag_unhandled_exception_filter );
		if ( displaced_filter != diag_unhandled_exception_filter )
		{
			g_previous_exception_filter.store(
				displaced_filter,
				std::memory_order_release );
		}

		if ( diag::is_module_address( info->ExceptionRecord->ExceptionAddress ) )
		{
			diag::record_crash(
				info,
				diag::g_exception_scope_depth
					? diag::g_exception_phase
					: "first-chance fault in mintaly DLL" );
			return EXCEPTION_CONTINUE_SEARCH;
		}

		if ( diag::g_exception_scope_depth == 0 )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		const auto code = info->ExceptionRecord->ExceptionCode;
		const auto instruction =
			reinterpret_cast<std::uintptr_t>( info->ExceptionRecord->ExceptionAddress );
		const auto accessed =
			info->ExceptionRecord->NumberParameters > 1
				? info->ExceptionRecord->ExceptionInformation[ 1 ]
				: 0;

		HMODULE fault_module{};
		char module_path[ MAX_PATH ]{ "unknown" };
		std::uintptr_t module_base{};
		if ( instruction &&
			GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>( instruction ), &fault_module ) )
		{
			module_base = reinterpret_cast<std::uintptr_t>( fault_module );
			GetModuleFileNameA( fault_module, module_path, MAX_PATH );
		}

		const auto* module_name = strrchr( module_path, '\\' );
		module_name = module_name ? module_name + 1 : module_path;

		char buf[ 384 ]{};
		_snprintf_s(
			buf, sizeof( buf ), _TRUNCATE,
			"FEATURE EXCEPTION [%s] 0x%08lX at %s+0x%llX (0x%p), accessed 0x%p",
			diag::g_exception_phase,
			code,
			module_name,
			module_base
				? static_cast<unsigned long long>( instruction - module_base )
				: 0ull,
			info->ExceptionRecord->ExceptionAddress,
			reinterpret_cast<void*>( accessed ) );
		diag::step( buf );

		return EXCEPTION_CONTINUE_SEARCH;
	}

	LONG WINAPI diag_unhandled_exception_filter( EXCEPTION_POINTERS* info )
	{
		if ( !info || !info->ExceptionRecord )
		{
			return EXCEPTION_CONTINUE_SEARCH;
		}

		const auto instruction =
			reinterpret_cast<std::uintptr_t>( info->ExceptionRecord->ExceptionAddress );
		const auto accessed =
			info->ExceptionRecord->NumberParameters > 1
				? info->ExceptionRecord->ExceptionInformation[ 1 ]
				: 0;

		HMODULE fault_module{};
		char module_path[ MAX_PATH ]{ "unknown" };
		std::uintptr_t module_base{};
		if ( instruction &&
			GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
					GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>( instruction ), &fault_module ) )
		{
			module_base = reinterpret_cast<std::uintptr_t>( fault_module );
			GetModuleFileNameA( fault_module, module_path, MAX_PATH );
		}

		const auto* module_name = strrchr( module_path, '\\' );
		module_name = module_name ? module_name + 1 : module_path;

		char buf[ 384 ]{};
		_snprintf_s(
			buf, sizeof( buf ), _TRUNCATE,
			"UNHANDLED EXCEPTION 0x%08lX at %s+0x%llX (0x%p), accessed 0x%p",
			info->ExceptionRecord->ExceptionCode,
			module_name,
			module_base
				? static_cast<unsigned long long>( instruction - module_base )
				: 0ull,
			info->ExceptionRecord->ExceptionAddress,
			reinterpret_cast<void*>( accessed ) );
		diag::write( diag::level::fatal, buf );
		diag::record_crash( info, "unhandled exception" );

		const auto previous_filter =
			g_previous_exception_filter.load( std::memory_order_acquire );
		if ( previous_filter &&
			previous_filter != diag_unhandled_exception_filter )
		{
			return previous_filter( info );
		}

		return EXCEPTION_CONTINUE_SEARCH;
	}

#if defined( DEV )
	#define INIT_FAIL( msg ) \
		do { \
			diag::write( diag::level::error, msg ); \
			loader_session::fail( msg ); \
			return 0; \
		} while ( 0 )

	#define INIT_WARN( msg ) diag::write( diag::level::warning, msg )
#else
	#define INIT_FAIL( msg ) \
		do { \
			diag::write( diag::level::error, msg ); \
			loader_session::fail( msg ); \
			MessageBoxA( nullptr, xs( msg ), xs( "..." ), MB_ICONERROR ); \
			return 0; \
		} while ( 0 )

	#define INIT_WARN( msg ) INIT_FAIL( msg )
#endif

	DWORD WINAPI init_thread_impl( LPVOID param )
	{
		const auto module_handle = resolve_self_module( static_cast<HMODULE>( param ) );

		diag::step( "stage: thread start" );
		const bool connected = loader_session::connect();
		if ( connected )
			diag::step( "stage: loader session connected (protocol v1)" );
		else
			diag::write( diag::level::warning, "loader session unavailable" );

		const auto access = loader_session::check_access();
		if ( access != loader_session::access_status::granted )
		{
			const char* err_msg = "Отсутствует активная подписка или dev доступ.";
			if ( access == loader_session::access_status::subscription_expired )
				err_msg = "Срок действия вашей подписки истек.";
			else if ( access == loader_session::access_status::no_session )
				err_msg = "Запуск разрешен только через официальный лоадер.";

			diag::writef( diag::level::fatal, "startup blocked: %s", err_msg );
			loader_session::fail( err_msg );
#if !defined( DEV )
			if ( access == loader_session::access_status::subscription_expired )
				MessageBoxA( nullptr, xs( "Срок действия вашей подписки истек." ), xs( "Mintaly" ), MB_ICONERROR );
			else if ( access == loader_session::access_status::no_session )
				MessageBoxA( nullptr, xs( "Запуск разрешен только через официальный лоадер." ), xs( "Mintaly" ), MB_ICONERROR );
			else
				MessageBoxA( nullptr, xs( "Отсутствует активная подписка или dev доступ." ), xs( "Mintaly" ), MB_ICONERROR );
#endif
			return 0;
		}

		diag::initialize_crash_dumps( );

		g_previous_exception_filter.store(
			SetUnhandledExceptionFilter( diag_unhandled_exception_filter ),
			std::memory_order_release );
		g_vectored_exception_handler =
			AddVectoredExceptionHandler( 1, diag_vectored_exception_filter );
		if ( !g_vectored_exception_handler )
		{
			diag::writef(
				diag::level::error,
				"failed to install vectored exception handler; win32_error=%lu",
				GetLastError( ) );
		}
		else
		{
			diag::write( diag::level::info, "crash handlers installed" );
		}

		diag::step( "stage: coinit" );
		const auto coinit_result =
			CoInitializeEx( nullptr, COINIT_MULTITHREADED );
		if ( FAILED( coinit_result ) )
		{
			diag::writef(
				diag::level::warning,
				"CoInitializeEx failed; hresult=0x%08lX",
				coinit_result );
		}

		diag::step( "stage: config" );
		config::initialize( );
		settings::finalize_binds( );

		diag::step( "stage: regions" );
		security::regions::add_module( module_handle );

		diag::step( "stage: logging" );
		{
			if ( !logging::console::initialize( ) )
			{
#if defined( DEV )
				INIT_WARN( "failed to initialize console logging." );
#else
				INIT_FAIL( "failed to initialize console logging." );
#endif
			}

			if ( !logging::popup::initialize( ) )
			{
#if defined( DEV )
				INIT_WARN( "failed to initialize popup logging." );
#else
				INIT_FAIL( "failed to initialize popup logging." );
#endif
			}
		}

		diag::step( "stage: integrity" );
		{
			if ( !security::integrity::initialize( ) )
			{
				INIT_FAIL( "failed to initialize integrity checks." );
			}

#if defined( DEV )
			install_game_crash_capture( );
			install_termination_capture( );
#endif

			if ( !threadpool::initialize( ) )
			{
				INIT_FAIL( "failed to initialize thread pool." );
			}

			if ( !steam::http::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam http." );
			}

			if ( !steam::friends::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam friends." );
			}

			if ( !steam::user::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam user." );
			}

			if ( !steam::utils::initialize( ) )
			{
				INIT_FAIL( "failed to initialize steam utils." );
			}
		}

		diag::step( "stage: addresses" );
		{
			if ( !addresses::modules::initialize( ) )
			{
				INIT_FAIL( "failed to initialize module addresses." );
			}

			if ( !addresses::globals::initialize( ) )
			{
				INIT_FAIL( "failed to initialize global addresses." );
			}

			if ( !addresses::functions::initialize( ) )
			{
				INIT_FAIL( "failed to initialize function addresses." );
			}
		}

		diag::step( "stage: systems" );
		{
			if ( !systems::materials::initialize( ) )
			{
				INIT_FAIL( "failed to initialize materials system." );
			}

			if ( !systems::events::initialize( ) )
			{
				INIT_FAIL( "failed to initialize event system." );
			}

			if ( !systems::g_icons.initialize( ) )
			{
				INIT_FAIL( "failed to initialize vpk parse system." );
			}

			if ( !systems::g_model_preview.initialize( ) )
			{
				INIT_FAIL( "failed to initialize model preview system." );
			}
		}

		diag::step( "stage: econ" );
		{
			if ( !features::changer::g_econ_item_system.initialize( ) )
			{
				INIT_FAIL( "failed to initialize econ item system." );
			}
		}

		diag::step( "stage: hooks" );
		{
			if ( !hooks::utility::initialize( ) )
			{
				INIT_FAIL( "failed to initialize utility hooks." );
			}

			if ( !hooks::cheat::initialize( ) )
			{
				INIT_FAIL( "failed to initialize cheat hooks." );
			}
		}

		diag::step( "stage: cvars" );
		{
			if ( !addresses::globals::cvar->unlock_all( ) )
			{
				INIT_FAIL( "failed to unlock hidden cvars." );
			}
		}

		diag::step( "stage: skyboxes" );
		features::world::g_scene.discover_skyboxes( );

		diag::step( "stage: done" );
		features::changer::g_skin_sync.initialize( );
		loader_session::ready();

		lifecycle::start_subscription_monitor( module_handle );
		return 1;
	}

	DWORD diag_exception_filter( EXCEPTION_POINTERS* info )
	{
		char buf[ 128 ]{};
		_snprintf_s( buf, sizeof( buf ), _TRUNCATE, "EXCEPTION 0x%08lX at 0x%p", info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress );
		diag::write( diag::level::fatal, buf );
		diag::record_crash( info, "initialization thread" );
		loader_session::fail( buf );
		return EXCEPTION_EXECUTE_HANDLER;
	}

	DWORD WINAPI init_thread( LPVOID param )
	{
		__try
		{
			return init_thread_impl( param );
		}
		__except ( diag_exception_filter( GetExceptionInformation( ) ) )
		{
			return 0;
		}
	}

} // namespace

namespace lifecycle {

void print_expired_chat_notification( )
{
	const auto r = tokens::col_accent.r;
	const auto g = tokens::col_accent.g;
	const auto b = tokens::col_accent.b;

	const auto formatted = std::format(
		"<font color='#{:02X}{:02X}{:02X}'>[<b> mintaly </b>]</font> <font color='#888888'>:</font> "
		"<font color='#38BDF8'>Ваша подписка на </font>"
		"<font color='#{:02X}{:02X}{:02X}'>[ mintaly ]</font>"
		"<font color='#38BDF8'> закончилась, возобновить ее вы можете на нашем сайте </font>"
		"<font color='#{:02X}{:02X}{:02X}'>mintaly.cc</font>",
		r, g, b, r, g, b, r, g, b
	);

	features::misc::detail::chat_print_raw( formatted.c_str( ) );
}

void shutdown_all_cheat_systems( )
{
	if ( g_has_shutdown.exchange( true ) )
	{
		return;
	}

	g_is_unloading.store( true, std::memory_order_release );
	g_stop_monitor.store( true, std::memory_order_release );

	// 1. Immediately disable and unhook all cheat & utility hooks first!
	// This restores the original game bytes so NO NEW CALLS enter any detour.
	hooks::cheat::shutdown( );
	hooks::utility::shutdown( );

#if defined( DEV )
	g_terminate_process_hook.reset( );
	g_minidump_hook.reset( );
#endif

	// 2. Wait 300ms so any in-flight game threads currently inside hook detours
	// finish their execution and safely return to caller.
	Sleep( 300 );

	// 3. Unregister exception handlers and function tables
	if ( g_vectored_exception_handler )
	{
		RemoveVectoredExceptionHandler( g_vectored_exception_handler );
		g_vectored_exception_handler = nullptr;
	}

	const auto previous_filter =
		g_previous_exception_filter.exchange(
			nullptr,
			std::memory_order_acq_rel );
	const auto current_filter =
		SetUnhandledExceptionFilter( previous_filter );
	if ( current_filter != diag_unhandled_exception_filter )
	{
		SetUnhandledExceptionFilter( current_filter );
	}

	unregister_exception_table( );

	// 4. Clean up menu, cursor, and features
	rendering::g_menu.shutdown( );
	ClipCursor( nullptr );

	features::esp::player::g_chams.bt( ).shutdown( );
	features::esp::player::g_chams.os( ).shutdown( );
	features::world::g_weather.release( );

	systems::events::shutdown( );

	// 5. Clean up DirectX rendering context
	rendering::g_context.shutdown( );

	CoUninitialize( );
	diag::shutdown( );

	if ( auto* block = loader_session::shared.load( std::memory_order_acquire ) )
	{
		UnmapViewOfFile( block );
		loader_session::shared.store( nullptr, std::memory_order_release );
	}
	if ( loader_session::mapping )
	{
		CloseHandle( loader_session::mapping );
		loader_session::mapping = nullptr;
	}
}

void unload_and_exit( HMODULE module_handle )
{
	if ( !module_handle )
	{
		module_handle = g_module_handle ? g_module_handle : resolve_self_module( );
	}

	// Print expiration notification into CS2 chat and HUD toast banner
	print_expired_chat_notification( );

	// Short pause so Panorama and HUD process the chat event before tear-down
	Sleep( 100 );

	shutdown_all_cheat_systems( );

	// Wait 200ms for CPU caches
	Sleep( 200 );

	BOOL is_load_library_module = FALSE;
	if ( module_handle )
	{
		HMODULE check_mod = nullptr;
		if ( GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast< LPCSTR >( module_handle ), &check_mod ) && check_mod == module_handle )
		{
			is_load_library_module = TRUE;
		}
	}

	if ( is_load_library_module )
	{
		FreeLibraryAndExitThread( module_handle, 0 );
		return;
	}

	// For manual-mapped DLL: DO NOT VirtualFree MEM_RELEASE!
	// Unmapping the DLL memory causes instant access violations if any threads,
	// CRT runtime structures, or system callbacks touch addresses in this range.
	// Leaving the dormant, unhooked image in memory is completely safe and stable.
	ExitThread( 0 );
}

void request_unload( )
{
	if ( g_is_unloading.exchange( true ) )
	{
		return;
	}

	const auto h = CreateThread( nullptr, 0, []( LPVOID param ) -> DWORD {
		unload_and_exit( static_cast< HMODULE >( param ) );
		return 0;
	}, g_module_handle ? g_module_handle : resolve_self_module( ), 0, nullptr );

	if ( h )
	{
		CloseHandle( h );
	}
}

DWORD WINAPI subscription_monitor_thread( LPVOID param )
{
	const auto module_handle = static_cast< HMODULE >( param );

	while ( !g_stop_monitor.load( std::memory_order_relaxed ) )
	{
		for ( int i = 0; i < 60; ++i )
		{
			if ( g_stop_monitor.load( std::memory_order_relaxed ) )
			{
				return 0;
			}
			Sleep( 1000 );
		}

		if ( g_stop_monitor.load( std::memory_order_relaxed ) )
		{
			return 0;
		}

		if ( loader_session::check_access( ) != loader_session::access_status::granted )
		{
			diag::write( diag::level::info, "subscription expired during runtime, shutting down and unloading" );
			unload_and_exit( module_handle );
			return 0;
		}
	}

	return 0;
}

void start_subscription_monitor( HMODULE module_handle )
{
	g_module_handle = module_handle;
	g_stop_monitor.store( false, std::memory_order_release );

	g_monitor_thread = CreateThread( nullptr, 0, subscription_monitor_thread, module_handle, 0, nullptr );
	if ( g_monitor_thread )
	{
		CloseHandle( g_monitor_thread );
		g_monitor_thread = nullptr;
	}
}

void stop_subscription_monitor( )
{
	g_stop_monitor.store( true, std::memory_order_release );
}

} // namespace lifecycle

extern "C" int __stdcall entry( HMODULE module_handle, DWORD reason, LPVOID reserved )
{
	module_handle = resolve_self_module( module_handle );

	register_exception_table( module_handle );

	// Support manual mappers that execute entry via CreateRemoteThread(..., entry, base, ...).
	// In that scenario, reason (RDX) is typically 0 (which numeric-wise equals DLL_PROCESS_DETACH).
	// If the DLL has not attached yet, treat reason == 0 or DLL_PROCESS_ATTACH as the initial attach.
	const bool is_initial_attach = !g_is_attached.load( std::memory_order_acquire ) &&
		( reason == DLL_PROCESS_ATTACH || reason == 0 );

	if ( is_initial_attach )
	{
		if ( g_is_attached.exchange( true, std::memory_order_acq_rel ) )
		{
			return 1;
		}

		_CRT_INIT( module_handle, DLL_PROCESS_ATTACH, reserved );
		DisableThreadLibraryCalls( module_handle );

		diag::set_module( module_handle );
		lifecycle::g_module_handle = module_handle;
		diag::step( "stage: dll attach" );
		diag::step( "build: development diagnostics" );

		diag::step( "stage: crt done, spawning thread" );

		const auto thread = CreateThread( nullptr, 0, init_thread, module_handle, 0, nullptr );
		if ( !thread )
		{
			diag::writef(
				diag::level::error,
				"failed to create initialization thread; win32_error=%lu",
				GetLastError( ) );
			return 0;
		}

		CloseHandle( thread );
		return 1;
	}
	else if ( reason == DLL_PROCESS_DETACH && g_is_attached.load( std::memory_order_acquire ) )
	{
		g_is_attached.store( false, std::memory_order_release );
		lifecycle::shutdown_all_cheat_systems( );

#if defined( DEV )
		_CRT_INIT( module_handle, reason, reserved );
#endif
	}

	return 1;
}

