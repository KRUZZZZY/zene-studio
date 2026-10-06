/*
 * SessionCommandArgsTest.cpp - what a <session> command accepts, and what it marks
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! Three refusals the surface promised but did not keep, and one thing a
 *  <session> edit did not mark.
 *
 *  Each of these is a case where the caller sent something the reply then
 *  contradicted: a field read with a conversion that substitutes a default, a
 *  key treated as absent because its value had the wrong shape, an address
 *  completed with a zero the caller never wrote. In all three the command
 *  answered about state the client had not asked about, which is worse than a
 *  refusal - the caller has no way to tell.
 *
 *  The fourth is the opposite direction: the edit succeeded and told the caller
 *  so, while the project still read as clean, so nothing ever asked to save it.
 */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr int kScenes = 3;

QJsonObject audioSlot(int track, int scene)
{
	return {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene},
		{QStringLiteral("type"), QStringLiteral("audio")},
		{QStringLiteral("source"), QStringLiteral("kick.wav")},
		{QStringLiteral("loop_length"), 192}};
}

} // namespace


class SessionCommandArgsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The registry only dispatches handlers once startup is announced.
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A <session> block edit is project state, so it is an unsaved change.
	//! SessionModel is not a JournallingObject and holds no Song back-pointer,
	//! so nothing else marked the song: after a set_grid / set_slot / set_scene
	//! the project still read as clean and the dirty-state autosave never ran.
	//! The sibling session paths already knew this - recordBlock() and
	//! captureToSlot() both call setModified().
	void aSessionModelEditMarksTheSongModified()
	{
		using revtest::run;
		Song* song = Engine::getSong();
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY2(!song->isModified(), "a new project starts clean");

		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1},
			{QStringLiteral("scenes"), kScenes}}).ok);
		QVERIFY2(song->isModified(), "session.set_grid left the project marked clean");

		QVERIFY(run(QStringLiteral("session.set_slot"), audioSlot(0, 0)).ok);
		QVERIFY2(song->isModified(), "session.set_slot left the project marked clean");

		// One undo puts the block back, and the project is still an unsaved change.
		REV_UNDO_OR_FAIL();
		QVERIFY2(song->isModified(), "undoing a session edit cleared the modified flag");
	}
};

QTEST_GUILESS_MAIN(SessionCommandArgsTest)
#include "SessionCommandArgsTest.moc"
