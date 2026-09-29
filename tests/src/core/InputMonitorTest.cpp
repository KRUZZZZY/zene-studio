/*
 * InputMonitorTest.cpp - R2.1: input monitoring - Off, Auto and In
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

/*! The plan's acceptance for R2.1, on the real engine: Engine::init(true) runs the Dummy
 *  device, which renders periods on its own thread, and the test is the synthetic capture
 *  feed - it pushes a constant 0.5 on the left input for the whole run. What is measured is
 *  the PCM the master channel's meter saw, plus the monitor gate's own frame count:
 *    * In always passes the input to the track's output;
 *    * Off never does (and costs nothing: the track has no monitor handle);
 *    * Auto passes while the track is armed, and not while it plays a clip;
 *    * an instrument track refuses Auto, and Off keeps live notes out;
 *    * the mode round-trips a save and is written only when not the default; one undo
 *      takes a change back.
 */

#include <QtTest>

#include <QDomDocument>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QTemporaryDir>

#include <vector>

#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InputMonitor.h"
#include "Mixer.h"
#include "ReversibilityTestSupport.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;
using namespace revtest;

namespace
{

//! Feeds the capture input for @a ms, then returns the master meter's peak over that time.
float feedAndMeasure(int ms)
{
	std::vector<SampleFrame> block(256, SampleFrame(0.5f, 0.0f));
	MixerChannel* master = Engine::mixer()->mixerChannel(0);
	master->m_peakLeft = 0.0f;
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < ms)
	{
		Engine::audioEngine()->pushInputFrames(block.data(), static_cast<f_cnt_t>(block.size()));
		QTest::qWait(5);
	}
	return master->m_peakLeft;
}

} // namespace

class InputMonitorTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theRuleIsLivesThreeStates()
	{
		QVERIFY(!monitorPasses(MonitorMode::Off, true, false));
		QVERIFY(monitorPasses(MonitorMode::In, false, true));
		QVERIFY(monitorPasses(MonitorMode::Auto, true, false));
		QVERIFY(!monitorPasses(MonitorMode::Auto, false, false));
		QVERIFY(!monitorPasses(MonitorMode::Auto, true, true));
	}

	void inPassesTheInputAndOffDoesNot()
	{
		const QString id = addTrack(QStringLiteral("sample"));
		ControlResult error;
		auto* track = dynamic_cast<SampleTrack*>(control::resolveTrack(id, &error));
		QVERIFY(track != nullptr);
		QCOMPARE(track->monitorMode(), MonitorMode::Off);
		QVERIFY(track->monitorHandle() == nullptr);
		const float off = feedAndMeasure(300);

		QVERIFY(run(QStringLiteral("track.set_monitor"), {{QStringLiteral("track"), id},
			{QStringLiteral("mode"), QStringLiteral("in")}}).ok);
		QVERIFY(track->monitorHandle() != nullptr);
		const float in = feedAndMeasure(300);
		std::printf("MONITOR_EVIDENCE off peak %.4f, in peak %.4f, gate frames %llu\n", off, in,
			static_cast<unsigned long long>(track->monitorHandle()->framesPassed()));
		QVERIFY2(off < 0.01f, qPrintable(QStringLiteral("input was heard with monitoring off: %1").arg(off)));
		QVERIFY2(in > 0.3f, qPrintable(QStringLiteral("monitoring in did not reach the master: %1").arg(in)));
		QVERIFY(track->monitorHandle()->framesPassed() > 0u);
		QCOMPARE(run(QStringLiteral("track.get_state"), {{QStringLiteral("track"), id}})
			.result.value(QStringLiteral("monitor")).toString(), QStringLiteral("in"));

		// One undo takes the change back - and removes the handle with it.
		REV_UNDO_OR_FAIL();
		QCOMPARE(track->monitorMode(), MonitorMode::Off);
		QVERIFY(track->monitorHandle() == nullptr);
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), id}}).ok);
	}

	void autoPassesOnlyWhileArmedAndNotPlayingAClip()
	{
		const QString id = addTrack(QStringLiteral("sample"));
		ControlResult error;
		auto* track = dynamic_cast<SampleTrack*>(control::resolveTrack(id, &error));
		QVERIFY(run(QStringLiteral("track.set_monitor"), {{QStringLiteral("track"), id},
			{QStringLiteral("mode"), QStringLiteral("auto")}}).ok);
		const std::uint64_t unarmed = passedDuring(track, 200);
		QCOMPARE(unarmed, std::uint64_t{0});

		const ControlResult armed = run(QStringLiteral("track.set_arm"), {{QStringLiteral("track"), id},
			{QStringLiteral("armed"), true}, {QStringLiteral("input_channel"), 0},
			{QStringLiteral("file"), m_dir.filePath(QStringLiteral("take.wav"))}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QVERIFY2(passedDuring(track, 200) > 0u, "an armed track in auto did not pass its input");

		track->setPlaying(true);
		QCOMPARE(passedDuring(track, 200), std::uint64_t{0});
		track->setPlaying(false);
		QVERIFY(run(QStringLiteral("track.set_arm"), {{QStringLiteral("track"), id},
			{QStringLiteral("armed"), false}}).ok);
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), id}}).ok);
	}

	void anInstrumentTrackTakesOffAndInOnly()
	{
		const QString id = addTrack(QStringLiteral("instrument"));
		QCOMPARE(run(QStringLiteral("track.get_state"), {{QStringLiteral("track"), id}})
			.result.value(QStringLiteral("monitor")).toString(), QStringLiteral("in"));
		QCOMPARE(run(QStringLiteral("track.set_monitor"), {{QStringLiteral("track"), id},
			{QStringLiteral("mode"), QStringLiteral("auto")}}).errorKind, ControlErrorKind::Refused);
		QVERIFY(run(QStringLiteral("track.set_monitor"), {{QStringLiteral("track"), id},
			{QStringLiteral("mode"), QStringLiteral("off")}}).ok);
	}

	void theModeIsSavedOnlyWhenNotTheDefault()
	{
		ControlResult error;
		auto* track = dynamic_cast<SampleTrack*>(control::resolveTrack(addTrack(QStringLiteral("sample")), &error));
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		doc.appendChild(root);
		// saveState appends the track's own element to the parent and returns it.
		const QDomElement plain = track->saveState(doc, root);
		QVERIFY(!plain.hasAttribute(QStringLiteral("monitor")));

		track->setMonitorMode(MonitorMode::Auto);
		const QDomElement changed = track->saveState(doc, root);
		QCOMPARE(changed.attribute(QStringLiteral("monitor")), QStringLiteral("1"));
		track->restoreState(plain);
		QCOMPARE(track->monitorMode(), MonitorMode::Off);
		track->restoreState(changed);
		QCOMPARE(track->monitorMode(), MonitorMode::Auto);
		QVERIFY(track->monitorHandle() != nullptr);
		track->setMonitorMode(MonitorMode::Off);
	}

private:
	static std::uint64_t passedDuring(const SampleTrack* track, int ms)
	{
		const std::uint64_t before = track->monitorHandle()->framesPassed();
		feedAndMeasure(ms);
		return track->monitorHandle()->framesPassed() - before;
	}

	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(InputMonitorTest)
#include "InputMonitorTest.moc"
