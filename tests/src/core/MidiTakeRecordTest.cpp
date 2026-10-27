/*
 * MidiTakeRecordTest.cpp - Q050-054.1: MIDI played into armed instrument tracks while
 *                          transport.record runs becomes arrangement clips and loop takes
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

/*! The engine is driven period by period (AudioEngine::renderNextPeriod with the device stopped), so every
 *  position below is the transport's own, and the MIDI enters through InstrumentTrack::processInEvent - the
 *  entry point a controller's events reach - between periods. Everything an agent would do goes through the
 *  control registry: track.set_midi_arm, transport.record, transport.stop, control.undo / control.redo,
 *  comp.take_select, comp.get_state and note.list. */

#include <QtTest>

#include <algorithm>
#include <thread>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "AllocationProbe.h"

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiEvent.h"
#include "MidiTakeCapture.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "RecordOptions.h"
#include "Song.h"
#include "TakeLane.h"
#include "Timeline.h"

using namespace lmms;

namespace
{

ControlResult run(const char* id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(QLatin1String(id), args);
}

Song* song() { return Engine::getSong(); }

tick_t songTick() { return song()->getPlayPos(Song::PlayMode::Song).getTicks(); }

//! Render periods until the transport reaches @a tick (or has wrapped past the loop, @a wraps more times).
void renderUntil(tick_t tick, std::uint64_t wraps = 0)
{
	const std::uint64_t target = song()->loopWraps() + wraps;
	for (int guard = 0; guard < 200000; ++guard)
	{
		if (song()->loopWraps() >= target && songTick() >= tick) { return; }
		Engine::audioEngine()->renderNextPeriod();
	}
	QFAIL("the transport never reached the tick");
}

void play(InstrumentTrack* track, int key, int velocity)
{
	track->processInEvent(MidiEvent(MidiNoteOn, 0, key, velocity));
}

void release(InstrumentTrack* track, int key)
{
	track->processInEvent(MidiEvent(MidiNoteOff, 0, key, 0));
}

std::vector<MidiClip*> midiClips(Track* track)
{
	std::vector<MidiClip*> clips;
	for (Clip* clip : track->getClips())
	{
		if (auto* midi = dynamic_cast<MidiClip*>(clip)) { clips.push_back(midi); }
	}
	return clips;
}

//! Within one tick: positions are stamped per audio period, a fraction of a tick.
bool near(tick_t a, tick_t b) { return std::abs(a - b) <= 1; }

} // namespace


class MidiTakeRecordTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	QString m_trackId;

	InstrumentTrack* addTrack(QString* id)
	{
		const ControlResult added = run("track.add", {{QStringLiteral("type"), QStringLiteral("instrument")}});
		if (!added.ok) { return nullptr; }
		*id = added.result.value(QStringLiteral("track")).toString();
		ControlResult error;
		auto* track = dynamic_cast<InstrumentTrack*>(control::resolveTrack(*id, &error));
		// 127 makes the volume arithmetic exact: velocity v plays at v * 100 / 127.
		if (track != nullptr) { track->midiPort()->setBaseVelocity(127); }
		return track;
	}

	void stopAt(tick_t tick)
	{
		auto& timeline = song()->getTimeline(Song::PlayMode::Song);
		timeline.setLoopEnabled(false);
		timeline.setStopBehaviour(Timeline::StopBehaviour::KeepPosition);
		song()->setPlayPos(tick, Song::PlayMode::Song);
	}

	//! A loop recording of the one-bar loop [0, 192): one note (its own key) in each listed pass.
	QJsonObject recordLoopPasses(const std::vector<int>& keys)
	{
		auto& timeline = song()->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(TimePos(0), TimePos(192));
		timeline.setLoopEnabled(true);
		song()->setPlayPos(0, Song::PlayMode::Song);
		const ControlResult started = run("transport.record");
		if (!started.ok) { return {}; }
		for (std::size_t pass = 0; pass < keys.size(); ++pass)
		{
			renderUntil(24, pass == 0 ? 0 : 1);
			play(m_track, keys[pass], 100);
			renderUntil(72);
			release(m_track, keys[pass]);
		}
		renderUntil(120);
		run("transport.stop");
		timeline.setLoopEnabled(false);
		return run("record.midi_capture_status").result.value(QStringLiteral("last_commit")).toObject();
	}

private slots:
	void initTestCase()
	{
		// main() does this before the engine starts: a live note-on needs the handle pool.
		NotePlayHandleManager::init();
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
		ControlRegistry::setReady(true);
		// The test renders each period itself.
		if (auto* device = Engine::audioEngine()->audioDev()) { device->stopProcessing(); }
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
	}

	void init()
	{
		m_track = addTrack(&m_trackId);
		QVERIFY(m_track != nullptr);
		stopAt(0);
	}

	// ------------------------------------------------------------------ the realtime half

	//! The capture queue is bounded, counts what it drops, and its producer side allocates nothing.
	void theCaptureQueueIsBoundedAndAllocationFree()
	{
		MidiTakeCapture capture;
		const MidiEvent on(MidiNoteOn, 0, 60, 100);
		capture.capture(on);  // not armed, no session: nothing is queued
		capture.arm(true);
		QVERIFY(capture.startCapture());
		constexpr std::size_t extra = 10;
		lmms::test::resetAllocationCount();
		lmms::test::tlCountAllocations = true;
		for (std::size_t i = 0; i < MidiTakeCapture::CapacityEvents + extra; ++i) { capture.capture(on); }
		lmms::test::tlCountAllocations = false;
		QCOMPARE(lmms::test::tlAllocationCount, std::uint64_t{0});
		QCOMPARE(lmms::test::tlFreeCount, std::uint64_t{0});
		QCOMPARE(capture.dropped(), std::uint64_t{extra});
		std::vector<MidiTakeEvent> drained;
		QCOMPARE(capture.drain(&drained), MidiTakeCapture::CapacityEvents);
		capture.stopCapture();
		capture.capture(on);
		QCOMPARE(capture.drain(&drained), std::size_t{0});
	}

	//! Two input threads at once (a controller and the on-screen keyboard): nothing is lost, each
	//! thread's events stay in its own order.
	void twoProducersLoseNothing()
	{
		MidiTakeCapture capture;
		capture.arm(true);
		QVERIFY(capture.startCapture());
		constexpr int each = 1500;
		std::vector<MidiTakeEvent> drained;
		auto producer = [&capture](int key) {
			for (int i = 0; i < each; ++i) { capture.capture(MidiEvent(MidiNoteOn, 0, key, 1 + i % 127)); }
		};
		std::thread a(producer, 10);
		std::thread b(producer, 20);
		while (drained.size() < 2 * each) { capture.drain(&drained); }
		a.join();
		b.join();
		capture.drain(&drained);
		QCOMPARE(drained.size(), std::size_t{2 * each});
		QCOMPARE(capture.dropped(), std::uint64_t{0});
		int nextA = 0, nextB = 0;
		for (const MidiTakeEvent& event : drained)
		{
			int& next = event.key == 10 ? nextA : nextB;
			QCOMPARE(int{event.velocity}, 1 + next % 127);
			++next;
		}
	}

	// ------------------------------------------------------------------ a plain pass

	//! Armed, recorded alongside, stopped: one MIDI clip with the notes as they were played.
	void aPlainPassBecomesAClipWithThePlayedNotes()
	{
		QString otherId;
		InstrumentTrack* unarmed = addTrack(&otherId);
		QVERIFY(unarmed != nullptr);
		const ControlResult armed = run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), true}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QCOMPARE(armed.result.value(QStringLiteral("changed")).toBool(), true);

		const ControlResult started = run("transport.record");
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QCOMPARE(started.result.value(QStringLiteral("midi_tracks")).toArray(), QJsonArray{m_trackId});
		renderUntil(200);
		play(m_track, 60, 127);
		play(unarmed, 61, 127);
		renderUntil(224);
		release(m_track, 60);
		release(unarmed, 61);
		play(m_track, 64, 64);
		renderUntil(260);
		release(m_track, 64);
		renderUntil(270);
		QVERIFY(midiClips(m_track).empty());  // nothing lands while the pass runs
		QVERIFY(run("transport.stop").ok);

		QCOMPARE(int(midiClips(unarmed).size()), 0);
		const std::vector<MidiClip*> clips = midiClips(m_track);
		QCOMPARE(int(clips.size()), 1);
		MidiClip* clip = clips.front();
		QCOMPARE(clip->startPosition().getTicks(), 192);  // from the bar of the first note
		QVERIFY(!clip->hasTakeGroup());
		QCOMPARE(int(clip->notes().size()), 2);
		const Note* first = clip->notes()[0];
		const Note* second = clip->notes()[1];
		QCOMPARE(first->key(), 60);
		QVERIFY2(near(192 + first->pos().getTicks(), 200), qPrintable(QString::number(first->pos().getTicks())));
		QVERIFY2(near(first->length().getTicks(), 24), qPrintable(QString::number(first->length().getTicks())));
		QCOMPARE(int(first->getVolume()), 100);  // 127 * 100 / 127
		QCOMPARE(second->key(), 64);
		QVERIFY(near(192 + second->pos().getTicks(), 224));
		QVERIFY(near(second->length().getTicks(), 36));
		QCOMPARE(int(second->getVolume()), 64 * 100 / 127);

		// An agent reads the take back: the commit's report names the clip, note.list answers its notes.
		const QJsonObject status = run("record.midi_capture_status").result;
		const QJsonArray takes = status.value(QStringLiteral("last_commit")).toObject().value(QStringLiteral("takes")).toArray();
		QCOMPARE(takes.size(), 1);
		const QString clipId = takes[0].toObject().value(QStringLiteral("clip")).toString();
		QCOMPARE(clipId, control::clipIdOf(clip));
		const QJsonArray listed = run("note.list", {{QStringLiteral("clip"), clipId}}).result.value(QStringLiteral("notes")).toArray();
		QCOMPARE(listed.size(), 2);
		QCOMPARE(listed[0].toObject().value(QStringLiteral("key")).toInt(), 60);
		QCOMPARE(ControlRegistry::instance()->lastTransaction()->command, QStringLiteral("record.midi_capture_commit"));
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), false}});
	}

	//! Playing, not recording - or recording, not armed - leaves the arrangement alone.
	void nothingIsRecordedUnlessArmedAndRecording()
	{
		QVERIFY(run("transport.play").ok);
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), true}});
		renderUntil(10);
		play(m_track, 60, 100);
		renderUntil(20);
		release(m_track, 60);
		run("transport.stop");
		QVERIFY(midiClips(m_track).empty());
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), false}});
		stopAt(0);
		QVERIFY(run("transport.record").ok);
		renderUntil(10);
		play(m_track, 60, 100);
		renderUntil(20);
		release(m_track, 60);
		run("transport.stop");
		QVERIFY(midiClips(m_track).empty());
		QCOMPARE(run("record.midi_capture_commit").errorKind, ControlErrorKind::NotFound);
	}

	//! One control.undo removes the recorded take, one control.redo brings it back.
	void oneUndoStepCoversTheTake()
	{
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), true}});
		QVERIFY(run("transport.record").ok);
		renderUntil(10);
		play(m_track, 67, 100);
		renderUntil(40);
		release(m_track, 67);
		run("transport.stop");
		QCOMPARE(int(midiClips(m_track).size()), 1);

		const ControlResult undone = run("control.undo");
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(undone.result.value(QStringLiteral("undone_command")).toString(), QStringLiteral("record.midi_capture_commit"));
		QVERIFY(midiClips(m_track).empty());
		QVERIFY(run("control.redo").ok);
		const std::vector<MidiClip*> back = midiClips(m_track);
		QCOMPARE(int(back.size()), 1);
		QCOMPARE(int(back.front()->notes().size()), 1);
		QCOMPARE(back.front()->notes()[0]->key(), 67);
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), false}});
	}

	// ------------------------------------------------------------------ loop passes

	//! Each loop pass becomes a take clip over the loop on its own lane, one group, the newest active;
	//! a note held across the wrap ends at the loop end; only the active take plays.
	void loopPassesBecomeTakesNewestActive()
	{
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), true}});
		const QJsonObject commit = recordLoopPasses({60, 62, 64});
		QCOMPARE(commit.value(QStringLiteral("loop_recording")).toBool(), true);
		const std::vector<MidiClip*> takes = midiClips(m_track);
		QCOMPARE(int(takes.size()), 3);
		const int group = takes.front()->takeGroupId();
		std::vector<int> lanes;
		for (std::size_t i = 0; i < takes.size(); ++i)
		{
			MidiClip* take = takes[i];
			QVERIFY(take->hasTakeGroup());
			QCOMPARE(take->takeGroupId(), group);
			QCOMPARE(take->startPosition().getTicks(), 0);
			QCOMPARE(take->length().getTicks(), 192);
			QCOMPARE(int(take->notes().size()), 1);
			QCOMPARE(take->notes()[0]->key(), 60 + 2 * int(i));
			QVERIFY(near(take->notes()[0]->pos().getTicks(), 24));
			lanes.push_back(take->laneIndex());
		}
		QCOMPARE(int(std::unique(lanes.begin(), lanes.end()) - lanes.begin()), 3);  // three lanes
		const TakeLaneModel& model = m_track->takeLanes();
		QCOMPARE(model.activeTakeLane(group), takes.back()->laneIndex());
		QVERIFY(!model.userChoseTake(group));
		QVERIFY(!m_track->takePlays(takes[0]) && !m_track->takePlays(takes[1]) && m_track->takePlays(takes[2]));

		const QJsonObject comp = run("comp.get_state", {{QStringLiteral("track"), m_trackId}}).result;
		const QJsonObject reported = comp.value(QStringLiteral("take_groups")).toArray().at(0).toObject();
		QCOMPARE(reported.value(QStringLiteral("active_lane")).toInt(), takes.back()->laneIndex());
		QCOMPARE(reported.value(QStringLiteral("takes")).toArray().size(), 3);

		// The whole loop recording is one undo step.
		QVERIFY(run("control.undo").ok);
		QVERIFY(midiClips(m_track).empty());
		QVERIFY(run("control.redo").ok);
		QCOMPARE(int(midiClips(m_track).size()), 3);
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), false}});
	}

	//! The active-take model: a take the musician chose stays active through a later loop recording
	//! over the same region, whose passes join the same group.
	void aChosenTakeSurvivesALaterLoopRecording()
	{
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), true}});
		recordLoopPasses({60, 62});
		std::vector<MidiClip*> takes = midiClips(m_track);
		QCOMPARE(int(takes.size()), 2);
		const int group = takes.front()->takeGroupId();
		const int chosen = takes.front()->laneIndex();
		QVERIFY(run("comp.take_select", {{QStringLiteral("clip"), control::clipIdOf(takes.front())},
			{QStringLiteral("lane"), chosen}}).ok);
		stopAt(0);
		recordLoopPasses({65, 67});
		takes = midiClips(m_track);
		QCOMPARE(int(takes.size()), 4);
		for (MidiClip* take : takes) { QCOMPARE(take->takeGroupId(), group); }
		QCOMPARE(m_track->takeLanes().activeTakeLane(group), chosen);
		QVERIFY(m_track->takePlays(takes.front()));
		QVERIFY(!m_track->takePlays(takes.back()));
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), false}});
	}

	//! The takes, their lanes, their group and its active take survive a save and a reload.
	void takesRoundTripThroughSaveAndLoad()
	{
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), true}});
		recordLoopPasses({60, 62});
		const QString name = m_track->name();
		m_track->setName(QStringLiteral("midi-take-roundtrip"));
		QTemporaryDir dir;
		const QString path = dir.filePath(QStringLiteral("takes.mmp"));
		QVERIFY(song()->saveProjectFile(path));
		song()->loadProject(path);
		InstrumentTrack* loaded = nullptr;
		for (Track* track : song()->tracks())
		{
			if (track->name() == QLatin1String("midi-take-roundtrip")) { loaded = dynamic_cast<InstrumentTrack*>(track); }
		}
		QVERIFY(loaded != nullptr);
		const std::vector<MidiClip*> takes = midiClips(loaded);
		QCOMPARE(int(takes.size()), 2);
		const int group = takes.front()->takeGroupId();
		QVERIFY(takes.front()->hasTakeGroup() && takes.back()->takeGroupId() == group);
		QVERIFY(takes.front()->laneIndex() != takes.back()->laneIndex());
		QCOMPARE(loaded->takeLanes().activeTakeLane(group), takes.back()->laneIndex());
		QCOMPARE(takes.back()->notes()[0]->key(), 62);
		QVERIFY(!loaded->takePlays(takes.front()) && loaded->takePlays(takes.back()));
		Q_UNUSED(name)
	}

	//! Replace (Q050-094): a plain pass erases the earlier notes it overlaps - notes only.
	void replaceErasesOnlyOverlappedNotes()
	{
		const ControlResult clip = run("clip.add", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("position"), 0}});
		QVERIFY(clip.ok);
		const QString clipId = clip.result.value(QStringLiteral("clip")).toString();
		QVERIFY(run("note.add", {{QStringLiteral("clip"), clipId}, {QStringLiteral("key"), 50}, {QStringLiteral("position"), 0},
			{QStringLiteral("length"), 48}}).ok);
		QVERIFY(run("note.add", {{QStringLiteral("clip"), clipId}, {QStringLiteral("key"), 52}, {QStringLiteral("position"), 120},
			{QStringLiteral("length"), 24}}).ok);
		const recordoptions::Options before = recordoptions::options();
		recordoptions::Options replace = before;
		replace.mode = recordoptions::Mode::Replace;
		recordoptions::setOptions(replace);
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), true}});
		QVERIFY(run("transport.record").ok);
		renderUntil(10);
		play(m_track, 70, 100);
		renderUntil(30);
		release(m_track, 70);
		renderUntil(40);
		run("transport.stop");
		recordoptions::setOptions(before);
		run("track.set_midi_arm", {{QStringLiteral("track"), m_trackId}, {QStringLiteral("armed"), false}});
		const QJsonArray kept = run("note.list", {{QStringLiteral("clip"), clipId}}).result.value(QStringLiteral("notes")).toArray();
		QCOMPARE(kept.size(), 1);
		QCOMPARE(kept[0].toObject().value(QStringLiteral("key")).toInt(), 52);
		QCOMPARE(int(midiClips(m_track).size()), 2);
	}
};

QTEST_MAIN(MidiTakeRecordTest)
#include "MidiTakeRecordTest.moc"
