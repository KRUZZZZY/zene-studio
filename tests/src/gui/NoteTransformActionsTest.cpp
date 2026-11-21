/*
 * NoteTransformActionsTest.cpp - the piano roll's randomise, humanise and scale tools
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

/*! The three note-tools items over a real clip: Scale velocities multiplies the SELECTED notes
 *  only when a note is selected (the whole clip when none is), Randomize velocities changes
 *  velocities and not positions, Humanize timing changes positions within its bound and not
 *  velocities - each through its registry command, each one undo step. The value prompt is
 *  replaced by a fixed answer (setNoteTransformPrompt). */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteTransformActions.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* item(const QList<QAction*>& actions, const QString& text)
{
	for (QAction* action : actions)
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

std::vector<std::pair<int, int>> positionsAndVelocities(const MidiClip* clip)
{
	std::vector<std::pair<int, int>> out;
	for (const Note* note : clip->notes()) { out.emplace_back(note->pos().getTicks(), static_cast<int>(note->getVolume())); }
	return out;
}

//! One expression-stream point, the same {tick, value} shape the
//! note.expression_stream_set command reads. Built in a helper (not nested
//! inside the invoke call): moc misparses triply-nested closing braces here.
QJsonObject streamPoint(int tick, int value)
{
	return QJsonObject{{QStringLiteral("tick"), tick}, {QStringLiteral("value"), value}};
}

} // namespace

class NoteTransformActionsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		m_clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		for (int i = 0; i < 8; ++i) { m_clip->addNote(Note(TimePos(24), TimePos(i * 48), 60 + i, 100), false); }
	}

	void cleanupTestCase()
	{
		setNoteTransformPrompt({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void scaleActsOnTheSelectionOnly()
	{
		QObject owner;
		const QList<QAction*> actions = makeNoteTransformActions([this] { return m_clip; }, &owner);
		setNoteTransformPrompt([](const QString&, const QString&, double, double, double, int) { return 0.5; });
		m_clip->notes().front()->setSelected(true);
		item(actions, QObject::tr("Scale velocities..."))->trigger();
		const int scaled = static_cast<int>(m_clip->notes().front()->getVolume());
		const int untouched = static_cast<int>(m_clip->notes().back()->getVolume());
		for (Note* note : m_clip->notes()) { note->setSelected(false); }
		ControlRegistry::instance()->invoke(QStringLiteral("note.select"),
			{{QStringLiteral("clip"), control::clipIdOf(m_clip)}, {QStringLiteral("notes"), QJsonArray{}}});
		QCOMPARE(scaled, 50);
		QCOMPARE(untouched, 100);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QCOMPARE(static_cast<int>(m_clip->notes().front()->getVolume()), 100);
	}

	void randomizeMovesVelocitiesHumanizeMovesTime()
	{
		QObject owner;
		const QList<QAction*> actions = makeNoteTransformActions([this] { return m_clip; }, &owner);
		const auto before = positionsAndVelocities(m_clip);

		setNoteTransformPrompt([](const QString&, const QString&, double, double, double, int) { return 0.5; });
		item(actions, QObject::tr("Randomize velocities..."))->trigger();
		const auto rolled = positionsAndVelocities(m_clip);
		bool velocityMoved = false;
		for (std::size_t i = 0; i < before.size(); ++i)
		{
			QCOMPARE(rolled[i].first, before[i].first);
			velocityMoved = velocityMoved || rolled[i].second != before[i].second;
		}
		QVERIFY2(velocityMoved, "Randomize velocities changed no velocity");
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);

		setNoteTransformPrompt([](const QString&, const QString&, double, double, double, int) { return 6.0; });
		item(actions, QObject::tr("Humanize timing..."))->trigger();
		const auto humanized = positionsAndVelocities(m_clip);
		bool timeMoved = false;
		for (std::size_t i = 0; i < before.size(); ++i)
		{
			QCOMPARE(humanized[i].second, before[i].second);
			QVERIFY(std::abs(humanized[i].first - before[i].first) <= 6);
			timeMoved = timeMoved || humanized[i].first != before[i].first;
		}
		QVERIFY2(timeMoved, "Humanize timing moved no note");
	}

// The prompt showed the command's own argument name - 'velocity_jitter', 'position_jitter' - as
//	the label the user reads. The label is what the item is called instead.
	void thePromptNamesTheToolNotItsArgument()
	{
		QObject owner;
		const QList<QAction*> actions = makeNoteTransformActions([this] { return m_clip; }, &owner);
		const auto labelFor = [&actions](const QString& text) {
			QString label;
			setNoteTransformPrompt([&label](const QString&, const QString& shown, double, double, double, int) {
				label = shown;
				return 0.5;
			});
			QAction* action = item(actions, text);
			if (action != nullptr) { action->trigger(); }
			return label;
		};
		for (const QString& text : {QString(QObject::tr("Randomize velocities...")),
			 QString(QObject::tr("Humanize timing...")), QString(QObject::tr("Scale velocities..."))})
		{
			const QString label = labelFor(text);
			QVERIFY2(!label.isEmpty(), qPrintable(text));
			QVERIFY2(!label.contains(QLatin1Char('_')), qPrintable(QStringLiteral("%1 prompted '%2'")
				.arg(text, label)));
		}
		setNoteTransformPrompt({});
	}

// The exported helper walked clip->notes() with no null check, so a caller with no clip read
//	through a null pointer before it could return false.
	void noClipHandsOverNothing()
	{
		QVERIFY(!handOverNoteSelection(nullptr));
	}

// Q050-016: the piano roll's GUI path to the expression streams. The ramp item asks for the
//	bend at the note's end and writes a {0:0, len:value} pitch stream through the SHARED
//	note.expression_stream_set command (one undo step per note), keeping the other axes; the
//	clear item empties every axis of the notes that carry a stream. The value prompt is
//	replaced by a fixed answer (setNoteTransformPrompt), like the transform items above.
	void mpePitchRampWritesStreamsThroughTheRegistry()
	{
		QObject owner;
		const QList<QAction*> actions = makeNoteTransformActions([this] { return m_clip; }, &owner);
		// Fresh pointers every time: control.undo restores the clip by
		// re-loading its note list, which deletes and recreates every Note -
		// a pointer held across an undo (or any checkpointed command) dangles.
		auto noteAt = [this](int i) { return m_clip->notes().at(i); };
		noteAt(0)->setSelected(true);
		const int end = noteAt(0)->length().getTicks();
		const QString firstId = control::noteIdOf(noteAt(0));
		const QString lastId = control::noteIdOf(noteAt(7));

		// A pressure stream the ramp must keep (the set call omits that axis).
		const QJsonArray keptPressure{streamPoint(0, 64)};
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("note.expression_stream_set"),
			{{QStringLiteral("clip"), control::clipIdOf(m_clip)},
				{QStringLiteral("note"), firstId},
				{QStringLiteral("pressure"), keptPressure}}).ok);

		setNoteTransformPrompt([](const QString&, const QString&, double, double, double, int) { return 1200.0; });
		item(actions, QObject::tr("MPE pitch ramp..."))->trigger();

		QVERIFY(noteAt(0)->hasMpeStream());
		const auto pitch = noteAt(0)->mpeStream().pitch();
		QCOMPARE(static_cast<int>(pitch.size()), 2);
		QCOMPARE(pitch[0].tick, tick_t(0));
		QCOMPARE(pitch[0].value, 0);
		QCOMPARE(pitch[1].tick, tick_t(end));
		QCOMPARE(pitch[1].value, 1200);
		QCOMPARE(noteAt(0)->mpeStream().pressure()[0].value, 64);
		// The unselected note carries nothing.
		QVERIFY(!noteAt(7)->hasMpeStream());
		QCOMPARE(control::noteIdOf(noteAt(7)), lastId);

		for (Note* note : m_clip->notes()) { note->setSelected(false); }
		setNoteTransformPrompt({});
		// Two writes happened (the fixture set, the ramp): one undo takes the ramp back.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QCOMPARE(static_cast<int>(noteAt(0)->mpeStream().pitch().size()), 0);
		QCOMPARE(noteAt(0)->mpeStream().pressure()[0].value, 64);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QVERIFY(!noteAt(0)->hasMpeStream());
	}

	void clearMpeStreamsRemovesStreamsThroughTheRegistry()
	{
		QObject owner;
		const QList<QAction*> actions = makeNoteTransformActions([this] { return m_clip; }, &owner);
		// Fresh pointers every time (see the ramp slot above): an undo
		// recreates every Note, so a held pointer dangles.
		auto noteAt = [this](int i) { return m_clip->notes().at(i); };
		const QJsonArray onePitch{streamPoint(0, 100)};
		// Two explicit calls (no loop over them): moc misparses the nested
		// invoke calls inside a loop body in this file, and unrolled calls
		// read the same.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("note.expression_stream_set"),
			{{QStringLiteral("clip"), control::clipIdOf(m_clip)},
				{QStringLiteral("note"), control::noteIdOf(noteAt(0))},
				{QStringLiteral("pitch"), onePitch}}).ok);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("note.expression_stream_set"),
			{{QStringLiteral("clip"), control::clipIdOf(m_clip)},
				{QStringLiteral("note"), control::noteIdOf(noteAt(1))},
				{QStringLiteral("pitch"), onePitch}}).ok);

		// No selection: every note is in scope, but only streamed notes change.
		item(actions, QObject::tr("Clear MPE expression streams"))->trigger();
		QVERIFY(!noteAt(0)->hasMpeStream());
		QVERIFY(!noteAt(1)->hasMpeStream());
		QCOMPARE(ControlRegistry::instance()->lastTransaction()->command,
			QStringLiteral("note.expression_stream_set"));

		// One undo step per note: the last clear comes back first.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QVERIFY(!noteAt(0)->hasMpeStream());
		QVERIFY(noteAt(1)->hasMpeStream());
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QVERIFY(noteAt(0)->hasMpeStream());
		QVERIFY(noteAt(1)->hasMpeStream());

		// A clear over notes that carry no stream invokes nothing, so it
		// pushes no undo step: the last transaction is unchanged.
		noteAt(2)->setSelected(true);
		const QString before = ControlRegistry::instance()->lastTransaction()->command;
		item(actions, QObject::tr("Clear MPE expression streams"))->trigger();
		QCOMPARE(ControlRegistry::instance()->lastTransaction()->command, before);
		noteAt(2)->setSelected(false);

		// Leave the journal as found: the two fixture sets above are still on
		// it, so take them back too.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo"), QJsonObject{}).ok);
		QVERIFY(!noteAt(0)->hasMpeStream());
		QVERIFY(!noteAt(1)->hasMpeStream());
	}

private:
	MidiClip* m_clip = nullptr;
};

QTEST_MAIN(NoteTransformActionsTest)
#include "NoteTransformActionsTest.moc"
