/*
 * ClipFadesRenderTest.cpp - clip fades, crossfades and clip gain ON THE AUDIO
 *                           PATH, measured from rendered files.
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
 */

//! Everything here renders the song through the REAL export path (ProjectRenderer,
//! the code `lmms render` uses) and measures the written WAV. The arithmetic
//! these renders are compared against is asserted separately, without audio, by
//! tests/src/core/ClipEditsTest.cpp.
//!
//! The claims:
//!  1. A clip nobody has edited renders BIT FOR BIT what it rendered before this
//!     feature existed. No comparison against an older build is possible from
//!     inside one build, so it is proved the way it actually holds: the envelope
//!     is skipped wholesale for a neutral clip, and this test renders the same
//!     song three ways - untouched, with an explicit `ClipEdits{}` assigned, and
//!     with gain set to exactly unity - and requires ONE hash. The negative
//!     control in the same test adds a real fade and requires the hash to MOVE,
//!     so the equality cannot pass vacuously.
//!  2. A fade of N ticks ramps over exactly N ticks of the timeline, measured
//!     against the closed form in both directions.
//!  3. Two clips crossfaded with equal-power ramps render the SUM OF THE TWO
//!     RAMPS, which is where a double-counted gain or a ramp longer than the
//!     overlap would show.
//!  4. Clip gain of -6 dB halves the amplitude.
//!  5. The render loop allocates nothing, and the preview path - which shares
//!     `Sample::render` with the clip path - is NOT faded.

#include <QtTest>

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "AllocationProbe.h"

#include "AudioEngine.h"
#include "ClipEdits.h"
#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "Sample.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr int kSampleRate = 44100;
constexpr float kLevel = 0.5f;
constexpr f_cnt_t kPeriod = 1024;
constexpr double kHalfPi = 1.5707963267948966;

//! One second of source, in the ticks a clip length uses: with the clip this
//! long, a fade of half the clip is half the rendered audio.
int clipTicksForOneSecond()
{
	return static_cast<int>(kSampleRate / Engine::framesPerTick(kSampleRate));
}

f_cnt_t framesPerTick()
{
	return static_cast<f_cnt_t>(Engine::framesPerTick(kSampleRate));
}

/*! The output frames the clip's OWN audio occupies.
 *
 *  A whole-project render is rounded up to a whole bar (`Song::updateLength`),
 *  so the file is longer than the clip: everything past this frame is the
 *  arrangement's silence, and a measurement taken there would pass for any
 *  envelope at all. Every assertion below stays inside this span. */
int clipAudioFrames(int ticks)
{
	return static_cast<int>(static_cast<f_cnt_t>(ticks) * framesPerTick());
}

//! A constant level, so the sample value identifies the envelope and nothing
//! else: a constant survives the resampler at any ratio (the same technique
//! tests/src/tracks/SampleClipWindowTest.cpp uses).
std::shared_ptr<const SampleBuffer> constantBuffer(int seconds)
{
	const auto frames = static_cast<std::size_t>(seconds) * kSampleRate;
	return std::make_shared<const SampleBuffer>(
		std::vector<SampleFrame>(frames, SampleFrame(kLevel, kLevel)).data(), frames, kSampleRate);
}

void printEvidence(const char* label, const QByteArray& data)
{
	const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
	std::fprintf(stdout, "AB_EVIDENCE %s bytes=%d sha256=%s\n",
		label, static_cast<int>(data.size()), hash.constData());
	std::fflush(stdout);
}

// Just enough of the 16-bit PCM WAV format for the renderer's own output: the
// offset of the `data` chunk's payload, or -1.
int dataOffset(const QByteArray& wav)
{
	int offset = 12;
	while (offset + 8 <= wav.size())
	{
		qint32 size = 0;
		std::memcpy(&size, wav.constData() + offset + 4, sizeof(size));
		if (wav.mid(offset, 4) == QByteArrayLiteral("data")) { return offset + 8; }
		offset += 8 + size + (size & 1);
	}
	return -1;
}

int wavFrameCount(const QByteArray& wav)
{
	const int offset = dataOffset(wav);
	return offset < 0 ? 0 : (wav.size() - offset) / 4;
}

//! The left channel at \p frame, scaled to [-1, 1).
float wavLeft(const QByteArray& wav, int frame)
{
	const int offset = dataOffset(wav);
	if (offset < 0) { return 0.0f; }
	qint16 value = 0;
	std::memcpy(&value, wav.constData() + offset + frame * 4, sizeof(value));
	return static_cast<float>(value) / 32768.0f;
}

} // namespace


class ClipFadesRenderTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The dummy device thread renders in the background; this test drives the
		// export synchronously through ProjectRenderer, so stop it.
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase() { Engine::destroy(); }

	//! The release's reproducibility claim in miniature: the feature must not
	//! move a render for a clip that does not use it, and must move one for a
	//! clip that does.
	void aNeutralClipRendersExactlyAsItDidAndARealFadeDoesNot()
	{
		buildSong(clipTicksForOneSecond());
		const QByteArray untouched = render();

		// The value the commands write to mean "no change", assigned the way a
		// handler assigns it: a whole-struct write of the defaults, then of an
		// explicitly unity gain.
		clip()->setClipEdits(ClipEdits{});
		const QByteArray neutralAssigned = render();
		ClipEdits unityGain = ClipEdits{};
		unityGain.gain = 1.0f;
		clip()->setClipEdits(unityGain);
		const QByteArray unityAssigned = render();

		QVERIFY2(!untouched.isEmpty(), "the export produced no file");
		QVERIFY2(untouched != QByteArray(untouched.size(), '\0'), "the render is silent");
		QVERIFY2(untouched == neutralAssigned,
			"assigning neutral edits changed the render - the envelope is not skipped");
		QVERIFY2(untouched == unityAssigned,
			"a unity clip gain changed the render - the neutral path is not exact");
		printEvidence("no-edits", untouched);
		printEvidence("neutral-edits", neutralAssigned);

		ClipEdits real = ClipEdits{};
		real.fadeInTicks = clipTicksForOneSecond() / 4;
		clip()->setClipEdits(real);
		const QByteArray faded = render();
		printEvidence("fade-in", faded);
		QVERIFY2(untouched != faded,
			"a real fade did not change the render - the envelope is not applied");
	}

	//! A fade-in of a quarter of the clip ramps from silence to unity over exactly
	//! those ticks; the rest of the clip is untouched.
	void aFadeInRampsFromSilenceOverExactlyItsTicks()
	{
		const int ticks = clipTicksForOneSecond();
		buildSong(ticks);
		const auto fade = static_cast<int>(ticks / 4);
		ClipEdits edits = ClipEdits{};
		edits.fadeInTicks = fade;
		clip()->setClipEdits(edits);

		const QByteArray wav = render();
		const auto fadeFrames = static_cast<f_cnt_t>(fade) * framesPerTick();
		QVERIFY2(static_cast<f_cnt_t>(wavFrameCount(wav)) >= fadeFrames,
			"the render is shorter than the fade");

		QVERIFY2(std::fabs(wavLeft(wav, 0)) < 0.002f, "the fade-in did not start at silence");
		for (int i = 0; i < 8; ++i)
		{
			const auto at = static_cast<int>(fadeFrames * i / 8);
			const float measured = wavLeft(wav, at);
			const float expected = kLevel * static_cast<float>(i) / 8.0f;
			QVERIFY2(std::fabs(measured - expected) < 0.02f,
				qPrintable(QStringLiteral("fade-in at frame %1 measured %2, the linear shape "
					"predicts %3").arg(at).arg(measured).arg(expected)));
		}
		const float after = wavLeft(wav, static_cast<int>(fadeFrames) + 512);
		QVERIFY2(std::fabs(after - kLevel) < 0.005f,
			qPrintable(QStringLiteral("past the ramp the level is %1, expected %2")
				.arg(after).arg(kLevel)));
	}

	//! The mirror: a fade-out over the last half of the clip, monotonic to within
	//! the 16-bit quantisation, and silent at the end.
	void aFadeOutRampsToSilenceOverExactlyItsTicks()
	{
		const int ticks = clipTicksForOneSecond();
		buildSong(ticks);
		const auto fade = static_cast<int>(ticks / 2);
		ClipEdits edits = ClipEdits{};
		edits.fadeOutTicks = fade;
		clip()->setClipEdits(edits);

		const QByteArray wav = render();
		const auto frames = static_cast<f_cnt_t>(clipAudioFrames(ticks));
		const auto fadeFrames = static_cast<f_cnt_t>(fade) * framesPerTick();
		QVERIFY2(frames >= fadeFrames, "the render is shorter than the fade");

		QVERIFY2(std::fabs(wavLeft(wav, 256) - kLevel) < 0.005f,
			"the fade-out reached back before its own region");

		float previous = wavLeft(wav, static_cast<int>(frames - fadeFrames));
		QVERIFY2(std::fabs(previous - kLevel) < 0.005f,
			"the fade-out does not start at unity");
		for (f_cnt_t f = frames - fadeFrames + 64; f < frames; f += 64)
		{
			const float measured = wavLeft(wav, static_cast<int>(f));
			QVERIFY2(measured <= previous + 1e-4f,
				qPrintable(QStringLiteral("the fade-out is not monotonic at frame %1 "
					"(%2 after %3)").arg(f).arg(measured).arg(previous)));
			previous = measured;
		}
		// The ramp's last frame is 1/fadeFrames of the level, not exactly zero:
		// the region is [frames - fadeFrames, frames) and the gain at its last
		// frame is shape(1/fadeFrames). -87 dBFS for a second-long ramp. That is
		// what buys the crossfade identity below its exactness: this ramp is
		// exactly shape(1 - x) at every point of the region it shares.
		QVERIFY2(std::fabs(previous) < 0.02f, "the fade-out did not reach silence");
	}

	//! Two clips overlapping on one track, crossfaded over exactly their overlap:
	//! the summed audio is the SUM OF THE TWO RAMPS. A double-counted gain, a ramp
	//! longer than the overlap, or two ramps that do not meet would each leave a
	//! different curve here, which is the whole point of measuring it.
	void aCrossfadedPairRendersTheSumOfItsTwoRamps()
	{
		const int ticks = clipTicksForOneSecond();
		buildSong(ticks);
		auto* incoming = dynamic_cast<SampleClip*>(selfTrack()->createClip(TimePos(ticks / 2)));
		QVERIFY(incoming != nullptr);
		incoming->setSampleBuffer(constantBuffer(2));
		incoming->changeLength(TimePos(ticks));
		incoming->setAutoResize(false);

		// The outgoing clip's fade-out and the incoming clip's fade-in are both
		// exactly the overlap, which is the only arrangement in which a crossfade
		// cannot double-count.
		const int overlap = ticks / 2;
		ClipEdits out = ClipEdits{};
		out.fadeOutTicks = overlap;
		out.fadeOutShape = FadeShape::EqualPower;
		ClipEdits in = ClipEdits{};
		in.fadeInTicks = overlap;
		in.fadeInShape = FadeShape::EqualPower;
		clip()->setClipEdits(out);
		incoming->setClipEdits(in);

		song()->updateLength();
		const QByteArray wav = render();

		// The outgoing clip plays from tick 0, so frame 0 of the render is
		// progress 0 of its fade-out and the overlap starts at tick `overlap`.
		const auto overlapStart = static_cast<int>(overlap * framesPerTick());
		for (int i = 0; i <= 8; ++i)
		{
			const double x = i / 8.0;
			const double expected = kLevel * (std::sin((1.0 - x) * kHalfPi) + std::sin(x * kHalfPi));
			const auto at = overlapStart + static_cast<int>(overlapStart * x);
			const float measured = wavLeft(wav, at);
			QVERIFY2(std::fabs(measured - expected) < 0.04f,
				qPrintable(QStringLiteral("crossfade at x=%1 (frame %2) measured %3, the "
					"sum of the two ramps predicts %4").arg(x).arg(at).arg(measured).arg(expected)));
		}
		// Outside the overlap one clip plays alone at the source's own level, both
		// before and after: the "no dip and no bump at the seam" half.
		QVERIFY2(std::fabs(wavLeft(wav, overlapStart / 2) - kLevel) < 0.005f,
			"the outgoing clip is not at unity before the crossfade");
		QVERIFY2(std::fabs(wavLeft(wav, overlapStart * 2 + 1024) - kLevel) < 0.005f,
			"the incoming clip is not at unity after the crossfade");
		printEvidence("crossfade-equal-power", wav);
	}

	//! -6 dB is half the amplitude, for the whole clip, start to end.
	void aGainOfMinusSixDbHalvesTheAmplitude()
	{
		buildSong(clipTicksForOneSecond());
		ClipEdits edits = ClipEdits{};
		edits.gain = gainDbToLinear(-6.0f);
		clip()->setClipEdits(edits);

		const QByteArray wav = render();
		const float expected = kLevel * 0.5011872f;  // 10^(-6/20), the exact ratio
		const int audio = clipAudioFrames(clipTicksForOneSecond());
		for (const int at : { 128, 8192, audio - 512 })
		{
			const float measured = wavLeft(wav, at);
			QVERIFY2(std::fabs(measured - expected) < 0.005f,
				qPrintable(QStringLiteral("-6 dB at frame %1 measured %2, expected %3")
					.arg(at).arg(measured).arg(expected)));
		}
	}

	//! I8: the envelope allocates nothing per period, with both ramps and a gain
	//! live. The handle is constructed OUTSIDE the probe - what allocates on this
	//! path is the handle, not the render loop (the boundary
	//! tests/src/tracks/SampleClipWindowTest.cpp draws).
	void theRenderPathAllocatesNothing()
	{
		SampleTrack track(song());
		auto* subject = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(subject != nullptr);
		subject->setSampleBuffer(constantBuffer(1));
		ClipEdits edits = ClipEdits{};
		edits.gain = gainDbToLinear(-6.0f);
		edits.fadeInTicks = 48;
		edits.fadeOutTicks = 96;
		edits.fadeInShape = FadeShape::EqualPower;
		edits.fadeOutShape = FadeShape::Exponential;
		subject->setClipEdits(edits);

		SamplePlayHandle handle(subject);
		std::vector<SampleFrame> buffer(kPeriod, SampleFrame(0.0f, 0.0f));
		const auto span = std::span<SampleFrame>(buffer);

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int period = 0; period < 64; ++period) { handle.play(span); }
		test::tlCountAllocations = false;
		QCOMPARE(static_cast<qulonglong>(test::tlAllocationCount), static_cast<qulonglong>(0));
	}

	//! The negative control for WHERE the envelope is applied: `Sample::render` is
	//! shared with the browser preview and the metronome, so a handle built from
	//! the same buffer WITHOUT a clip must not be ramped (design §3 row 3, OQ-2).
	void thePreviewPathIsNotFaded()
	{
		SampleTrack track(song());
		auto* subject = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(subject != nullptr);
		const auto buffer = constantBuffer(1);
		subject->setSampleBuffer(buffer);
		subject->changeLength(TimePos(clipTicksForOneSecond()));
		subject->setAutoResize(false);
		ClipEdits edits = ClipEdits{};
		edits.fadeInTicks = clipTicksForOneSecond() / 4;
		subject->setClipEdits(edits);

		std::vector<SampleFrame> clipRendered(kPeriod, SampleFrame(0.0f, 0.0f));
		std::vector<SampleFrame> previewRendered(kPeriod, SampleFrame(0.0f, 0.0f));

		SamplePlayHandle clipHandle(subject);
		clipHandle.play(std::span<SampleFrame>(clipRendered));
		Sample previewSample(buffer);
		SamplePlayHandle previewHandle(&previewSample, false);
		previewHandle.play(std::span<SampleFrame>(previewRendered));

		QVERIFY2(std::fabs(clipRendered[0][0]) < 0.01f,
			"the clip handle did not fade its first frame");
		QVERIFY2(std::fabs(previewRendered[0][0] - kLevel) < 0.005f,
			qPrintable(QStringLiteral("the preview path was faded: first frame %1, expected %2")
				.arg(previewRendered[0][0]).arg(kLevel)));
	}

private:
	Song* song() const { return Engine::getSong(); }

	SampleTrack* selfTrack() const
	{
		const auto& tracks = song()->tracks();
		return tracks.empty() ? nullptr : dynamic_cast<SampleTrack*>(tracks.front());
	}

	SampleClip* clip() const
	{
		return dynamic_cast<SampleClip*>(selfTrack()->getClip(0));
	}

	//! One song: a sample track carrying one constant-level clip from tick 0, with
	//! a source LONGER than the clip so the clip's own length, not the source,
	//! bounds what is rendered.
	void buildSong(int ticks)
	{
		song()->clearProject();
		auto* track = new SampleTrack(song());
		track->setName(QStringLiteral("FadeFixture"));
		auto* newClip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		Q_ASSERT(newClip != nullptr);
		newClip->setSampleBuffer(constantBuffer(2));
		newClip->changeLength(TimePos(ticks));
		newClip->setAutoResize(false);
		song()->updateLength();
	}

	//! Renders the song through the real export path and returns the WAV's bytes.
	QByteArray render()
	{
		const QString path = m_dir.filePath(QStringLiteral("fades-render.wav"));
		QFile::remove(path);
		const OutputSettings settings(kSampleRate, 192,
			OutputSettings::BitDepth::Depth16Bit, OutputSettings::StereoMode::Stereo);
		ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
		if (!renderer.isReady()) { return QByteArray(); }
		renderer.startProcessing();
		renderer.wait();
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) { return QByteArray(); }
		const QByteArray bytes = file.readAll();
		file.close();
		return bytes;
	}

	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(ClipFadesRenderTest)
#include "ClipFadesRenderTest.moc"
