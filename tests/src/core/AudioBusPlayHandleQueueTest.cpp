/*
 * AudioBusPlayHandleQueueTest.cpp - play-handle list edits reach the bus through its queue (cluster 5467)
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

// Cluster 5467 (M0 blockers): AudioBusHandle::addPlayHandle()/removePlayHandle() used to take a mutex that
// doProcessing() held while it read the list on a worker thread. They now post an edit to a lock-free queue,
// and doProcessing() applies the queued edits, in posting order, before it reads the list. The white-box
// access below (the repo's precedent for checking a lock-free handoff: InstrumentRoutingTest) is what lets the
// test see the list itself; the public API cannot show where an edit is applied.

#include <QtTest>

#include <algorithm>
#include <span>
#include <thread>

// The test reads the bus's private list and queue. Everything the header needs is included first.
#include "AudioBus.h"
#include "AudioEngine.h"
#include "AudioDevice.h"
#include "Engine.h"
#include "LatencyCompensation.h"
#include "MpscQueue.h"
#include "PlayHandle.h"
#include "SampleFrame.h"
#include "Track.h"

#define private public
#include "AudioBusHandle.h"
#undef private

using namespace lmms;

namespace
{

//! A play handle that owns no buffer and has nothing to render: a placeholder the bus can list.
class IdlePlayHandle : public PlayHandle
{
public:
	IdlePlayHandle() : PlayHandle(Type::SamplePlayHandle) {}
	void play(std::span<SampleFrame>) override {}
	bool isFinished() const override { return true; }
	bool isFromTrack(const Track*) const override { return false; }
};

} // namespace

class AudioBusPlayHandleQueueTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! The posting thread never writes the list itself: an edit is queued and nothing is applied until the
	//! bus processes. (A direct write would need the lock the render thread holds while it reads the list.)
	void postingAnEditDoesNotTouchTheListUntilTheBusProcesses()
	{
		AudioBusHandle bus{QStringLiteral("AudioBusPlayHandleQueueTest"), false};
		IdlePlayHandle handle;

		std::thread poster([&bus, &handle] { bus.addPlayHandle(&handle); });
		poster.join();

		// The posting thread wrote nothing: the edit waits for the bus's own processing.
		QCOMPARE(static_cast<int>(bus.m_playHandles.size()), 0);
		bus.doProcessing();
		QCOMPARE(static_cast<int>(bus.m_playHandles.size()), 1);

		bus.removePlayHandle(&handle);
		bus.doProcessing();
		QVERIFY(bus.m_playHandles.isEmpty());
	}

	//! Edits posted from another thread, including a remove and a re-add of the same handle, land in posting
	//! order when the bus next processes: the list ends with the handle it was last given.
	void editsFromAnotherThreadApplyInPostingOrderAtTheNextProcessing()
	{
		AudioBusHandle bus{QStringLiteral("AudioBusPlayHandleQueueTest"), false};
		IdlePlayHandle first;
		IdlePlayHandle second;

		std::thread poster([&bus, &first, &second] {
			bus.addPlayHandle(&first);
			bus.addPlayHandle(&second);
			bus.removePlayHandle(&first);
			bus.addPlayHandle(&first);
		});
		poster.join();

		bus.doProcessing();

		QCOMPARE(static_cast<int>(bus.m_playHandles.size()), 2);
		QVERIFY(bus.m_playHandles.at(0) == &second);
		QVERIFY(bus.m_playHandles.at(1) == &first);

		bus.removePlayHandle(&first);
		bus.removePlayHandle(&second);
		bus.doProcessing();
		QVERIFY(bus.m_playHandles.isEmpty());
	}
};

QTEST_GUILESS_MAIN(AudioBusPlayHandleQueueTest)
#include "AudioBusPlayHandleQueueTest.moc"
