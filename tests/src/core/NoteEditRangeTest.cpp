/*
 * NoteEditRangeTest.cpp - note.edit's range validation: a note may not end past MaxSongLength
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
 */

/*! note.edit checks 'position' and 'length' against MaxSongLength one at a time, so the note's
 *  END - the sum of the two - needs its own check: a note written at the very end of the
 *  supported range with a length would otherwise end past it. The position+length edit and a
 *  length-only edit are covered here, as is note.resize's independent range check. The limit
 *  itself stays reachable. Other note commands (scale, ratchet, add_many, clipboard, slide and
 *  Mod X / Y) are covered by NoteEditingCommandsTest. A batch that changes no note records no undo
 *  step (BUG 12, cluster new-p15-22): tested at the end of this file. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

class NoteEditRangeTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;

	QString clip() const { return control::clipIdOf(m_clip); }

	void freshClip()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		m_clip->addNote(Note(TimePos(48), TimePos(0), 60), false);
		m_clip->rearrangeAllNotes();
	}

	//! The error kind one edit makes; Ok when the command succeeded.
	ControlErrorKind edit(const QString& note, const QJsonObject& fields)
	{
		QJsonObject entry{{QStringLiteral("note"), note}};
		for (auto it = fields.begin(); it != fields.end(); ++it) { entry.insert(it.key(), it.value()); }
		const ControlResult result = run(QStringLiteral("note.edit"),
			{{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"), QJsonArray{entry}}});
		return result.ok ? ControlErrorKind::None : result.errorKind;
	}

	//! One note.edit setting the velocity of @a note.
	ControlResult editVelocity(const QString& note, int value)
	{
		return run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"),
			QJsonArray{QJsonObject{{QStringLiteral("note"), note}, {QStringLiteral("velocity"), value}}}}});
	}

	//! Coalescing groups same-command calls inside a wall-clock window; 0 makes each call its own undo step.
	static bool setCoalescing(int windowMs)
	{
		return run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), windowMs}}).ok;
	}

	int velocity() const { return static_cast<int>(m_clip->notes()[0]->getVolume()); }

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void constructionClampsKeysToMidiRange()
	{
		const Note note(TimePos(48), TimePos(0), NumKeys);
		QCOMPARE(note.key(), NumKeys - 1);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! Luna cluster 3914: position and length were each validated on their own, so a note
	//! could be written ending past the supported song range.
	void aNoteMayNotEndPastTheSongLimit()
	{
		freshClip();
		const QString note = control::noteIdOf(m_clip->notes()[0]);
		// The limit the check is against, read from the engine's own constant.
		QCOMPARE(static_cast<int>(MaxSongLength), 9999 * static_cast<int>(DefaultTicksPerBar));

		// (a) position and length together: each passes on its own, the sum does not.
		QCOMPARE(edit(note, {{QStringLiteral("position"), MaxSongLength}, {QStringLiteral("length"), 48}}),
			ControlErrorKind::InvalidArgs);
		// ...and nothing was written: the batch validates every entry before the first one.
		QCOMPARE(m_clip->notes()[0]->pos().getTicks(), 0);
		QCOMPARE(m_clip->notes()[0]->length().getTicks(), 48);

		// (b) a resize alone counts too: at the far end of the range, a longer note is refused.
		QCOMPARE(edit(note, {{QStringLiteral("position"), MaxSongLength - 48}}), ControlErrorKind::None);
		QCOMPARE(edit(note, {{QStringLiteral("length"), 96}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(m_clip->notes()[0]->length().getTicks(), 48);

		// (c) the limit itself is still reachable: ending exactly at MaxSongLength passes.
		QCOMPARE(edit(note, {{QStringLiteral("length"), 48}}), ControlErrorKind::None);
		QCOMPARE(m_clip->notes()[0]->pos().getTicks() + m_clip->notes()[0]->length().getTicks(),
			static_cast<int>(MaxSongLength));
	}

	void noteResizeMayNotEndPastTheSongLimit()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		m_clip->addNote(Note(TimePos(48), TimePos(48), 60), false);
		m_clip->rearrangeAllNotes();
		const QString note = control::noteIdOf(m_clip->notes()[0]);

		const ControlResult pastLimit = run(QStringLiteral("note.resize"),
			{{QStringLiteral("clip"), clip()}, {QStringLiteral("note"), note},
				{QStringLiteral("length"), MaxSongLength}});
		QCOMPARE(pastLimit.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(m_clip->notes()[0]->length().getTicks(), 48);

		const ControlResult atLimit = run(QStringLiteral("note.resize"),
			{{QStringLiteral("clip"), clip()}, {QStringLiteral("note"), note},
				{QStringLiteral("length"), MaxSongLength - 48}});
		QVERIFY(atLimit.ok);
		QCOMPARE(m_clip->notes()[0]->pos().getTicks() + m_clip->notes()[0]->length().getTicks(),
			static_cast<int>(MaxSongLength));
	}

	void duplicateEditsMustFitTheirCombinedFinalShape()
	{
		freshClip();
		Note* note = m_clip->notes()[0];
		note->setLength(TimePos(1));
		const QString id = control::noteIdOf(note);
		const int halfLimit = static_cast<int>(MaxSongLength / 2);
		const QJsonArray edits{
			QJsonObject{{QStringLiteral("note"), id}, {QStringLiteral("length"), halfLimit}},
			QJsonObject{{QStringLiteral("note"), id}, {QStringLiteral("position"), halfLimit + 1}}};

		const ControlResult result = run(QStringLiteral("note.edit"),
			{{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"), edits}});
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(note->pos().getTicks(), 0);
		QCOMPARE(note->length().getTicks(), 1);
	}

	//! new-p12-152: a saved quantise position with no quantise length was read as a one-tick
	//! length (qlen defaulted to "1"), so note.restore_timing moved the note to that position
	//! and shortened it to one tick. Both halves are required; a lone qpos restores nothing.
	void savedTimingWithoutLengthIsIgnored()
	{
		freshClip();
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("notes"));
		QDomElement element = Note(TimePos(48), TimePos(24), 60).saveState(doc, root);
		element.setAttribute(QStringLiteral("qpos"), 192);
		QVERIFY(!element.hasAttribute(QStringLiteral("qlen")));
		m_clip->notes()[0]->restoreState(element);

		const ControlResult restored = run(QStringLiteral("note.restore_timing"), {{QStringLiteral("clip"), clip()}});
		QVERIFY(restored.ok);
		QCOMPARE(restored.result.value(QStringLiteral("restored")).toInt(), 0);
		QCOMPARE(m_clip->notes()[0]->pos().getTicks(), 24);
		QCOMPARE(m_clip->notes()[0]->length().getTicks(), 48);
	}

	void noteEditOfValuesAlreadySetRecordsNoUndoStep()
	{
		// BUG 12 (cluster new-p15-22): the call that changed nothing took a MidiClip checkpoint, so
		// control.undo consumed its step and the real edit before it stayed.
		QVERIFY(setCoalescing(0));
		freshClip();
		const QString note = control::noteIdOf(m_clip->notes()[0]);
		const int original = velocity();
		QVERIFY(editVelocity(note, 90).ok);
		QCOMPARE(velocity(), 90);
		const ControlResult again = editVelocity(note, 90);
		QVERIFY(again.ok);
		QCOMPARE(again.result.value(QStringLiteral("edited")).toInt(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		// The undo reverts the real edit: the call that changed nothing left no step to consume.
		QCOMPARE(velocity(), original);
		QVERIFY(setCoalescing(control::UndoCoalesceWindowMs));
	}

	void aBatchThatSetsAValueBackChangesNothing()
	{
		// Two entries on one note that cancel out leave it as it was, so the batch takes no undo step.
		QVERIFY(setCoalescing(0));
		freshClip();
		const QString note = control::noteIdOf(m_clip->notes()[0]);
		const int original = velocity();
		QVERIFY(editVelocity(note, 90).ok);
		const ControlResult back = run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"),
			QJsonArray{QJsonObject{{QStringLiteral("note"), note}, {QStringLiteral("velocity"), 80}},
				QJsonObject{{QStringLiteral("note"), note}, {QStringLiteral("velocity"), 90}}}}});
		QVERIFY(back.ok);
		QCOMPARE(back.result.value(QStringLiteral("edited")).toInt(), 0);
		QCOMPARE(velocity(), 90);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(velocity(), original);
		QVERIFY(setCoalescing(control::UndoCoalesceWindowMs));
	}

	void aZeroTimbreOnAPlainNoteIsStillAChange()
	{
		// Timbre 0 is a value, not "no expression": setting it captures the note's MPE expression, so the
		// note changes although its timbre reads 0 as before. Guards the no-change check against
		// dropping such an edit.
		freshClip();
		QVERIFY(!m_clip->notes()[0]->hasMpeExpression());
		const ControlResult result = run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"),
			QJsonArray{QJsonObject{{QStringLiteral("note"), control::noteIdOf(m_clip->notes()[0])}, {QStringLiteral("timbre"), 0}}}}});
		QVERIFY(result.ok);
		QCOMPARE(result.result.value(QStringLiteral("edited")).toInt(), 1);
		QVERIFY(m_clip->notes()[0]->hasMpeExpression());
	}

	void aMicroPitchEditIsAChange()
	{
		// Q050-099 added note.edit's MPE "pitch" field; the no-change check must see it, or a pitch-only
		// batch is dropped as a no-op (found integrating Q050-099/Q050-100 onto train 16).
		freshClip();
		const ControlResult result = run(QStringLiteral("note.edit"), {{QStringLiteral("clip"), clip()}, {QStringLiteral("edits"),
			QJsonArray{QJsonObject{{QStringLiteral("note"), control::noteIdOf(m_clip->notes()[0])}, {QStringLiteral("pitch"), 25}}}}});
		QVERIFY(result.ok);
		QCOMPARE(result.result.value(QStringLiteral("edited")).toInt(), 1);
		QCOMPARE(m_clip->notes()[0]->mpePitchCents(), 25);
	}
};

QTEST_MAIN(NoteEditRangeTest)
#include "NoteEditRangeTest.moc"
