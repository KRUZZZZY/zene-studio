/*
 * NotePlaybackTest.cpp - repeats, recurrence, articulations, chase, swing, lyrics and groups
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
/*! 0.5.0 L07 (W3): what a note carries beyond pitch and time, and how notes are played -
 *  - a note's repeats sound as that many more hits, its recurrence skips the passes it is not on,
 *    and its articulation plays the track's keyswitch first (ED-133, ED-121);
 *  - chase sounds a note playback starts inside, only when it is on (ED-075);
 *  - the global swing delays off-beats only, and the MIDI offset delays everything (ED-142, ED-076);
 *  - lyrics split into syllables, groups take ids, and all of it saves only when set (ED-088, ED-069). */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiPlayback.h"
#include "Note.h"
#include "NoteConditions.h"
#include "NotePlayHandle.h"
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

//! The warnings qWarning printed while it is alive.
class WarningCapture
{
public:
	WarningCapture()
	{
		s_texts.clear();
		qInstallMessageHandler(&collect);
	}

	~WarningCapture() { qInstallMessageHandler(nullptr); }
	QStringList texts() const { return s_texts; }

private:
	static void collect(QtMsgType, const QMessageLogContext&, const QString& text) { s_texts << text; }
	static QStringList s_texts;
};

QStringList WarningCapture::s_texts;

} // namespace

class NotePlaybackTest : public QObject
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

	//! Play from @a tick for @a periods periods; the keys of the handles the track has sounding then.
	QList<int> playFrom(tick_t tick, int periods = 3)
	{
		Song* song = Engine::getSong();
		song->setPlayPos(tick, Song::PlayMode::Song);
		song->playSong();
		for (int period = 0; period < periods; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		const QList<int> keys = handleKeys(m_track);
		song->stop();
		for (int period = 0; period < 64 && !handleKeys(m_track).isEmpty(); ++period) { Engine::audioEngine()->renderNextPeriod(); }
		return keys;
	}

	QJsonObject edit(const Note* note, const QJsonObject& values)
	{
		QJsonObject entry = values;
		entry.insert(QStringLiteral("note"), control::noteIdOf(note));
		return {{QStringLiteral("clip"), control::clipIdOf(m_clip)}, {QStringLiteral("edits"), QJsonArray{entry}}};
	}

private slots:
	void initTestCase()
	{
		#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // a real instrument, so notes start (SessionTuningTest's pattern)
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

	void repeatsSoundAsMoreHits()
	{
		Note* note = oneNote();
		QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("repeats"), 3}})).ok);
		QCOMPARE(note->conditions().repeats, 3);
		QCOMPARE(playFrom(0), (QList<int>{60, 60, 60, 60}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->notes().front()->conditions().repeats, 0);
	}

	void aRecurrenceSkipsThePassesItIsNotOn()
	{
		Note* note = oneNote();
		QCOMPARE(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("recur_mask"), 2}})).errorKind, ControlErrorKind::InvalidArgs);
		// The second of every two passes: not on the first.
		QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("recur_length"), 2}, {QStringLiteral("recur_mask"), 2}})).ok);
		QVERIFY(playFrom(0).isEmpty());
		NoteConditions conditions = m_clip->notes().front()->conditions();
		QVERIFY(!conditions.recursOnPass(0) && conditions.recursOnPass(1) && !conditions.recursOnPass(2));
		// The pass counts a loop wrap (Song::loopWraps) - not a jump back - and a stop starts it again (0.5.0 W2).
		MidiPlayback& playback = MidiPlayback::instance();
		playback.stopped();
		playback.tickPlayed(10, 5);
		QVERIFY(playback.chasingNow());
		QCOMPARE(playback.pass(), 0);  // the wraps before the start are no pass
		playback.tickPlayed(11, 5);
		QVERIFY(!playback.chasingNow());
		playback.tickPlayed(0, 5);     // a seek back
		QVERIFY(playback.chasingNow());
		QCOMPARE(playback.pass(), 0);
		playback.tickPlayed(0, 6);     // a wrap
		QCOMPARE(playback.pass(), 1);
		playback.stopped();
		playback.tickPlayed(0, 6);
		QCOMPARE(playback.pass(), 0);
	}

	//! 0.5.0 W2: the song counts the loop wraps its render plays, and a seek back while playing is no wrap
	//! and no pass (the pass used to count every jump back).
	void aLoopWrapIsAPassAndASeekBackIsNot()
	{
		oneNote();
		Song* song = Engine::getSong();
		Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(TimePos(0), TimePos(48));
		timeline.setLoopEnabled(true);
		song->setPlayPos(0, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();  // the test above fed it by hand: a fresh start, as after a stop
		song->playSong();
		const unsigned before = song->loopWraps();
		for (int period = 0; period < 2000 && song->loopWraps() == before; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		QCOMPARE(song->loopWraps(), before + 1);
		QCOMPARE(MidiPlayback::instance().pass(), 1);
		for (int period = 0; period < 2000 && song->getPlayPos().getTicks() < 24; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		song->setPlayPos(4, Song::PlayMode::Song);  // a seek back inside the loop
		Engine::audioEngine()->renderNextPeriod();
		QCOMPARE(song->loopWraps(), before + 1);
		QCOMPARE(MidiPlayback::instance().pass(), 1);
		song->setPlayPos(100, Song::PlayMode::Song);  // a seek past the loop: put back inside, no wrap either
		Engine::audioEngine()->renderNextPeriod();
		QVERIFY(song->getPlayPos().getTicks() < 48);
		QCOMPARE(song->loopWraps(), before + 1);
		QCOMPARE(MidiPlayback::instance().pass(), 1);
		song->stop();
		timeline.setLoopEnabled(false);
	}

	void anArticulationPlaysItsKeyswitchFirst()
	{
		Note* note = oneNote();
		const QString track = control::trackIdOf(m_track);
		QCOMPARE(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track},
			{QStringLiteral("articulations"), QJsonArray{QJsonObject{{QStringLiteral("keyswitch"), 200}}}}}).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), track}, {QStringLiteral("articulations"),
			QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("legato")}}, QJsonObject{{QStringLiteral("name"), QStringLiteral("staccato")},
				{QStringLiteral("keyswitch"), 25}}}}}).ok);
		QCOMPARE(m_track->articulations().indexOf(QStringLiteral("Staccato")), 2);
		const ControlResult listed = run(QStringLiteral("track.articulations"), {{QStringLiteral("track"), track}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		const QJsonArray map = listed.result.value(QStringLiteral("articulations")).toArray();
		QCOMPARE(map.size(), 2);  // in order: a note's 'articulation' is the 1-based index into it
		QCOMPARE(map.at(1).toObject().value(QStringLiteral("keyswitch")).toInt(), 25);
		QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("articulation"), 2}})).ok);
		QCOMPARE(playFrom(0, 1), (QList<int>{25, 60}));  // one period in: the one-tick keyswitch still sounds
		// Saved with the track, and one undo takes the map away.
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("track"));
		m_track->articulations().save(element);
		QVERIFY(element.attribute(QStringLiteral("articulations")).contains(QStringLiteral("staccato")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the note's articulation
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the map
		QVERIFY(m_track->articulations().empty());
	}

	void aMalformedArticulationJsonWarnsAndLoadsWithNone()
	{
		oneNote();
		QVERIFY(run(QStringLiteral("track.articulations_set"), {{QStringLiteral("track"), control::trackIdOf(m_track)},
			{QStringLiteral("articulations"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("legato")}}}}}).ok);
		QCOMPARE(m_track->articulations().indexOf(QStringLiteral("Legato")), 1);
		QDomDocument doc;
		QDomElement truncated = doc.createElement(QStringLiteral("track"));
		truncated.setAttribute(QStringLiteral("articulations"), QStringLiteral("[{\"name\":\"legato\","));
		QDomElement notAnArray = doc.createElement(QStringLiteral("track"));
		notAnArray.setAttribute(QStringLiteral("articulations"), QStringLiteral("{\"name\":\"legato\"}"));
		WarningCapture warnings;
		m_track->articulations().load(truncated);
		QVERIFY(m_track->articulations().empty());
		QVERIFY2(warnings.texts().join(QLatin1Char('\n')).contains(QStringLiteral("could not be read")),
			qPrintable(warnings.texts().join(QLatin1Char('\n'))));
		m_track->articulations().load(notAnArray);
		QVERIFY(m_track->articulations().empty());
		QVERIFY2(warnings.texts().join(QLatin1Char('\n')).contains(QStringLiteral("could not be read")),
			qPrintable(warnings.texts().join(QLatin1Char('\n'))));
	}

	void anInvalidArticulationLeavesNoPrefixLoaded()
	{
		oneNote();
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("track"));
		element.setAttribute(QStringLiteral("articulations"),
			QStringLiteral("[{\"name\":\"legato\"},{\"keyswitch\":25}]"));  // the second entry has no name
		WarningCapture warnings;
		m_track->articulations().load(element);
		QVERIFY(m_track->articulations().empty());  // not the valid prefix alone, which would save back as the whole map
		QVERIFY2(warnings.texts().join(QLatin1Char('\n')).contains(QStringLiteral("could not be read")),
			qPrintable(warnings.texts().join(QLatin1Char('\n'))));
	}

	void aLoopedClipPlaysItsStartAgain()
	{
		Note* note = oneNote(60, 24);
		m_clip->changeLength(TimePos(384));
		QVERIFY(playFrom(96).isEmpty());  // no loop: nothing starts at 96
		QVERIFY(run(QStringLiteral("clip.set_loop"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)}, {QStringLiteral("length"), 96}}).ok);
		QCOMPARE(playFrom(96), (QList<int>{60}));  // 96 folds to 0, where the note is
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->clipEdits().loopTicks, 0);
		(void) note;
	}

	void chaseSoundsANotePlaybackStartsInside()
	{
		oneNote();
		QVERIFY(playFrom(96).isEmpty());  // off by default: playback as it always was
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("chase"), true}}).ok);
		QCOMPARE(playFrom(96), (QList<int>{60}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!MidiPlayback::instance().chase());
	}

	void swingDelaysTheOffBeatsOnly()
	{
		MidiPlayback& playback = MidiPlayback::instance();
		QCOMPARE(playback.delayFrames(12, 100.f, 48000), f_cnt_t{0});
		QVERIFY(run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), 0.5}}).ok);
		QCOMPARE(playback.delayFrames(0, 100.f, 48000), f_cnt_t{0});
		QCOMPARE(playback.delayFrames(12, 100.f, 48000), f_cnt_t{600});  // half of a 12-tick step
		QCOMPARE(playback.delayFrames(24, 100.f, 48000), f_cnt_t{0});
		QVERIFY(playback.shouldPersist());
		QVERIFY(run(QStringLiteral("transport.groove"), {{QStringLiteral("grid"), QStringLiteral("8th")}}).ok);
		QCOMPARE(playback.delayFrames(12, 100.f, 48000), f_cnt_t{0});
		QCOMPARE(playback.delayFrames(24, 100.f, 48000), f_cnt_t{1200});
		QVERIFY(run(QStringLiteral("transport.midi_options"), {{QStringLiteral("offset_ms"), 10}}).ok);
		QCOMPARE(playback.delayFrames(0, 100.f, 48000), f_cnt_t{480});
		for (int i = 0; i < 3; ++i) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
		QCOMPARE(playback.swing(), 0.f);
		QCOMPARE(playback.offsetMs(), 0);
		QVERIFY(!playback.shouldPersist());
		QCOMPARE(run(QStringLiteral("transport.groove"), {{QStringLiteral("swing"), 0.9}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void lyricsAndGroups()
	{
		oneNote();
		for (int i = 1; i < 5; ++i) { m_clip->addNote(Note(TimePos(24), TimePos(i * 48), 60 + i), false); }
		const QString clip = control::clipIdOf(m_clip);
		QCOMPARE(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}}).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("note.lyrics"), {{QStringLiteral("clip"), clip}, {QStringLiteral("text"), QStringLiteral("Hal-le-lu-jah sing")}}).ok);
		QStringList sung;
		for (const Note* note : m_clip->notes()) { sung << lyricText(note->conditions().lyric); }
		QCOMPARE(sung, (QStringList{QStringLiteral("Hal-"), QStringLiteral("le-"), QStringLiteral("lu-"), QStringLiteral("jah"), QStringLiteral("sing")}));
		for (Note* note : m_clip->notes()) { note->setSelected(note->key() >= 62); }
		const ControlResult grouped = run(QStringLiteral("note.group"), {{QStringLiteral("clip"), clip}});
		QVERIFY2(grouped.ok, qPrintable(grouped.errorMessage));
		QCOMPARE(grouped.result.value(QStringLiteral("notes")).toInt(), 3);
		Note* last = m_clip->notes().back();
		QCOMPARE(last->conditions().group, 1);
		// Saved only when set, and read back whole.
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = last->saveState(doc, root);
		QCOMPARE(saved.attribute(QStringLiteral("lyr")), QStringLiteral("sing"));
		Note restored;
		restored.restoreState(saved);
		QVERIFY(restored.conditions() == last->conditions());
		QVERIFY(!m_clip->notes().front()->saveState(doc, root).hasAttribute(QStringLiteral("grp")));
		QVERIFY(run(QStringLiteral("note.ungroup"), {{QStringLiteral("clip"), clip}}).ok);
		QCOMPARE(m_clip->notes().back()->conditions().group, 0);
		QCOMPARE(run(QStringLiteral("note.group"), {{QStringLiteral("clip"), clip}, {QStringLiteral("scope"), QStringLiteral("clip")}}).result
			.value(QStringLiteral("group")).toInt(), 1);
	}
};

QTEST_MAIN(NotePlaybackTest)
#include "NotePlaybackTest.moc"
