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
#include <cstdint>
#include <memory>
#include <vector>

#include <QTemporaryDir>

#include <sndfile.h>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "Timeline.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t kOutputLatency = 200;
constexpr f_cnt_t kInputLatency = 300;
constexpr std::size_t kImpulseAt = 4410;
//! 0.5.0 L03 (Q050-147): a second transient inside a punch region that starts half a bar in.
constexpr std::size_t kInsidePunchAt = 24000;
//! 0.5.0 L03 (Q050-147): a third transient after punch-out; it must not reach the take.
constexpr std::size_t kAfterPunchAt = 40000;
constexpr tick_t kPunchBegin = 48;
constexpr tick_t kPunchEnd = 96;

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

	//! Engine audit E3: an armed punch region that starts after the played transient keeps it
	//! out of the take - the take records silence outside the region - and disarming the region
	//! records it again.
	void anArmedPunchRegionKeepsWhatIsOutsideItOutOfTheTake()
	{
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.setPunchRange(96, 192);  // half a bar on: the impulse (tick ~11) is before it
		timeline.setPunchEnabled(true);
		SampleClip* take = nullptr;
		recordTake(TimePos(0), &take);
		timeline.setPunchEnabled(false);
		QVERIFY(take != nullptr);
		QCOMPARE(firstTransient(take), -1LL);
	}

	//! 0.5.0 L03 (Q050-147): the same gate, proven from both sides. The loopback plays two
	//! transients, one before the punch-in and one inside the range; the take holds the inside
	//! one at the tick it was played and silence everywhere before the range.
	void anArmedPunchRegionCapturesTheInsideTransientAndOnlyIt()
	{
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.setPunchRange(kPunchBegin, kPunchEnd);
		timeline.setPunchEnabled(true);

		Song* song = Engine::getSong();
		const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		auto* player = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		auto* recorder = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		std::vector<SampleFrame> data(static_cast<std::size_t>(rate), SampleFrame(0.0f, 0.0f));
		data[kImpulseAt] = SampleFrame(1.0f, 1.0f);
		data[kInsidePunchAt] = SampleFrame(1.0f, 1.0f);
		data[kAfterPunchAt] = SampleFrame(1.0f, 1.0f);
		auto* played = dynamic_cast<SampleClip*>(player->createClip(TimePos(0)));
		played->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
		auto* take = dynamic_cast<SampleClip*>(recorder->createClip(TimePos(0)));
		take->changeLength(played->length());
		take->setAutoResize(false);
		take->setRecord(true);
		song->playAndRecord();
		QTest::qWait(1100);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!take->isRecord(), 3000);
		player->setMuted(true);
		recorder->setMuted(true);
		timeline.setPunchEnabled(false);

		const auto buffer = take->sample().buffer();
		QVERIFY(buffer != nullptr);
		QVERIFY(buffer->size() > kInsidePunchAt);
		// The outside impulse (frame 4410) is not in the take at all: silence before punch-in.
		for (std::size_t i = 0; i < static_cast<std::size_t>(kPunchBegin * Engine::framesPerTick()); ++i)
		{
			QVERIFY2(std::fabs(buffer->data()[i][0]) <= 0.5f,
				qPrintable(QStringLiteral("the take captured a frame outside the punch range at %1").arg(i)));
		}
		// The later outside impulse is past punch-out; the rest of the take stays silent too.
		QVERIFY(buffer->size() > kAfterPunchAt);
		const std::size_t punchOutFrame = static_cast<std::size_t>(std::ceil(kPunchEnd * Engine::framesPerTick()));
		for (std::size_t i = punchOutFrame; i < buffer->size(); ++i)
		{
			QVERIFY2(std::fabs(buffer->data()[i][0]) <= 0.5f,
				qPrintable(QStringLiteral("the take captured a frame after punch-out at %1").arg(i)));
		}
		// The inside one is there, aligned to the frame it was played on.
		const long long recordedAt = firstTransient(take);
		QVERIFY2(recordedAt >= 0, "the take captured nothing inside the punch range");
		QVERIFY2(std::llabs(recordedAt - static_cast<long long>(kInsidePunchAt)) <= 1,
			qPrintable(QStringLiteral("the inside transient landed at %1, not %2")
				.arg(recordedAt).arg(kInsidePunchAt)));
	}

	//! Cluster 5465: the capture staging ring is drained whole, so one render can
	//! take a batch that holds more than one engine period - a render that fell
	//! behind takes the backlog with it. Such a batch ends at the end of the
	//! period being rendered; the punch gate must place it by that end, not start
	//! all of it at this period's start. Here a two-period batch is staged for the
	//! first period with the punch region covering song frames [0, one tick): the
	//! batch's first (older, pre-punch) period must be cut and its second period
	//! kept. Mapping the whole batch from the period start kept the wrong half and
	//! dropped the half inside the region.
	void anOversizedCaptureBatchIsGatedAtItsOwnSongPosition()
	{
		AudioEngine* engine = Engine::audioEngine();
		Song* song = Engine::getSong();
		QVERIFY(engine != nullptr && song != nullptr);
		// Drive the periods ourselves: with the device stopped this test's staged
		// batch is the only input, so the assertion does not depend on wall time.
		engine->audioDev()->stopProcessing();
		engine->renderNextPeriod();  // drain whatever the stopped device left staged

		Timeline& timeline = song->getTimeline(Song::PlayMode::Song);
		timeline.setPunchRange(0, 1);  // song frames [0, framesPerTick): the first tick
		timeline.setPunchEnabled(true);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString takePath = dir.filePath(QStringLiteral("oversized-batch.wav"));
		MultiTrackRecorder& recorder = engine->recorder();
		// A failing assertion must still join the writer before the temp dir goes.
		struct DisarmOnExit
		{
			MultiTrackRecorder& recorder;
			~DisarmOnExit() { recorder.disarmAll(); }
		} disarmOnExit{recorder};
		QVERIFY2(recorder.armTrack(0, takePath.toStdString(),
			static_cast<int>(engine->outputSampleRate()), 0), "the test route would not arm");

		const f_cnt_t periodFrames = engine->framesPerPeriod();
		const f_cnt_t batchFrames = 2 * periodFrames;
		std::vector<SampleFrame> batch(static_cast<std::size_t>(batchFrames));
		for (f_cnt_t frame = 0; frame < batchFrames; ++frame)
		{
			const auto value = static_cast<float>(frame + 1) / 1024.0f;
			batch[static_cast<std::size_t>(frame)] = SampleFrame(value, value);
		}

		song->setPlayPos(0, Song::PlayMode::Song);
		song->playSong();
		engine->pushInputFrames(batch.data(), batchFrames);
		// This render is song frames [0, periodFrames), so the batch's SECOND
		// period is the period being rendered and its first is the backlog.
		engine->renderNextPeriod();
		song->stop();
		timeline.setPunchEnabled(false);
		// The whole staged batch reached the engine, and the gate handed the
		// recorder exactly the period inside the region.
		QCOMPARE(engine->inputBufferFrames(), batchFrames);
		QCOMPARE(recorder.track(0).framesPushed(), static_cast<std::uint64_t>(periodFrames));
		recorder.disarmAll();

		SF_INFO info{};
		SNDFILE* file = sf_open(takePath.toUtf8().constData(), SFM_READ, &info);
		QVERIFY(file != nullptr);
		std::vector<float> recorded(static_cast<std::size_t>(info.frames), 0.0f);
		const sf_count_t read = sf_readf_float(file, recorded.data(), info.frames);
		sf_close(file);
		// Only the batch's second period lies inside the punch region.
		QCOMPARE(info.frames, static_cast<sf_count_t>(periodFrames));
		QCOMPARE(read, static_cast<sf_count_t>(periodFrames));
		if (recorded.size() != static_cast<std::size_t>(periodFrames)) { return; }
		for (f_cnt_t frame = 0; frame < periodFrames; ++frame)
		{
			const float expected = static_cast<float>(periodFrames + frame + 1) / 1024.0f;
			QVERIFY2(std::fabs(recorded[static_cast<std::size_t>(frame)] - expected) <= 1e-6f,
				qPrintable(QStringLiteral("take frame %1 is %2, not batch frame %3 (%4)")
					.arg(frame).arg(recorded[static_cast<std::size_t>(frame)])
					.arg(periodFrames + frame).arg(expected)));
		}
	}

private:
	void recordTake(const TimePos& at, SampleClip** out)
	{
		Song* song = Engine::getSong();
		const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		auto* player = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		auto* recorder = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		std::vector<SampleFrame> data(static_cast<std::size_t>(rate), SampleFrame(0.0f, 0.0f));
		data[kImpulseAt] = SampleFrame(1.0f, 1.0f);
		auto* played = dynamic_cast<SampleClip*>(player->createClip(at));
		played->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
		auto* take = dynamic_cast<SampleClip*>(recorder->createClip(at));
		take->changeLength(played->length());
		take->setAutoResize(false);
		take->setRecord(true);
		song->playAndRecord();
		QTest::qWait(700);
		song->stop();
		QTRY_VERIFY_WITH_TIMEOUT(!take->isRecord(), 3000);
		player->setMuted(true);
		recorder->setMuted(true);
		*out = take;
	}

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
