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
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		return m_clip->addNote(Note(TimePos(length), TimePos(0), key), false);
	}

	//! Play from @a tick for a few periods; the keys of the handles the track started.
	QList<int> playFrom(tick_t tick)
	{
		Song* song = Engine::getSong();
		song->setPlayPos(tick, Song::PlayMode::Song);
		song->playSong();
		for (int period = 0; period < 3; ++period) { Engine::audioEngine()->renderNextPeriod(); }
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
		// The pass counts a jump back, and a stop starts it again.
		MidiPlayback& playback = MidiPlayback::instance();
		playback.stopped();
		playback.tickPlayed(10);
		QVERIFY(playback.chasingNow());
		playback.tickPlayed(11);
		QVERIFY(!playback.chasingNow());
		playback.tickPlayed(0);
		QVERIFY(playback.chasingNow());
		QCOMPARE(playback.pass(), 1);
		playback.stopped();
		playback.tickPlayed(0);
		QCOMPARE(playback.pass(), 0);
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
		QVERIFY(run(QStringLiteral("note.edit"), edit(note, {{QStringLiteral("articulation"), 2}})).ok);
		QCOMPARE(playFrom(0), (QList<int>{25, 60}));
		// Saved with the track, and one undo takes the map away.
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("track"));
		m_track->articulations().save(element);
		QVERIFY(element.attribute(QStringLiteral("articulations")).contains(QStringLiteral("staccato")));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the note's articulation
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the map
		QVERIFY(m_track->articulations().empty());
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
