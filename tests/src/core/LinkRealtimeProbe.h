/*
 * LinkRealtimeProbe.h - a program-wide allocation counter for the Ableton Link
 *                       realtime tests (AbletonLinkSessionTest,
 *                       AbletonLinkInteropTest).
 *
 * WHY NOT AllocationProbe.h. That header defines the replacement operator new
 * `inline`. The standard forbids that ([replacement.functions]: "shall not be
 * specified as inline"), and with GCC the consequence is concrete: an inline
 * definition is emitted only where it is odr-used and not inlined, so the test
 * binary ends up with NO definition of operator new at all (`nm -C <test> |
 * grep "operator new"` prints only `U ... @GLIBCXX_3.4`). Every allocation made
 * by code compiled outside the test's own translation unit - lmmsobjs, a static
 * library - goes to libstdc++'s operator new and is never counted. Measured on
 * this lane: an allocation injected into AbletonLinkSession::audioPeriod() left
 * an AllocationProbe-based count at 0. The definitions below are ordinary
 * (non-inline) replacements, so they replace operator new for the WHOLE program,
 * and the same injected allocation is counted (docs/LINK-SYNC.md section 7 has
 * the red and green runs).
 *
 * Include it in exactly ONE translation unit of a test binary (each Zene test
 * binary is one TU). Counting is per thread and off until a test arms it.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_TEST_LINK_REALTIME_PROBE_H
#define LMMS_TEST_LINK_REALTIME_PROBE_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace lmms::test::link
{

//! When true, allocations on THIS thread are counted in tlAllocations.
inline thread_local bool tlCounting = false;
inline thread_local std::uint64_t tlAllocations = 0;

inline void noteAllocation() noexcept
{
	if (tlCounting) { ++tlAllocations; }
}

//! Count the allocations \a body makes on this thread.
template<typename Body>
std::uint64_t allocationsDuring(Body&& body)
{
	tlAllocations = 0;
	tlCounting = true;
	body();
	tlCounting = false;
	return tlAllocations;
}

} // namespace lmms::test::link

void* operator new(std::size_t size)
{
	lmms::test::link::noteAllocation();
	if (void* ptr = std::malloc(size != 0 ? size : 1)) { return ptr; }
	throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	lmms::test::link::noteAllocation();
	return std::malloc(size != 0 ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
	return ::operator new(size, tag);
}

void* operator new(std::size_t size, std::align_val_t align)
{
	lmms::test::link::noteAllocation();
	const std::size_t alignment = static_cast<std::size_t>(align);
	const std::size_t rounded = (size + alignment - 1) / alignment * alignment;
#ifdef _WIN32
	if (void* ptr = _aligned_malloc(rounded != 0 ? rounded : alignment, alignment)) { return ptr; }
#else
	if (void* ptr = std::aligned_alloc(alignment, rounded != 0 ? rounded : alignment)) { return ptr; }
#endif
	throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t align)
{
	return ::operator new(size, align);
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete(void* ptr, const std::nothrow_t&) noexcept { std::free(ptr); }
void operator delete[](void* ptr, const std::nothrow_t&) noexcept { std::free(ptr); }
#ifdef _WIN32
// _aligned_malloc memory must go back through _aligned_free.
void operator delete(void* ptr, std::align_val_t) noexcept { _aligned_free(ptr); }
#else
void operator delete(void* ptr, std::align_val_t) noexcept { std::free(ptr); }
#endif
void operator delete[](void* ptr, std::align_val_t align) noexcept { ::operator delete(ptr, align); }
void operator delete(void* ptr, std::size_t, std::align_val_t align) noexcept { ::operator delete(ptr, align); }
void operator delete[](void* ptr, std::size_t, std::align_val_t align) noexcept { ::operator delete(ptr, align); }

#endif // LMMS_TEST_LINK_REALTIME_PROBE_H
