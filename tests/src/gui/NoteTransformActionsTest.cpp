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

private:
	MidiClip* m_clip = nullptr;
};

QTEST_MAIN(NoteTransformActionsTest)
#include "NoteTransformActionsTest.moc"
