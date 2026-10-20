/*
 * RemotePluginShmFifoTest.cpp - deterministic shared-memory transport regressions
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "RemotePluginBase.h"

#include <QtTest>

#include <atomic>
#include <barrier>
#include <cstdint>
#include <thread>
#include <type_traits>

using namespace lmms;

class RemotePluginShmFifoTest : public QObject
{
	Q_OBJECT

	template<typename T, typename = void>
	struct InvalidFlagIsAtomic : std::false_type {};
	template<typename T>
	struct InvalidFlagIsAtomic<T, std::void_t<decltype(T::m_invalid)>>
		: std::is_same<decltype(T::m_invalid), std::atomic<bool>> {};

private slots:
	void sharedMemoryInvalidFlagIsAtomic()
	{
		QVERIFY(( InvalidFlagIsAtomic<shmFifo>::value ));

		shmFifo fifo;
		QVERIFY(!fifo.isInvalid());
		fifo.invalidate();
		QVERIFY(fifo.isInvalid());
	}

	void callerKeysSurviveForcedHashCollisions()
	{
		shmFifo::setCallerIdHashForTest(&collidingThreadHash);
		std::barrier start{3};
		std::uint64_t keys[2]{};
		std::thread first([&] {
			start.arrive_and_wait();
			keys[0] = shmFifo::callerKey();
		});
		std::thread second([&] {
			start.arrive_and_wait();
			keys[1] = shmFifo::callerKey();
		});
		start.arrive_and_wait();
		first.join();
		second.join();
		shmFifo::setCallerIdHashForTest(nullptr);

		QVERIFY(keys[0] != keys[1]);
	}

private:
	static std::uint64_t collidingThreadHash(std::thread::id) noexcept
	{
		return 7;
	}
};

QTEST_GUILESS_MAIN(RemotePluginShmFifoTest)

#include "RemotePluginShmFifoTest.moc"
