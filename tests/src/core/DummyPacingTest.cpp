/*
 * DummyPacingTest.cpp - the Dummy audio device keeps real time
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

/*! The headless Dummy device is the clock every harness test and every agent session without an
 *  audio interface plays by. It used to sleep "a period minus the render time" and lose each
 *  sleep's overshoot, so on a host with coarse sleep slack it fell far behind real time (the
 *  hosted macOS runner played two bars in 25 s). Here the device's own thread drives the song
 *  for two seconds of wall time and the song must have advanced at least 80 % of two seconds of
 *  ticks - a bound loose enough for a loaded runner and far tighter than the drift it guards. */

#include <QtTest>

#include <QElapsedTimer>

#include "AudioEngine.h"
#include "Engine.h"
#include "Song.h"

using namespace lmms;

class DummyPacingTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void theSongAdvancesInRealTime()
	{
		Song* song = Engine::getSong();
		song->playSong();
		QElapsedTimer wall;
		wall.start();
		const tick_t from = song->getPlayPos().getTicks();
		QTest::qWait(2000);
		const tick_t to = song->getPlayPos().getTicks();
		const double seconds = wall.elapsed() / 1000.0;
		song->stop();
		const double ticksPerSecond = song->getTempo() / 60.0 * DefaultTicksPerBar / 4.0;
		const double ratio = (to - from) / (ticksPerSecond * seconds);
		std::printf("DUMMY_PACING %.3f of real time over %.2f s\n", ratio, seconds);
		QVERIFY2(ratio >= 0.8, qPrintable(QStringLiteral("the Dummy played %1 of real time").arg(ratio)));
		QVERIFY2(ratio <= 1.2, qPrintable(QStringLiteral("the Dummy played %1 of real time - faster than real").arg(ratio)));
	}
};

QTEST_GUILESS_MAIN(DummyPacingTest)
#include "DummyPacingTest.moc"
