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
#include "RemotePluginShmFifoTargetDefinitions.h"
#include "SharedMemory.h"

#include <QtTest>

#include <atomic>
#include <barrier>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>

using namespace lmms;

// RemotePluginShmFifoNoSysIpcProbe.cpp
int remotePluginShmFifoValueWithoutSysIpc();

class ShmRemotePluginHarness : public RemotePluginBase
{
public:
	using RemotePluginBase::RemotePluginBase;
	bool processMessage( const message & ) override { return true; }
};

class RemotePluginShmFifoTest : public QObject
{
	Q_OBJECT

	template<typename T, typename = void>
	struct InvalidFlagIsAtomic : std::false_type {};
	template<typename T>
	struct InvalidFlagIsAtomic<T, std::void_t<decltype(T::m_invalid)>>
		: std::is_same<decltype(T::m_invalid), std::atomic<bool>> {};

private slots:
	void headerKeepsTheBuildsShmFifoDefinitionWithoutSysIpc()
	{
		// The build defines SYNC_WITH_SHM_FIFO as 1; the header's own fallback
		// definition (hosts without <sys/ipc.h>) must not replace it.
		QCOMPARE(remotePluginShmFifoValueWithoutSysIpc(), 1);
	}

	void targetsCompilingRemotePluginBaseUseTheStaticExportBranch()
	{
		// RemotePluginBase.cpp defines LMMS_EXPORT members out of line. Outside
		// lmmsobjs (which defines lmmsobjs_EXPORTS) the generated lmms_export.h
		// makes LMMS_EXPORT __declspec(dllimport) on Windows unless
		// LMMS_STATIC_DEFINE is set - mingw: "redeclared without dllimport
		// attribute: previous dllimport ignored" (cluster ci-v041-5).
		QCOMPARE(ZENE_SHM_FIFO_TEST_IS_STATIC_EXPORT, 1);
		QCOMPARE(ZENE_SHM_FIFO_COMPILE_CHECK_IS_STATIC_EXPORT, 1);
	}

	void sharedMemoryInvalidFlagIsAtomic()
	{
		QVERIFY(( InvalidFlagIsAtomic<shmFifo>::value ));

		shmFifo fifo;
		QVERIFY(!fifo.isInvalid());
		fifo.invalidate();
		QVERIFY(fifo.isInvalid());
	}

	void sharedMemoryArrayWithoutKeyRejectsHeaderAdditionOverflow()
	{
		using Array = SharedMemory<char[]>;
		constexpr auto overflowingBytes = std::numeric_limits<std::size_t>::max() - sizeof(std::uint64_t) + 1;

		Array array;
		array.create(1);
		array[0] = 'a';
		QVERIFY_EXCEPTION_THROWN(array.create(overflowingBytes), std::length_error);
		QCOMPARE(array.size(), std::size_t{1});
		QCOMPARE(array[0], 'a');
	}

	void sharedMemoryArrayWithKeyRejectsHeaderAdditionOverflow()
	{
		using Array = SharedMemory<char[]>;
		constexpr auto overflowingBytes = std::numeric_limits<std::size_t>::max() - sizeof(std::uint64_t) + 1;

		Array array;
		array.create(1);
		array[0] = 'a';
		QVERIFY_EXCEPTION_THROWN(
			array.create("array-header-overflow-test", overflowingBytes), std::length_error);
		QCOMPARE(array.size(), std::size_t{1});
		QCOMPARE(array[0], 'a');
	}

	void callerTokensStayDistinctDespiteLegacyHashCollision()
	{
		shmFifo::setCallerIdHashForTest(&collidingThreadHash);
		std::barrier start{3};
		std::barrier captured{3};
		std::barrier release{3};
		using CallerKey = decltype(shmFifo::callerKey());
		const bool callerKeyUsesAddress = std::is_same_v<CallerKey, const void *>;
		CallerKey keys[2]{};
		std::thread::id ids[2];
		std::thread first([&] {
			start.arrive_and_wait();
			ids[0] = std::this_thread::get_id();
			keys[0] = shmFifo::callerKey();
			captured.arrive_and_wait();
			release.arrive_and_wait();
		});
		std::thread second([&] {
			start.arrive_and_wait();
			ids[1] = std::this_thread::get_id();
			keys[1] = shmFifo::callerKey();
			captured.arrive_and_wait();
			release.arrive_and_wait();
		});
		start.arrive_and_wait();
		captured.arrive_and_wait();
		const bool legacyHashesCollide = shmFifo::callerIdHashForTest( ids[0] )
			== shmFifo::callerIdHashForTest( ids[1] );
		const bool distinct = keys[0] != keys[1];
		release.arrive_and_wait();
		first.join();
		second.join();
		shmFifo::setCallerIdHashForTest(nullptr);

		QVERIFY(callerKeyUsesAddress);
		QVERIFY(legacyHashesCollide);
		QVERIFY(distinct);
	}

	void oversizedAggregateMessageFailsAndReleasesTheReceiver()
	{
		auto * hostIn = new shmFifo;
		auto * clientOut = new shmFifo{hostIn->shmKey()};
		auto * hostOut = new shmFifo;
		auto * clientIn = new shmFifo{hostOut->shmKey()};
		ShmRemotePluginHarness sender{hostIn, hostOut};
		ShmRemotePluginHarness receiver{clientIn, clientOut};

		const std::string field( 300 * 1024, 'x' );
		QVERIFY(field.size() < static_cast<std::size_t>( SHM_FIFO_SIZE ));
		RemotePluginBase::message oversized{IdDebugMessage};
		oversized.addString( field ).addString( field );

		s_spaceWaitObserved.store( false, std::memory_order_relaxed );
		shmFifo::setSpaceWaitHookForTest( &interruptSpaceWait );
		const int bytesSent = sender.sendMessage( oversized );
		shmFifo::setSpaceWaitHookForTest( nullptr );

		QVERIFY(!s_spaceWaitObserved.load( std::memory_order_relaxed ));
		QCOMPARE(bytesSent, 0);
		QVERIFY(sender.isInvalid());
		RemotePluginBase::message received;
		const auto result = receiver.receiveFrameBefore( std::chrono::steady_clock::time_point::min(), received );
		QCOMPARE(result, RemotePluginBase::ReceiveResult::Lost);
		QCOMPARE(received.id, static_cast<int>( IdUndefined ));
		QVERIFY(receiver.isInvalid());
	}

	void recursiveSpaceWaitReleasesTheDataSemaphore()
	{
		shmFifo fifo;
		fifo.lock();
		fifo.lock();
		const int savedDepth = fifo.releaseLockForSpaceWait();
		QCOMPARE(savedDepth, 2);

		bool readerAcquired = false;
		std::thread reader([&] {
			readerAcquired = fifo.tryLockFor(0);
			if( readerAcquired ) { fifo.unlock(); }
		});
		reader.join();
		if( !readerAcquired ) { fifo.unlock(); }

		QVERIFY(readerAcquired);
	}

	void writerReservationExcludesAnotherWriter()
	{
		shmFifo fifo;
		fifo.lockWriter();
		const bool secondWriterEntered = fifo.tryLockWriter();
		fifo.unlockWriter();
		QVERIFY(!secondWriterEntered);

		QVERIFY(fifo.tryLockWriter());
		fifo.unlockWriter();
	}

private:
	static void interruptSpaceWait( shmFifo * _fifo ) noexcept
	{
		s_spaceWaitObserved.store( true, std::memory_order_relaxed );
		_fifo->invalidate();
	}

	static std::uint64_t collidingThreadHash(std::thread::id) noexcept
	{
		return 7;
	}

	inline static std::atomic<bool> s_spaceWaitObserved{false};
};

QTEST_GUILESS_MAIN(RemotePluginShmFifoTest)

#include "RemotePluginShmFifoTest.moc"
