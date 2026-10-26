/*
 * ControlTrackRouteNoSongTest.cpp - the track lookups with no song container
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

/*! The record-arm query and the track resolver both read Engine::getSong(). With no song
 *  (the state after Engine::destroy(), which nulls the song and the audio engine) the arm
 *  query dereferenced it unguarded in trackRouteOf, and the resolver did the same. The
 *  documented contract is "not armed" and "no such track", both typed, never a crash.
 *  Engine::destroy() is the only headless way to reach a song-less engine, so each test
 *  that needs it drops the engine itself and the cleanup tolerates an engine already gone. */

#include <QtTest>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"

using namespace lmms;

class ControlTrackRouteNoSongTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		dropEngine();
	}

	//! Normal path, live song: an id naming no track is typed NotFound, and no track is armed.
	void liveSongUnknownTrackIsNotFoundAndNotArmed()
	{
		QVERIFY(Engine::getSong() != nullptr);
		ControlResult error;
		QVERIFY(control::resolveTrack(QStringLiteral("trk-999999"), &error) == nullptr);
		QVERIFY(!error.ok);
		QCOMPARE(error.errorKind, ControlErrorKind::NotFound);
		QVERIFY(!control::trackRecordArmed(nullptr));
	}

	//! No song: the arm query reports "not armed" for any track and does not dereference the song.
	void armQueryWithNoSongReportsNotArmed()
	{
		dropEngine();
		QVERIFY(Engine::getSong() == nullptr);
		QVERIFY(!control::trackRecordArmed(nullptr));
	}

	//! No song: a well-formed track id resolves to no track, with a typed error and no crash.
	void resolveTrackWithNoSongFailsTyped()
	{
		dropEngine();
		QVERIFY(Engine::getSong() == nullptr);
		ControlResult error;
		QVERIFY(control::resolveTrack(QStringLiteral("trk-1"), &error) == nullptr);
		QVERIFY(!error.ok);
		QCOMPARE(error.errorKind, ControlErrorKind::NotFound);
	}

private:

	//! Destroys the engine only while it is still alive: Engine::destroy() twice is not safe.
	static void dropEngine()
	{
		if (Engine::getSong() != nullptr) { Engine::destroy(); }
	}
};

QTEST_GUILESS_MAIN(ControlTrackRouteNoSongTest)

#include "ControlTrackRouteNoSongTest.moc"
