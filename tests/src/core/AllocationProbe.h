/*
 * AllocationProbe.h - test-only global operator new/delete replacement that
 *                     counts allocations on the calling thread
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
 *
 */

#ifndef LMMS_TEST_ALLOCATION_PROBE_H
#define LMMS_TEST_ALLOCATION_PROBE_H

#include <cstdint>

// The replacement global operator new/new[]/delete/delete[] that do the
// counting are defined ONCE per test executable, in AllocationProbe.cpp, which
// tests/CMakeLists.txt links into every test whose source includes this header.
//
// They used to be defined here as `inline`. The standard forbids that for a
// replacement allocation function ([replacement.functions]), and GCC then
// emitted no definition at all: the test binary had no operator new of its
// own, so every allocation made OUTSIDE the test's translation unit - i.e. in
// lmmsobjs, the library code the tests exist to check - went to libstdc++ and
// was never counted. docs/KNOWN-LIMITATIONS.md ("The allocation probe") has the measurement.
//
// The two counters are defined in AllocationProbe.cpp too, so a test that
// includes this header without linking the probe fails to LINK rather than
// silently counting nothing.

namespace lmms::test
{

//! When true, allocations on this thread are counted in tlAllocationCount.
extern thread_local bool tlCountAllocations;
//! Allocations observed on this thread while tlCountAllocations was true.
extern thread_local std::uint64_t tlAllocationCount;
//! Frees (operator delete of a non-null pointer, every form) observed on this
//! thread while tlCountAllocations was true - the realtime rule forbids a free
//! on the audio thread as much as an allocation (Q050-016 round 3 added it).
extern thread_local std::uint64_t tlFreeCount;

inline void noteAllocation() noexcept
{
	if (tlCountAllocations) { ++tlAllocationCount; }
}

inline void noteFree(const void* ptr) noexcept
{
	if (tlCountAllocations && ptr != nullptr) { ++tlFreeCount; }
}

//! Zeroes both counters, the allocation count and the free count.
inline void resetAllocationCount() noexcept
{
	tlAllocationCount = 0;
	tlFreeCount = 0;
}

} // namespace lmms::test

#endif // LMMS_TEST_ALLOCATION_PROBE_H
