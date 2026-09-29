/*
 * SampleClipRubberBandTest.cpp - the optional Rubber Band warp stretch, through the clip path
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

/*! Owner decision 12: Rubber Band as an optional stretch, WSOLA the default.
 *
 *  SampleClipStretchTest holds WSOLA; this file holds the third mode on the
 *  same fixture (the 440 + 660 Hz two-tone, a 2x warp, a Goertzel bin per
 *  frequency), so the numbers the two print are comparable:
 *   * the pitch survives a 2x warp, the length is the mapping's, and the render
 *     is NOT the WSOLA one (the voice really rendered it);
 *   * a source at half the engine rate keeps its pitch (the pitch-scale term);
 *   * the audio-thread half - claim, begin, every period - allocates nothing,
 *     measured with a malloc interposer because Rubber Band allocates through
 *     posix_memalign, which an operator-new probe cannot see;
 *   * two voices per clip, a third concurrent handle falls back to WSOLA and is
 *     counted, and the recycler makes released voices ready again;
 *   * the mode round-trips as stretch="rubberband" and warp.stretch drives it.
 *  Every slot but the persistence one is skipped by a build without the library.
 */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "RubberBandStretch.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "WarpMarkers.h"

// The allocation probe. Interposing malloc and friends is a glibc executable's
// privilege and a sanitizer runtime's job, so the probe exists only where both
// hold; elsewhere the allocation slot says it was skipped.
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
#define ZENE_MALLOC_PROBE 1
extern "C" void* __libc_malloc(size_t);
extern "C" void* __libc_calloc(size_t, size_t);
extern "C" void* __libc_realloc(void*, size_t);
extern "C" void* __libc_memalign(size_t, size_t);
namespace
{
thread_local bool tlCounting = false;
thread_local unsigned long tlAllocations = 0;
inline void note() { if (tlCounting) { ++tlAllocations; } }
} // namespace
extern "C" void* malloc(size_t size) { note(); return __libc_malloc(size); }
extern "C" void* calloc(size_t count, size_t size) { note(); return __libc_calloc(count, size); }
extern "C" void* realloc(void* ptr, size_t size) { note(); return __libc_realloc(ptr, size); }
extern "C" void* aligned_alloc(size_t alignment, size_t size) { note(); return __libc_memalign(alignment, size); }
extern "C" int posix_memalign(void** ptr, size_t alignment, size_t size)
{
	note();
	*ptr = __libc_memalign(alignment, size);
	return *ptr != nullptr ? 0 : 12; // ENOMEM
}
#endif

using namespace lmms;

namespace
{

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kLowTone = 440.0;
constexpr double kHighTone = 660.0;

SampleClip* makeClip(SampleTrack& track, int rate)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	std::vector<SampleFrame> data(static_cast<std::size_t>(2 * rate));
	for (std::size_t f = 0; f < data.size(); ++f)
	{
		const auto value = static_cast<sample_t>(0.5 * std::sin(kTwoPi * kLowTone * f / rate)
			+ 0.3 * std::sin(kTwoPi * kHighTone * f / rate));
		data[f] = SampleFrame(value, value);
	}
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

//! The clip's 2 s pinned to half the ticks: a 2x warp, at the SOURCE's rate.
void applyTwoTimesWarp(SampleClip& clip, int sourceRate)
{
	const auto frames = static_cast<f_cnt_t>(clip.sample().sampleSize());
	const auto ticks = static_cast<tick_t>(std::llround(frames / (2.0 * Engine::framesPerTick(sourceRate))));
	const std::array<WarpMarker, 2> markers{ WarpMarker{ 0, 0 }, WarpMarker{ frames, ticks } };
	QVERIFY(clip.setWarpMarkers(std::span<const WarpMarker>(markers.data(), markers.size())));
}

double amplitudeAt(const std::vector<SampleFrame>& data, double freq, double rate)
{
	const int from = static_cast<int>(data.size() / 4);
	const int frames = static_cast<int>(data.size() / 2);
	const double omega = kTwoPi * std::round(frames * freq / rate) / frames;
	double previous = 0.0, previousPrevious = 0.0;
	for (int i = from; i < from + frames; ++i)
	{
		const double current = data[static_cast<std::size_t>(i)][0] + 2.0 * std::cos(omega) * previous
			- previousPrevious;
		previousPrevious = previous;
		previous = current;
	}
	const double real = previous - previousPrevious * std::cos(omega);
	const double imaginary = previousPrevious * std::sin(omega);
	return 2.0 * std::sqrt(real * real + imaginary * imaginary) / frames;
}

//! A whole pass through the constructor SampleTrack::play uses, period by period.
std::vector<SampleFrame> renderClip(SampleClip* clip)
{
	SamplePlayHandle handle(clip, clip->sampleWindow());
	const auto total = static_cast<int>(handle.totalFrames());
	std::vector<SampleFrame> rendered;
	std::vector<SampleFrame> buffer(512);
	for (int guard = 0; static_cast<int>(handle.framesDone()) < total && guard < total / 512 + 16; ++guard)
	{
		std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
		handle.play(std::span<SampleFrame>(buffer));
		rendered.insert(rendered.end(), buffer.begin(), buffer.end());
	}
	rendered.resize(static_cast<std::size_t>(total));
	return rendered;
}

void evidence(const char* label, double a, double b = 0.0, double c = 0.0, double d = 0.0)
{
	std::printf("RUBBERBAND_EVIDENCE %-40s %10.4f %10.4f %10.4f %10.4f\n", label, a, b, c, d);
	std::fflush(stdout);
}

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace


class SampleClipRubberBandTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_rate = Engine::audioEngine()->outputSampleRate();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		if (!RubberBandPool::available() && QTest::currentTestFunction()
			!= QByteArray("theModeRoundTripsThroughTheProjectFile"))
		{
			QSKIP("this build has no Rubber Band (WANT_RUBBERBAND off or librubberband absent)");
		}
	}

	void aWarpedClipKeepsItsPitchThroughRubberBand()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyTwoTimesWarp(*clip, m_rate);
		clip->setWarpStretchMode(WarpStretchMode::PreservePitch);
		const std::vector<SampleFrame> wsola = renderClip(clip);
		clip->setWarpStretchMode(WarpStretchMode::RubberBand);
		QVERIFY(clip->rubberBandPool() != nullptr);
		const std::vector<SampleFrame> rubberBand = renderClip(clip);

		const double low = amplitudeAt(rubberBand, kLowTone, m_rate);
		const double high = amplitudeAt(rubberBand, kHighTone, m_rate);
		const double doubled = amplitudeAt(rubberBand, 2.0 * kLowTone, m_rate);
		const double tripled = amplitudeAt(rubberBand, 2.0 * kHighTone, m_rate);
		evidence("rubberband: 440 / 660 / 880 / 1320", low, high, doubled, tripled);
		QVERIFY2(std::fabs(low - 0.5) < 0.05, qPrintable(QStringLiteral("440 Hz came out at %1").arg(low)));
		QVERIFY2(std::fabs(high - 0.3) < 0.05, qPrintable(QStringLiteral("660 Hz came out at %1").arg(high)));
		QVERIFY2(doubled < 0.05 && tripled < 0.05, "the pitch moved an octave");

		// Same timeline as WSOLA: the mapping sets the length, not the stretcher.
		QCOMPARE(rubberBand.size(), wsola.size());
		// And it IS the voice's render, not the fallback's.
		double difference = 0.0;
		for (std::size_t i = 0; i < wsola.size(); ++i) { difference += std::fabs(wsola[i][0] - rubberBand[i][0]); }
		evidence("mean |rubberband - wsola|, misses", difference / wsola.size(),
			static_cast<double>(clip->rubberBandPool()->misses()));
		QVERIFY2(difference / wsola.size() > 1e-3, "the Rubber Band render is the WSOLA render");
		QCOMPARE(clip->rubberBandPool()->misses(), std::uint64_t{0});
	}

	//! The stretcher keeps a period in samples; the pitch scale is what keeps a
	//! source at half the engine rate from coming out an octave low.
	void aSourceAtHalfTheEngineRateKeepsItsPitch()
	{
		SampleTrack track(Engine::getSong());
		const int sourceRate = m_rate / 2;
		auto* clip = makeClip(track, sourceRate);
		applyTwoTimesWarp(*clip, sourceRate);
		clip->setWarpStretchMode(WarpStretchMode::RubberBand);
		const std::vector<SampleFrame> rendered = renderClip(clip);
		const double low = amplitudeAt(rendered, kLowTone, m_rate);
		const double octaveDown = amplitudeAt(rendered, kLowTone / 2.0, m_rate);
		evidence("half-rate source: 440 / 220", low, octaveDown);
		QVERIFY2(std::fabs(low - 0.5) < 0.05, qPrintable(QStringLiteral("440 Hz came out at %1").arg(low)));
		QVERIFY2(octaveDown < 0.05, "the half-rate source came out an octave low");
	}

	void theAudioThreadHalfAllocatesNothing()
	{
#ifdef ZENE_MALLOC_PROBE
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyTwoTimesWarp(*clip, m_rate);
		clip->setWarpStretchMode(WarpStretchMode::RubberBand);
		RubberBandPool::recycleNow();

		// The claim and the begin, directly: the handle constructor does other
		// things (the play-handle base) this lane does not own.
		std::vector<SampleFrame> buffer(1024);
		tlAllocations = 0;
		tlCounting = true;
		RubberBandVoice* voice = clip->rubberBandPool()->claim();
		tlCounting = false;
		QVERIFY(voice != nullptr);
		const unsigned long claimAllocations = tlAllocations;
		// The negative control: the recycler's reset() is the call measured to
		// allocate, so a probe that cannot see Rubber Band's posix_memalign
		// would read 0 here and make every zero in this slot vacuous.
		voice->release();
		tlAllocations = 0;
		tlCounting = true;
		RubberBandPool::recycleNow();
		tlCounting = false;
		const unsigned long resetAllocations = tlAllocations;
		QVERIFY2(resetAllocations > 0, "the probe saw no allocation in Rubber Band's reset()");

		SamplePlayHandle handle(clip, clip->sampleWindow());
		tlAllocations = 0;
		tlCounting = true;
		for (int period = 0; period < 32; ++period) { handle.play(std::span<SampleFrame>(buffer)); }
		tlCounting = false;
		evidence("allocations: reset (control), claim, 32 periods", static_cast<double>(resetAllocations),
			static_cast<double>(claimAllocations), static_cast<double>(tlAllocations));
		QCOMPARE(claimAllocations, 0UL);
		QCOMPARE(tlAllocations, 0UL);
		QCOMPARE(clip->rubberBandPool()->misses(), std::uint64_t{0});
#else
		QSKIP("no malloc interposition in this build (sanitizer or non-glibc)");
#endif
	}

	void aThirdConcurrentHandleFallsBackAndReleasedVoicesComeBack()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyTwoTimesWarp(*clip, m_rate);
		clip->setWarpStretchMode(WarpStretchMode::RubberBand);
		RubberBandPool* pool = clip->rubberBandPool();
		RubberBandPool::recycleNow();
		QCOMPARE(pool->readyVoices(), RubberBandPool::VoiceCount);
		{
			auto first = std::make_unique<SamplePlayHandle>(clip, clip->sampleWindow());
			auto second = std::make_unique<SamplePlayHandle>(clip, clip->sampleWindow());
			QCOMPARE(pool->readyVoices(), 0);
			auto third = std::make_unique<SamplePlayHandle>(clip, clip->sampleWindow());
			QCOMPARE(pool->misses(), std::uint64_t{1});
			// The fallback still renders: WSOLA, not silence.
			std::vector<SampleFrame> buffer(4096);
			for (int period = 0; period < 8; ++period) { third->play(std::span<SampleFrame>(buffer)); }
			double energy = 0.0;
			for (const auto& frame : buffer) { energy += frame[0] * frame[0]; }
			QVERIFY2(energy > 1.0, "the fallback handle rendered silence");
		}
		RubberBandPool::recycleNow();
		QCOMPARE(pool->readyVoices(), RubberBandPool::VoiceCount);
		// And the background recycler does the same without being asked.
		{ SamplePlayHandle handle(clip, clip->sampleWindow()); }
		QTRY_COMPARE_WITH_TIMEOUT(pool->readyVoices(), RubberBandPool::VoiceCount, 2000);
	}

	void theModeRoundTripsThroughTheProjectFile()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyTwoTimesWarp(*clip, m_rate);
		clip->setWarpStretchMode(WarpStretchMode::RubberBand);
		QDomDocument document;
		QDomElement parent = document.createElement("track");
		const QDomElement saved = clip->saveState(document, parent);
		QCOMPARE(saved.firstChildElement("warp").attribute("stretch"), QStringLiteral("rubberband"));

		SampleTrack reloadedTrack(Engine::getSong());
		auto* reloaded = makeClip(reloadedTrack, m_rate);
		reloaded->restoreState(saved);
		QCOMPARE(static_cast<int>(reloaded->warpStretchMode()), static_cast<int>(WarpStretchMode::RubberBand));
		// The restore built the voices, off the audio thread, before any handle.
		QCOMPARE(reloaded->rubberBandPool() != nullptr, RubberBandPool::available());

		// A value this build does not know is the default, not an error.
		QDomElement unknown = saved.cloneNode(true).toElement();
		unknown.firstChildElement("warp").setAttribute("stretch", "elastique");
		reloaded->restoreState(unknown);
		QCOMPARE(static_cast<int>(reloaded->warpStretchMode()), static_cast<int>(WarpStretchMode::Resample));
	}

	void theStretchCommandDrivesTheMode()
	{
		const ControlResult trackAdded = run(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("sample")}, {QStringLiteral("name"), QStringLiteral("RB")}});
		QVERIFY(trackAdded.ok);
		const ControlResult clipAdded = run(QStringLiteral("clip.add"),
			{{QStringLiteral("track"), trackAdded.result.value(QStringLiteral("track"))},
				{QStringLiteral("position"), 0}});
		QVERIFY(clipAdded.ok);
		const QString clip = clipAdded.result.value(QStringLiteral("clip")).toString();
		const QJsonObject args{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), QStringLiteral("rubberband")}};

		QVERIFY2(!run(QStringLiteral("warp.stretch"), args).ok, "a linear clip was accepted");
		QVERIFY(run(QStringLiteral("warp.set"),
			{{QStringLiteral("clip"), clip},
				{QStringLiteral("markers"), QJsonArray{
					QJsonObject{{QStringLiteral("source_frame"), 0}, {QStringLiteral("offset_ticks"), 0}},
					QJsonObject{{QStringLiteral("source_frame"), 44100}, {QStringLiteral("offset_ticks"), 48}}}}}).ok);
		const ControlResult stretched = run(QStringLiteral("warp.stretch"), args);
		QVERIFY2(stretched.ok, qPrintable(stretched.errorMessage));
		QCOMPARE(stretched.result.value(QStringLiteral("stretch")).toString(), QStringLiteral("rubberband"));
		QCOMPARE(stretched.result.value(QStringLiteral("stretch_algorithm")).toString(), QStringLiteral("rubberband"));
		QCOMPARE(stretched.result.value(QStringLiteral("previous_mode")).toString(), QStringLiteral("resample"));

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), clip}})
			.result.value(QStringLiteral("stretch")).toString(), QStringLiteral("resample"));
	}

private:
	int m_rate = 44100;
};

QTEST_GUILESS_MAIN(SampleClipRubberBandTest)
#include "SampleClipRubberBandTest.moc"
