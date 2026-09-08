/*
 * AllocationProbe.h - test-only global operator new/delete replacement that
 *                     counts allocations on the calling thread
 *
 * Copyright (c) 2026 LMMS developers
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
 *
 */

#ifndef LMMS_TEST_ALLOCATION_PROBE_H
#define LMMS_TEST_ALLOCATION_PROBE_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

namespace lmms::test
{

//! When true, allocations on this thread are counted in tlAllocationCount.
inline thread_local bool tlCountAllocations = false;
//! Allocations observed on this thread while tlCountAllocations was true.
inline thread_local std::uint64_t tlAllocationCount = 0;

inline void noteAllocation() noexcept
{
	if (tlCountAllocations) { ++tlAllocationCount; }
}

inline void resetAllocationCount() noexcept
{
	tlAllocationCount = 0;
}

} // namespace lmms::test

// Replaceable global allocation functions: every C++ allocation in the test
// binary goes through these, so a thread that does not allocate keeps
// tlAllocationCount at zero.
inline void* operator new(std::size_t size)
{
	lmms::test::noteAllocation();
	void* ptr = std::malloc(size != 0 ? size : 1);
	if (ptr == nullptr) { throw std::bad_alloc(); }
	return ptr;
}

inline void* operator new[](std::size_t size)
{
	lmms::test::noteAllocation();
	void* ptr = std::malloc(size != 0 ? size : 1);
	if (ptr == nullptr) { throw std::bad_alloc(); }
	return ptr;
}

inline void operator delete(void* ptr) noexcept { std::free(ptr); }
inline void operator delete[](void* ptr) noexcept { std::free(ptr); }
inline void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
inline void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

#endif // LMMS_TEST_ALLOCATION_PROBE_H
