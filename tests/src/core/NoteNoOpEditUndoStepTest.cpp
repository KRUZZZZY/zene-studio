/*
 * NoteNoOpEditUndoStepTest.cpp - a note edit that changes NOTHING takes no undo step
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

/*! The no-op rule for the note edits (Luna cluster new-p14-41).
 *
 *  An edit that changes NOTHING must not push an undo step. A step that undoes
 *  nothing sits ON TOP of the real edit behind it, so the next control.undo
 *  spends itself on it and the edit the caller meant to reverse stays applied -
 *  and the project is marked modified although no note state changed.
 *
 *  The rule the codebase already follows elsewhere (note.transpose,
 *  note.velocity_offset, note.velocity_scale, and note.slide_clear - Luna
 *  cluster 3904) is that the command answers "does this edit change anything?"
 *  BEFORE the edit runs, and takes the journal checkpoint only when the answer
 *  is yes. The note.shape family (note.scale, note.transform_where) and
 *  note.slide_set took the checkpoint first and asked nothing, so a no-op left a
 *  step behind.
 *
 *  What this file proves, for each of those commands:
 *
 *   - the no-op call takes NO undo step (the journal depth is unchanged);
 *   - it records NO transaction (the control surface does not claim a change);
 *   - it does not dirty the project (the song's modified flag stays down);
 *   - the notes themselves are untouched;
 *   - and the NEGATIVE CONTROL: the same command with a real change still takes
 *     exactly one step, and one undo puts the notes back.
 */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The clip's notes as "key@position+length" rows, so a change (or the absence
//! of one) is visible in a failure message.
QStringList shape(const MidiClip& clip)
{
	QStringList rows;
	for (const Note* note : clip.notes())
	{
		rows << QStringLiteral("%1@%2+%3").arg(note->key()).arg(note->pos().getTicks())
			.arg(note->length().getTicks());
	}
	return rows;
}

//! How many transactions the control surface has recorded.
int recordedTransactions()
{
	return run(QStringLiteral("control.transactions")).result
		.value(QStringLiteral("transactions")).toArray().size();
}

} // namespace

class NoteNoOpEditUndoStepTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;

	QString clip() const { return control::clipIdOf(m_clip); }
	QString firstNote() const { return control::noteIdOf(m_clip->notes().front()); }

	//! Two notes at distinct keys, positions and velocities.
	void freshClip()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		m_clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		m_clip->addNote(Note(TimePos(12), TimePos(48), 61, 40), false);
		m_clip->rearrangeAllNotes();
	}

	//! The project's modified flag, cleared: a no-op edit must not dirty it.
	void clearModifiedFlag()
	{
		Engine::getSong()->noteProjectSaved(QString());
		QVERIFY(!Engine::getSong()->isModified());
	}

	//! The engine's undo stack - the thing a no-op edit must leave alone.
	ProjectJournal* journal() { return Engine::projectJournal(); }

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! note.scale with the DEFAULT factors (1.0 for both) moves nothing: it must
	//! take no step, record no transaction and leave the project clean.
	void anIdentityScaleTakesNoUndoStep()
	{
		freshClip();
		const QStringList before = shape(*m_clip);
		ProjectJournal* project = journal();
		QVERIFY(project != nullptr);
		const int depthBefore = project->undoDepth();
		const int transactionsBefore = recordedTransactions();
		clearModifiedFlag();

		const ControlResult scaled = run(QStringLiteral("note.scale"),
			QJsonObject{{QStringLiteral("clip"), clip()}});
		QVERIFY2(scaled.ok, qPrintable(scaled.errorMessage));
		QCOMPARE(scaled.result.value(QStringLiteral("scaled")).toInt(), 2);
		QCOMPARE(project->undoDepth(), depthBefore);
		QCOMPARE(recordedTransactions(), transactionsBefore);
		QVERIFY2(!Engine::getSong()->isModified(),
			"a scale that changed nothing marked the project modified");
		QCOMPARE(shape(*m_clip), before);
	}

	//! The same for an EXPLICIT factor of 1.0 - the default is not the only
	//! spelling of "do not move anything".
	void anExplicitUnitScaleTakesNoUndoStep()
	{
		freshClip();
		const QStringList before = shape(*m_clip);
		ProjectJournal* project = journal();
		QVERIFY(project != nullptr);
		const int depthBefore = project->undoDepth();
		clearModifiedFlag();

		const ControlResult scaled = run(QStringLiteral("note.scale"),
			QJsonObject{{QStringLiteral("clip"), clip()}, {QStringLiteral("time_factor"), 1.0},
				{QStringLiteral("length_factor"), 1.0}});
		QVERIFY2(scaled.ok, qPrintable(scaled.errorMessage));
		QCOMPARE(project->undoDepth(), depthBefore);
		QVERIFY2(!Engine::getSong()->isModified(),
			"a scale that changed nothing marked the project modified");
		QCOMPARE(shape(*m_clip), before);
	}

	//! note.transform_where with an action that changes nothing: a transpose of
	//! zero semitones leaves every key where it was.
	void aTransposeOfZeroTakesNoUndoStep()
	{
		freshClip();
		const QStringList before = shape(*m_clip);
		ProjectJournal* project = journal();
		QVERIFY(project != nullptr);
		const int depthBefore = project->undoDepth();
		clearModifiedFlag();

		const ControlResult transformed = run(QStringLiteral("note.transform_where"),
			QJsonObject{{QStringLiteral("clip"), clip()}, {QStringLiteral("filter"), QJsonObject{}},
				{QStringLiteral("action"), QStringLiteral("transpose")}, {QStringLiteral("amount"), 0}});
		QVERIFY2(transformed.ok, qPrintable(transformed.errorMessage));
		QCOMPARE(transformed.result.value(QStringLiteral("matched")).toInt(), 2);
		QCOMPARE(project->undoDepth(), depthBefore);
		QVERIFY2(!Engine::getSong()->isModified(),
			"a transform that changed nothing marked the project modified");
		QCOMPARE(shape(*m_clip), before);
	}

	//! Muting a note that is ALREADY muted changes nothing.
	void mutingAnAlreadyMutedNoteTakesNoUndoStep()
	{
		freshClip();
		m_clip->notes().front()->setMuted(true);
		const QStringList before = shape(*m_clip);
		ProjectJournal* project = journal();
		QVERIFY(project != nullptr);
		const int depthBefore = project->undoDepth();
		clearModifiedFlag();

		const ControlResult muted = run(QStringLiteral("note.transform_where"),
			QJsonObject{{QStringLiteral("clip"), clip()},
				{QStringLiteral("filter"), QJsonObject{{QStringLiteral("key_min"), 60},
					{QStringLiteral("key_max"), 60}}},
				{QStringLiteral("action"), QStringLiteral("mute")}});
		QVERIFY2(muted.ok, qPrintable(muted.errorMessage));
		QCOMPARE(muted.result.value(QStringLiteral("matched")).toInt(), 1);
		QCOMPARE(project->undoDepth(), depthBefore);
		QVERIFY2(!Engine::getSong()->isModified(),
			"a transform that changed nothing marked the project modified");
		QCOMPARE(shape(*m_clip), before);
	}

	//! note.slide_set with the flag the note ALREADY carries changes nothing.
	void settingTheSlideFlagItAlreadyHasTakesNoUndoStep()
	{
		freshClip();
		ProjectJournal* project = journal();
		QVERIFY(project != nullptr);
		const int depthBefore = project->undoDepth();
		clearModifiedFlag();

		// The default flag is off: setting it off again changes nothing.
		const ControlResult alreadyOff = run(QStringLiteral("note.slide_set"),
			QJsonObject{{QStringLiteral("clip"), clip()}, {QStringLiteral("note"), firstNote()},
				{QStringLiteral("slide"), false}});
		QVERIFY2(alreadyOff.ok, qPrintable(alreadyOff.errorMessage));
		QCOMPARE(alreadyOff.result.value(QStringLiteral("slide")).toBool(), false);
		QCOMPARE(project->undoDepth(), depthBefore);

		// And the other way round: a slide note set to slide again.
		m_clip->notes().front()->setSlide(true);
		const ControlResult alreadyOn = run(QStringLiteral("note.slide_set"),
			QJsonObject{{QStringLiteral("clip"), clip()}, {QStringLiteral("note"), firstNote()},
				{QStringLiteral("slide"), true}});
		QVERIFY2(alreadyOn.ok, qPrintable(alreadyOn.errorMessage));
		QCOMPARE(alreadyOn.result.value(QStringLiteral("slide")).toBool(), true);
		QCOMPARE(project->undoDepth(), depthBefore);
		QVERIFY2(!Engine::getSong()->isModified(),
			"a slide flag that was already set marked the project modified");
	}

	//! The NEGATIVE CONTROL: a real change still takes exactly ONE step, and one
	//! undo puts the notes back. The no-op rule must not cost the real edits
	//! their undo step.
	void aRealChangeStillTakesExactlyOneUndoStep()
	{
		freshClip();
		const QStringList before = shape(*m_clip);
		ProjectJournal* project = journal();
		QVERIFY(project != nullptr);
		const int depthBefore = project->undoDepth();

		const ControlResult scaled = run(QStringLiteral("note.scale"),
			QJsonObject{{QStringLiteral("clip"), clip()}, {QStringLiteral("time_factor"), 2.0}});
		QVERIFY2(scaled.ok, qPrintable(scaled.errorMessage));
		QCOMPARE(project->undoDepth(), depthBefore + 1);
		QVERIFY(Engine::getSong()->isModified());
		QVERIFY2(shape(*m_clip) != before, "the scale did not move anything");

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(shape(*m_clip), before);
	}

	//! The same negative control for the other two commands of the family.
	void aRealTransformAndSlideStillTakeOneUndoStepEach()
	{
		freshClip();
		const QStringList before = shape(*m_clip);
		ProjectJournal* project = journal();
		QVERIFY(project != nullptr);
		const int depthBefore = project->undoDepth();

		const ControlResult transformed = run(QStringLiteral("note.transform_where"),
			QJsonObject{{QStringLiteral("clip"), clip()}, {QStringLiteral("filter"), QJsonObject{}},
				{QStringLiteral("action"), QStringLiteral("transpose")}, {QStringLiteral("amount"), 12}});
		QVERIFY2(transformed.ok, qPrintable(transformed.errorMessage));
		QCOMPARE(project->undoDepth(), depthBefore + 1);

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(shape(*m_clip), before);

		const ControlResult slid = run(QStringLiteral("note.slide_set"),
			QJsonObject{{QStringLiteral("clip"), clip()}, {QStringLiteral("note"), firstNote()},
				{QStringLiteral("slide"), true}});
		QVERIFY2(slid.ok, qPrintable(slid.errorMessage));
		QCOMPARE(project->undoDepth(), depthBefore + 1);
		QVERIFY(m_clip->notes().front()->slide());

		const ControlResult undoneAgain = run(QStringLiteral("control.undo"));
		QVERIFY2(undoneAgain.ok, qPrintable(undoneAgain.errorMessage));
		QVERIFY(!m_clip->notes().front()->slide());
	}
};

QTEST_MAIN(NoteNoOpEditUndoStepTest)

#include "NoteNoOpEditUndoStepTest.moc"
