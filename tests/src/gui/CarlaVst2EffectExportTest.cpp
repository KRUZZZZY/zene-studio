/*
 * CarlaVst2EffectExportTest.cpp - exporting a project through a VST2 effect hosted
 *                                 by Carla never waits inside a render period
 *                                 (Q050-481.2 review r1).
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

/*
 * Review r1 of Q050-481.2: an export must not wait for Carla INSIDE a render
 * period (the period runs in AudioEngine::renderNextPeriod's RealtimeScope), and
 * must still put the VST2 effect's output into the file. The product's answer:
 * the render path never waits; the exporter (ProjectRenderer::run) waits for the
 * VST2 render workers between periods (OfflineRenderBarrier.h).
 *
 * The case exports through the real exporter with the VST2 test effect stalling
 * 150 ms in every process call, natively and through Carla's Wine bridge, while
 * sampling the render period clock: no period may last anywhere near the stall,
 * and the file must hold the processed note without a gap. Same environment as
 * CarlaVst2EffectHostingTest (a real Carla runtime; otherwise Skipped).
 */

#include <QtTest>

#include <chrono>
#include <cstring>
#include <thread>

#include "CarlaVst2EffectTestSupport.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "RenderPeriodClock.h"

namespace
{

constexpr qint64 ExportStallMs = 150;
//! One bar at 600 BPM, 44.1 kHz: how long the exported note sounds.
constexpr std::ptrdiff_t NoteFrames = 17640;

//! The interleaved float frames of a 32-bit float stereo WAV, or empty.
std::vector<SampleFrame> readFloatWav(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return {}; }
	const QByteArray bytes = file.readAll();
	int at = 12;  // past "RIFF" <size> "WAVE"
	while (at + 8 <= bytes.size())
	{
		const QByteArray id = bytes.mid(at, 4);
		quint32 size = 0;
		std::memcpy(&size, bytes.constData() + at + 4, 4);
		if (id == "data")
		{
			const auto count = std::min<qsizetype>(size, bytes.size() - at - 8) / qsizetype(2 * sizeof(float));
			std::vector<SampleFrame> frames(static_cast<std::size_t>(count));
			for (qsizetype f = 0; f < count; ++f)
			{
				float pair[2];
				std::memcpy(pair, bytes.constData() + at + 8 + f * 2 * sizeof(float), sizeof(pair));
				frames[static_cast<std::size_t>(f)] = SampleFrame(pair[0], pair[1]);
			}
			return frames;
		}
		at += 8 + static_cast<int>(size) + static_cast<int>(size & 1);
	}
	return {};
}

//! What sampling the render period clock saw while an export ran.
struct PeriodSamples
{
	qint64 longestMs = 0;
	int seen = 0;
};

//! Runs @a renderer to the end, sampling (from this thread) how long each render period it observes has lasted.
PeriodSamples exportSampling(ProjectRenderer& renderer)
{
	PeriodSamples samples;
	bool inPeriod = false;
	renderer.startProcessing();
	while (!renderer.wait(0))
	{
		const bool now = RenderPeriodClock::inPeriod();
		if (now)
		{
			const auto lasted = std::chrono::steady_clock::now() - RenderPeriodClock::periodStart();
			samples.longestMs = std::max<qint64>(samples.longestMs,
				std::chrono::duration_cast<std::chrono::milliseconds>(lasted).count());
			samples.seen += inPeriod ? 0 : 1;
		}
		inPeriod = now;
		std::this_thread::sleep_for(std::chrono::microseconds(200));
	}
	return samples;
}

//! The exported note: its sounding span, the silent frames inside it, and the frames that are not the effect's output.
struct NoteSpan
{
	std::ptrdiff_t length = 0;
	int gaps = 0;
	int unprocessed = 0;
};

NoteSpan analyse(const std::vector<SampleFrame>& frames)
{
	auto sounding = [](const SampleFrame& frame) { return std::abs(frame.left()) > Tolerance; };
	const auto first = std::find_if(frames.begin(), frames.end(), sounding);
	const auto last = std::find_if(frames.rbegin(), frames.rend(), sounding).base();
	NoteSpan span;
	if (first == frames.end()) { return span; }
	span.length = std::distance(first, last);
	for (auto frame = first; frame != last; ++frame)
	{
		// The source's (0.5, 0.25) through Left 0.5 / Right 0.25: right = left * 0.25.
		const bool processed = std::abs(frame->right() - frame->left() * 0.25f) <= 1e-3f;
		span.gaps += sounding(*frame) ? 0 : 1;
		if (sounding(*frame) && !processed && span.unprocessed++ == 0)
		{
			qInfo("first unprocessed frame at %td of the span: (%f, %f)", std::distance(first, frame),
				static_cast<double>(frame->left()), static_cast<double>(frame->right()));
		}
	}
	return span;
}

//! Renders @a periods periods, each given the time the stalled worker needs to process it.
void renderSlowly(int periods)
{
	for (int period = 0; period < periods; ++period)
	{
		Engine::audioEngine()->renderNextPeriod();
		QTest::qWait(static_cast<int>(ExportStallMs) + 20);
	}
}

} // namespace

class CarlaVst2EffectExportTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		QVERIFY(m_plugins.isValid());
		QVERIFY(m_files.isValid());
		initCarlaHostingProcess(m_home, m_plugins, {QStringLiteral(CARLA_VST2_NATIVE_FIXTURE_DIR),
			QStringLiteral(CARLA_VST2_EFFECT_NATIVE_FIXTURE_DIR), QStringLiteral(CARLA_VST2_EFFECT_WINE_FIXTURE_DIR)},
			effectWineFixture());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void anExportWaitsBetweenPeriodsAndKeepsTheEffectsOutput_data()
	{
		QTest::addColumn<QString>("file");
		QTest::newRow("native") << EffectNativeFile;
		QTest::newRow("wine-bridged") << EffectWineFile;
	}

	//! A project export through the real exporter (ProjectRenderer) with the effect stalling 150 ms in every process
	//! call: no render period lasts anywhere near the stall (sampled from the render period clock while the export
	//! runs), and the file still holds the effect's output, unbroken - the exporter waits BETWEEN periods instead.
	void anExportWaitsBetweenPeriodsAndKeepsTheEffectsOutput()
	{
		QFETCH(QString, file);
		if (const QString why = missingEffectRuntime(); !why.isEmpty()) { QSKIP(qPrintable(why)); }
		if (file == EffectWineFile && !effectWineAvailable()) { QSKIP("no Wine, no Windows PE fixture or no Carla Wine bridge in the runtime"); }
		InstrumentTrack* track = sourceWithEffect(file, ExportStallMs);
		QVERIFY(track != nullptr);
		QVERIFY(setEffectParam(track, QStringLiteral("Right"), 0.25).ok);  // processed: R = 0.5 * input, L = input
		Song* song = Engine::getSong();
		const int tempo = song->getTempo();
		song->setTempo(600);  // one bar is 0.4 s: a short file, so the stalled export stays short
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		clip->addNote(Note(TimePos(DefaultTicksPerBar), TimePos(0), 60, 100), false);
		song->updateLength();
		// Played once first, as any session is before an export: a Wine bridge applies a parameter change one or two
		// blocks late after a long silence (measured, with or without the stall: the first sounding blocks at the old
		// value), which is not what this case is about.
		song->setPlayPos(0, Song::PlayMode::Song);
		MidiPlayback::instance().stopped();
		song->playSong();
		renderSlowly(8);
		song->stop();
		renderSlowly(4);

		const QString path = QDir(m_files.path()).filePath(QStringLiteral("vst2-effect-export-%1.wav").arg(QTest::currentDataTag()));
		PeriodSamples periods;
		{
			ProjectRenderer renderer(OutputSettings(44100, 192, OutputSettings::BitDepth::Depth32Bit,
				OutputSettings::StereoMode::Stereo), ProjectRenderer::ExportFileFormat::Wave, path);
			QVERIFY(renderer.isReady());
			periods = exportSampling(renderer);
		}
		song->setTempo(tempo);
		delete clip;
		delete track;
		const std::vector<SampleFrame> frames = readFloatWav(path);
		const NoteSpan span = analyse(frames);
		qInfo("export: %d periods seen, longest %lld ms; %zu frames, %td sounding in one span, %d silent inside it, "
			"%d not processed", periods.seen, periods.longestMs, frames.size(), span.length, span.gaps, span.unprocessed);
		QVERIFY2(periods.seen > 0, "no render period was observed: nothing was measured");
		QVERIFY2(periods.longestMs < ExportStallMs / 2, qPrintable(QStringLiteral("a render period took %1 ms").arg(periods.longestMs)));
		QVERIFY2(span.length > NoteFrames / 2, "the exported file lost the effect's output");
		QCOMPARE(span.gaps, 0);
		QCOMPARE(span.unprocessed, 0);
	}

private:
	QTemporaryDir m_home;
	QTemporaryDir m_plugins;
	QTemporaryDir m_files;
};

QTEST_MAIN(CarlaVst2EffectExportTest)
#include "CarlaVst2EffectExportTest.moc"
