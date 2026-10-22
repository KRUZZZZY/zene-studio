/*
 * AudioWorkerWake.cpp - the platform half of the audio worker wake
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

#include "AudioWorkerWake.h"

#include "RealtimeScope.h"

#include <cerrno>
#include <climits>
#include <cstdint>

// The selection (LMMS_WORKER_WAKE_WAIT_ON_ADDRESS) and the Linux futex constants come from
// the header. The Apple branch there is deliberate rather than a fallback: std::atomic::wait
// is unavailable below macOS 11, and __ulock covers every target this project builds for.
// The Linux branch spells out the same protocol so the tests on the development box compile
// and run the shipping code, not a stand-in: only the two syscall shims differ from Apple.

#if defined(__linux__) && defined(LMMS_WORKER_WAKE_WAIT_ON_ADDRESS)
#include <unistd.h>
#endif

#if defined(__APPLE__)
// The compare-and-wait trap libc++ itself uses under std::atomic::wait. It is not in a
// public SDK header, so it is declared here; these are ulock.h's signatures, the same ones
// libc++'s own __threading_support declares. Available since macOS 10.12, below this
// project's 10.13 floor (README, "macOS Intel and Apple Silicon"), so no availability guard
// is needed - and std::atomic::wait, which *is* gated at 11.0, is not used on Apple at all.
extern "C" int __ulock_wait(std::uint32_t operation, void* address, std::uint64_t value,
	std::uint32_t timeout);
extern "C" int __ulock_wake(std::uint32_t operation, void* address, std::uint64_t wakeValue);
namespace
{
//! UL_COMPARE_AND_WAIT: sleep while the word at `address` still equals `value`.
constexpr std::uint32_t UlCompareAndWait = 1;
//! ULF_WAKE_ALL: wake every waiter on the address, which is what notify_all meant here.
constexpr std::uint32_t UlfWakeAll = 0x00000100;
} // namespace
#endif

namespace lmms::workerwake
{

namespace
{

// The trap reads a 32-bit word at the address it is given. A lock-based std::atomic would
// carry a mutex in that word instead of the value, which is the defect this fix removes, so
// the representation is asserted rather than assumed.
static_assert(sizeof(std::atomic<std::uint32_t>) == sizeof(std::uint32_t),
	"the wake word must be the value itself, not a lock and a value");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
	"the wake word must be lock-free: a lock-based atomic is the mutex this path must not take");

#ifdef LMMS_TESTING
std::atomic<std::uint64_t> s_notifyCount{0};
#endif

} // namespace

Primitive compiledPrimitive() noexcept
{
#ifdef LMMS_WORKER_WAKE_WAIT_ON_ADDRESS
	return Primitive::WaitOnAddress;
#else
	return Primitive::AtomicWait;
#endif
}

#if defined(__APPLE__)
// Compile-time, not a test-only claim: an Apple build whose wake is not the wait-on-address
// path would put std::atomic::wait (unavailable below macOS 11) back on the render thread.
static_assert(compiledPrimitive() == Primitive::WaitOnAddress,
	"the Apple wake must be the wait-on-address path");
#endif

void notify(std::atomic<std::uint32_t>& generation) noexcept
{
#ifdef LMMS_TESTING
	s_notifyCount.fetch_add(1, std::memory_order_relaxed);
#endif
	// FUTEX_WAKE / __ulock_wake do not block: they wake sleepers and return. A wake with no
	// waiter is a no-op, and the value the caller already stored is the actual signal, so no
	// wake can be lost. RealtimeSanitizer still reports the trap as a syscall.
	const RealtimeExemption syscallCannotBlock;
#ifdef LMMS_WORKER_WAKE_WAIT_ON_ADDRESS
#if defined(__APPLE__)
	// Returns non-zero (ENOENT) when no worker is parked, which is not an error: the
	// generation move is what the worker checks, so a post with no waiter loses nothing.
	__ulock_wake(UlCompareAndWait | UlfWakeAll, static_cast<void*>(&generation), 0);
#else
	::syscall(SYS_futex, reinterpret_cast<std::int32_t*>(&generation), FUTEX_WAKE_PRIVATE,
		INT_MAX, nullptr, nullptr, 0);
#endif
#else
	generation.notify_all();
#endif
}

#ifdef LMMS_WORKER_WAKE_WAIT_ON_ADDRESS
namespace
{

//! One platform park on the generation word. Returns true when the caller should stop
//! waiting: the word moved, or the platform reported something other than a signal.
//!
//! Apple: 0 means woken; a -1 return sets errno and a negative return is -errno (both
//! conventions exist across SDKs), so both are read. EWOULDBLOCK means the word already
//! differs and ETIMEDOUT cannot happen with a zero (infinite) timeout; only EINTR parks
//! again. Linux: FUTEX_WAIT returns 0 when woken and -1 with errno otherwise; EAGAIN means
//! the word already differs and ETIMEDOUT cannot happen with a null timeout.
bool parkOnce(std::atomic<std::uint32_t>& generation, std::uint32_t seen) noexcept
{
#if defined(__APPLE__)
	const int result = __ulock_wait(UlCompareAndWait, static_cast<void*>(&generation), seen, 0);
	const int error = (result == -1) ? errno : -result;
	return result == 0 || (error != EINTR && error != 0);
#else
	const int result = ::syscall(SYS_futex, reinterpret_cast<std::int32_t*>(&generation),
		FUTEX_WAIT_PRIVATE, static_cast<std::int32_t>(seen), nullptr, nullptr, 0);
	return result == 0 || (errno != EINTR && errno != EAGAIN);
#endif
}

} // namespace
#endif // LMMS_WORKER_WAKE_WAIT_ON_ADDRESS

void wait(std::atomic<std::uint32_t>& generation, std::uint32_t seen) noexcept
{
#ifdef LMMS_WORKER_WAKE_WAIT_ON_ADDRESS
	// The value check before parking is what makes a wake that arrived first impossible to
	// lose: if the word already moved, the caller's wait is over. After every park the word
	// is read again, and a signal only parks this again while it is still unchanged.
	while (generation.load(std::memory_order_acquire) == seen && !parkOnce(generation, seen))
	{
	}
#else
	generation.wait(seen, std::memory_order_acquire);
#endif
}

#ifdef LMMS_TESTING
std::uint64_t notifyCountForTesting() noexcept
{
	return s_notifyCount.load(std::memory_order_relaxed);
}

void resetNotifyCountForTesting() noexcept
{
	s_notifyCount.store(0, std::memory_order_relaxed);
}
#endif

} // namespace lmms::workerwake
