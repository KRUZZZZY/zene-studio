/*
 * LinkRealtimeProbe.h - the Ableton Link realtime tests' allocation counter
 *                       (AbletonLinkSessionTest, LinkPhaseLockTest), now a thin
 *                       wrapper over the shared AllocationProbe.h.
 *
 * History. The Link lane wrote this header with its own NON-inline operator new
 * replacements, because AllocationProbe.h then defined its replacements `inline`
 * ([replacement.functions] forbids that; GCC emitted no definition, so
 * allocations outside the test's own TU were never counted - BUGS_FOUND 13.9).
 * 050/feat-alloc-probe-fix fixed the shared probe the same way (one ordinary
 * definition, tests/src/core/AllocationProbe.cpp, linked by
 * lmms_link_allocation_probe), so on the 2026-10-07 feature merge this header
 * was folded into it: it defines no allocation function of its own and keeps
 * only the allocationsDuring() helper its two tests call.
 *
 * A test that uses it must ALSO include "AllocationProbe.h" in its own source:
 * tests/CMakeLists.txt links the probe into a test whose source names that
 * header, and the probe's counters are defined only there, so forgetting it
 * fails to link instead of counting nothing.
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

#include <cstdint>

#include "AllocationProbe.h"

namespace lmms::test::link
{

//! Count the operator new calls \a body makes on this thread (the whole
//! program's, through AllocationProbe.cpp's replacements).
template<typename Body>
std::uint64_t allocationsDuring(Body&& body)
{
	resetAllocationCount();
	tlCountAllocations = true;
	body();
	tlCountAllocations = false;
	return tlAllocationCount;
}

} // namespace lmms::test::link

#endif // LMMS_TEST_LINK_REALTIME_PROBE_H
