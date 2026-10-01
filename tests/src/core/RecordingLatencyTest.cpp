/*
 * RecordingLatencyTest.cpp - R2.2: a recorded take lands where it was played
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

/*! The plan's acceptance for R2.2: "a loopback fixture: the recorded transient lands within 1
 *  sample of the played transient". The fixture is the Dummy device's loopback
 *  (include/AudioDummy.h): its output is fed back as its input after exactly 200 + 300
 *  frames, the output and input latency it then reports. One track plays a clip with a single
 *  impulse; another records through the loopback; the take's impulse must sit at the frame
 *  the played one sits at. Before R2.2 it sat one engine period plus the round trip late.
 */

#include <QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t kOutputLatency = 200;
constexpr f_cnt_t kInputLatency = 300;
constexpr std::size_t kImpulseAt = 4410;

//! The first frame of @a clip's audio above half scale, or -1.
long long firstTransient(SampleClip* clip)
{
	const auto buffer = clip->sample().buffer();
	for (std::size_t i = 0; i < buffer->size(); ++i)
	{
		if (std::fabs(buffer->data()[i][0]) > 0.5f) { return static_cast<long long>(i); }
	}
	return -1;
}

} // namespace

class RecordingLatencyTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		qputenv("LMMS_DUMMY_LOOPBACK", "1");
		qputenv("LMMS_DUMMY_OUTPUT_LATENCY", QByteArray::number(static_cast<qulonglong>(kOutputLatency)));
		qputenv("LMMS_DUMMY_INPUT_LATENCY", QByteArray::number(static_cast<qulonglong>(kInputLatency)));
		Engine::init(true);
	}

	void cleanupTestCase() { Engine::destroy(); }

	void theEngineReportsTheRoundTrip()
	{
		AudioEngine* engine = Engine::audioEngine();
		QCOMPARE(engine->audioDev()->outputLatencyFrames(), kOutputLatency);
		QCOMPARE(engine->audioDev()->inputLatencyFrames(), kInputLatency);
		// Device out + in, one capture push (the Dummy pushes an engine period), and one
		// period of the engine's output double buffer.
		QCOMPARE(engine->recordingLatencyFrames(), kOutputLatency + kInputLatency + 2 * engine->framesPerPeriod());
	}

	void aTakeRecordedThroughTheLoopbackLandsOnThePlayedTransient() { recordAndCompare(TimePos(0)); }

	//! A clip that starts on a tick inside an engine period is handed its play handle with a
	//! frame offset (SampleTrack::play -> setOffset). The take must begin at that offset too:
	//! it used to record the whole period from frame 0 and came out late by the offset
	//! (measured +139 - tick 1 is frame 394 at 44.1 kHz and the default 140 BPM, 138 frames
	//! into the second 256-frame period). BUGS_FOUND 11.17.
	void aTakeStartingInsideAPeriodLandsOnThePlayedTransient() { recordAndCompare(TimePos(1)); }

	//! A take armed on the device's FIRST period: the capture has delivered nothing yet,
	//! so that period's input is absent, not late. Under load the other cases land here by
	//! chance (the Dummy thread had rendered nothing when recording began) and came out one
	//! period early - "-255 frames" on hosted macos-x86_64 and locally 2 of 15 under CPU
	//! load. Here the device is stopped, the take armed, and the device started: every run
	//! is that case. BUGS_FOUND 11.29.
	void aTakeArmedOnTheDevicesFirstPeriodLandsOnThePlayedTransient()
	{
		recordAndCompare(TimePos(0), true);
	}

private:
	void recordAndCompare(const TimePos& at, bool fromDeviceStart = false)
	{
		Song* song = Engine::getSong();
		const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		auto* player = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		auto* recorder = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		QVERIFY(player != nullptr && recorder != nullptr);

		std::vector<SampleFrame> data(static_cast<std::size_t>(rate), SampleFrame(0.0f, 0.0f));
		data[kImpulseAt] = SampleFrame(1.0f, 1.0f);
		auto* played = dynamic_cast<SampleClip*>(player->createClip(at));
		played->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
		auto* take = dynamic_cast<SampleClip*>(recorder->createClip(at));
		take->changeLength(played->length());
		take->setAutoResize(false);
		take->setRecord(true);

		if (fromDeviceStart)
		{
			Engine::audioEngine()->audioDev()->stopProcessing();
			// The stopped device's last fed-back block is still staged; one render with
			// the song stopped drains it, so the started device's first period has no
			// input at all - exactly the engine's very first period after Engine::init.
			Engine::audioEngine()->renderNextPeriod();
			song->playAndRecord();
			Engine::audioEngine()->audioDev()->startProcessing();
		}
		else { song->playAndRecord(); }
		QTest::qWait(700);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!take->isRecord(), 3000);
		// Out of the next case's loopback: a muted track schedules none of its clips.
		player->setMuted(true);
		recorder->setMuted(true);

		const long long playedAt = firstTransient(played);
		const long long recordedAt = firstTransient(take);
		std::printf("LATENCY_EVIDENCE at tick %d: played %lld, recorded %lld, round trip %d\n", at.getTicks(),
			playedAt, recordedAt, static_cast<int>(Engine::audioEngine()->recordingLatencyFrames()));
		QCOMPARE(playedAt, static_cast<long long>(kImpulseAt));
		QVERIFY2(recordedAt >= 0, "the take recorded no transient");
		QVERIFY2(std::llabs(recordedAt - playedAt) <= 1,
			qPrintable(QStringLiteral("the take's transient is %1 frames from the played one").arg(recordedAt - playedAt)));
	}
};

QTEST_GUILESS_MAIN(RecordingLatencyTest)
#include "RecordingLatencyTest.moc"
