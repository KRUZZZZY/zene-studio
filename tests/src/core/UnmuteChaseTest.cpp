/*
 * UnmuteChaseTest.cpp - note chase when a track or clip is unmuted during playback
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
/*! 0.5.0 Q050-040 (L07): with chase on, a note already under the playhead starts when its track or
 *  clip is unmuted during playback - exactly once, beside the same-key note of an overlapping clip,
 *  and even when the unmute pass cannot take the track's processing lock. */

#include <QtTest>

#include <QJsonObject>

#include <condition_variable>
#include <mutex>
#include <thread>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiPlayback.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The keys of @a track's note play handles alive now.
QList<int> handleKeys(const InstrumentTrack* track)
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

//! Render @a periods audio periods by hand (the device is stopped).
void renderPeriods(int periods)
{
	for (int period = 0; period < periods; ++period) { Engine::audioEngine()->renderNextPeriod(); }
}

//! Stop and render until @a track has no note handle left; true when it drained (no stuck note).
bool renderUntilSilent(const InstrumentTrack* track)
{
	Engine::getSong()->stop();
	for (int period = 0; period < 64 && !handleKeys(track).isEmpty(); ++period)
	{
		Engine::audioEngine()->renderNextPeriod();
	}
	return handleKeys(track).isEmpty();
}

/*! Holds @a track's processing lock from another thread - as a second render context does in
 *  production - so InstrumentTrack::play()'s tryLock fails while it is alive. The lock is released
 *  on every exit path (a QVERIFY that fails returns from the slot), so the test cannot hang. */
class LockContender
{
public:
	explicit LockContender(InstrumentTrack& track) : m_track(track)
	{
		m_thread = std::thread([this]
		{
			m_track.lock();
			{
				std::lock_guard<std::mutex> guard(m_sync);
				m_locked = true;
			}
			m_cv.notify_one();
			std::unique_lock<std::mutex> wait(m_sync);
			m_cv.wait(wait, [this] { return m_release; });
			m_track.unlock();
		});
	}

	~LockContender()
	{
		{
			std::lock_guard<std::mutex> guard(m_sync);
			m_release = true;
		}
		m_cv.notify_one();
		if (m_thread.joinable()) { m_thread.join(); }
	}

	//! Block until the contender holds the lock: no pass called after this can take it.
	void waitUntilLocked()
	{
		std::unique_lock<std::mutex> guard(m_sync);
		m_cv.wait(guard, [this] { return m_locked; });
	}

private:
	InstrumentTrack& m_track;
	std::mutex m_sync;
	std::condition_variable m_cv;
	bool m_locked = false;
	bool m_release = false;
	std::thread m_thread;
};

} // namespace

class UnmuteChaseTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;

	//! A fresh project holding one clip with one note, and that note.
	Note* oneNote(int key = 60, int length = 384)
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		m_track->loadInstrument(QStringLiteral("tripleoscillator"));
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		return m_clip->addNote(Note(TimePos(length), TimePos(0), key), false);
	}

private slots:
	void initTestCase()
	{
		#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // a real instrument, so notes start
		#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! 0.5.0 Q050-040 (L07): with chase on, a note already under the playhead starts when its track
	//! or clip is unmuted, exactly once - and a seek into it does not start it a second time.
	void unmuteDuringPlaybackChasesTheNoteOnce()
	{
		oneNote();  // note at 0, length 384
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), true}}).ok);
		Song* song = Engine::getSong();

		// A muted track is silent: it schedules no note, so the unmute has a clean slate.
		m_track->setMuted(true);
		song->setPlayPos(96, Song::PlayMode::Song);
		song->playSong();
		renderPeriods(3);
		QVERIFY2(handleKeys(m_track).isEmpty(), "a muted track must schedule no note under the playhead");
		m_track->setMuted(false);
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60}));  // the note starts once at the unmute

		// A seek into the same, still ringing note must not start it a second time.
		song->setPlayPos(160, Song::PlayMode::Song);
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60}));
		QVERIFY(renderUntilSilent(m_track));  // stopped: nothing stuck

		// A muted clip is skipped: unmuting it chases its note under the playhead once.
		oneNote();
		m_clip->setMuted(true);
		song->setPlayPos(96, Song::PlayMode::Song);
		song->playSong();
		renderPeriods(3);
		QVERIFY2(handleKeys(m_track).isEmpty(), "a muted clip must schedule no note under the playhead");
		m_clip->setMuted(false);
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60}));
		QVERIFY(renderUntilSilent(m_track));

		// Leave the preference off for the tests that follow (the mute edits above own the undo top).
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), false}}).ok);
		QVERIFY(!MidiPlayback::instance().chase());
	}

	//! 0.5.0 Q050-040 (L07): after the unmute chase, re-muting and a loop must not leave a duplicate
	//! note-on or a stuck note-off behind.
	void unmuteChaseSurvivesRemuteAndLoop()
	{
		oneNote();  // note at 0, length 384
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), true}}).ok);
		Song* song = Engine::getSong();
		Timeline& timeline = song->getTimeline(Song::PlayMode::Song);

		// Unmute under the playhead: the note starts once.
		m_track->setMuted(true);
		song->setPlayPos(96, Song::PlayMode::Song);
		song->playSong();
		renderPeriods(3);
		m_track->setMuted(false);
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60}));

		// Re-mute and unmute again: no second note-on, and a stop still ends everything.
		m_track->setMuted(true);
		renderPeriods(3);
		m_track->setMuted(false);
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track).size(), 1);
		QVERIFY(renderUntilSilent(m_track));

		// A loop under the note does not double it.
		timeline.setLoopPoints(TimePos(0), TimePos(48));
		timeline.setLoopEnabled(true);
		song->setPlayPos(0, Song::PlayMode::Song);
		song->playSong();
		const unsigned before = song->loopWraps();
		for (int period = 0; period < 2000 && song->loopWraps() == before; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		QCOMPARE(song->loopWraps(), before + 1);
		QVERIFY2(handleKeys(m_track).size() <= 1, "a loop must not double the note already sounding");
		QVERIFY(renderUntilSilent(m_track));  // no stuck note-off
		timeline.setLoopEnabled(false);

		// Leave the preference off for the tests that follow (the mute edits above own the undo top).
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), false}}).ok);
		QVERIFY(!MidiPlayback::instance().chase());
	}

	//! 0.5.0 Q050-040 r4 (L07): the clip mute is a REGISTERED command (clip.set_mute), so the
	//! unmute-chase scenario is drivable through ControlRegistry exactly as an agent drives it -
	//! and the song editor's own mute dispatches the same command, one implementation both ways.
	void clipUnmuteThroughControlRegistryChasesTheNoteOnce()
	{
		oneNote();  // note at 0, length 384
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), true}}).ok);
		Song* song = Engine::getSong();
		const QString clip = control::clipIdOf(m_clip);
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);

		// Mute through the command. An idempotent repeat of it changes nothing, so it
		// must not spend an undo step: the checkpoint only fires when the value moves.
		const int depthBefore = journal->undoDepth();
		QVERIFY(run(QStringLiteral("clip.set_mute"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("muted"), true}}).ok);
		QVERIFY(m_clip->isMuted());
		QVERIFY(run(QStringLiteral("clip.set_mute"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("muted"), true}}).ok);
		QCOMPARE(journal->undoDepth(), depthBefore + 1);

		song->setPlayPos(96, Song::PlayMode::Song);
		song->playSong();
		renderPeriods(3);
		QVERIFY2(handleKeys(m_track).isEmpty(), "a clip muted through clip.set_mute must schedule no note");

		// Unmute through the same command: the note under the playhead starts, once.
		QVERIFY(run(QStringLiteral("clip.set_mute"), {{QStringLiteral("clip"), clip},
			{QStringLiteral("muted"), false}}).ok);
		QVERIFY(!m_clip->isMuted());
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60}));
		QVERIFY(renderUntilSilent(m_track));  // stopped: nothing stuck

		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), false}}).ok);
		QVERIFY(!MidiPlayback::instance().chase());
	}

	//! 0.5.0 Q050-040 r2 (L07): two overlapping clips holding the same key at the same clip-relative
	//! position each chase their own note - one clip's ringing note must not suppress the other's.
	void overlappingClipsEachChaseTheirOwnNote()
	{
		oneNote();  // clip A at 0: note key 60 at clip position 0, length 384
		MidiClip* clipB = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		QVERIFY(clipB != nullptr);
		clipB->addNote(Note(TimePos(384), TimePos(0), 60), false);
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), true}}).ok);
		Song* song = Engine::getSong();

		// With clip B muted, only clip A's note is under the playhead and sounds.
		clipB->setMuted(true);
		song->setPlayPos(96, Song::PlayMode::Song);
		song->playSong();
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60}));

		// Unmuting clip B chases ITS note: the same key at the same clip-relative position, but
		// another clip's instance - it starts over clip A's ringing note instead of being suppressed.
		clipB->setMuted(false);
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60, 60}));
		QVERIFY(renderUntilSilent(m_track));  // stopped: both notes drain, none stuck

		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), false}}).ok);
		QVERIFY(!MidiPlayback::instance().chase());
	}

	//! 0.5.0 Q050-040 r4 (L07 Luna rework): two distinct notes of ONE clip with the same key and the same
	//! position (the model permits it; Note::id() tells them apart) both sound after an unmute - the first
	//! handle must not suppress the second, nor a ringing twin its late-joined partner - and the seek and loop dedup still hold for the pair.
	void twinNotesInOneClipBothChaseAndDedup()
	{
		const Note* first = oneNote();  // key 60 at clip position 0, length 384
		QVERIFY(first != nullptr);
		QCOMPARE(m_clip->notes().size(), size_t(1));
		Note* twin = m_clip->addNote(Note(TimePos(384), TimePos(0), 60), false);
		QVERIFY(twin != nullptr);
		QVERIFY(twin != first);
		QVERIFY2(twin->id() != first->id(), "the two notes must be distinct by id");
		QCOMPARE(m_clip->notes().size(), size_t(2));
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), true}}).ok);
		Song* song = Engine::getSong();
		Timeline& timeline = song->getTimeline(Song::PlayMode::Song);

		m_clip->setMuted(true);
		song->setPlayPos(96, Song::PlayMode::Song);
		song->playSong();
		renderPeriods(3);
		QVERIFY(handleKeys(m_track).isEmpty());
		m_clip->setMuted(false);
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60, 60}));  // both twins sound, each once

		// A seek into the ringing pair starts neither again.
		song->setPlayPos(160, Song::PlayMode::Song);
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60, 60}));
		QVERIFY(renderUntilSilent(m_track));  // no stuck note

		// A loop under the pair does not double either of them.
		timeline.setLoopPoints(TimePos(0), TimePos(48));
		timeline.setLoopEnabled(true);
		song->setPlayPos(0, Song::PlayMode::Song);
		song->playSong();
		const unsigned before = song->loopWraps();
		for (int period = 0; period < 2000 && song->loopWraps() == before; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		QCOMPARE(song->loopWraps(), before + 1);
		QVERIFY2(handleKeys(m_track).size() <= 2, "a loop must not double a twin already sounding");
		QVERIFY(renderUntilSilent(m_track));
		timeline.setLoopEnabled(false);

		// A twin that joins while the first note rings: the live handle belongs to the first note by id,
		// so a seek under the pair chases the twin and only the twin (a clip+key+position match would
		// have taken the ringing handle for the twin as well and left it silent).
		QVERIFY(oneNote() != nullptr);
		song->setPlayPos(0, Song::PlayMode::Song);
		song->playSong();
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60}));
		QVERIFY(m_clip->addNote(Note(TimePos(384), TimePos(0), 60), false) != nullptr);
		song->setPlayPos(96, Song::PlayMode::Song);
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60, 60}));
		song->setPlayPos(160, Song::PlayMode::Song);
		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60, 60}));  // and neither starts twice
		QVERIFY(renderUntilSilent(m_track));

		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), false}}).ok);
		QVERIFY(!MidiPlayback::instance().chase());
	}

	//! 0.5.0 Q050-040 r2 (L07): when play() cannot take the processing lock, no pass happens - and
	//! the unmute edge survives the passes that failed, chasing the note once on the release.
	void unmuteEdgeSurvivesAContendedLock()
	{
		oneNote();  // note at 0, length 384
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), true}}).ok);
		Song* song = Engine::getSong();

		// A muted track is silent: it schedules no note, so the unmute has a clean slate.
		m_track->setMuted(true);
		song->setPlayPos(96, Song::PlayMode::Song);
		song->playSong();
		renderPeriods(3);
		QVERIFY2(handleKeys(m_track).isEmpty(), "a muted track must schedule no note under the playhead");

		// Another thread holds the track's processing lock, so every unmute pass fails play()'s
		// tryLock and no pass happens: the edge must not be consumed by a pass that never ran.
		{
			LockContender contender(*m_track);
			contender.waitUntilLocked();
			m_track->setMuted(false);
			renderPeriods(3);
			QVERIFY2(handleKeys(m_track).isEmpty(), "no note starts while the pass cannot take the lock");
		}  // the contender releases the lock here

		renderPeriods(3);
		QCOMPARE(handleKeys(m_track), (QList<int>{60}));  // the preserved edge chases the note once
		QVERIFY(renderUntilSilent(m_track));  // stopped: nothing stuck

		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), false}}).ok);
		QVERIFY(!MidiPlayback::instance().chase());
	}
};

QTEST_MAIN(UnmuteChaseTest)
#include "UnmuteChaseTest.moc"
