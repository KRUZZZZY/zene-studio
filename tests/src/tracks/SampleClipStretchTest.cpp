/*
 * SampleClipStretchTest.cpp - pitch-preserving time stretch (0.3.0
 *                             feature-list row 30) through the CLIP path.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*! WHY THIS FILE EXISTS.
 *
 *  `AudioStretcherTest` proves the DSP in isolation. This file proves the
 *  THING THE TASK ASKS FOR: that the warp/clip path can select it, that the
 *  selection is persisted and drivable, and that a warped clip rendered
 *  through the real path (`SampleClip` -> `SamplePlayHandle::play`, the same
 *  call `SampleTrack::play` drives) comes out with the pitch it went in with
 *  while the resample mode on the SAME clip moves it an octave.
 *
 *  The fixture is the same two-tone signal the DSP test uses (440 Hz at 0.5,
 *  660 Hz at 0.3) and the same measurement (a Goertzel bin per frequency), so
 *  the number printed here and the number printed there are comparable, and
 *  the claim is about SAMPLES THAT CAME OUT OF THE CLIP, not about a claim in
 *  a comment.
 *
 *  The warp is the 0.3.0 warp engine's own: two markers pinning the clip's
 *  2 s to 96 ticks instead of 192 - a 2x rate - which is exactly the mapping
 *  docs/WARP.md §3 says "changes pitch" today.
 */

#include <QtTest>

#include <QDomDocument>
#include <QJsonObject>
#include <QString>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "../core/AllocationProbe.h"

#include "AudioEngine.h"
#include "Clip.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "WarpMarkers.h"

using namespace lmms;

namespace
{

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kLowTone = 440.0;
constexpr double kHighTone = 660.0;
constexpr int kSourceSeconds = 2;

//! The wire-facing counts print as unsigned 64-bit, the same helper the warp
//! tests use.
template <typename T>
qulonglong N(T value) { return static_cast<qulonglong>(value); }

//! The fixture tone: two components, one that a 2x resample would move to 880
//! and one to 1320, so "did the pitch move" is a question with an answer.
std::vector<SampleFrame> makeTwoTone(int rate)
{
	const auto frames = static_cast<f_cnt_t>(kSourceSeconds) * rate;
	std::vector<SampleFrame> data(static_cast<std::size_t>(frames));
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const auto value = static_cast<sample_t>(0.5 * std::sin(kTwoPi * kLowTone * f / rate)
			+ 0.3 * std::sin(kTwoPi * kHighTone * f / rate));
		data[static_cast<std::size_t>(f)] = SampleFrame(value, value);
	}
	return data;
}

SampleClip* makeClip(SampleTrack& track, int rate)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	auto data = makeTwoTone(rate);
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

/*! Pins the clip's own 2 s to HALF the ticks the project's rate would give it:
 *  a 2x warp, authored as the two markers the warp engine's own tests use. The
 *  tick count is derived from the engine's rate rather than hardcoded, so the
 *  test measures 2x at whatever tempo the fixture song is at. */
void applyTwoTimesWarp(SampleClip& clip, int rate)
{
	const auto frames = static_cast<f_cnt_t>(clip->sample().sampleSize());
	const auto ticks = static_cast<tick_t>(std::llround(frames / (2.0 * Engine::framesPerTick(rate))));
	const std::array<WarpMarker, 2> markers{ WarpMarker{ 0, 0 }, WarpMarker{ frames, ticks } };
	QVERIFY(clip.setWarpMarkers(std::span<const WarpMarker>(markers.data(), markers.size())));
}

//! Amplitude at \a freq (Goertzel), the same measurement the DSP test makes.
double amplitudeAt(const std::vector<SampleFrame>& data, int from, int to, double freq, double rate)
{
	const int frames = to - from;
	if (frames <= 8) { return 0.0; }
	const double cycles = std::round(frames * freq / rate);
	const double omega = kTwoPi * cycles / frames;
	const double coefficient = 2.0 * std::cos(omega);
	double previous = 0.0;
	double previousPrevious = 0.0;
	for (int i = from; i < to; ++i)
	{
		const double current = data[static_cast<std::size_t>(i)][0] + coefficient * previous - previousPrevious;
		previousPrevious = previous;
		previous = current;
	}
	const double real = previous - previousPrevious * std::cos(omega);
	const double imaginary = previousPrevious * std::sin(omega);
	return 2.0 * std::sqrt(real * real + imaginary * imaginary) / frames;
}

/*! Renders a clip through the real playback path, in engine-sized periods.
 *
 *  `SamplePlayHandle(clip, clip->sampleWindow())` is the constructor
 *  `SampleTrack::play` uses, so this is the pass a user's playback takes; the
 *  handle is asked for one period at a time exactly as the audio engine asks.
 */
std::vector<SampleFrame> renderClip(SampleClip* clip, int periodFrames = 512)
{
	SamplePlayHandle handle(clip, clip->sampleWindow());
	const auto total = static_cast<int>(handle.totalFrames());
	std::vector<SampleFrame> rendered;
	rendered.reserve(static_cast<std::size_t>(total));
	std::vector<SampleFrame> buffer(static_cast<std::size_t>(periodFrames));
	int guard = 0;
	while (static_cast<int>(handle.framesDone()) < total && guard < 16)
	{
		std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
		handle.play(std::span<SampleFrame>(buffer));
		rendered.insert(rendered.end(), buffer.begin(), buffer.end());
		++guard;
	}
	rendered.resize(static_cast<std::size_t>(total));
	return rendered;
}

void evidence(const char* label, double a, double b = 0.0, double c = 0.0, double d = 0.0)
{
	std::printf("CLIP_STRETCH_EVIDENCE %-44s %10.4f %10.4f %10.4f %10.4f\n", label, a, b, c, d);
	std::fflush(stdout);
}

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A fresh SAMPLECLIP created through the product's own commands (the
//! ControlWarpCommandsTest fixture), so the id comes back from the engine.
QString makeCommandClip()
{
	const ControlResult track = run(QStringLiteral("track.add"),
		{{QStringLiteral("type"), QStringLiteral("sample")},
			{QStringLiteral("name"), QStringLiteral("Stretch Target")}});
	if (!track.ok) { return QString(); }
	const ControlResult clip = run(QStringLiteral("clip.add"),
		{{QStringLiteral("track"), track.result.value(QStringLiteral("track"))},
			{QStringLiteral("position"), 0}});
	if (!clip.ok) { return QString(); }
	return clip.result.value(QStringLiteral("clip")).toString();
}

QString stretchOf(const QString& clip)
{
	return run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), clip}})
		.result.value(QStringLiteral("stretch")).toString();
}

} // namespace


class SampleClipStretchTest : public QObject
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

	/*! THE ACCEPTANCE ITEM, through the clip path: one warped clip, two
	 *  render modes, the pitch measured in both.
	 *
	 *  Both renders are the SAME clip, the SAME markers and the SAME mapping,
	 *  so the only thing that differs between the two rows is the mode this
	 *  lane added. That is what makes the comparison a measurement of the
	 *  feature rather than of the fixture.
	 */
	void aWarpedClipKeepsItsPitchWhenItAsksToAndLosesItWhenItDoesNot()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyTwoTimesWarp(*clip, m_rate);
		QVERIFY(!clip->rendersLinearly());
		QCOMPARE(static_cast<int>(clip->warpStretchMode()), static_cast<int>(WarpStretchMode::Resample));

		// (1) The mode the engine had before this lane: plain resampling.
		const std::vector<SampleFrame> resampled = renderClip(clip);

		// (2) The mode this lane adds.
		clip->setWarpStretchMode(WarpStretchMode::PreservePitch);
		QCOMPARE(static_cast<int>(clip->warpStretchMode()), static_cast<int>(WarpStretchMode::PreservePitch));
		const std::vector<SampleFrame> stretched = renderClip(clip);

		QVERIFY(!stretched.empty());
		QCOMPARE(static_cast<int>(resampled.size()), static_cast<int>(stretched.size()));

		const int frames = static_cast<int>(stretched.size());
		const int from = frames / 4;
		const int to = frames * 3 / 4;
		const double resampledLow = amplitudeAt(resampled, from, to, kLowTone, m_rate);
		const double resampledHigh = amplitudeAt(resampled, from, to, kHighTone, m_rate);
		const double resampledDoubled = amplitudeAt(resampled, from, to, 2.0 * kLowTone, m_rate);
		const double resampledTripled = amplitudeAt(resampled, from, to, 2.0 * kHighTone, m_rate);
		const double stretchedLow = amplitudeAt(stretched, from, to, kLowTone, m_rate);
		const double stretchedHigh = amplitudeAt(stretched, from, to, kHighTone, m_rate);
		const double stretchedDoubled = amplitudeAt(stretched, from, to, 2.0 * kLowTone, m_rate);
		const double stretchedTripled = amplitudeAt(stretched, from, to, 2.0 * kHighTone, m_rate);

		evidence("rendered frames", frames);
		evidence("resample: 440 / 660 / 880 / 1320",
			resampledLow, resampledHigh, resampledDoubled, resampledTripled);
		evidence("preserve: 440 / 660 / 880 / 1320",
			stretchedLow, stretchedHigh, stretchedDoubled, stretchedTripled);

		// The control really is the historical behaviour: the tones moved up
		// an octave and nothing is left where they were.
		QVERIFY2(resampledDoubled > 0.40 && resampledTripled > 0.24,
			qPrintable(QStringLiteral("the resample render did not move the pitch (880 = %1, 1320 = %2)")
				.arg(resampledDoubled).arg(resampledTripled)));
		QVERIFY2(resampledLow < 0.05 && resampledHigh < 0.05,
			"the resample render kept energy at the original frequencies: the fixture cannot "
			"distinguish the two modes");

		// And the clip's new mode kept them.
		QVERIFY2(std::fabs(stretchedLow - 0.5) < 0.05,
			qPrintable(QStringLiteral("preserve_pitch rendered %1 at 440 Hz, the source has 0.5")
				.arg(stretchedLow)));
		QVERIFY2(std::fabs(stretchedHigh - 0.3) < 0.05,
			qPrintable(QStringLiteral("preserve_pitch rendered %1 at 660 Hz, the source has 0.3")
				.arg(stretchedHigh)));
		QVERIFY2(stretchedDoubled < 0.05 && stretchedTripled < 0.05,
			qPrintable(QStringLiteral("preserve_pitch moved the pitch anyway (880 = %1, 1320 = %2)")
				.arg(stretchedDoubled).arg(stretchedTripled)));

		// Same timeline: a 2x warp is 2x SHORTER in both modes; the difference
		// is the waveform, not the length.
		const auto expectedFrames = static_cast<int>(std::llround(
			static_cast<double>(clip->sample().sampleSize())
			/ (2.0 * Engine::framesPerTick(m_rate)) * Engine::framesPerTick(m_rate)));
		QVERIFY2(std::abs(frames - expectedFrames) <= 1,
			qPrintable(QStringLiteral("the render is %1 frames, the mapping says %2")
				.arg(frames).arg(expectedFrames)));
	}

	/*! BEHAVIOUR PRESERVATION, and the reason the default is `Resample`: a clip
	 *  that renders linearly is never routed through the stretcher, even when
	 *  the mode says preserve_pitch. Both renders below are byte-identical -
	 *  not "close", identical - so the mode cannot change the sound of a clip
	 *  that has no rate change to render.
	 */
	void aClipWithoutARateChangeRendersIdenticallyInBothModes()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		QVERIFY(clip->rendersLinearly());

		clip->setWarpStretchMode(WarpStretchMode::PreservePitch);
		const std::vector<SampleFrame> askedForStretch = renderClip(clip);

		clip->setWarpStretchMode(WarpStretchMode::Resample);
		const std::vector<SampleFrame> resampled = renderClip(clip);

		QVERIFY(!askedForStretch.empty());
		QCOMPARE(static_cast<int>(askedForStretch.size()), static_cast<int>(resampled.size()));
		int differing = 0;
		for (std::size_t i = 0; i < resampled.size(); ++i)
		{
			if (askedForStretch[i][0] != resampled[i][0] || askedForStretch[i][1] != resampled[i][1])
			{
				++differing;
			}
		}
		evidence("linear clip, both modes: differing frames", static_cast<double>(differing),
			static_cast<double>(resampled.size()));
		QCOMPARE(differing, 0);
	}

	/*! The mode is the clip's own serialized state: it round-trips through the
	 *  same `<warp>` element the markers ride in, and a file that does not
	 *  carry the attribute loads as `resample` - which is every project
	 *  written before this feature.
	 */
	void theStretchModeRoundTripsThroughTheProjectFile()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyTwoTimesWarp(*clip, m_rate);
		clip->setWarpStretchMode(WarpStretchMode::PreservePitch);

		QDomDocument document;
		QDomElement parent = document.createElement("track");
		const QDomElement saved = clip->saveState(document, parent);
		const QDomElement warp = saved.firstChildElement("warp");
		QVERIFY2(!warp.isNull(), "a warped clip must write a <warp> element");
		QCOMPARE(warp.attribute("stretch"), QStringLiteral("wsola"));

		SampleTrack reloadedTrack(Engine::getSong());
		auto* reloaded = makeClip(reloadedTrack, m_rate);
		reloaded->setWarpStretchMode(WarpStretchMode::PreservePitch);
		reloaded->setWarpStretchMode(WarpStretchMode::Resample);
		reloaded->restoreState(saved);
		QCOMPARE(static_cast<int>(reloaded->warpStretchMode()),
			static_cast<int>(WarpStretchMode::PreservePitch));
		QCOMPARE(N(reloaded->warpMarkers().size()), N(clip->warpMarkers().size()));

		// A clip with no stretch attribute at all - i.e. every file #597 wrote
		// - loads as the historical resampling.
		QDomDocument oldDocument;
		QDomElement oldParent = oldDocument.createElement("track");
		QDomElement oldClip = oldDocument.createElement("sampleclip");
		oldClip.setAttribute("pos", 0);
		oldClip.setAttribute("len", 384);
		oldClip.setAttribute("muted", 0);
		oldClip.setAttribute("off", 0);
		oldClip.setAttribute("autoresize", 0);
		QDomElement oldWarp = oldDocument.createElement("warp");
		oldWarp.setAttribute("mode", "follow");
		oldClip.appendChild(oldWarp);
		oldParent.appendChild(oldClip);
		SampleTrack oldTrack(Engine::getSong());
		auto* oldStyle = makeClip(oldTrack, m_rate);
		oldStyle->setWarpStretchMode(WarpStretchMode::PreservePitch);
		oldStyle->restoreState(oldClip);
		QCOMPARE(static_cast<int>(oldStyle->warpStretchMode()),
			static_cast<int>(WarpStretchMode::Resample));
	}

	/*! DRIVABILITY + A16. `warp.stretch` is the command the parent drives this
	 *  feature with: it refuses the meaningless case (a clip with no rate
	 *  change), it reports its own state and the previous mode, it carries a
	 *  contract row, and `control.undo` takes it back through the clip's
	 *  journal checkpoint.
	 */
	void theStretchCommandRefusesALinearClipAndUndoesItself()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QVERIFY(registry->hasCommand(QStringLiteral("warp.stretch")));
		const ControlCommand* command = registry->command(QStringLiteral("warp.stretch"));
		QVERIFY(command != nullptr);
		QCOMPARE(command->group, QStringLiteral("warp"));
		QCOMPARE(command->verb, QStringLiteral("stretch"));
		QVERIFY(command->mutating);
		QVERIFY(command->requiresDecl.isEmpty());

		const control::ReversibilityEntry* row =
			control::ReversibilityTable::instance().lookup(QStringLiteral("warp.stretch"));
		QVERIFY2(row != nullptr, "warp.stretch has no A16 contract row");
		QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("true_inverse"));
		QVERIFY(row->reversible);
		QVERIFY(!row->reason.isEmpty());
		QVERIFY(!row->mechanism.isEmpty());

		const QString clip = makeCommandClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");

		// A linear clip has no rate change to render: REFUSED, typed, with
		// nothing written.
		const ControlResult refused = run(QStringLiteral("warp.stretch"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), QStringLiteral("preserve_pitch")}});
		QVERIFY2(!refused.ok, "warp.stretch accepted a clip that renders linearly");
		QCOMPARE(stretchOf(clip), QStringLiteral("resample"));

		// A mode the engine does not know is invalid_args.
		const ControlResult bogus = run(QStringLiteral("warp.stretch"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), QStringLiteral("vocoder")}});
		QVERIFY(!bogus.ok);

		// With a warp on the clip, the mode is accepted and reported.
		QVERIFY(run(QStringLiteral("warp.set"),
			{{QStringLiteral("clip"), clip},
				{QStringLiteral("markers"), QJsonArray{
					QJsonObject{{QStringLiteral("source_frame"), 0}, {QStringLiteral("offset_ticks"), 0}},
					QJsonObject{{QStringLiteral("source_frame"), 44100}, {QStringLiteral("offset_ticks"), 48}}}}}).ok);
		const ControlResult stretched = run(QStringLiteral("warp.stretch"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), QStringLiteral("preserve_pitch")}});
		QVERIFY2(stretched.ok, qPrintable(stretched.errorMessage));
		QCOMPARE(stretched.result.value(QStringLiteral("stretch")).toString(),
			QStringLiteral("preserve_pitch"));
		QCOMPARE(stretched.result.value(QStringLiteral("previous_mode")).toString(),
			QStringLiteral("resample"));
		QCOMPARE(stretched.result.value(QStringLiteral("changed")).toBool(), true);
		QCOMPARE(stretched.result.value(QStringLiteral("stretch_algorithm")).toString(),
			QStringLiteral("wsola"));
		QCOMPARE(stretchOf(clip), QStringLiteral("preserve_pitch"));

		// A16: the registry stamped the class from the table, and the recorded
		// inverse is this same command with the before-state's own mode.
		const ControlRegistry::Transaction* transaction = registry->lastTransaction();
		QVERIFY(transaction != nullptr);
		QCOMPARE(transaction->command, QStringLiteral("warp.stretch"));
		QCOMPARE(transaction->cls, QStringLiteral("true_inverse"));
		QCOMPARE(transaction->reversible, true);
		QVERIFY2(transaction->mechanism.contains(QStringLiteral("Clip checkpoint")),
			qPrintable(transaction->mechanism));
		QCOMPARE(transaction->inverse.value(QStringLiteral("op")).toString(),
			QStringLiteral("warp.stretch"));
		QCOMPARE(transaction->inverse.value(QStringLiteral("args")).toObject()
			.value(QStringLiteral("mode")).toString(), QStringLiteral("resample"));

		// And the engine's own undo is the inverse.
		QVERIFY2(run(QStringLiteral("control.undo")).ok, "control.undo refused the stretch edit");
		QCOMPARE(stretchOf(clip), QStringLiteral("resample"));
	}

	/*! I8 on the new path: a stretch handle renders a period without
	 *  allocating. The stretcher is a fixed-size member of the handle and
	 *  `prepare()` is the only call that fills anything. */
	void aStretchedClipRendersWithoutAllocating()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeClip(track, m_rate);
		applyTwoTimesWarp(*clip, m_rate);
		clip->setWarpStretchMode(WarpStretchMode::PreservePitch);

		SamplePlayHandle handle(clip, clip->sampleWindow());
		std::vector<SampleFrame> buffer(1024, SampleFrame(0.0f, 0.0f));

		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int period = 0; period < 16; ++period)
		{
			handle.play(std::span<SampleFrame>(buffer));
		}
		test::tlCountAllocations = false;
		evidence("allocations over 16 stretched periods", static_cast<double>(test::tlAllocationCount));
		QCOMPARE(N(test::tlAllocationCount), N(0));
	}

private:
	int m_rate = 44100;
};

QTEST_GUILESS_MAIN(SampleClipStretchTest)
#include "SampleClipStretchTest.moc"
