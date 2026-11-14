/*
 * EarconsRenderTest.cpp - earcons reach the device output only, never a render or a take (Q050-497)
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*! The monitoring set is mixed in AudioEngine::renderNextBuffer (the device path) and nowhere else.
 *
 *  render.render's child and the recorder taps both drive AudioEngine::renderNextPeriod, so this
 *  test drives that same real export path twice, with a song that has sound: file A with every
 *  category off and file B with every category on and every Sound queued. The two files must be
 *  equal frame for frame and sample for sample, and the earcons must still be pending afterwards.
 *
 *  The second case arms a master-tap route (recordertaps::Source::Master, the resampling take a
 *  recording of the master output makes) on an empty song with every earcon queued, renders
 *  periods, and requires every recorded sample to be exactly zero - and the earcons still pending.
 */

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <vector>

#include <sndfile.h>

#include "AudioEngine.h"
#include "Earcons.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "RecorderTaps.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr int kSampleRate = 44100;
constexpr float kLevel = 0.5f;

//! A constant source: its value survives any resampling ratio, so "same audio" is an exact test.
std::shared_ptr<const SampleBuffer> constantBuffer(int seconds)
{
	const auto frames = static_cast<std::size_t>(seconds) * kSampleRate;
	return std::make_shared<const SampleBuffer>(
		std::vector<SampleFrame>(frames, SampleFrame(kLevel, kLevel)).data(), frames, kSampleRate);
}

//! Every sample of a WAV, all channels, exactly as written (float in, float out).
bool readSamples(const QString& path, std::vector<float>* samples, sf_count_t* frames)
{
	SF_INFO info{};
	SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_READ, &info);
	if (wav == nullptr) { return false; }
	std::vector<float> data(static_cast<std::size_t>(info.frames) * info.channels);
	const sf_count_t read = sf_readf_float(wav, data.data(), info.frames);
	sf_close(wav);
	data.resize(static_cast<std::size_t>(read) * info.channels);
	*samples = std::move(data);
	*frames = read;
	return read >= 0;
}

void enableEveryCategory()
{
	for (auto category = 0; category < static_cast<int>(earcons::Category::Count); ++category)
	{
		earcons::setEnabled(static_cast<earcons::Category>(category), true);
		earcons::setLevel(static_cast<earcons::Category>(category), 1.f);
	}
}

void queueEverySound()
{
	for (auto sound = 0; sound < static_cast<int>(earcons::Sound::Count); ++sound)
	{
		earcons::play(static_cast<earcons::Sound>(sound));
	}
}

} // namespace

class EarconsRenderTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The dummy device renders in the background; this test drives the export path itself.
		if (auto* device = Engine::audioEngine()->audioDev()) { device->stopProcessing(); }
		earcons::prepare(kSampleRate);
	}

	void cleanupTestCase() { Engine::destroy(); }

	//! The export/recorder path renders periods; the earcons are neither written nor consumed.
	void renderOutputIsIdenticalWithEarconsOn()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		auto* track = new SampleTrack(song);
		track->setName(QStringLiteral("EarconFixture"));
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setSampleBuffer(constantBuffer(1));
		const int ticks = static_cast<int>(kSampleRate / Engine::framesPerTick(kSampleRate));
		clip->changeLength(TimePos(ticks));
		clip->setAutoResize(false);

		// The loop is on over the clip's own span. A whole-project render does not loop (Song's
		// playback wrap is gated on !m_exporting), so the LoopWrap earcon is queued explicitly
		// below; the flag still proves that a loop-switched song renders the same either way.
		song->getTimeline().setLoopPoints(TimePos(0), TimePos(ticks));
		song->getTimeline().setLoopEnabled(true);
		song->updateLength();

		for (auto category = 0; category < static_cast<int>(earcons::Category::Count); ++category)
		{
			earcons::setEnabled(static_cast<earcons::Category>(category), false);
		}
		earcons::beginBuffer();
		for (int i = 0; i < 12000; ++i) { (void)earcons::nextSample(); }

		const QString a = QDir(QDir::tempPath()).filePath(QStringLiteral("earcons-render-a-%1.wav").arg(QCoreApplication::applicationPid()));
		const QString b = QDir(QDir::tempPath()).filePath(QStringLiteral("earcons-render-b-%1.wav").arg(QCoreApplication::applicationPid()));
		QFile::remove(a);
		QFile::remove(b);

		const auto render = [](const QString& path) {
			const OutputSettings settings(kSampleRate, 256, OutputSettings::BitDepth::Depth32Bit,
				OutputSettings::StereoMode::Stereo);
			ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
			if (!renderer.isReady()) { return false; }
			renderer.startProcessing();
			renderer.wait();
			if (auto* device = Engine::audioEngine()->audioDev()) { device->stopProcessing(); }
			return QFileInfo(path).size() > 0;
		};
		QVERIFY2(render(a), "the render with every category off failed");

		enableEveryCategory();
		queueEverySound();  // includes LoopWrap: a wrap is the one earcon the song itself can trigger
		QVERIFY2(render(b), "the render with every category on failed");

		std::vector<float> samplesA;
		std::vector<float> samplesB;
		sf_count_t framesA = 0;
		sf_count_t framesB = 0;
		QVERIFY2(readSamples(a, &samplesA, &framesA), "could not read render A");
		QVERIFY2(readSamples(b, &samplesB, &framesB), "could not read render B");
		QCOMPARE(framesB, framesA);
		QCOMPARE(samplesB.size(), samplesA.size());
		QVERIFY(framesA > 0);
		for (std::size_t i = 0; i < samplesA.size(); ++i)
		{
			QVERIFY2(samplesB[i] == samplesA[i],
				qPrintable(QStringLiteral("sample %1 differs: %2 vs %3").arg(i).arg(samplesA[i]).arg(samplesB[i])));
		}
		float peak = 0.f;
		for (const float value : samplesA) { peak = std::max(peak, std::fabs(value)); }
		QVERIFY2(peak > 0.f, "the fixture rendered silence, so equality would prove nothing");

		// The queued earcons are still there: the render did not consume or mix them.
		earcons::beginBuffer();
		int heard = 0;
		for (int i = 0; i < 12000; ++i)
		{
			if (earcons::nextSample() != 0.f) { ++heard; }
		}
		QVERIFY2(heard > 0, "the render path consumed the queued earcons");
	}

	//! A master-tap (resampling) take is the master output, not the monitoring mix.
	void aRecordedTakeHasNoEarcons()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		song->updateLength();

		QTemporaryDir takes;
		QVERIFY(takes.isValid());
		const QString take = takes.filePath(QStringLiteral("master-tap.wav"));

		MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
		QVERIFY2(recorder.armTrack(0, take.toStdString(), kSampleRate, 0), "could not arm the master-tap route");
		recordertaps::set(0, recordertaps::Tap{recordertaps::Source::Master, -1, 0, 0});
		QVERIFY(recordertaps::tapped(0));

		enableEveryCategory();
		earcons::beginBuffer();
		for (int i = 0; i < 12000; ++i) { (void)earcons::nextSample(); }
		queueEverySound();

		for (int period = 0; period < 4; ++period) { (void)Engine::audioEngine()->renderNextPeriod(); }

		recordertaps::set(0, recordertaps::Tap{});
		recorder.disarmAll();  // flushes the take's writer
		QVERIFY(!recordertaps::tapped(0));

		std::vector<float> samples;
		sf_count_t frames = 0;
		QVERIFY2(QFileInfo::exists(take), "the master-tap route wrote no take");
		QVERIFY2(readSamples(take, &samples, &frames), "could not read the recorded take");
		QVERIFY2(frames > 0, "the take holds no frames, so 'all zero' would be vacuous");
		for (const float value : samples)
		{
			QVERIFY2(value == 0.f, qPrintable(QStringLiteral("the take recorded an earcon sample %1").arg(value)));
		}

		earcons::beginBuffer();
		int heard = 0;
		for (int i = 0; i < 12000; ++i)
		{
			if (earcons::nextSample() != 0.f) { ++heard; }
		}
		QVERIFY2(heard > 0, "the recorder path consumed the queued earcons");
	}

	//! A real song loop wrap on the render path queues the LoopWrap earcon through Song::enforceLoop.
	void aLoopWrapQueuesItsEarcon()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		const tick_t bar = TimePos::ticksPerBar();
		// The wrap happens in PlayMode::Song, so the loop points must live on that timeline
		// (getTimeline() without a mode follows the current one, which a stopped song may not be).
		auto& timeline = song->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(TimePos(0), TimePos(bar));
		timeline.setLoopEnabled(true);
		song->setPlayPos(0, Song::PlayMode::Song);
		song->updateLength();

		for (auto category = 0; category < static_cast<int>(earcons::Category::Count); ++category)
		{
			earcons::setEnabled(static_cast<earcons::Category>(category), false);
		}
		earcons::setEnabled(earcons::Category::Loop, true);
		earcons::setLevel(earcons::Category::Loop, 1.f);

		const auto flush = [] {
			earcons::beginBuffer();
			for (int i = 0; i < 12000; ++i) { (void)earcons::nextSample(); }
		};
		flush();

		// Three bars is ample for one wrap; the bound only keeps a failed fixture finite.
		const auto framesPerBar = static_cast<double>(bar) * Engine::framesPerTick(kSampleRate);
		const auto framesPerPeriod = static_cast<double>(Engine::audioEngine()->framesPerPeriod());
		const int maxPeriods = static_cast<int>(std::ceil(3.0 * framesPerBar / framesPerPeriod)) + 16;

		const auto playUntilWrap = [song, maxPeriods](std::uint64_t before) {
			song->setPlayPos(0, Song::PlayMode::Song);
			song->playSong();
			for (int period = 0; period < maxPeriods && song->loopWraps() == before; ++period)
			{
				(void)Engine::audioEngine()->renderNextPeriod();
			}
			song->stop();
		};

		const auto before = song->loopWraps();
		playUntilWrap(before);
		QVERIFY2(song->loopWraps() > before, "the song never wrapped at its loop end");

		earcons::beginBuffer();
		int heard = 0;
		for (int i = 0; i < 12000; ++i)
		{
			if (earcons::nextSample() != 0.f) { ++heard; }
		}
		QVERIFY2(heard > 0, "the loop wrap queued no earcon");

		// With the Loop category off, a second, real wrap must stay silent.
		earcons::setEnabled(earcons::Category::Loop, false);
		flush();
		const auto beforeSilent = song->loopWraps();
		playUntilWrap(beforeSilent);
		QVERIFY2(song->loopWraps() > beforeSilent, "the silent fixture never wrapped");

		earcons::beginBuffer();
		int silentHeard = 0;
		for (int i = 0; i < 12000; ++i)
		{
			if (earcons::nextSample() != 0.f) { ++silentHeard; }
		}
		QVERIFY2(silentHeard == 0, "a loop wrap was heard with the Loop category disabled");
	}
};

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	EarconsRenderTest test;
	QTEST_SET_MAIN_SOURCE_PATH
	return QTest::qExec(&test, argc, argv);
}

#include "EarconsRenderTest.moc"
