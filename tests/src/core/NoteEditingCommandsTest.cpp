/*
 * NoteEditingCommandsTest.cpp - the L07 note commands: scale, ratchet, add_many, list, find and change, swing, key names
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

/*! 0.5.0 L07: the note editor's clip-wide commands, each asserted on the exact resulting notes -
 *  note.scale (x2, x0.5, a refused factor), note.ratchet, note.add_many (one undo step; a bad
 *  entry adds nothing), note.list, note.transform_where through NoteTransform's filter (ranges,
 *  length, scale membership, invert, each action), groove.quantize's swing / scope / range, and
 *  key_names.* (the GM drum preset, one undo step, the project file both ways); and the Note
 *  fields - mute (kept, drawn, not played), release velocity (the note-off's), the pre-quantise
 *  timing note.restore_timing puts back - each optional in the file. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QTemporaryDir>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "TrackKeyNames.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString fileText(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

//! "key@pos+len" for every note, in the clip's order.
QStringList shape(const MidiClip* clip)
{
	QStringList out;
	for (const Note* note : clip->notes())
	{
		out << QStringLiteral("%1@%2+%3").arg(note->key()).arg(note->pos().getTicks()).arg(note->length().getTicks());
	}
	return out;
}

//! Whether any note of @a track is playing (the render pipeline started one).
bool anyNotePlaying(const InstrumentTrack* track)
{
	for (PlayHandle* handle : Engine::audioEngine()->playHandles())
	{
		auto* note = dynamic_cast<NotePlayHandle*>(handle);
		if (note != nullptr && note->instrumentTrack() == track) { return true; }
	}
	return false;
}

QList<int> velocities(const MidiClip* clip)
{
	QList<int> out;
	for (const Note* note : clip->notes()) { out << static_cast<int>(note->getVolume()); }
	return out;
}

} // namespace

class NoteEditingCommandsTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;

	QString clip() const { return control::clipIdOf(m_clip); }

	void freshClip(std::initializer_list<Note> notes)
	{
		Song* song = Engine::getSong();
		song->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		for (const Note& note : notes) { m_clip->addNote(note, false); }
		m_clip->rearrangeAllNotes();
	}

	QJsonObject ok(const QString& id, const QJsonObject& args)
	{
		const ControlResult result = run(id, args);
		if (!result.ok) { qWarning("%s refused: %s", qPrintable(id), qPrintable(result.errorMessage)); }
		return result.ok ? result.result : QJsonObject{{QStringLiteral("refused"), true}};
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

	void scaleDoublesAndHalvesAboutTheFirstNote()
	{
		freshClip({Note(TimePos(24), TimePos(48), 60), Note(TimePos(12), TimePos(96), 62)});
		ok(QStringLiteral("note.scale"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("time_factor"), 2.0}});
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@48+48"), QStringLiteral("62@144+24")}));
		ok(QStringLiteral("note.scale"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("time_factor"), 0.5},
			{QStringLiteral("length_factor"), 1.0}});
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@48+48"), QStringLiteral("62@96+24")}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@48+48"), QStringLiteral("62@144+24")}));
		const ControlResult zero = run(QStringLiteral("note.scale"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("time_factor"), 0.0}});
		QCOMPARE(zero.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@48+48"), QStringLiteral("62@144+24")}));
	}

	void ratchetSplitsOnlyTheScopedNotes()
	{
		freshClip({Note(TimePos(96), TimePos(0), 60), Note(TimePos(96), TimePos(96), 64)});
		m_clip->notes().back()->setSelected(true);
		const QJsonObject result = ok(QStringLiteral("note.ratchet"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("scope"), QStringLiteral("selection")}, {QStringLiteral("count"), 3}});
		QCOMPARE(result.value(QStringLiteral("added")).toInt(), 2);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+96"), QStringLiteral("64@96+32"),
			QStringLiteral("64@128+32"), QStringLiteral("64@160+32")}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+96"), QStringLiteral("64@96+96")}));
		QCOMPARE(run(QStringLiteral("note.ratchet"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("count"), 17}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void addManyIsOneEditAndAllOrNothing()
	{
		freshClip({});
		const QJsonArray notes{QJsonObject{{QStringLiteral("key"), 60}, {QStringLiteral("position"), 0}, {QStringLiteral("length"), 12}},
			QJsonObject{{QStringLiteral("key"), 62}, {QStringLiteral("position"), 12}, {QStringLiteral("length"), 12},
				{QStringLiteral("velocity"), 150}}};
		const QJsonObject added = ok(QStringLiteral("note.add_many"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("notes"), notes}});
		QCOMPARE(added.value(QStringLiteral("added")).toInt(), 2);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+12"), QStringLiteral("62@12+12")}));
		QCOMPARE(velocities(m_clip), (QList<int>{100, 150}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(m_clip->notes().empty());

		QJsonArray bad = notes;
		bad.append(QJsonObject{{QStringLiteral("key"), 128}, {QStringLiteral("position"), 0}, {QStringLiteral("length"), 12}});
		QCOMPARE(run(QStringLiteral("note.add_many"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("notes"), bad}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY2(m_clip->notes().empty(), "a refused batch added some of its notes");
	}

	void listAnswersEveryNote()
	{
		freshClip({Note(TimePos(24), TimePos(0), 60, 80), Note(TimePos(12), TimePos(48), 67, 120)});
		const QJsonArray rows = ok(QStringLiteral("note.list"), {{QStringLiteral("clip"), clip()}}).value(QStringLiteral("notes")).toArray();
		QCOMPARE(rows.size(), 2);
		QCOMPARE(rows.at(1).toObject().value(QStringLiteral("key")).toInt(), 67);
		QCOMPARE(rows.at(1).toObject().value(QStringLiteral("position")).toInt(), 48);
		QCOMPARE(rows.at(1).toObject().value(QStringLiteral("velocity")).toInt(), 120);
		QCOMPARE(rows.at(0).toObject().value(QStringLiteral("length")).toInt(), 24);
	}

	void findAndChangeActsOnTheMatchingNotes()
	{
		const auto fixture = [this]() {
			freshClip({Note(TimePos(48), TimePos(0), 60, 100), Note(TimePos(12), TimePos(48), 61, 40),
				Note(TimePos(12), TimePos(96), 67, 160)});
		};
		const auto where = [this](const QJsonObject& filter, const char* action, int amount = 0) {
			return ok(QStringLiteral("note.transform_where"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("filter"), filter},
				{QStringLiteral("action"), QLatin1String(action)}, {QStringLiteral("amount"), amount}});
		};
		fixture();
		QCOMPARE(where({{QStringLiteral("velocity_max"), 100}}, "velocity_offset", 10).value(QStringLiteral("matched")).toInt(), 2);
		QCOMPARE(velocities(m_clip), (QList<int>{110, 50, 160}));
		fixture();
		QCOMPARE(where({{QStringLiteral("length_max"), 12}}, "remove").value(QStringLiteral("removed")).toInt(), 2);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+48")}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
		fixture();
		where({{QStringLiteral("key_min"), 61}, {QStringLiteral("invert"), true}}, "transpose", 12);
		QCOMPARE(shape(m_clip).first(), QStringLiteral("72@0+48"));
		fixture();
		where({{QStringLiteral("position_min"), 48}}, "velocity_set", 90);
		QCOMPARE(velocities(m_clip), (QList<int>{100, 90, 90}));
		fixture();
		where({{QStringLiteral("key_max"), 61}}, "select");
		QList<bool> selected;
		for (const Note* note : m_clip->notes()) { selected << note->selected(); }
		QCOMPARE(selected, (QList<bool>{true, true, false}));
		// Scale membership against the scale context: C# is the only key outside C major.
		fixture();
		QVERIFY(run(QStringLiteral("scale.root_set"), {{QStringLiteral("root"), 0}}).ok);
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QStringLiteral("Major")}}).ok);
		QCOMPARE(where({{QStringLiteral("scale"), QStringLiteral("out")}}, "remove").value(QStringLiteral("removed")).toInt(), 1);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+48"), QStringLiteral("67@96+12")}));
		QVERIFY(run(QStringLiteral("scale.set"), {{QStringLiteral("scale"), QString()}}).ok);
		QCOMPARE(run(QStringLiteral("note.transform_where"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("action"), QStringLiteral("select")},
			{QStringLiteral("filter"), QJsonObject{{QStringLiteral("scale"), QStringLiteral("in")}}}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void quantizeSwingsTheOffStepsAndHonoursScopeAndRange()
	{
		freshClip({Note(TimePos(12), TimePos(1), 60), Note(TimePos(12), TimePos(25), 62), Note(TimePos(12), TimePos(49), 64),
			Note(TimePos(12), TimePos(73), 65)});
		ok(QStringLiteral("groove.quantize"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("grid"), 24}, {QStringLiteral("swing"), 0.5}});
		// steps 0..3 at 24 ticks; the odd ones (1, 3) are delayed by 0.5 x 12 = 6.
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+12"), QStringLiteral("62@30+12"), QStringLiteral("64@48+12"),
			QStringLiteral("65@78+12")}));
		freshClip({Note(TimePos(12), TimePos(5), 60), Note(TimePos(12), TimePos(29), 62), Note(TimePos(12), TimePos(53), 64)});
		ok(QStringLiteral("groove.quantize"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("grid"), 24},
			{QStringLiteral("start"), 24}, {QStringLiteral("end"), 48}});
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@5+12"), QStringLiteral("62@24+12"), QStringLiteral("64@53+12")}));
		m_clip->notes().back()->setSelected(true);
		ok(QStringLiteral("groove.quantize"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("grid"), 24},
			{QStringLiteral("scope"), QStringLiteral("selection")}});
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@5+12"), QStringLiteral("62@24+12"), QStringLiteral("64@48+12")}));
	}

	void aMutedNoteIsKeptButNotPlayed()
	{
		freshClip({Note(TimePos(96), TimePos(0), 60)});
		const QString note = control::noteIdOf(m_clip->notes().front());
		ok(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"),
			QJsonArray{QJsonObject{{QStringLiteral("note"), note}, {QStringLiteral("muted"), true}}}}});
		QVERIFY(m_clip->notes().front()->muted());
		QVERIFY(ok(QStringLiteral("note.list"), {{QStringLiteral("clip"), clip()}}).value(QStringLiteral("notes")).toArray()
			.first().toObject().value(QStringLiteral("muted")).toBool());
		Song* song = Engine::getSong();
		song->playSong();
		bool played = false;
		for (int period = 0; period < 32 && !played; ++period)
		{
			Engine::audioEngine()->renderNextPeriod();
			played = anyNotePlaying(m_track);
		}
		song->stop();
		Engine::audioEngine()->renderNextPeriod();
		QVERIFY2(!played, "a muted note was played");
		// The same render with the note unmuted does play it - the check above can fail.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!m_clip->notes().front()->muted());
		song->playSong();
		for (int period = 0; period < 32 && !played; ++period)
		{
			Engine::audioEngine()->renderNextPeriod();
			played = anyNotePlaying(m_track);
		}
		song->stop();
		Engine::audioEngine()->renderNextPeriod();
		QVERIFY2(played, "the unmuted note never started");
		ok(QStringLiteral("note.transform_where"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("filter"), QJsonObject{}},
			{QStringLiteral("action"), QStringLiteral("mute")}});
		QVERIFY(m_clip->notes().front()->muted());
	}

	void releaseVelocityIsTheNoteOffsAndOptional()
	{
		Note plain(TimePos(48), TimePos(0), 60);
		QCOMPARE(plain.releaseVelocity(), -1);
		QCOMPARE(plain.midiReleaseVelocity(64), 0);  // as before: a note-off of velocity 0
		freshClip({plain});
		const QString note = control::noteIdOf(m_clip->notes().front());
		ok(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"),
			QJsonArray{QJsonObject{{QStringLiteral("note"), note}, {QStringLiteral("release_velocity"), 150}}}}});
		const Note* edited = m_clip->notes().front();
		QCOMPARE(edited->releaseVelocity(), 150);
		QCOMPARE(edited->midiReleaseVelocity(64), 96);
		QCOMPARE(run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"),
			QJsonArray{QJsonObject{{QStringLiteral("note"), note}, {QStringLiteral("release_velocity"), 201}}}}}).errorKind,
			ControlErrorKind::InvalidArgs);

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement bare = plain.saveState(doc, root);
		for (const char* name : {"mute", "relvel", "qpos", "qlen"})
		{
			QVERIFY2(!bare.hasAttribute(QLatin1String(name)), name);  // a plain note saves exactly as before
		}
		Note full(TimePos(48), TimePos(24), 60);
		full.setMuted(true);
		full.setReleaseVelocity(30);
		full.rememberTiming();
		full.setPos(TimePos(0));
		const QDomElement element = full.saveState(doc, root);
		Note loaded;
		loaded.restoreState(element);
		QVERIFY(loaded.muted());
		QCOMPARE(loaded.releaseVelocity(), 30);
		QVERIFY(loaded.restoreTiming());
		QCOMPARE(static_cast<int>(loaded.pos().getTicks()), 24);
	}

	void restoreTimingPutsBackTheFirstQuantisesTiming()
	{
		freshClip({Note(TimePos(12), TimePos(5), 60), Note(TimePos(12), TimePos(29), 62)});
		ok(QStringLiteral("groove.quantize"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("grid"), 24}});
		ok(QStringLiteral("groove.quantize"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("grid"), 48}});
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+12"), QStringLiteral("62@48+12")}));
		const QJsonObject restored = ok(QStringLiteral("note.restore_timing"), {{QStringLiteral("clip"), clip()}});
		QCOMPARE(restored.value(QStringLiteral("restored")).toInt(), 2);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@5+12"), QStringLiteral("62@29+12")}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+12"), QStringLiteral("62@48+12")}));
		const QJsonObject frozen = ok(QStringLiteral("note.restore_timing"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("freeze"), true}});
		QCOMPARE(frozen.value(QStringLiteral("frozen")).toInt(), 2);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+12"), QStringLiteral("62@48+12")}));
		QVERIFY(!m_clip->notes().front()->hasUnquantisedTiming());
		QCOMPARE(ok(QStringLiteral("note.restore_timing"), {{QStringLiteral("clip"), clip()}}).value(QStringLiteral("restored")).toInt(), 0);
	}

	void keyNamesPresetUndoAndProjectFile()
	{
		freshClip({});
		const QString track = control::trackIdOf(m_track);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString bare = dir.filePath(QStringLiteral("bare.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(bare));
		QVERIFY2(!fileText(bare).contains(QStringLiteral("<key-names")), "a project with no key names wrote <key-names>");

		ok(QStringLiteral("key_names.preset"), {{QStringLiteral("track"), track}, {QStringLiteral("preset"), QStringLiteral("gm_drums")}});
		QCOMPARE(TrackKeyNames::instance().name(m_track->id(), 36 - 12), QStringLiteral("Bass Drum 1"));
		QCOMPARE(TrackKeyNames::instance().name(m_track->id(), 38 - 12), QStringLiteral("Acoustic Snare"));
		ok(QStringLiteral("key_names.set"), {{QStringLiteral("track"), track}, {QStringLiteral("key"), 24},
			{QStringLiteral("name"), QStringLiteral("Kick")}});
		QCOMPARE(TrackKeyNames::instance().name(m_track->id(), 24), QStringLiteral("Kick"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(TrackKeyNames::instance().name(m_track->id(), 24), QStringLiteral("Bass Drum 1"));

		const QString named = dir.filePath(QStringLiteral("named.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(named));
		const int id = m_track->id();
		Engine::getSong()->loadProject(bare);
		QVERIFY2(TrackKeyNames::instance().names(id).isEmpty(), "loading a project without key names kept the old ones");
		Engine::getSong()->loadProject(named);
		const Track* loaded = Engine::getSong()->tracks().front();
		QCOMPARE(TrackKeyNames::instance().name(loaded->id(), 26), QStringLiteral("Acoustic Snare"));
		const QJsonArray got = ok(QStringLiteral("key_names.get"), {{QStringLiteral("track"), control::trackIdOf(loaded)}})
			.value(QStringLiteral("names")).toArray();
		QCOMPARE(got.size(), TrackKeyNames::generalMidiDrums().size());

		ok(QStringLiteral("key_names.preset"), {{QStringLiteral("track"), control::trackIdOf(loaded)}, {QStringLiteral("preset"), QStringLiteral("none")}});
		QVERIFY(TrackKeyNames::instance().names(loaded->id()).isEmpty());
	}
};

QTEST_MAIN(NoteEditingCommandsTest)
#include "NoteEditingCommandsTest.moc"
