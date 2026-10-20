/*
 * AllocationProbe.cpp - the replacement global allocation functions behind
 *                       AllocationProbe.h, defined once per test executable
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

#include "AllocationProbe.h"

#include <cstddef>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#else
#include <stdlib.h> // posix_memalign
#endif

namespace lmms::test
{

thread_local bool tlCountAllocations = false;
thread_local std::uint64_t tlAllocationCount = 0;
thread_local std::uint64_t tlFreeCount = 0;

} // namespace lmms::test

// Ordinary (non-inline) replacements: one definition in the executable replaces
// operator new for the WHOLE program - the test's own code, the statically
// linked lmmsobjs, and any plugin module the test loads - so an allocation in
// library code is counted. Every form is replaced, so memory never crosses
// between this allocator and the library's (they agree anyway: both are malloc).

void* operator new(std::size_t size)
{
	lmms::test::noteAllocation();
	if (void* ptr = std::malloc(size != 0 ? size : 1)) { return ptr; }
	throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	lmms::test::noteAllocation();
	return std::malloc(size != 0 ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
	return ::operator new(size, tag);
}

void* operator new(std::size_t size, std::align_val_t align)
{
	lmms::test::noteAllocation();
	const std::size_t alignment = static_cast<std::size_t>(align);
	const std::size_t bytes = size != 0 ? size : 1;
#ifdef _WIN32
	if (void* ptr = _aligned_malloc(bytes, alignment)) { return ptr; }
#else
	// posix_memalign rather than std::aligned_alloc: no size-multiple rule, and it
	// exists on every macOS deployment target (aligned_alloc needs 10.15).
	void* ptr = nullptr;
	if (posix_memalign(&ptr, alignment < sizeof(void*) ? sizeof(void*) : alignment, bytes) == 0) { return ptr; }
#endif
	throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t align)
{
	return ::operator new(size, align);
}

void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept
{
	try { return ::operator new(size, align); }
	catch (...) { return nullptr; }
}

void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t& tag) noexcept
{
	return ::operator new(size, align, tag);
}

// Every deallocation form ends in one of the two counted base forms below, so
// a free anywhere in the program - library code included - is counted once.
void operator delete(void* ptr) noexcept
{
	lmms::test::noteFree(ptr);
	std::free(ptr);
}
void operator delete[](void* ptr) noexcept { ::operator delete(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { ::operator delete(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { ::operator delete(ptr); }
void operator delete(void* ptr, const std::nothrow_t&) noexcept { ::operator delete(ptr); }
void operator delete[](void* ptr, const std::nothrow_t&) noexcept { ::operator delete(ptr); }
void operator delete(void* ptr, std::align_val_t) noexcept
{
	lmms::test::noteFree(ptr);
#ifdef _WIN32
	// _aligned_malloc memory must go back through _aligned_free.
	_aligned_free(ptr);
#else
	std::free(ptr);
#endif
}
void operator delete[](void* ptr, std::align_val_t align) noexcept { ::operator delete(ptr, align); }
void operator delete(void* ptr, std::size_t, std::align_val_t align) noexcept { ::operator delete(ptr, align); }
void operator delete[](void* ptr, std::size_t, std::align_val_t align) noexcept { ::operator delete(ptr, align); }
void operator delete(void* ptr, std::align_val_t align, const std::nothrow_t&) noexcept
{
	::operator delete(ptr, align);
}
void operator delete[](void* ptr, std::align_val_t align, const std::nothrow_t&) noexcept
{
	::operator delete(ptr, align);
}
