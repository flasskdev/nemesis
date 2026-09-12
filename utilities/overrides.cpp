#include <pch/pch.hpp>
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <new>

namespace {
	constexpr std::uint64_t k_magic_tier0 = 0x54494552304D454DULL; // "TIER0MEM"
	constexpr std::uint64_t k_magic_heap  = 0x48454150414C4C4CULL; // "HEAPALLL"

	struct alignas(16) alloc_header_t {
		std::uint64_t magic;
		std::size_t   size;
	};

	inline void* get_game_allocator() noexcept {
		HMODULE tier0 = GetModuleHandleA("tier0.dll");
		if (!tier0)
			return nullptr;
		auto ppMemAlloc = reinterpret_cast<void**>(GetProcAddress(tier0, "g_pMemAlloc"));
		if (!ppMemAlloc || !*ppMemAlloc)
			return nullptr;
		return *ppMemAlloc;
	}
}

void* game_alloc(std::size_t size) {
	const std::size_t total_size = sizeof(alloc_header_t) + size;
	void* memalloc = get_game_allocator();
	if (memalloc) {
		const auto vtable = *reinterpret_cast<void***>(memalloc);
		auto alloc_fn = reinterpret_cast<void* (__thiscall*)(void*, std::size_t)>(vtable[1]);
		auto* header = static_cast<alloc_header_t*>(alloc_fn(memalloc, total_size));
		if (header) {
			header->magic = k_magic_tier0;
			header->size = size;
			return header + 1;
		}
	}

	HANDLE heap = GetProcessHeap();
	if (!heap)
		return nullptr;
	auto* header = static_cast<alloc_header_t*>(HeapAlloc(heap, 0, total_size));
	if (!header)
		return nullptr;
	header->magic = k_magic_heap;
	header->size = size;
	return header + 1;
}

void game_free(void* ptr) noexcept {
	if (!ptr)
		return;

	auto* header = static_cast<alloc_header_t*>(ptr) - 1;
	if (header->magic == k_magic_tier0) {
		void* memalloc = get_game_allocator();
		if (memalloc) {
			const auto vtable = *reinterpret_cast<void***>(memalloc);
			auto free_fn = reinterpret_cast<void (__thiscall*)(void*, void*)>(vtable[3]);
			free_fn(memalloc, header);
			return;
		}
	}

	if (header->magic == k_magic_heap) {
		HANDLE heap = GetProcessHeap();
		if (heap) {
			HeapFree(heap, 0, header);
			return;
		}
	}
}

void* __cdecl operator new(std::size_t size) {
	void* p = game_alloc(size);
	if (!p)
		throw std::bad_alloc();
	return p;
}

void* __cdecl operator new[](std::size_t size) {
	void* p = game_alloc(size);
	if (!p)
		throw std::bad_alloc();
	return p;
}

void* __cdecl operator new(std::size_t size, const std::nothrow_t&) noexcept {
	return game_alloc(size);
}

void* __cdecl operator new[](std::size_t size, const std::nothrow_t&) noexcept {
	return game_alloc(size);
}

void __cdecl operator delete(void* ptr) noexcept {
	game_free(ptr);
}

void __cdecl operator delete[](void* ptr) noexcept {
	game_free(ptr);
}

void __cdecl operator delete(void* ptr, std::size_t) noexcept {
	game_free(ptr);
}

void __cdecl operator delete[](void* ptr, std::size_t) noexcept {
	game_free(ptr);
}

void __cdecl operator delete(void* ptr, const std::nothrow_t&) noexcept {
	game_free(ptr);
}

void __cdecl operator delete[](void* ptr, const std::nothrow_t&) noexcept {
	game_free(ptr);
}
