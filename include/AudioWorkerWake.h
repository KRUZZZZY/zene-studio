/*
 * AudioWorkerWake.h - the platform half of the audio worker wake
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

#ifndef LMMS_AUDIO_WORKER_WAKE_H
#define LMMS_AUDIO_WORKER_WAKE_H

#include <atomic>
#include <cstdint>

// The wake primitive this build uses. compiledPrimitive() is constexpr in this header, so
// the implementation's compile-time assertion, the runtime test and any future caller all
// read the same answer; the test asserts it is the wait-on-address path on Apple and Linux
// so those jobs prove they exercise the path the unit is about.
//
// * Apple: `__ulock_wait` / `__ulock_wake` on the generation word itself - the
//   compare-and-wait trap libc++ itself uses under std::atomic::wait. std::atomic::wait is
//   UNAVAILABLE below macOS 11 (libc++ annotates it "introduced in macOS 11.0"; hosted run
//   36802800273 on macos-x86_64 is the build that said so), while __ulock is available since
//   macOS 10.12, below this project's 10.13 floor. The previous Apple path used a
//   process-global std::mutex and std::condition_variable: a mutex on the render thread every
//   period, and a lazy `new` of both objects on the first call. Both are gone.
//
//   The Apple selection is NOT conditional on the deployment target. A build for macOS 11+
//   uses the same trap, so the macOS x86_64 CI job runs the code legacy users run instead of
//   a second path only those users would ever exercise.
//
// * Linux: the same wait-on-address protocol on the same word, spelled out with
//   FUTEX_WAIT_PRIVATE / FUTEX_WAKE_PRIVATE (what std::atomic::wait lowers to there). This
//   makes the shipping wake path - not a stand-in for it - the thing the tests on the
//   development box exercise, with only the two-syscall shim differing from Apple.
//
// * Windows (and any POSIX without those calls): std::atomic::wait / notify_all, which is
//   what the tree used before. Both MSVC and mingw provide it; nothing in the CI matrix
//   changes there.
//
// The selection lives here, in the header, so the implementation, the test and any future
// caller compile against the same answer.
#if defined(__APPLE__)
#define LMMS_WORKER_WAKE_WAIT_ON_ADDRESS 1
#elif defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#if defined(SYS_futex) && defined(FUTEX_WAIT_PRIVATE) && defined(FUTEX_WAKE_PRIVATE)
#define LMMS_WORKER_WAKE_WAIT_ON_ADDRESS 1
#endif
#endif

namespace lmms::workerwake
{

//! The primitive this build's wake uses.
enum class Primitive
{
	//! std::atomic::wait / notify_all. Correct wherever C++20 is complete; unavailable below
	//! macOS 11, which is why no Apple build selects it.
	AtomicWait,
	//! Compare-and-wait on the generation word itself: no mutex, no allocation, and no wait
	//! on the notifying side (__ulock_wake on Apple, FUTEX_WAKE on Linux).
	WaitOnAddress
};

//! The primitive this build compiled. constexpr so a translation unit can assert on it at
//! compile time - the Apple build must not silently fall back to std::atomic::wait, which is
//! unavailable below macOS 11.
constexpr Primitive compiledPrimitive() noexcept
{
#ifdef LMMS_WORKER_WAKE_WAIT_ON_ADDRESS
	return Primitive::WaitOnAddress;
#else
	return Primitive::AtomicWait;
#endif
}

//! Wake every thread waiting on \a generation. The caller has already advanced the word (the
//! value, not this call, is the signal): this is a pure post. It runs on the render thread
//! every period, so it must not take a mutex, allocate, free or wait.
void notify(std::atomic<std::uint32_t>& generation) noexcept;

//! Block until \a generation no longer equals \a seen. A spurious return is allowed - the
//! caller re-reads the word and re-checks its own quit flag - and a value that already moved
//! makes this return at once, which is the no-lost-wake guarantee. The wait side is a parked
//! worker, never the render thread.
void wait(std::atomic<std::uint32_t>& generation, std::uint32_t seen) noexcept;

#ifdef LMMS_TESTING
//! Test-only observation point: the number of notify() calls since the counter was reset.
//! Production never carries the counter or the increment. A test uses it to prove that the
//! shipping AudioEngineWorkerThread::wakeWorkers() actually reaches this primitive - a test
//! that only included this header would pass against a worker reverted to its own wake.
std::uint64_t notifyCountForTesting() noexcept;
void resetNotifyCountForTesting() noexcept;
#endif

} // namespace lmms::workerwake

#endif // LMMS_AUDIO_WORKER_WAKE_H
