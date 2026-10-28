/*
 * AbletonLinkWorkDispatcherTest.cpp - the vendored asio's work_dispatcher, moved
 *                                     through its legacy (TS) executor specialisation.
 *
 * asio's work_dispatcher carries the handler's function and the executor's
 * outstanding work from one place to the next. Its TS-executor move constructor
 * (src/3rdparty/ableton-link/asio/include/asio/detail/work_dispatcher.hpp) must
 * move the function base out of the dispatcher it is given. It instead moved the
 * base out of *this, the object still being constructed, before that base existed.
 *
 * Reading the unconstructed base is undefined behaviour, so the test must not read
 * the value it would see there. The function's move constructor first compares the
 * address of its source with its own. A correct move has two distinct objects and
 * reads the source's value. The buggy move's source is the destination itself: that
 * is detected by the address comparison alone, and the value is replaced by a
 * sentinel without being read. Each test asserts that the move was not a self-move,
 * so the RED result is the same on every run. The value check then proves the moved
 * dispatcher still runs its function and hands the result to its handler.
 *
 * Requires asio's TS executors (ASIO_NO_TS_EXECUTORS undefined), which the Link
 * build never defines: without them there is no TS specialisation to move.
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

#include <QtTest>

#include <new>
#include <utility>

#include "asio/detail/work_dispatcher.hpp"

namespace
{

// A legacy (Networking TS) executor: it has the member functions asio's is_executor
// trait looks for, so work_dispatcher selects its TS specialisation. Outstanding work
// is counted in a test-owned integer.
struct LegacyExecutor
{
	int* outstanding;

	void context() {}
	void on_work_started() { ++*outstanding; }
	void on_work_finished() { --*outstanding; }

	template <typename F, typename Allocator>
	void dispatch(F&& function, const Allocator&) { std::forward<F>(function)(); }
	template <typename F, typename Allocator>
	void post(F&&, const Allocator&) {}
	template <typename F, typename Allocator>
	void defer(F&&, const Allocator&) {}
};

// The function a dispatcher carries. Its move constructor compares its source's address
// with its own before it reads any value (see the file comment). A self-source move
// takes the sentinel instead of reading the unconstructed value, and records that it
// happened.
struct Payload
{
	// Stands in for the value a self-source move cannot read. No test expects it.
	static constexpr int unreadValue = -1;
	static inline bool movedFromSelf = false;

	int value;

	explicit Payload(int v) : value(v) {}
	Payload(Payload&& other) noexcept
		: value(&other == this ? unreadValue : other.value)
	{
		movedFromSelf = &other == this;
	}
	Payload(const Payload&) = default;

	int operator()() { return value; }
};

struct Handler
{
	int* received;

	void operator()(int result) { *received = result; }
};

using Dispatcher = asio::detail::work_dispatcher<Payload, Handler, LegacyExecutor>;

} // namespace

class AbletonLinkWorkDispatcherTest : public QObject
{
	Q_OBJECT
private slots:
	void movedDispatcherRunsItsFunction();
	void movedDispatcherTransfersItsWorkOnce();
};

void AbletonLinkWorkDispatcherTest::movedDispatcherRunsItsFunction()
{
	int received = -1;
	int outstanding = 0;
	Dispatcher source(Payload(42), Handler{&received}, LegacyExecutor{&outstanding});

	// The moved-to dispatcher lives in storage the test owns, so the move has a
	// destination that is not the source.
	alignas(Dispatcher) unsigned char storage[sizeof(Dispatcher)];
	Payload::movedFromSelf = false;
	Dispatcher* moved = new (storage) Dispatcher(std::move(source));

	QVERIFY2(!Payload::movedFromSelf,
		"the move read the dispatcher being constructed, not the one it moves from");

	(*moved)();
	QCOMPARE(received, 42);
	moved->~Dispatcher();
}

void AbletonLinkWorkDispatcherTest::movedDispatcherTransfersItsWorkOnce()
{
	int received = -1;
	int outstanding = 0;
	Dispatcher source(Payload(7), Handler{&received}, LegacyExecutor{&outstanding});
	QCOMPARE(outstanding, 1);

	alignas(Dispatcher) unsigned char storage[sizeof(Dispatcher)];
	Payload::movedFromSelf = false;
	Dispatcher* moved = new (storage) Dispatcher(std::move(source));
	QVERIFY2(!Payload::movedFromSelf,
		"the move read the dispatcher being constructed, not the one it moves from");
	QCOMPARE(outstanding, 1);   // the move transfers the unit of work, it does not add one

	(*moved)();
	QCOMPARE(received, 7);
	QCOMPARE(outstanding, 0);   // running the dispatcher releases that one unit
	moved->~Dispatcher();
	QCOMPARE(outstanding, 0);   // the moved-from source releases nothing
}

QTEST_GUILESS_MAIN(AbletonLinkWorkDispatcherTest)
#include "AbletonLinkWorkDispatcherTest.moc"
