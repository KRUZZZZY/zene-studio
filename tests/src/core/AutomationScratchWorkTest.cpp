/*
 * AutomationScratchWorkTest.cpp - one automation read builds its lists in bounded work
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

// The automation scratch (include/AutomationRenderScratch.h) is filled on the audio thread, once per
// period, by Song::processAutomations. It reserves its capacity, so it never allocates - but a reserved
// vector whose entries are INSERTED in order still moves every entry behind the insertion point, and an
// input order opposite the sort order (clips handed over by a track that starts earlier, targets whose
// addresses descend) makes each insert pay for the whole list: N items cost O(N^2) relocations, which at
// the 16384-entry render capacity is time the period does not have.
//
// These tests pin the work, not the wall clock: the scratch counts its own relocations (clipMoves() /
// valueMoves()), so a quadratic build fails the same way on every machine and at every optimisation
// level. The budget is an O(N log N) build with room to spare; the pre-fix build took N^2/2, five times
// over that budget for the 1024 items used here.

#include <QtTest>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "AllocationProbe.h"
#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationRenderScratch.h"
#include "AutomationTrack.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "RealtimeScope.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

//! What an O(N log N) build of \a count entries may relocate: two passes per merge level, and slack.
std::size_t moveBudget(int count)
{
	std::size_t levels = 1;
	for (std::size_t width = 1; static_cast<int>(width) < count; width *= 2) { ++levels; }
	return 4 * static_cast<std::size_t>(count) * levels;
}

bool byPointedAddress(const AutomatableModel* a, const AutomatableModel* b)
{
	return std::less<const AutomatableModel*>{}(a, b);
}

//! The scratch's entry order: by model address, as the read has always handed it out.
bool entriesByAddress(const AutomationRenderScratch::Entry& a, const AutomationRenderScratch::Entry& b)
{
	return byPointedAddress(a.model, b.model);
}

//! \a count automation clips at start positions 0, 1, 2, ...: the worst case for an ordered insert is
//! handing them over in the opposite order, because then every one of them belongs at the front.
class PositionedClips
{
public:
	explicit PositionedClips(int count) : m_track(Engine::getSong())
	{
		for (int i = 0; i < count; ++i)
		{
			auto clip = std::make_unique<AutomationClip>(&m_track);
			clip->movePosition(TimePos(i));
			m_clips.push_back(std::move(clip));
		}
	}

	//! The clips must go before the track whose list they are still in.
	~PositionedClips() { m_clips.clear(); }

	int count() const { return static_cast<int>(m_clips.size()); }

	Clip* at(int i) const { return m_clips[static_cast<std::size_t>(i)].get(); }

	Track* track() { return &m_track; }

	//! Hands every clip over from the last position to the first: in an ordered build each insert is at the front.
	void fillReversed(AutomationRenderScratch& scratch) const
	{
		for (auto it = m_clips.rbegin(); it != m_clips.rend(); ++it) { scratch.insertClip(it->get()); }
	}

	//! Hands them over in position order: an ordered build moves nothing.
	void fillInOrder(AutomationRenderScratch& scratch) const
	{
		for (const auto& clip : m_clips) { scratch.insertClip(clip.get()); }
	}

private:
	AutomationTrack m_track;
	std::vector<std::unique_ptr<AutomationClip>> m_clips;
};

//! Distinct automation targets, held in descending address order - what an insert ordered by address has
//! to shift the whole list for. valueAt(i) is the value of address(i), a whole number so it reads back exactly.
class Targets
{
public:
	explicit Targets(int count)
	{
		std::vector<AutomatableModel*> ascending;
		for (int i = 0; i < count; ++i)
		{
			m_models.push_back(std::make_unique<FloatModel>(0.0f, 0.0f, 100.0f, 1.0f));
			ascending.push_back(m_models.back().get());
		}
		std::sort(ascending.begin(), ascending.end(), byPointedAddress);
		m_addresses.assign(ascending.rbegin(), ascending.rend());
	}

	std::size_t count() const { return m_addresses.size(); }

	AutomatableModel* address(std::size_t i) const { return m_addresses[i]; }

	float valueAt(std::size_t i) const { return static_cast<float>(i + 1); }

	//! Sets every target, the highest address first: the worst order for an insert ordered by address.
	//! Returns how many settings the scratch refused.
	int fillReversed(AutomationRenderScratch& scratch) const
	{
		int refused = 0;
		for (std::size_t i = 0; i < m_addresses.size(); ++i)
		{
			if (!scratch.set(m_addresses[i], valueAt(i))) { ++refused; }
		}
		return refused;
	}

	//! Sets them in address order: an ordered build moves nothing.
	int fillInOrder(AutomationRenderScratch& scratch) const
	{
		int refused = 0;
		for (std::size_t i = m_addresses.size(); i > 0; --i)
		{
			if (!scratch.set(m_addresses[i - 1], valueAt(i - 1))) { ++refused; }
		}
		return refused;
	}

private:
	std::vector<std::unique_ptr<FloatModel>> m_models;
	std::vector<AutomatableModel*> m_addresses;  //!< descending by address
};

} // namespace

class AutomationScratchWorkTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // this harness drives nothing; the periods are called by hand
	}

	void cleanupTestCase() { Engine::destroy(); }

	//! The regression: clips handed over in the order opposite their sort order.
	void clipsHandedOverInReverseOrderAreBuiltInBoundedWork()
	{
		constexpr int Count = 1024;
		const PositionedClips clips(Count);
		AutomationRenderScratch scratch(AutomationRenderScratch::RenderCapacity);
		scratch.clear();

		clips.fillReversed(scratch);
		const std::vector<Clip*>& built = scratch.clips();

		QCOMPARE(static_cast<int>(built.size()), Count);
		QVERIFY2(std::is_sorted(built.begin(), built.end(), Clip::comparePosition),
			"the clips were not handed out in start order");
		for (int i = 0; i < Count; ++i) { QCOMPARE(built[static_cast<std::size_t>(i)], clips.at(i)); }
		const std::size_t budget = moveBudget(Count);
		QVERIFY2(scratch.clipMoves() <= budget,
			qPrintable(QStringLiteral("filling %1 clips in reverse moved %2 scratch entries; the O(N log N) "
				"bound is %3").arg(Count).arg(scratch.clipMoves()).arg(budget)));
	}

	//! The regression, the other half: targets whose addresses descend.
	void targetsSetInReverseAddressOrderAreBuiltInBoundedWork()
	{
		constexpr int Count = 1024;
		const Targets targets(Count);
		AutomationRenderScratch scratch(AutomationRenderScratch::RenderCapacity);
		scratch.clear();

		QCOMPARE(targets.fillReversed(scratch), 0);
		const std::vector<AutomationRenderScratch::Entry>& built = scratch.entries();

		QCOMPARE(static_cast<int>(built.size()), Count);
		QVERIFY2(std::is_sorted(built.begin(), built.end(), entriesByAddress),
			"the values were not handed out in model order");
		int wrong = 0;
		for (std::size_t i = 0; i < targets.count(); ++i)
		{
			const float* known = scratch.find(targets.address(i));
			if (known == nullptr || *known != targets.valueAt(i)) { ++wrong; }
		}
		QVERIFY2(wrong == 0, qPrintable(QStringLiteral("%1 of %2 targets were lost or held a wrong value")
			.arg(wrong).arg(Count)));
		const std::size_t budget = moveBudget(Count);
		QVERIFY2(scratch.valueMoves() <= budget,
			qPrintable(QStringLiteral("setting %1 targets in reverse address order moved %2 scratch entries; "
				"the O(N log N) bound is %3").arg(Count).arg(scratch.valueMoves()).arg(budget)));
	}

	//! The common case must not pay for the fix: an input already in order moves nothing at all.
	void anOrderedReadMovesNothing()
	{
		constexpr int Count = 256;
		const PositionedClips clips(Count);
		const Targets targets(Count);
		AutomationRenderScratch scratch(AutomationRenderScratch::RenderCapacity);
		scratch.clear();

		clips.fillInOrder(scratch);
		QCOMPARE(targets.fillInOrder(scratch), 0);
		const std::vector<Clip*>& builtClips = scratch.clips();
		const std::vector<AutomationRenderScratch::Entry>& builtValues = scratch.entries();

		QCOMPARE(static_cast<int>(builtClips.size()), Count);
		QCOMPARE(static_cast<int>(builtValues.size()), Count);
		QVERIFY(std::is_sorted(builtClips.begin(), builtClips.end(), Clip::comparePosition));
		QVERIFY(std::is_sorted(builtValues.begin(), builtValues.end(), entriesByAddress));
		QCOMPARE(scratch.clipMoves(), static_cast<std::size_t>(0));
		QCOMPARE(scratch.valueMoves(), static_cast<std::size_t>(0));
	}

	//! Clips starting at the same tick keep the order they were handed over in, as an ordered insert did.
	void clipsAtTheSameTickKeepTheirOrder()
	{
		const PositionedClips clips(3);
		for (int i = 0; i < clips.count(); ++i) { clips.at(i)->movePosition(TimePos(100)); }
		AutomationRenderScratch scratch(AutomationRenderScratch::RenderCapacity);
		scratch.clear();

		QVERIFY(scratch.insertClip(clips.at(2)));
		QVERIFY(scratch.insertClip(clips.at(0)));
		QVERIFY(scratch.insertClip(clips.at(1)));
		const std::vector<Clip*>& built = scratch.clips();

		QCOMPARE(static_cast<int>(built.size()), clips.count());
		QCOMPARE(built[0], clips.at(2));
		QCOMPARE(built[1], clips.at(0));
		QCOMPARE(built[2], clips.at(1));
	}

	//! The read's real route: TrackContainer::gatherAutomationClips hands a whole track's range over.
	void aTracksRangeIsHandedOutInStartOrder()
	{
		constexpr int Count = 64;
		PositionedClips clips(Count);
		AutomationRenderScratch scratch(AutomationRenderScratch::RenderCapacity);
		scratch.clear();

		scratch.insertClipsInRange(*clips.track(), TimePos(0), TimePos(Count - 1));
		const std::vector<Clip*>& built = scratch.clips();

		QCOMPARE(static_cast<int>(built.size()), Count);
		QVERIFY(std::is_sorted(built.begin(), built.end(), Clip::comparePosition));
		for (int i = 0; i < Count; ++i) { QCOMPARE(built[static_cast<std::size_t>(i)]->startPosition(), i); }
		QCOMPARE(scratch.overflows(), static_cast<std::size_t>(0));
	}

	//! A bounded scratch with no room drops what does not fit, counts it, and still answers in order.
	void aFullScratchDropsAndCountsInsteadOfGrowing()
	{
		constexpr int Room = 4;
		const PositionedClips clips(Room + 1);
		const Targets targets(Room + 1);
		AutomationRenderScratch scratch(Room);
		scratch.clear();

		int fitted = 0;
		for (int i = clips.count() - 1; i >= 0; --i) { if (scratch.insertClip(clips.at(i))) { ++fitted; } }
		QCOMPARE(fitted, Room);
		QCOMPARE(static_cast<int>(scratch.clips().size()), Room);
		QVERIFY(std::is_sorted(scratch.clips().begin(), scratch.clips().end(), Clip::comparePosition));
		QCOMPARE(scratch.overflows(), static_cast<std::size_t>(1));

		fitted = 0;
		for (std::size_t i = 0; i < targets.count(); ++i)
		{
			if (scratch.set(targets.address(i), targets.valueAt(i))) { ++fitted; }
		}
		QCOMPARE(fitted, Room);
		QCOMPARE(static_cast<int>(scratch.entries().size()), Room);
		QCOMPARE(scratch.overflows(), static_cast<std::size_t>(2));
		// The four that fitted are the last four set: the address order still holds, the rest were counted.
		QVERIFY(std::is_sorted(scratch.entries().begin(), scratch.entries().end(), entriesByAddress));
		QVERIFY(!scratch.contains(targets.address(targets.count() - 1)));
		QVERIFY(scratch.contains(targets.address(0)));
	}

	//! The control thread's growth (Song::ensureAutomationScratchCapacity) keeps what the scratch holds.
	void reserveKeepsWhatTheScratchHolds()
	{
		constexpr std::size_t Room = 4;
		const Targets targets(64);
		AutomationRenderScratch scratch(Room);
		scratch.clear();

		for (std::size_t i = 0; i < Room; ++i) { QVERIFY(scratch.set(targets.address(i), targets.valueAt(i))); }
		scratch.reserve(64);
		QCOMPARE(scratch.size(), Room);
		QCOMPARE(scratch.overflows(), static_cast<std::size_t>(0));
		for (std::size_t i = 0; i < Room; ++i)
		{
			const float* known = scratch.find(targets.address(i));
			QVERIFY2(known != nullptr, "a target was lost when the scratch grew");
			QCOMPARE(*known, targets.valueAt(i));
		}
		for (std::size_t i = Room; i < 64; ++i) { QVERIFY(scratch.set(targets.address(i), targets.valueAt(i))); }
		QCOMPARE(scratch.size(), static_cast<std::size_t>(64));
		QCOMPARE(scratch.overflows(), static_cast<std::size_t>(0));
		QVERIFY(std::is_sorted(scratch.entries().begin(), scratch.entries().end(), entriesByAddress));
		for (std::size_t i = 0; i < 64; ++i) { QCOMPARE(*scratch.find(targets.address(i)), targets.valueAt(i)); }
	}

	//! One model set twice keeps one entry and the last value, however the scratch is ordered.
	void aModelSetTwiceKeepsTheLastValue()
	{
		const Targets targets(2);
		AutomationRenderScratch scratch(AutomationRenderScratch::RenderCapacity);
		scratch.clear();

		QVERIFY(scratch.set(targets.address(1), 1.0f));
		QVERIFY(scratch.set(targets.address(0), 2.0f));
		QVERIFY(scratch.set(targets.address(1), 3.0f));
		QVERIFY(std::is_sorted(scratch.entries().begin(), scratch.entries().end(), entriesByAddress));
		QCOMPARE(scratch.size(), static_cast<std::size_t>(2));
		QCOMPARE(*scratch.find(targets.address(1)), 3.0f);
		QCOMPARE(*scratch.find(targets.address(0)), 2.0f);
	}

	//! The audio thread's read still allocates nothing and frees nothing, sorting included.
	void theBoundedReadAllocatesNothingOnTheAudioThread()
	{
		constexpr int Count = 512;
		const PositionedClips clips(Count);
		const Targets targets(Count);
		AutomationRenderScratch scratch(AutomationRenderScratch::RenderCapacity);
		scratch.clear();
		int refused = 0;

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		{
			const RealtimeScope realtime;
			scratch.clear();
			clips.fillReversed(scratch);              // out of start order: the clip list sorts inside the scope
			refused += targets.fillReversed(scratch);  // out of address order: the value list sorts here too
			QCOMPARE(static_cast<int>(scratch.clips().size()), Count);
			QCOMPARE(static_cast<int>(scratch.entries().size()), Count);
		}
		test::tlCountAllocations = false;
		QCOMPARE(refused, 0);
		QCOMPARE(test::tlAllocationCount, 0ull);
		QCOMPARE(test::tlFreeCount, 0ull);
	}
};

QTEST_GUILESS_MAIN(AutomationScratchWorkTest)
#include "AutomationScratchWorkTest.moc"
