/*
 * TakeLaneCompPlaybackTest.cpp - R3.1: the composite PLAYS, and a lane can be auditioned
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

/*! Take lanes were comped into a composite that NOTHING played (include/TakeLane.h said so:
 *  "no playback path consumes the composite yet"). Now each take's play handle snapshots the
 *  spans the composite selects for its lane and sounds only there. Two takes of one passage -
 *  a constant 0.5 on lane 0, 0.25 on lane 1 - sit on top of each other; the composite takes
 *  lane 0 for the first half and lane 1 for the second, and each take is rendered through the
 *  real handle:
 *    * lane 0 sounds in the first half and is SILENT in the second, lane 1 the reverse, and
 *      the two sum to one continuous performance;
 *    * each switch is a short ramp, not a step (no click);
 *    * auditioning lane 1 plays it WHOLE and silences lane 0, and stopping the audition
 *      plays the comp again;
 *    * a track with no composite renders exactly as it did (no gate at all).
 */

#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "../core/AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiPort.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "RealtimeScope.h"
#include "Song.h"
#include "TakeLane.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

SampleClip* makeTake(SampleTrack& track, int rate, float level, int lane)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	std::vector<SampleFrame> data(static_cast<std::size_t>(2 * rate), SampleFrame(level, level));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	clip->setLaneIndex(lane);
	return clip;
}

//! A take whose first second is 0.5 and second second is 0.25: where a span reads from shows in its level.
SampleClip* makeStepTake(SampleTrack& track, int rate)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	std::vector<SampleFrame> data(static_cast<std::size_t>(2 * rate), SampleFrame(0.5f, 0.5f));
	std::fill(data.begin() + rate, data.end(), SampleFrame(0.25f, 0.25f));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

std::vector<SampleFrame> render(SampleClip* clip)
{
	SamplePlayHandle handle(clip, clip->sampleWindow());
	const auto total = static_cast<int>(handle.totalFrames());
	std::vector<SampleFrame> out;
	std::vector<SampleFrame> buffer(512);
	for (int guard = 0; static_cast<int>(handle.framesDone()) < total && guard < total / 512 + 16; ++guard)
	{
		std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
		handle.play(std::span<SampleFrame>(buffer));
		out.insert(out.end(), buffer.begin(), buffer.end());
	}
	out.resize(static_cast<std::size_t>(total));
	return out;
}

double meanLevel(const std::vector<SampleFrame>& data, std::size_t from, std::size_t to)
{
	double sum = 0.0;
	for (std::size_t i = from; i < to; ++i) { sum += std::fabs(data[i][0]); }
	return to > from ? sum / static_cast<double>(to - from) : 0.0;
}

//! The keys of @a track's note play handles alive now.
QList<int> noteKeys(const InstrumentTrack* track)
{
	QList<int> keys;
	for (PlayHandle* handle : Engine::audioEngine()->playHandles())
	{
		auto* note = dynamic_cast<NotePlayHandle*>(handle);
		if (note != nullptr && note->instrumentTrack() == track) { keys << note->key(); }
	}
	std::sort(keys.begin(), keys.end());
	return keys;
}
//! Play the arrangement from @a tick; the keys the track has sounding then.
QList<int> playFrom(InstrumentTrack* track, int tick)
{
	Song* song = Engine::getSong();
	song->setPlayPos(tick, Song::PlayMode::Song);
	song->playSong();
	for (int period = 0; period < 3; ++period) { Engine::audioEngine()->renderNextPeriod(); }
	const QList<int> keys = noteKeys(track);
	song->stop();
	for (int period = 0; period < 64 && !noteKeys(track).isEmpty(); ++period) { Engine::audioEngine()->renderNextPeriod(); }
	return keys;
}
} // namespace

class TakeLaneCompPlaybackTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		m_rate = Engine::audioEngine()->outputSampleRate();
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase() { Engine::destroy(); }

	void theCompositePlaysEachLaneOnlyWhereItIsSelected()
	{
		SampleTrack track(Engine::getSong());
		SampleClip* first = makeTake(track, m_rate, 0.5f, 0);
		SampleClip* second = makeTake(track, m_rate, 0.25f, 1);
		TakeLaneModel& comp = track.takeLanes();
		comp.addLane(QStringLiteral("take 1"));
		comp.addLane(QStringLiteral("take 2"));
		const int half = first->length().getTicks() / 2;
		QVERIFY(comp.selectSegment(0, half, 0));
		QVERIFY(comp.selectSegment(half, first->length().getTicks(), 1));

		const std::vector<SampleFrame> a = render(first);
		const std::vector<SampleFrame> b = render(second);
		QCOMPARE(a.size(), b.size());
		const std::size_t mid = static_cast<std::size_t>(std::llround(half * Engine::framesPerTick(m_rate)));
		const std::size_t guard = 256; // clear of the ramps
		QVERIFY(mid > guard && mid + guard < a.size());
		std::printf("COMP_EVIDENCE lane0 first/second half %.4f / %.4f, lane1 %.4f / %.4f\n",
			meanLevel(a, guard, mid - guard), meanLevel(a, mid + guard, a.size() - guard),
			meanLevel(b, guard, mid - guard), meanLevel(b, mid + guard, a.size() - guard));
		QVERIFY(std::fabs(meanLevel(a, guard, mid - guard) - 0.5) < 1e-6);
		QCOMPARE(meanLevel(a, mid + guard, a.size() - guard), 0.0);
		QCOMPARE(meanLevel(b, guard, mid - guard), 0.0);
		QVERIFY(std::fabs(meanLevel(b, mid + guard, a.size() - guard) - 0.25) < 1e-6);

		// The switch is a ramp: no frame jumps by the full difference in one step.
		float largestStep = 0.0f;
		for (std::size_t i = mid - guard; i < mid + guard; ++i)
		{
			const float here = a[i][0] + b[i][0];
			const float next = a[i + 1][0] + b[i + 1][0];
			largestStep = std::max(largestStep, std::fabs(next - here));
		}
		QVERIFY2(largestStep < 0.05f, qPrintable(QStringLiteral("the lane switch steps by %1").arg(largestStep)));
	}

	void auditioningALanePlaysItWholeAndSilencesTheOthers()
	{
		SampleTrack track(Engine::getSong());
		SampleClip* first = makeTake(track, m_rate, 0.5f, 0);
		SampleClip* second = makeTake(track, m_rate, 0.25f, 1);
		TakeLaneModel& comp = track.takeLanes();
		comp.addLane(); comp.addLane();
		QVERIFY(comp.selectSegment(0, first->length().getTicks(), 0));

		comp.setAuditionLane(1);
		const std::vector<SampleFrame> a = render(first);
		const std::vector<SampleFrame> b = render(second);
		QCOMPARE(meanLevel(a, 0, a.size()), 0.0);
		QVERIFY(meanLevel(b, 256, b.size() - 256) > 0.2499);

		comp.setAuditionLane(-1);
		QVERIFY2(meanLevel(render(first), 256, a.size() - 256) > 0.4999, "stopping the audition did not bring the comp back");
	}

	//! No composite, no gate: the same clip renders byte-for-byte alike on a track with no
	//! lanes and on one whose lanes exist but select nothing (and nothing is auditioned).
	void aTrackWithNoCompositeRendersUnchanged()
	{
		SampleTrack plainTrack(Engine::getSong());
		const std::vector<SampleFrame> plain = render(makeTake(plainTrack, m_rate, 0.5f, 0));
		SampleTrack lanedTrack(Engine::getSong());
		SampleClip* laned = makeTake(lanedTrack, m_rate, 0.5f, 0);
		lanedTrack.takeLanes().addLane();
		const std::vector<SampleFrame> withLanes = render(laned);
		QCOMPARE(withLanes.size(), plain.size());
		for (std::size_t i = 0; i < plain.size(); ++i)
		{
			QVERIFY(withLanes[i][0] == plain[i][0] && withLanes[i][1] == plain[i][1]);
		}
		QVERIFY(std::fabs(meanLevel(plain, 256, plain.size() / 2) - 0.5) < 1e-6);
	}

	//! A clip assigned to group zero obeys the active-take gate; ordinary clips remain ungated.
	void aZeroIdTakeGroupIsDistinctFromAnOrdinaryClip()
	{
		SampleTrack track(Engine::getSong());
		SampleClip* zeroGroup = makeTake(track, m_rate, 0.5f, 0);
		zeroGroup->setTakeGroupId(0);
		zeroGroup->publishActiveTakeLane(1);
		SampleClip* ordinary = makeTake(track, m_rate, 0.5f, 0);
		ordinary->publishActiveTakeLane(1);
		SamplePlayHandle zeroGroupHandle(zeroGroup, zeroGroup->sampleWindow());
		SamplePlayHandle ordinaryHandle(ordinary, ordinary->sampleWindow());
		std::array<SampleFrame, 512> buffer{};

		const auto renderLevel = [&buffer](SamplePlayHandle& handle) {
			std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
			test::resetAllocationCount();
			test::tlCountAllocations = true;
			{
				const RealtimeScope realtime;
				handle.play(std::span<SampleFrame>(buffer));
			}
			test::tlCountAllocations = false;
			double sum = 0.0;
			for (const SampleFrame& frame : buffer) { sum += std::fabs(frame[0]); }
			return std::pair{test::tlAllocationCount, sum / static_cast<double>(buffer.size())};
		};

		const auto [takeAllocations, takeLevel] = renderLevel(zeroGroupHandle);
		QCOMPARE(takeAllocations, std::uint64_t{0});
		QCOMPARE(takeLevel, 0.0);
		const auto [ordinaryAllocations, ordinaryLevel] = renderLevel(ordinaryHandle);
		QCOMPARE(ordinaryAllocations, std::uint64_t{0});
		QVERIFY(std::fabs(ordinaryLevel - 0.5) < 1e-6);
	}

	//! Existing play handles follow the atomic active-take mirror without allocation or a lock.
	void activeTakeRenderGateIsLockFreeAndAllocationFree()
	{
		SampleTrack track(Engine::getSong());
		SampleClip* first = makeTake(track, m_rate, 0.5f, 0);
		SampleClip* second = makeTake(track, m_rate, 0.25f, 1);
		TakeLaneModel& model = track.takeLanes();
		model.addLane();
		model.addLane();
		const int groupId = first->id();
		first->setTakeGroupId(groupId);
		second->setTakeGroupId(groupId);
		QVERIFY(model.recordTake(groupId, 0, track.getClips()));
		QVERIFY(model.recordTake(groupId, 1, track.getClips()));
		SamplePlayHandle firstHandle(first, first->sampleWindow());
		SamplePlayHandle secondHandle(second, second->sampleWindow());
		std::array<SampleFrame, 512> buffer{};

		const auto playRealtime = [&buffer](SamplePlayHandle& handle) {
			std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
			test::resetAllocationCount();
			test::tlCountAllocations = true;
			{
				const RealtimeScope realtime;
				handle.play(std::span<SampleFrame>(buffer));
			}
			test::tlCountAllocations = false;
			return test::tlAllocationCount;
		};
		const auto renderedLevel = [&buffer] {
		double sum = 0.0;
		for (const SampleFrame& frame : buffer) { sum += std::fabs(frame[0]); }
		return sum / static_cast<double>(buffer.size());
		};
		QCOMPARE(playRealtime(firstHandle), std::uint64_t{0});
		QCOMPARE(renderedLevel(), 0.0);

		QVERIFY(model.selectTake(groupId, 0, track.getClips()));
		QCOMPARE(playRealtime(firstHandle), std::uint64_t{0});	// this handle predates the choice
		QVERIFY(renderedLevel() > 0.49);
		QCOMPARE(playRealtime(secondHandle), std::uint64_t{0});
		QCOMPARE(renderedLevel(), 0.0);
	}

	//! 0.5.0 E2: a slipped segment READS the take slipped. One take, its first half at 0.5 and its second at 0.25,
	//! comped in quarters Q: [0, Q) at its own place, then [Q, 4Q) slipped by 2Q - one segment, one slip. So
	//! [Q, 2Q) reads the take's [3Q, 4Q) (0.25 - unslipped it would be 0.5), and [2Q, 4Q) reads past the take's end
	//! (silence - unslipped it would be 0.25).
	void aSlippedSegmentReadsTheTakeSlipped()
	{
		SampleTrack track(Engine::getSong());
		SampleClip* take = makeStepTake(track, m_rate);
		TakeLaneModel& comp = track.takeLanes();
		comp.addLane();
		const int quarter = take->length().getTicks() / 4;
		QVERIFY(comp.selectSegment(0, 4 * quarter, 0));
		QVERIFY(comp.selectSegment(quarter, 4 * quarter, 0, 2 * quarter));
		QCOMPARE(comp.segments().size(), std::size_t{2});

		const std::vector<SampleFrame> out = render(take);
		const auto at = [this](int ticks) { return static_cast<std::size_t>(std::llround(ticks * Engine::framesPerTick(m_rate))); };
		const std::size_t guard = 512;  // clear of the comp ramps and the resampler's edges
		QVERIFY(at(4 * quarter) <= out.size());
		std::printf("SLIP_EVIDENCE [0,Q) %.4f  [Q,2Q) %.4f  [2Q+,4Q) %.4f\n", meanLevel(out, guard, at(quarter) - guard),
			meanLevel(out, at(quarter) + guard, at(2 * quarter) - guard), meanLevel(out, at(3 * quarter) - at(quarter) / 2, at(4 * quarter) - guard));
		QVERIFY(std::fabs(meanLevel(out, guard, at(quarter) - guard) - 0.5) < 1e-4);
		QVERIFY(std::fabs(meanLevel(out, at(quarter) + guard, at(2 * quarter) - guard) - 0.25) < 1e-4);
		QVERIFY(meanLevel(out, at(3 * quarter) - at(quarter) / 2, at(4 * quarter) - guard) < 1e-4);

		// The same comp with no slip reads the take at its own place: the slip is the whole difference.
		QVERIFY(comp.selectSegment(quarter, 4 * quarter, 0, 0));
		const std::vector<SampleFrame> plain = render(take);
		QVERIFY(std::fabs(meanLevel(plain, at(quarter) + guard, at(2 * quarter) - guard) - 0.5) < 1e-4);
	}

	//! Luna cluster 3511: the composite may select more spans of one lane than
	//! SamplePlayHandle::MaxCompSpans could store, and the surplus was dropped SILENTLY -
	//! the tail of a heavily comped take just went missing. 40 lane-0 selections
	//! alternating with lane 1: the first and the LAST of lane 0's spans must both sound.
	void everySelectedSpanSoundsEvenPastTheOldThirtySecondCap()
	{
		SampleTrack track(Engine::getSong());
		SampleClip* take = makeTake(track, m_rate, 0.5f, 0);
		makeTake(track, m_rate, 0.25f, 1);
		TakeLaneModel& comp = track.takeLanes();
		comp.addLane(); comp.addLane();
		const int part = take->length().getTicks() / 160;
		QVERIFY(part > 0);
		for (int i = 0; i < 80; ++i)
		{
			QVERIFY(comp.selectSegment((2 * i) * part, (2 * i + 1) * part, i % 2));
		}
		// 40 disjoint lane-0 spans, none of them contiguous with the next: the model keeps them apart.
		QCOMPARE(comp.segments().size(), std::size_t{80});

		const std::vector<SampleFrame> out = render(take);
		const auto at = [this](int ticks) { return static_cast<std::size_t>(std::llround(ticks * Engine::framesPerTick(m_rate))); };
		const std::size_t guard = 96;  // clear of the 64-frame comp ramp at both span edges
		QVERIFY(at(part) > 2 * guard + 16);
		//! The level in the middle of the part numbered k, past both of its ramps.
		const auto levelIn = [&](int k)
		{ return meanLevel(out, at(k * part) + guard, at((k + 1) * part) - guard); };
		std::printf("SPAN_EVIDENCE out=%zu span32(=%d)=%.4f span39(=%d)=%.4f lane1(=%d)=%.4f\n",
			out.size(), 4 * 31, levelIn(4 * 31), 4 * 39, levelIn(4 * 39), 4 * 39 + 1, levelIn(4 * 39 + 1));
		// The 32nd span is the last one the old cap could store, the 40th is the one it dropped.
		QVERIFY2(std::fabs(levelIn(4 * 31) - 0.5) < 1e-4, "the 32nd selected span must sound");
		QVERIFY2(std::fabs(levelIn(4 * 39) - 0.5) < 1e-4,
			"the 40th selected span was dropped: the cap silently ignored it");
		// The lane-1 part beside it stays silent: the fix adds no leakage.
		QCOMPARE(levelIn(4 * 39 + 1), 0.0);
	}

	//! Comp authoring must refuse a lane span the fixed render snapshot cannot hold.
	void refusesSelectionsBeyondThePlaybackSpanBudget()
	{
		SampleTrack track(Engine::getSong());
		TakeLaneModel& comp = track.takeLanes();
		comp.addLane(); comp.addLane();
		for (int i = 0; i < 256; ++i)
		{
			QVERIFY(comp.selectSegment(2 * i, 2 * i + 1, 0));
		}

		QVERIFY2(!comp.canSelect(512, 513, 0), "the 257th span cannot be rendered by a play handle");
		QVERIFY(!comp.selectSegment(512, 513, 0));
		QCOMPARE(comp.segments().size(), std::size_t{256});
	}

	//! Rebuild and lane removal are authoring paths too: a result over the budget is refused and the composite is kept.
	void refusesRebuildAndLaneRemovalBeyondThePlaybackSpanBudget()
	{
		SampleTrack rebuilt(Engine::getSong());
		TakeLaneModel& rebuildComp = rebuilt.takeLanes();
		rebuildComp.addLane();
		rebuildComp.addLane();
		for (int i = 0; i < 256; ++i)
		{
			QVERIFY(rebuildComp.selectSegment(2 * i + 1, 2 * i + 2, 1));
		}
		// Gap-filling a [0, 513) span puts 257 base-lane spans around the 256 alternate spans.
		QCOMPARE(rebuildComp.rebuild(0, 513), -1);
		QCOMPARE(rebuildComp.segments().size(), std::size_t{256});

		SampleTrack removed(Engine::getSong());
		TakeLaneModel& removeComp = removed.takeLanes();
		removeComp.addLane();
		removeComp.addLane();
		QVERIFY(removeComp.selectSegment(0, 1, 0));
		for (int i = 0; i < 256; ++i)
		{
			QVERIFY(removeComp.selectSegment(2 * i + 2, 2 * i + 3, 1));
		}
		// Folding the alternate lane into the base leaves 257 separate base spans.
		QVERIFY(!removeComp.removeLane(1));
		QVERIFY(removeComp.hasLane(1));
		QCOMPARE(removeComp.segments().size(), std::size_t{257});
	}

	//! R3.3: a MIDI take is comped by note - the gate InstrumentTrack::play asks per note.
	void midiNotesPlayWhereTheirLaneIsSelected()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* first = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		auto* second = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(first != nullptr && second != nullptr);
		second->setLaneIndex(1);
		QVERIFY2(track->compPlays(first, 120) && track->compPlays(second, 120),
			"with no composite every note must play");
		TakeLaneModel& comp = track->takeLanes();
		comp.addLane(); comp.addLane();
		QVERIFY(comp.selectSegment(0, 96, 0));
		QVERIFY(comp.selectSegment(96, 192, 1));
		QVERIFY(track->compPlays(first, 50));
		QVERIFY(!track->compPlays(first, 120));
		QVERIFY(!track->compPlays(second, 50));
		QVERIFY(track->compPlays(second, 120));
		comp.setAuditionLane(1);
		QVERIFY(!track->compPlays(first, 50));
		QVERIFY(track->compPlays(second, 50));
		comp.setAuditionLane(-1);
		delete track;
	}

	//! Cluster new-p16-89: the arrangement's clip collection must not grow on the audio thread.
	void clipCollectionDoesNotAllocateOnTheAudioThread()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setAutoResize(false); clip->changeLength(TimePos(384));
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		{
			const RealtimeScope realtime;
			track->play(TimePos(0), frames, 0, -1);
		}
		test::tlCountAllocations = false;
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
		delete track;
	}

	//! Cluster 4172: a controller lane of a take the composite does not select never reaches the instrument.
	void controllerLanesFollowTheComposite()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setAutoResize(false); clip->changeLength(TimePos(384));
		clip->setLaneIndex(1);
		clip->setControllerPoints(ControllerKind::ControlChange, 7, {{0, 64}});
		TakeLaneModel& comp = track->takeLanes();
		comp.addLane(); comp.addLane();
		QVERIFY(comp.selectSegment(0, 384, 0));
		MidiPort* port = track->midiPort();
		port->setOutputChannel(6);
		port->setMode(MidiPort::Mode::Output);
		const quint64 written = port->outputEventsWritten();
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		QVERIFY(!track->play(TimePos(0), frames, 0, -1));
		QCOMPARE(port->outputEventsWritten(), written);
		// Auditioning the take's own lane must still play its controllers (the gate is the comp, not the feature).
		comp.setAuditionLane(1);
		track->play(TimePos(1), frames, 0, -1);
		QCOMPARE(port->outputEventsWritten(), written + 1);
		comp.setAuditionLane(-1);
		delete track;
	}

	//! Cluster 5080: the comp lookup follows the loop pass playing, not the note's first-pass position.
	void loopedClipCompReadsThePassPlaying()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		QVERIFY(track->loadInstrument(QStringLiteral("tripleoscillator")) != nullptr);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setAutoResize(false); clip->changeLength(TimePos(384));
		QVERIFY(clip->addNote(Note(TimePos(24), TimePos(48), 60), false) != nullptr);
		TakeLaneModel& comp = track->takeLanes();
		comp.addLane(); comp.addLane();
		QVERIFY(comp.selectSegment(0, 192, 0));
		QVERIFY(comp.selectSegment(192, 384, 1));
		ClipEdits edits = clip->clipEdits();
		edits.loopTicks = 96;
		clip->setClipEdits(edits);
		QCOMPARE(playFrom(track, 144), (QList<int>{60}));  // second pass: song tick 144 is still lane 0
		QVERIFY2(playFrom(track, 240).isEmpty(),  // third pass: 240 is lane 1, so the lane-0 note must not play
			"the comp was read at the note's first-pass position (48) instead of the pass playing (240)");
		delete track;
	}

private:
	int m_rate = 44100;
};

QTEST_GUILESS_MAIN(TakeLaneCompPlaybackTest)
#include "TakeLaneCompPlaybackTest.moc"
