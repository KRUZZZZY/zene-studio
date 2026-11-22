/*
 * VstShmHandoffTest.cpp - the VST remote client's lock-free shared-block handoff
 *
 * The client itself is an out-of-process executable that cannot run inside a
 * test, so the properties its audio path relies on are pinned here on the
 * header it includes (plugins/VstBase/VstShmHandoff.h): a held read is visible
 * to the writer, invalidate() waits for the reader to drain before returning,
 * the read path neither allocates nor frees, and it is lock-free.
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

#include <QtTest>

#include <atomic>
#include <cstdint>
#include <thread>

#include "../../../plugins/VstBase/VstShmHandoff.h"
#include "AllocationProbe.h"

namespace
{

//! A handoff API that satisfies the source but not the drain: invalidate()
//! never waits for readers. The drain test must reject this mutant, which is
//! the proof that it is the waiting, not something else, that it observes.
class NoDrainHandoff
{
public:
	bool beginRead()
	{
		m_readers.fetch_add(1, std::memory_order_seq_cst);
		if (!m_valid.load(std::memory_order_seq_cst))
		{
			m_readers.fetch_sub(1, std::memory_order_seq_cst);
			return false;
		}
		return true;
	}

	void endRead()
	{
		m_readers.fetch_sub(1, std::memory_order_seq_cst);
	}

	void invalidate()
	{
		m_valid.store(false, std::memory_order_seq_cst);
	}

	void validate()
	{
		m_valid.store(true, std::memory_order_seq_cst);
	}

	bool isValid() const
	{
		return m_valid.load(std::memory_order_seq_cst);
	}

	bool isWriterWaiting() const
	{
		return false;
	}

	std::uint32_t activeReaders() const
	{
		return m_readers.load(std::memory_order_seq_cst);
	}

	static constexpr bool audioPathIsLockFree()
	{
		return true;
	}

private:
	std::atomic<bool> m_valid{false};
	std::atomic<std::uint32_t> m_readers{0};
};

//! Runs one held-reader/invalidate scenario. Returns true only when the writer
//! was observed waiting while the reader held the block, the reader's
//! beginRead() succeeded, and the writer had completed once it was released.
//! No QVERIFY here: the assertions live in the test slots.
template<typename H>
bool invalidateWaitsForHeldReader(H& handoff)
{
	handoff.validate();

	std::atomic<bool> readerEntered{false};
	std::atomic<bool> release{false};
	std::atomic<bool> readerAcquired{false};
	std::atomic<bool> writerDone{false};

	std::thread reader([&] {
		const bool acquired = handoff.beginRead();
		readerAcquired.store(acquired, std::memory_order_seq_cst);
		readerEntered.store(true, std::memory_order_seq_cst);
		while (!release.load(std::memory_order_seq_cst))
		{
			std::this_thread::yield();
		}
		if (acquired)
		{
			handoff.endRead();
		}
	});

	while (!readerEntered.load(std::memory_order_seq_cst))
	{
		std::this_thread::yield();
	}

	std::thread writer([&] {
		handoff.invalidate();
		writerDone.store(true, std::memory_order_seq_cst);
	});

	bool waited = false;
	while (!writerDone.load(std::memory_order_seq_cst))
	{
		if (handoff.isWriterWaiting())
		{
			waited = true;
			break;
		}
		std::this_thread::yield();
	}

	// The writer must still be blocked at the moment we observed it waiting.
	const bool stillBlocked = !writerDone.load(std::memory_order_seq_cst);

	release.store(true, std::memory_order_seq_cst);
	reader.join();
	writer.join();

	return waited && stillBlocked && readerAcquired.load(std::memory_order_seq_cst) &&
		writerDone.load(std::memory_order_seq_cst) && handoff.activeReaders() == 0;
}

} // namespace

class VstShmHandoffTest : public QObject
{
	Q_OBJECT

private slots:
	void normalRead()
	{
		lmms::vst::ShmHandoff handoff;
		handoff.validate();
		QVERIFY(handoff.beginRead());
		QCOMPARE(handoff.activeReaders(), std::uint32_t{1});
		handoff.endRead();
		QCOMPARE(handoff.activeReaders(), std::uint32_t{0});
	}

	void invalidReadRefused()
	{
		lmms::vst::ShmHandoff handoff;

		QVERIFY(!handoff.beginRead());
		QCOMPARE(handoff.activeReaders(), std::uint32_t{0});

		handoff.validate();
		handoff.invalidate();

		QVERIFY(!handoff.beginRead());
		QCOMPARE(handoff.activeReaders(), std::uint32_t{0});
	}

	void invalidateWaitsForHeldReaderTest()
	{
		lmms::vst::ShmHandoff handoff;
		QVERIFY(invalidateWaitsForHeldReader(handoff));
	}

	void mutantWithoutDrainIsCaught()
	{
		lmms::vst::ShmHandoff real;
		NoDrainHandoff mutant;

		QVERIFY(invalidateWaitsForHeldReader(real));
		QVERIFY(!invalidateWaitsForHeldReader(mutant));
	}

	void reconfigurationCycles()
	{
		lmms::vst::ShmHandoff handoff;
		for (int i = 0; i < 1000; ++i)
		{
			handoff.validate();
			QVERIFY(handoff.beginRead());
			handoff.endRead();
			handoff.invalidate();
		}

		QCOMPARE(handoff.activeReaders(), std::uint32_t{0});

		handoff.validate();
		QVERIFY(handoff.beginRead());
		handoff.endRead();
		QCOMPARE(handoff.activeReaders(), std::uint32_t{0});
	}

	void readPathDoesNotAllocate()
	{
		lmms::vst::ShmHandoff handoff;
		handoff.validate();

		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		bool acquired = true;
		for (int i = 0; i < 1000; ++i)
		{
			if (!handoff.beginRead())
			{
				acquired = false;
				break;
			}
			handoff.endRead();
		}
		lmms::test::tlCountAllocations = false;

		QVERIFY(acquired);
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
		QCOMPARE(lmms::test::tlFreeCount, std::uint64_t{0});
	}

	void readPathIsLockFree()
	{
		QVERIFY(lmms::vst::ShmHandoff::audioPathIsLockFree());
	}
};

QTEST_APPLESS_MAIN(VstShmHandoffTest)

#include "VstShmHandoffTest.moc"
