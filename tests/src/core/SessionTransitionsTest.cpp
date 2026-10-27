/*
 * SessionTransitionsTest.cpp - the launcher, the arrangement and the session grid stay in step (Q050-489)
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
/*! Q050-489, the reproduced transitions (docs/050-features/Q050-489.md, the transition map):
 *   T1  a playing cell that is cleared kept playing - the launcher reported it, the column stayed taken over and
 *       the track's arrangement stayed muted - and its undo and redo did not tell the launcher either;
 *   T2  a playing cell given a new pattern kept sounding the old one until some later launch republished the grid,
 *       and an undo of the edit did not put the old sound back;
 *   T3  deleting a pattern track renumbers the patterns, and a cell kept the old number - so it named ANOTHER
 *       pattern (or none); moving a pattern track swapped the numbers under the cells the same way;
 *   T4  a command queued before a reset (a project change) was applied AFTER it, so the closing project's
 *       published content came back into the next project;
 *   and the sequence of acceptance 2: launch / stop / edit / undo / redo / save / load keeps every authored identity
 *   and reference, and none of the transient launch state is persisted.
 *  The engine is the real one (Engine::init runs the Dummy device), so a launch is a real launch; the renders go
 *  through the real export path. */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <memory>
#include <vector>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "AudioEngine.h"
#include "DummyInstrument.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "OutputSettings.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "SessionModel.h"
#include "SessionPlayback.h"
#include "SessionScheduler.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr int SampleRate = 44100;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject cell(int track, int scene) { return {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}}; }

QJsonObject midiCell(int track, int scene, int pattern)
{
	QJsonObject args = cell(track, scene);
	args.insert(QStringLiteral("type"), QStringLiteral("midi"));
	args.insert(QStringLiteral("pattern"), pattern);
	return args;
}

//! The phase session.get_state reports for column @a track ("idle" when it lists none).
QString columnPhase(int track)
{
	for (const QJsonValue& value : run(QStringLiteral("session.get_state")).result.value(QStringLiteral("columns")).toArray())
	{
		if (value.toObject().value(QStringLiteral("track")).toInt() == track)
		{
			return value.toObject().value(QStringLiteral("phase")).toString();
		}
	}
	return QStringLiteral("idle");
}

//! The authored half of session.get_state: `launch` and `columns` are the engine's transient reading.
QJsonObject authored()
{
	QJsonObject state = run(QStringLiteral("session.get_state")).result;
	state.remove(QStringLiteral("launch"));
	state.remove(QStringLiteral("columns"));
	return state;
}

QJsonObject slotOf(int track, int scene)
{
	for (const QJsonValue& value : authored().value(QStringLiteral("slots")).toArray())
	{
		const QJsonObject slot = value.toObject();
		if (slot.value(QStringLiteral("track")).toInt() == track && slot.value(QStringLiteral("scene")).toInt() == scene) { return slot; }
	}
	return {};
}

//! The name of the pattern track a MIDI cell names - the identity a pattern NUMBER stands for.
QString patternNameOf(int track, int scene)
{
	const QJsonObject slot = slotOf(track, scene);
	if (slot.value(QStringLiteral("type")).toString() != QLatin1String("midi")) { return QString(); }
	const PatternTrack* named = PatternTrack::findPatternTrack(slot.value(QStringLiteral("pattern")).toInt());
	return named != nullptr ? named->name() : QStringLiteral("<no pattern>");
}

bool isSilent(const QByteArray& wav)
{
	const int at = wav.indexOf("data", 12);
	if (at < 0) { return true; }
	const QByteArray pcm = wav.mid(at + 8);
	return std::all_of(pcm.begin(), pcm.end(), [](char byte) { return byte == '\0'; });
}

} // namespace

class SessionTransitionsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the sounding pattern needs tripleoscillator
#endif
		Engine::init(true);
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("session.stop_all"));
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! T1: clearing a playing cell stops it, and neither the undo nor the redo launches anything.
	void aClearedPlayingCellStopsAndItsUndoDoesNotRelaunch()
	{
		QVERIFY(buildFixture());
		QVERIFY(run(QStringLiteral("session.launch_slot"), cell(0, 0)).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("playing"), 3000);
		const QJsonObject withClip = authored();

		QVERIFY(run(QStringLiteral("session.clear_slot"), cell(0, 0)).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("idle"), 3000);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(authored(), withClip);  // the authored cell is back ...
		QTest::qWait(100);
		QCOMPARE(columnPhase(0), QStringLiteral("idle"));  // ... its playback is not: undo is not a launch
		QVERIFY(run(QStringLiteral("session.launch_slot"), cell(0, 0)).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("playing"), 3000);
		QVERIFY(run(QStringLiteral("control.redo")).ok);  // the clear again: the playing cell stops again
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("idle"), 3000);
		QVERIFY(slotOf(0, 0).isEmpty());
	}

	//! T1 for the grid: a playing cell a shrunk grid no longer holds stops; a valid cell plays on.
	void aCellOutsideAShrunkGridStops()
	{
		QVERIFY(buildFixture());
		QVERIFY(run(QStringLiteral("session.set_slot"), midiCell(1, 1, 0)).ok);
		QVERIFY(run(QStringLiteral("session.launch_slot"), cell(0, 0)).ok);
		QVERIFY(run(QStringLiteral("session.launch_slot"), cell(1, 1)).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(1), QStringLiteral("playing"), 3000);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("playing"), 3000);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 2}, {QStringLiteral("scenes"), 1}}).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(1), QStringLiteral("idle"), 3000);
		QCOMPARE(columnPhase(0), QStringLiteral("playing"));
	}

	/*! T1 + T2, heard: the arrangement sounds; a launched cell holding an EMPTY pattern takes the track over (silence);
	 *  giving the playing cell the sounding pattern sounds at once, its undo silences it, its redo sounds it again;
	 *  clearing the cell gives the track back to its arrangement. */
	void aPlayingCellSoundsWhatTheGridHolds()
	{
		QVERIFY2(buildFixture(), "the sounding instrument (tripleoscillator) did not load: a failure, not a skip");
		QVERIFY(run(QStringLiteral("session.set_slot"), midiCell(0, 0, m_emptyPattern)).ok);
		QVERIFY2(!isSilent(render()), "the arrangement fixture must sound, or giving the track back proves nothing");
		QVERIFY(run(QStringLiteral("session.launch_slot"), cell(0, 0)).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("playing"), 3000);
		QVERIFY2(isSilent(render()), "a cell holding an empty pattern should take its track over");

		QVERIFY(run(QStringLiteral("session.set_slot"), midiCell(0, 0, m_soundingPattern)).ok);
		QVERIFY2(!isSilent(render()), "the playing cell was given a sounding pattern but still renders the old one");
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(isSilent(render()), "the undo put the empty pattern back in the grid but not in the launcher");
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY2(!isSilent(render()), "the redo put the sounding pattern back in the grid but not in the launcher");

		QVERIFY(run(QStringLiteral("session.clear_slot"), cell(0, 0)).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("idle"), 3000);
		QVERIFY2(!isSilent(render()), "a cleared cell kept its track taken over: the arrangement stayed muted");
	}

	//! T3: a deleted pattern track renumbers the patterns; every cell keeps naming the pattern it named.
	void aCellFollowsItsPatternWhenThePatternsAreRenumbered()
	{
		QVERIFY(buildFixture());
		auto* third = dynamic_cast<PatternTrack*>(Track::create(Track::Type::Pattern, Engine::getSong()));
		QVERIFY(third != nullptr);
		third->setName(QStringLiteral("Third"));
		QVERIFY(run(QStringLiteral("session.set_slot"), midiCell(0, 1, third->patternIndex())).ok);
		QCOMPARE(patternNameOf(0, 0), QStringLiteral("Sounding"));
		QCOMPARE(patternNameOf(0, 1), QStringLiteral("Third"));

		// The empty pattern (between the two) goes: the later pattern's number moves down under its cell.
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), control::trackIdOf(PatternTrack::findPatternTrack(m_emptyPattern))}}).ok);
		QCOMPARE(patternNameOf(0, 0), QStringLiteral("Sounding"));
		QCOMPARE(patternNameOf(0, 1), QStringLiteral("Third"));

		// The GUI's move of a pattern track (TrackContainerView::moveTrackView) swaps two numbers: the cells follow.
		PatternTrack::swapPatternTracks(PatternTrack::findPatternTrack(0), PatternTrack::findPatternTrack(1));
		QCOMPARE(patternNameOf(0, 0), QStringLiteral("Sounding"));
		QCOMPARE(patternNameOf(0, 1), QStringLiteral("Third"));

		// A cell whose pattern is deleted is emptied - and, if it was playing, stopped - rather than left naming another.
		QVERIFY(run(QStringLiteral("session.launch_slot"), cell(0, 1)).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("playing"), 3000);
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), control::trackIdOf(third)}}).ok);
		QVERIFY(slotOf(0, 1).isEmpty());
		QCOMPARE(patternNameOf(0, 0), QStringLiteral("Sounding"));
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("idle"), 3000);
	}

	/*! T4: a command queued before a reset (a project change, stop_all, back to arrangement) is dropped with what
	 *  the reset dropped. The reset is carried out first in a period and used to be followed by the queue it should
	 *  have emptied, so the previous project's published content came back into the next one. A scheduler of its
	 *  own: this test is its audio thread, so nothing races it. */
	void aCommandQueuedBeforeAResetIsDroppedWithIt()
	{
		auto scheduler = std::make_unique<SessionScheduler>();
		SessionClockContext clock;
		clock.ticksPerBar = 192;
		clock.framesPerTick = 4.0f;
		clock.transportRunning = true;
		QVERIFY(scheduler->publishSlotContent(0, 0, 7, 0));  // the closing project's cell
		scheduler->reset();
		QVERIFY(scheduler->requestLaunch(0, 0, LaunchMode::Trigger, LaunchQuantisation::None));  // the next project's
		scheduler->processAudio(clock, 64);
		clock.positionTicks = 4;
		scheduler->processAudio(clock, 64);
		const SessionSlotPlayback view = scheduler->playbackForColumn(0);
		QVERIFY2(view.playing, "the launch queued after the reset must still be carried out");
		QCOMPARE(view.patternId, -1);  // not 7: that content was published before the reset
	}

	/*! Acceptance 2: one deterministic sequence - launch, stop, edit, undo, redo, clear, undo, save, load. Every step compares
	 *  the AUTHORED state (session.get_state without its transient `launch` and `columns`), the identities the cells
	 *  name (pattern by its track's name, audio by its source, a chain by its jump target) survive the reload, and
	 *  the saved file carries none of the launch state. */
	void launchStopEditUndoRedoSaveLoadKeepsTheAuthoredState()
	{
		QVERIFY(buildFixture());
		QJsonObject audio = cell(1, 1);
		audio.insert(QStringLiteral("type"), QStringLiteral("audio"));
		audio.insert(QStringLiteral("source"), QStringLiteral("drums.wav"));
		audio.insert(QStringLiteral("name"), QStringLiteral("Drums"));
		QVERIFY(run(QStringLiteral("session.set_slot"), audio).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("name"), QStringLiteral("Lead")}, {QStringLiteral("follow_actions"), QJsonArray{QJsonObject{
				{QStringLiteral("type"), QStringLiteral("jump")}, {QStringLiteral("jump_to"), 1}}}}}).ok);
		QVERIFY(run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 1}, {QStringLiteral("name"), QStringLiteral("Out")}}).ok);
		const QJsonObject a0 = authored();

		QVERIFY(run(QStringLiteral("session.launch_slot"), cell(0, 0)).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("playing"), 3000);
		QVERIFY(run(QStringLiteral("session.stop_slot"), cell(0, 0)).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("idle"), 3000);
		QCOMPARE(authored(), a0);  // launching and stopping author nothing

		QVERIFY(run(QStringLiteral("session.launch_slot"), cell(0, 0)).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("playing"), 3000);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("name"), QStringLiteral("Lead 2")}, {QStringLiteral("legato"), true}}).ok);
		const QJsonObject a1 = authored();
		QVERIFY(a1 != a0);
		QCOMPARE(columnPhase(0), QStringLiteral("playing"));  // an edit of a cell that keeps a clip does not stop it
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(authored(), a0);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(authored(), a1);
		QCOMPARE(columnPhase(0), QStringLiteral("playing"));
		QVERIFY(run(QStringLiteral("session.clear_slot"), cell(0, 0)).ok);  // the playing cell goes, and stops
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("idle"), 3000);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(authored(), a1);  // the authored cell is back, its playback is not
		QCOMPARE(columnPhase(0), QStringLiteral("idle"));
		QVERIFY(run(QStringLiteral("session.launch_slot"), cell(0, 0)).ok);  // saved while it plays
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("playing"), 3000);

		const QString path = m_dir.filePath(QStringLiteral("sequence.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok);
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QByteArray saved = file.readAll();
		for (const char* transient : {"playing", "phase", "completed_launches", "start_line"})
		{
			QVERIFY2(!saved.contains(transient), transient);
		}
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}}).ok);
		QTRY_COMPARE_WITH_TIMEOUT(columnPhase(0), QStringLiteral("idle"), 3000);  // a reload launches nothing
		QJsonObject reloaded = authored();
		QCOMPARE(reloaded.value(QStringLiteral("slots")), a1.value(QStringLiteral("slots")));
		QCOMPARE(reloaded.value(QStringLiteral("scenes")), a1.value(QStringLiteral("scenes")));
		QCOMPARE(reloaded.value(QStringLiteral("quantisation")), a1.value(QStringLiteral("quantisation")));
		QCOMPARE(patternNameOf(0, 0), QStringLiteral("Sounding"));
		QCOMPARE(slotOf(1, 1).value(QStringLiteral("source")).toString(), QStringLiteral("drums.wav"));
		QCOMPARE(slotOf(0, 0).value(QStringLiteral("follow_actions")).toArray().at(0).toObject()
			.value(QStringLiteral("jump_to")).toInt(), 1);
	}

private:
	/*! A fresh project: column 0 is a sample track whose arrangement SOUNDS (a constant clip over the export), the
	 *  pattern store holds a real instrument, pattern "Sounding" has a held note and pattern "Empty" none. A 2 x 2
	 *  grid whose cell (0, 0) holds the sounding pattern, launch quantisation none (a launch starts next period). */
	bool buildFixture()
	{
		run(QStringLiteral("session.stop_all"));
		Song* song = Engine::getSong();
		song->clearProject();
		auto* arrangement = new SampleTrack(song);
		arrangement->setName(QStringLiteral("Arrangement"));
		std::vector<SampleFrame> frames(static_cast<std::size_t>(SampleRate), SampleFrame(0.25f, 0.25f));
		auto* clip = new SampleClip(arrangement);
		clip->setSampleBuffer(std::make_shared<const SampleBuffer>(std::move(frames), SampleRate));
		clip->movePosition(TimePos(0));
		auto* store = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::patternStore()));
		if (store == nullptr) { return false; }
		Instrument* instrument = store->loadInstrument(QStringLiteral("tripleoscillator"));
		if (instrument == nullptr || dynamic_cast<DummyInstrument*>(instrument) != nullptr) { return false; }
		// The store's first track made pattern 0 (PatternStore::updateAfterTrackAdd); one more is the empty one.
		if (PatternTrack::findPatternTrack(0) == nullptr) { song->addPatternTrack(); }
		PatternTrack::findPatternTrack(0)->setName(QStringLiteral("Sounding"));
		auto* empty = dynamic_cast<PatternTrack*>(Track::create(Track::Type::Pattern, song));
		if (empty == nullptr) { return false; }
		empty->setName(QStringLiteral("Empty"));
		m_soundingPattern = 0;
		m_emptyPattern = empty->patternIndex();
		auto* notes = dynamic_cast<MidiClip*>(store->getClip(static_cast<std::size_t>(m_soundingPattern)));
		if (notes == nullptr) { return false; }
		notes->addNote(Note(TimePos(190), TimePos(0), 60, 100), false);
		song->updateLength();
		return run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 2}, {QStringLiteral("scenes"), 2}}).ok
			&& run(QStringLiteral("session.set_quantisation"), {{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok
			&& run(QStringLiteral("session.set_slot"), midiCell(0, 0, m_soundingPattern)).ok
			&& song->tracks().size() >= 2;
	}

	/*! The song through the real export path, as WAV bytes. The live device is put back afterwards, as
	 *  RenderManager does (store, render, restore), so the launches after a render are real launches again. */
	QByteArray render()
	{
		const QString path = m_dir.filePath(QStringLiteral("transitions.wav"));
		QFile::remove(path);
		const OutputSettings settings(SampleRate, 192, OutputSettings::BitDepth::Depth16Bit, OutputSettings::StereoMode::Stereo);
		Engine::audioEngine()->storeAudioDevice();
		{
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
			if (renderer.isReady())
			{
				renderer.startProcessing();
				renderer.wait();
			}
		}
		Engine::audioEngine()->restoreAudioDevice();  // also deletes the file device
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	}

	QTemporaryDir m_dir;
	int m_soundingPattern = 0;
	int m_emptyPattern = 1;
};

QTEST_GUILESS_MAIN(SessionTransitionsTest)
#include "SessionTransitionsTest.moc"
