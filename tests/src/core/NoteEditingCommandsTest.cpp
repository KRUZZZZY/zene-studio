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
 *  timing note.restore_timing puts back - each optional in the file; and the clip's controller lanes
 *  (ED-130): the commands, the curve, the file (a lane is not read back as a note), and the play loop
 *  sending a value when it changes; and the FL Studio model's one-step verbs (2026-10-03) - note.remove_many,
 *  note.slice, the note clipboard (note.copy / cut / paste / duplicate) and note.edit's slide and Mod X / Y. */

#include <QtTest>

#include <memory>

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
		m_track->loadInstrument(QStringLiteral("tripleoscillator"));
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
		// A remainder goes to the last repeat; a scope with nothing long enough is refused, no step taken.
		freshClip({Note(TimePos(8), TimePos(0), 60), Note(TimePos(2), TimePos(48), 62)});
		ok(QStringLiteral("note.ratchet"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("count"), 3}});
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+2"), QStringLiteral("60@2+2"), QStringLiteral("60@4+4"),
			QStringLiteral("62@48+2")}));
		m_clip->notes().back()->setSelected(true);
		QCOMPARE(run(QStringLiteral("note.ratchet"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("scope"), QStringLiteral("selection")},
			{QStringLiteral("count"), 3}}).errorKind, ControlErrorKind::NotFound);
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
		const QJsonObject chosen = ok(QStringLiteral("note.select_where"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("filter"), QJsonObject{{QStringLiteral("key_max"), 61}}}});
		QCOMPARE(chosen.value(QStringLiteral("matched")).toInt(), 2);
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
		QCOMPARE(run(QStringLiteral("note.select_where"), {{QStringLiteral("clip"), clip()},
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

	void controllerLanesAreSetReadAndUndone()
	{
		freshClip({Note(TimePos(48), TimePos(0), 60)});
		const QJsonArray ramp{QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("value"), 0}},
			QJsonObject{{QStringLiteral("tick"), 96}, {QStringLiteral("value"), 127}}};
		ok(QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("lane"), QStringLiteral("cc:1")},
			{QStringLiteral("points"), ramp}});
		const ControllerLane* mod = m_clip->controllers().lane(ControllerKind::ControlChange, 1);
		QVERIFY(mod != nullptr);
		QCOMPARE(mod->valueAt(-10), 0);    // held before the first point
		QCOMPARE(mod->valueAt(48), 64);    // linear between (63.5 rounds up)
		QCOMPARE(mod->valueAt(500), 127);  // held after the last
		const QJsonArray lanes = ok(QStringLiteral("clip.controllers_get"), {{QStringLiteral("clip"), clip()}}).value(QStringLiteral("lanes")).toArray();
		QCOMPARE(lanes.size(), 1);
		QCOMPARE(lanes.first().toObject().value(QStringLiteral("lane")).toString(), QStringLiteral("cc:1"));
		QCOMPARE(lanes.first().toObject().value(QStringLiteral("points")).toArray().size(), 2);

		ok(QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("lane"), QStringLiteral("pitchbend")},
			{QStringLiteral("points"), QJsonArray{QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("value"), 16383}}}}});
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(m_clip->controllers().lane(ControllerKind::PitchBend, 0) == nullptr);
		QVERIFY(m_clip->controllers().lane(ControllerKind::ControlChange, 1) != nullptr);
		QCOMPARE(run(QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("lane"), QStringLiteral("cc:1")},
			{QStringLiteral("points"), QJsonArray{QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("value"), 128}}}}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("lane"), QStringLiteral("cc:200")},
			{QStringLiteral("points"), QJsonArray{}}}).errorKind, ControlErrorKind::InvalidArgs);
		ok(QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("lane"), QStringLiteral("cc:1")},
			{QStringLiteral("points"), QJsonArray{}}});
		QVERIFY(m_clip->controllers().empty());  // no points removes the lane
	}

	void controllerLanesRoundTripAndAreNotReadAsNotes()
	{
		freshClip({Note(TimePos(48), TimePos(0), 60), Note(TimePos(48), TimePos(48), 62)});
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		QVERIFY(m_clip->saveState(doc, root).firstChildElement(QStringLiteral("controllers")).isNull());  // none: nothing written
		m_clip->setControllerPoints(ControllerKind::ChannelPressure, 0, {{0, 10}, {48, 90}});
		const QDomElement saved = m_clip->saveState(doc, root);
		QCOMPARE(saved.firstChildElement(QStringLiteral("controllers")).attribute(QStringLiteral("metadata")), QStringLiteral("1"));
		auto* copy = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(384)));
		copy->restoreState(saved);
		QCOMPARE(copy->notes().size(), std::size_t{2});  // the lanes element is not a note
		const ControllerLane* pressure = copy->controllers().lane(ControllerKind::ChannelPressure, 0);
		QVERIFY(pressure != nullptr);
		QCOMPARE(pressure->points, (std::vector<ControllerPoint>{{0, 10}, {48, 90}}));
		const std::unique_ptr<MidiClip> duplicate(m_clip->clone());
		QVERIFY(duplicate->controllers().lane(ControllerKind::ChannelPressure, 0) != nullptr);  // a copy keeps its lanes
	}

	void thePlayLoopSendsAControllerWhenItChanges()
	{
		freshClip({Note(TimePos(192), TimePos(0), 60)});
		m_clip->setControllerPoints(ControllerKind::ControlChange, 74, {{0, 0}, {10, 100}});
		const ControllerLane* lane = m_clip->controllers().lane(ControllerKind::ControlChange, 74);
		m_track->playClipControllers(m_clip, 5, 0);
		QCOMPARE(lane->lastSent, 50);
		QCOMPARE(lane->lastTick, 5);
		m_track->playClipControllers(m_clip, 12, 0);  // a jump: sent again, the held 100
		QCOMPARE(lane->lastSent, 100);
		// Through the song's own render loop: the lane is played as the clip plays.
		m_clip->setControllerPoints(ControllerKind::ControlChange, 74, {{0, 7}});
		lane = m_clip->controllers().lane(ControllerKind::ControlChange, 74);
		QCOMPARE(lane->lastSent, -1);
		Song* song = Engine::getSong();
		song->playSong();
		for (int period = 0; period < 16 && lane->lastSent < 0; ++period) { Engine::audioEngine()->renderNextPeriod(); }
		song->stop();
		Engine::audioEngine()->renderNextPeriod();
		QCOMPARE(lane->lastSent, 7);
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

	// --- 0.5.0: the FL Studio model's one-step verbs (owner decision 2026-10-03) ---------------

	void removeManyIsOneEditAndAllOrNothing()
	{
		freshClip({Note(TimePos(12), TimePos(0), 60), Note(TimePos(12), TimePos(12), 62), Note(TimePos(12), TimePos(24), 64)});
		const QString first = control::noteIdOf(m_clip->notes()[0]);
		const QString last = control::noteIdOf(m_clip->notes()[2]);
		const ControlResult unknown = run(QStringLiteral("note.remove_many"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("notes"), QJsonArray{first, QStringLiteral("note-999999")}}});
		QCOMPARE(unknown.errorKind, ControlErrorKind::NotFound);
		QCOMPARE(m_clip->notes().size(), std::size_t{3});  // the known id was not removed either
		const QJsonObject removed = ok(QStringLiteral("note.remove_many"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("notes"), QJsonArray{first, last, first}}});
		QCOMPARE(removed.value(QStringLiteral("count")).toInt(), 2);  // an id named twice is removed once
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("62@12+12")}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // ONE step brings both back
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+12"), QStringLiteral("62@12+12"), QStringLiteral("64@24+12")}));
		QCOMPARE(run(QStringLiteral("note.remove_many"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("notes"), QJsonArray{}}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void sliceCutsTheCrossedNotesAsOneEdit()
	{
		freshClip({Note(TimePos(48), TimePos(0), 60), Note(TimePos(48), TimePos(0), 64), Note(TimePos(24), TimePos(96), 67)});
		Note* low = m_clip->notes()[0]->key() == 60 ? m_clip->notes()[0] : m_clip->notes()[1];
		const QString lowId = control::noteIdOf(low);
		ok(QStringLiteral("note.slice"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("at"), 24}, {QStringLiteral("notes"), QJsonArray{lowId}}});
		QStringList cut = shape(m_clip);
		cut.sort();
		QCOMPARE(cut, (QStringList{QStringLiteral("60@0+24"), QStringLiteral("60@24+24"), QStringLiteral("64@0+48"), QStringLiteral("67@96+24")}));
		QCOMPARE(control::noteIdOf(noteAtStart(0, 60)), lowId);  // the left half keeps the note's id
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
		// No 'notes': every note the tick falls inside; a tick inside none changes nothing.
		QCOMPARE(ok(QStringLiteral("note.slice"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("at"), 12}}).value(QStringLiteral("sliced")).toInt(), 2);
		QCOMPARE(ok(QStringLiteral("note.slice"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("at"), 150}}).value(QStringLiteral("sliced")).toInt(), 0);
		QCOMPARE(m_clip->notes().size(), std::size_t{5});
	}

	void theClipboardCopiesCutsPastesAndDuplicates()
	{
		freshClip({Note(TimePos(12), TimePos(0), 60), Note(TimePos(12), TimePos(24), 62)});
		QCOMPARE(run(QStringLiteral("note.paste"), {{QStringLiteral("clip"), clip()}}).errorKind, ControlErrorKind::NotFound);  // empty
		QCOMPARE(run(QStringLiteral("note.copy"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("scope"), QStringLiteral("selection")}}).errorKind,
			ControlErrorKind::NotFound);  // nothing selected
		m_clip->notes().back()->setSelected(true);
		QCOMPARE(ok(QStringLiteral("note.copy"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("scope"), QStringLiteral("selection")}})
			.value(QStringLiteral("copied")).toInt(), 1);
		ok(QStringLiteral("note.paste"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("position"), 96}});
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+12"), QStringLiteral("62@24+12"), QStringLiteral("62@96+12")}));
		QVERIFY2(m_clip->notes().back()->selected() && !m_clip->notes()[1]->selected(), "the pasted note is the selection");
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
		// Cut the clip: gone in one step, back in one undo, and the clipboard holds both for another clip.
		QCOMPARE(ok(QStringLiteral("note.cut"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("scope"), QStringLiteral("clip")}})
			.value(QStringLiteral("cut")).toInt(), 2);
		QVERIFY(m_clip->notes().empty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
		auto* other = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(384)));
		ok(QStringLiteral("note.paste"), {{QStringLiteral("clip"), control::clipIdOf(other)}});
		QCOMPARE(shape(other), (QStringList{QStringLiteral("60@0+12"), QStringLiteral("62@24+12")}));
		// Duplicate: shifted by the span rounded out to the grid (0..36 at 48 -> 48), the copies selected.
		const QJsonObject twice = ok(QStringLiteral("note.duplicate"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("grid"), 48}});
		QCOMPARE(twice.value(QStringLiteral("offset")).toInt(), 48);
		QCOMPARE(shape(m_clip), (QStringList{QStringLiteral("60@0+12"), QStringLiteral("62@24+12"), QStringLiteral("60@48+12"),
			QStringLiteral("62@72+12")}));
		QVERIFY(!m_clip->notes()[0]->selected() && m_clip->notes()[2]->selected() && m_clip->notes()[3]->selected());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
	}

	void noteEditRefusesAMisspelledField()
	{
		// Luna cluster 4807: a typo beside a valid field was silently ignored and the command
		// reported success, so a caller editing velocity and misspelling it lost the edit
		// with no error at all.
		freshClip({Note(TimePos(0), TimePos(48), 60)});
		const QString note = control::noteIdOf(m_clip->notes()[0]);
		const int before = static_cast<int>(m_clip->notes()[0]->getVolume());
		const ControlResult result = run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip()},
			{QStringLiteral("edits"), QJsonArray{QJsonObject{{QStringLiteral("note"), note},
				{QStringLiteral("velocity"), 90}, {QStringLiteral("velcoity"), 10}}}}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		// Nothing was written - the batch validates every entry before the first write.
		QCOMPARE(static_cast<int>(m_clip->notes()[0]->getVolume()), before);
	}

	void noteEditCarriesSlideAndModXY()
	{
		freshClip({Note(TimePos(12), TimePos(0), 60)});
		const QString note = control::noteIdOf(m_clip->notes()[0]);
		ok(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"), QJsonArray{QJsonObject{
			{QStringLiteral("note"), note}, {QStringLiteral("slide"), true}, {QStringLiteral("timbre"), 90}, {QStringLiteral("pressure"), 30}}}}});
		QVERIFY(m_clip->notes()[0]->slide());
		QCOMPARE(m_clip->notes()[0]->mpeTimbre(), 90);
		QCOMPARE(m_clip->notes()[0]->mpePressure(), 30);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!m_clip->notes()[0]->slide());
		QCOMPARE(m_clip->notes()[0]->mpeTimbre(), 0);
		QCOMPARE(run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"), QJsonArray{QJsonObject{
			{QStringLiteral("note"), note}, {QStringLiteral("timbre"), 128}}}}}).errorKind, ControlErrorKind::InvalidArgs);
	}

private:
	Note* noteAtStart(int position, int key) const
	{
		for (Note* note : m_clip->notes())
		{
			if (note->pos().getTicks() == position && note->key() == key) { return note; }
		}
		return nullptr;
	}
};

QTEST_MAIN(NoteEditingCommandsTest)
#include "NoteEditingCommandsTest.moc"
