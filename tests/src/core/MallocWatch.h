/* Copyright (c) 2026 Zene Studio contributors; GPL-2.0-or-later. */

#ifndef LMMS_TEST_MALLOC_WATCH_H
#define LMMS_TEST_MALLOC_WATCH_H

/*! Counts C-allocator traffic (malloc, calloc, realloc, free) on the calling thread.
 *
 *  AllocationProbe.h only replaces operator new, and Qt allocates the storage of its containers
 *  (QString, QStringList, QList) through malloc inside QtCore, so that probe cannot see the
 *  allocation this cluster removed. On Linux this header replaces the C allocator for the whole
 *  test executable and forwards to the real one (found with dlsym, as
 *  tests/src/plugins/PluginPortsMigrationTest.cpp does). Elsewhere \c Available is false and the
 *  counters stay at zero; the test then relies on the operator new probe alone.
 *
 *  Header-only, and it DEFINES the replacement functions: include it from exactly one
 *  translation unit of a test executable.
 */

#include <cstddef>
#include <cstring>

#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace lmms::test
{

struct MallocWatch
{
#if defined(__linux__)
	static constexpr bool Available = true;
#else
	static constexpr bool Available = false;
#endif

	inline static thread_local bool active = false;
	inline static thread_local std::size_t allocations = 0;
	inline static thread_local std::size_t frees = 0;

	static void begin() noexcept
	{
		allocations = 0;
		frees = 0;
		active = true;
	}

	static void end() noexcept { active = false; }
};

} // namespace lmms::test

#if defined(__linux__)

namespace lmms::test::malloc_watch_detail
{

using MallocFn = void* (*)(std::size_t);
using CallocFn = void* (*)(std::size_t, std::size_t);
using ReallocFn = void* (*)(void*, std::size_t);
using FreeFn = void (*)(void*);

inline MallocFn realMalloc = nullptr;
inline CallocFn realCalloc = nullptr;
inline ReallocFn realRealloc = nullptr;
inline FreeFn realFree = nullptr;

// Allocations made while the real allocator is being looked up come from this arena, so the
// lookup cannot recurse into itself.
constexpr std::size_t ArenaSize = 256 * 1024;
alignas(16) inline char arena[ArenaSize];
inline std::size_t arenaUsed = 0;
inline bool resolving = false;

inline void* arenaAlloc(std::size_t size) noexcept
{
	const std::size_t aligned = (size + 15u) & ~static_cast<std::size_t>(15u);
	if (arenaUsed + aligned > ArenaSize) { return nullptr; }
	void* const block = arena + arenaUsed;
	arenaUsed += aligned;
	return block;
}

inline bool isArena(const void* block) noexcept
{
	return block >= static_cast<const void*>(arena) && block < static_cast<const void*>(arena + ArenaSize);
}

inline void resolve() noexcept
{
	if (resolving) { return; }
	resolving = true;
	realMalloc = reinterpret_cast<MallocFn>(dlsym(RTLD_NEXT, "malloc"));
	realCalloc = reinterpret_cast<CallocFn>(dlsym(RTLD_NEXT, "calloc"));
	realRealloc = reinterpret_cast<ReallocFn>(dlsym(RTLD_NEXT, "realloc"));
	realFree = reinterpret_cast<FreeFn>(dlsym(RTLD_NEXT, "free"));
	resolving = false;
}

inline void noteAllocation() noexcept
{
	if (MallocWatch::active) { ++MallocWatch::allocations; }
}

inline void noteFree() noexcept
{
	if (MallocWatch::active) { ++MallocWatch::frees; }
}

} // namespace lmms::test::malloc_watch_detail

extern "C" void* malloc(std::size_t size)
{
	namespace d = lmms::test::malloc_watch_detail;
	if (d::resolving) { return d::arenaAlloc(size); }
	if (d::realMalloc == nullptr) { d::resolve(); }
	d::noteAllocation();
	return d::realMalloc(size);
}

extern "C" void* calloc(std::size_t count, std::size_t size)
{
	namespace d = lmms::test::malloc_watch_detail;
	if (d::resolving) { return d::arenaAlloc(count * size); }
	if (d::realCalloc == nullptr) { d::resolve(); }
	d::noteAllocation();
	return d::realCalloc(count, size);
}

extern "C" void* realloc(void* block, std::size_t size)
{
	namespace d = lmms::test::malloc_watch_detail;
	if (d::resolving) { return d::arenaAlloc(size); }
	if (d::realRealloc == nullptr) { d::resolve(); }
	if (d::isArena(block))
	{
		void* const fresh = d::realMalloc(size);
		const std::size_t room = static_cast<std::size_t>(d::arena + d::ArenaSize - static_cast<char*>(block));
		std::memcpy(fresh, block, size < room ? size : room);
		return fresh;
	}
	d::noteAllocation();
	return d::realRealloc(block, size);
}

extern "C" void free(void* block)
{
	namespace d = lmms::test::malloc_watch_detail;
	if (block == nullptr || d::isArena(block)) { return; }
	d::noteFree();
	if (d::realFree == nullptr) { d::resolve(); }
	d::realFree(block);
}

#endif // __linux__

#endif // LMMS_TEST_MALLOC_WATCH_H
