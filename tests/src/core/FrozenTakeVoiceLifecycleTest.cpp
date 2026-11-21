/*
 * FrozenTakeVoiceLifecycleTest.cpp - picking up a frozen-take voice ends it in
 *                                     mute, unfreeze and take replacement
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

// Cluster new-p12-285: a frozen take's pooled voice is armed once and then renders on its own. Muting the
// track, unfreezing it, or replacing the take with another one must also end a voice that is ALREADY
// rendering: the pass latch and the voice are separate state, and only the latch is cleared today.
//
// Every slot starts a real take pass, confirms that it is audible, changes the state, and renders a fixed
// number of periods - no sleeps or wall-clock waits inside the assertions.

#include <QtTest>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <span>

#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "AudioDevice.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "PlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLaneTestSupport.h"
#include "Timeline.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

double rms(std::span<const SampleFrame> period)
{
	double sum = 0.0;
	for (const SampleFrame& frame : period) { sum += frame[0] * frame[0] + frame[1] * frame[1]; }
	return period.empty() ? 0.0 : std::sqrt(sum / (2.0 * static_cast<double>(period.size())));
}

//! A sample track carrying `wav` as its frozen take over ticks [0, 1920), the take the handoff family uses.
QString prepareFrozenTake(QTemporaryDir& scratch, const QString& name, double frequency, SampleTrack** track,
	QString* path)
{
	const QString wav = taketest::writeToneWav(scratch.filePath(name), frequency);
	if (wav.isEmpty()) { return QStringLiteral("could not write a frozen-take fixture"); }
	Song* const song = Engine::getSong();
	song->clearProject();
	Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
	timeline.setLoopEnabled(false);
	timeline.setLoopPoints(TimePos(0), TimePos(DefaultTicksPerBar));
	if (!run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok)
	{
		return QStringLiteral("could not add the sample track");
	}
	*track = dynamic_cast<SampleTrack*>(song->tracks().back());
	if (*track == nullptr) { return QStringLiteral("track.add returned a non-sample track"); }
	QString reason;
	if (!(*track)->freezeTo(wav, 0, 1920, {}, &reason)) { return reason; }
	if (path != nullptr) { *path = wav; }
	return {};
}

//! Starts one real pass through the take and returns the louder of the two periods it can sound in.
double startPassAndMeasure(SampleTrack* track)
{
	AudioEngine* const audio = Engine::audioEngine();
	if (!track->play(TimePos(0), audio->framesPerPeriod(), 0, -1)) { return -1.0; }
	const double first = rms(audio->renderNextPeriod());
	const double second = rms(audio->renderNextPeriod());
	return std::max(first, second);
}

//! The master buffer lags one render period, so the first period after the change still carries the
//! pre-change audio. Discard exactly that one, then report the loudest of the two periods that follow.
double loudestAfterOneLagPeriod()
{
	AudioEngine* const audio = Engine::audioEngine();
	audio->renderNextPeriod();
	const double first = rms(audio->renderNextPeriod());
	const double second = rms(audio->renderNextPeriod());
	return std::max(first, second);
}

void stopVoices(SampleTrack* track)
{
	Engine::audioEngine()->removePlayHandlesOfTypes(track, PlayHandle::Type::SamplePlayHandle);
	QTest::qWait(130); // let the control-thread recycler return the finished slots
}

} // namespace

class FrozenTakeVoiceLifecycleTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		AudioEngineWorkerThread::setDeterministicProcessing(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		AudioEngineWorkerThread::setDeterministicProcessing(false);
		Engine::destroy();
	}

	//! Muting the track after a pass started must silence the voice that pass already armed.
	void muteStopsAnActiveFrozenVoice()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = nullptr;
		const QString error = prepareFrozenTake(scratch, QStringLiteral("take.wav"), 440.0, &track, nullptr);
		QVERIFY2(error.isEmpty(), qPrintable(error));

		const double audible = startPassAndMeasure(track);
		QVERIFY2(audible > 0.01, qPrintable(QStringLiteral("the take was not audible before the mute: %1").arg(audible)));

		QVERIFY(run(QStringLiteral("track.set_mute"),
			{{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("muted"), true}}).ok);
		QVERIFY(track->isMuted());

		const double level = loudestAfterOneLagPeriod();
		QVERIFY2(level < 0.001,
			qPrintable(QStringLiteral("a voice armed before the mute kept rendering the take: %1").arg(level)));

		stopVoices(track);
	}

	//! Unfreezing after a pass started must silence the voice that pass already armed.
	void unfreezeStopsAnActiveFrozenVoice()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = nullptr;
		const QString error = prepareFrozenTake(scratch, QStringLiteral("take.wav"), 440.0, &track, nullptr);
		QVERIFY2(error.isEmpty(), qPrintable(error));

		const double audible = startPassAndMeasure(track);
		QVERIFY2(audible > 0.01, qPrintable(QStringLiteral("the take was not audible before the unfreeze: %1").arg(audible)));

		QVERIFY(run(QStringLiteral("freeze.unfreeze"), {{QStringLiteral("track"), control::trackIdOf(track)}}).ok);
		QVERIFY(!track->isFrozen());

		const double level = loudestAfterOneLagPeriod();
		QVERIFY2(level < 0.001,
			qPrintable(QStringLiteral("a voice armed before the unfreeze kept rendering the take: %1").arg(level)));

		stopVoices(track);
	}

	//! Freezing a second take while the first voice renders must end that voice, not layer the old one under it.
	void replacingTheTakeStopsAnActiveFrozenVoice()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = nullptr;
		QString firstWav;
		const QString error = prepareFrozenTake(scratch, QStringLiteral("take.wav"), 440.0, &track, &firstWav);
		QVERIFY2(error.isEmpty(), qPrintable(error));

		const double audible = startPassAndMeasure(track);
		QVERIFY2(audible > 0.01, qPrintable(QStringLiteral("the take was not audible before the replacement: %1").arg(audible)));

		const QString secondWav = taketest::writeToneWav(scratch.filePath(QStringLiteral("take-b.wav")), 660.0);
		QVERIFY(!secondWav.isEmpty());
		QString reason;
		QVERIFY2(track->freezeTo(secondWav, 0, 1920, {}, &reason), qPrintable(reason));
		QVERIFY(track->isFrozen());

		const double level = loudestAfterOneLagPeriod();
		QVERIFY2(level < 0.001,
			qPrintable(QStringLiteral("the voice armed for the replaced take kept rendering: %1").arg(level)));

		stopVoices(track);
	}
};

QTEST_GUILESS_MAIN(FrozenTakeVoiceLifecycleTest)
#include "FrozenTakeVoiceLifecycleTest.moc"
