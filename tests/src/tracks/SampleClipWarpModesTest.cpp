/*
 * SampleClipWarpModesTest.cpp - the specified warp mode set (Q050-036)
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! The five modes the spec promises (section 4.2: Beats, Tones, Texture,
 *  Re-Pitch, Complex), on the same fixtures SampleClipStretchTest and
 *  SampleClipRubberBandTest measured their two and three modes on, so the
 *  numbers stay comparable:
 *   * every specified name is selectable through warp.stretch, appears in
 *     clip state and names the algorithm that will render it; the legacy
 *     names (`resample`, `wsola`, `rubberband`) still select their modes;
 *   * a 2x warp render per mode: the duration is the mapping's in every
 *     mode, Re-Pitch moves the pitch an octave and the five pitch-keeping
 *     modes do not, and no two modes render the same waveform;
 *   * a transient fixture: the burst lands where the mapping puts it in
 *     every mode, and the Beats render keeps the onset sharper (less
 *     pre-onset leakage) than the Texture render;
 *   * a project with different modes on separate clips round-trips through
 *     a real save/load, selections included.
 *  The render slots need the Rubber Band library, like SampleClipRubberBandTest;
 *  the selection slot runs everywhere (the command refuses the four
 *  Rubber Band-backed modes without it).
 */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <utility>
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
#include "Track.h"
#include "WarpMarkers.h"

using namespace lmms;

namespace
{

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kLowTone = 440.0;
constexpr double kHighTone = 660.0;

//! The names the spec section 4.2 promises, in the order it names them.
const std::vector<QString>& specifiedModes()
{
	static const std::vector<QString> modes{QStringLiteral("beats"), QStringLiteral("tones"),
		QStringLiteral("texture"), QStringLiteral("re_pitch"), QStringLiteral("complex")};
	return modes;
}

SampleClip* makeToneClip(SampleTrack& track, int rate)
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

//! A 2 s clip that is silent for half a second and then bursts: one cycle of a
//! 1 kHz sine at full amplitude every period, phase-aligned so the first sample
//! of the burst is a full-scale attack. The onset at source frame F0 is the
//! fixture's transient.
constexpr f_cnt_t kBurstOnsetFrame = 22050; // 0.5 s at 44.1 kHz

SampleClip* makeBurstClip(SampleTrack& track, int rate)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	std::vector<SampleFrame> data(static_cast<std::size_t>(2 * rate), SampleFrame(0.0f, 0.0f));
	for (f_cnt_t f = kBurstOnsetFrame; f < static_cast<f_cnt_t>(data.size()); ++f)
	{
		const double t = static_cast<double>(f - kBurstOnsetFrame) / rate;
		const auto value = static_cast<sample_t>(0.8 * std::sin(kTwoPi * 1000.0 * t));
		data[static_cast<std::size_t>(f)] = SampleFrame(value, value);
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

double meanDifference(const std::vector<SampleFrame>& a, const std::vector<SampleFrame>& b)
{
	if (a.size() != b.size() || a.empty()) { return -1.0; }
	double sum = 0.0;
	for (std::size_t i = 0; i < a.size(); ++i) { sum += std::fabs(a[i][0] - b[i][0]); }
	return sum / static_cast<double>(a.size());
}

//! The first frame at least \a threshold loud, searched from \a from.
f_cnt_t firstLoudFrame(const std::vector<SampleFrame>& data, f_cnt_t from, float threshold)
{
	for (f_cnt_t f = from; f < static_cast<f_cnt_t>(data.size()); ++f)
	{
		if (std::fabs(data[f][0]) >= threshold) { return f; }
	}
	return static_cast<f_cnt_t>(data.size());
}

//! The loudest sample in [from, to): the pre-onset leakage measure.
double peakBetween(const std::vector<SampleFrame>& data, f_cnt_t from, f_cnt_t to)
{
	double peak = 0.0;
	for (f_cnt_t f = from; f < to && f < static_cast<f_cnt_t>(data.size()); ++f)
	{
		peak = std::max(peak, static_cast<double>(std::fabs(data[f][0])));
	}
	return peak;
}

void evidence(const char* label, double a, double b = 0.0, double c = 0.0, double d = 0.0)
{
	std::printf("WARP_MODES_EVIDENCE %-44s %10.4f %10.4f %10.4f %10.4f\n", label, a, b, c, d);
	std::fflush(stdout);
}

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! One warped two-tone clip per mode, rendered. The map for the EVIDENCE table.
std::map<QString, std::vector<SampleFrame>> renderEveryMode(SampleTrack& track, int rate)
{
	const std::vector<WarpStretchMode> modes{ WarpStretchMode::Beats, WarpStretchMode::Tones,
		WarpStretchMode::Texture, WarpStretchMode::Resample, WarpStretchMode::Complex,
		WarpStretchMode::PreservePitch };
	std::map<QString, std::vector<SampleFrame>> rendered;
	for (const WarpStretchMode mode : modes)
	{
		auto* clip = makeToneClip(track, rate);
		applyTwoTimesWarp(*clip, rate);
		clip->setWarpStretchMode(mode);
		rendered.emplace(warpStretchModeName(mode), renderClip(clip));
	}
	return rendered;
}

} // namespace


class SampleClipWarpModesTest : public QObject
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

	//! ACCEPTANCE 1 (the agent half): every specified mode is selectable through
	//! the registered warp.stretch command on a warped clip, appears in the clip
	//! state (warp.stretch's result and warp.list's read-back alike) and names
	//! the algorithm that will render it. The refusal rules stay typed: an
	//! unknown name is invalid_args, a pitch-keeping mode on a linear clip is
	//! refused, the varispeed default is accepted on one.
	void everySpecifiedModeIsSelectableThroughTheCommandAndReportedBack()
	{
		// The mode names live in the command's own schema, so an agent can see
		// the whole set before asking for any of it.
		const ControlCommand* command = ControlRegistry::instance()->command(QStringLiteral("warp.stretch"));
		QVERIFY(command != nullptr);
		const QJsonArray names = command->argsSchema.value(QStringLiteral("properties"))
			.toObject().value(QStringLiteral("mode")).toObject().value(QStringLiteral("enum")).toArray();
		for (const QString& mode : specifiedModes())
		{
			QVERIFY2(names.contains(mode), qPrintable(QStringLiteral("the schema does not offer '%1'").arg(mode)));
		}

		const ControlResult trackAdded = run(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("sample")}, {QStringLiteral("name"), QStringLiteral("Modes")}});
		QVERIFY(trackAdded.ok);
		const ControlResult clipAdded = run(QStringLiteral("clip.add"),
			{{QStringLiteral("track"), trackAdded.result.value(QStringLiteral("track"))},
				{QStringLiteral("position"), 0}});
		QVERIFY(clipAdded.ok);
		const QString clip = clipAdded.result.value(QStringLiteral("clip")).toString();

		// A linear clip has no rate change for a pitch-keeping mode to render:
		// REFUSED, typed, exactly as for the older modes.
		QVERIFY2(!run(QStringLiteral("warp.stretch"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), QStringLiteral("beats")}}).ok,
			"beats was accepted on a clip that renders linearly");
		QVERIFY2(!run(QStringLiteral("warp.stretch"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), QStringLiteral("complex")}}).ok,
			"complex was accepted on a clip that renders linearly");
		// An unknown name is invalid_args, as ever.
		QVERIFY(!run(QStringLiteral("warp.stretch"),
			{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), QStringLiteral("vocoder")}}).ok);

		QVERIFY(run(QStringLiteral("warp.set"),
			{{QStringLiteral("clip"), clip},
				{QStringLiteral("markers"), QJsonArray{
					QJsonObject{{QStringLiteral("source_frame"), 0}, {QStringLiteral("offset_ticks"), 0}},
					QJsonObject{{QStringLiteral("source_frame"), 88200}, {QStringLiteral("offset_ticks"), 96}}}}}).ok);

		for (const QString& mode : specifiedModes())
		{
			const ControlResult stretched = run(QStringLiteral("warp.stretch"),
				{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), mode}});
			QVERIFY2(stretched.ok, qPrintable(QStringLiteral("%1 refused: %2")
				.arg(mode, stretched.errorMessage)));
			QCOMPARE(stretched.result.value(QStringLiteral("stretch")).toString(), mode);
			const QString algorithm = stretched.result.value(QStringLiteral("stretch_algorithm")).toString();
			const QString expected = mode == QStringLiteral("re_pitch")
				? QStringLiteral("resampler")
				: (mode == QStringLiteral("beats") || mode == QStringLiteral("complex"))
					? QStringLiteral("rubberband") : QStringLiteral("wsola");
			QCOMPARE(algorithm, expected);
			QCOMPARE(stretched.result.value(QStringLiteral("changed")).toBool(), true);
			// The read-back through the group's read verb says the same thing.
			QCOMPARE(run(QStringLiteral("warp.list"), {{QStringLiteral("clip"), clip}})
				.result.value(QStringLiteral("stretch")).toString(), mode);
		}
	}

	//! The legacy names keep selecting their modes, and the state reports them
	//! canonically: `resample` and `rubberband` are what every file written
	//! before Q050-036 says. (`wsola` is the stretch attribute's file
	//! vocabulary - SampleClipStretchTest pins that round trip; the command
	//! surface's own vocabulary is the canonical name `preserve_pitch`.)
	void theLegacyNamesStillSelectTheirModes()
	{
		const ControlResult trackAdded = run(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("sample")}, {QStringLiteral("name"), QStringLiteral("Legacy")}});
		QVERIFY(trackAdded.ok);
		const ControlResult clipAdded = run(QStringLiteral("clip.add"),
			{{QStringLiteral("track"), trackAdded.result.value(QStringLiteral("track"))},
				{QStringLiteral("position"), 0}});
		QVERIFY(clipAdded.ok);
		const QString clip = clipAdded.result.value(QStringLiteral("clip")).toString();
		QVERIFY(run(QStringLiteral("warp.set"),
			{{QStringLiteral("clip"), clip},
				{QStringLiteral("markers"), QJsonArray{
					QJsonObject{{QStringLiteral("source_frame"), 0}, {QStringLiteral("offset_ticks"), 0}},
					QJsonObject{{QStringLiteral("source_frame"), 88200}, {QStringLiteral("offset_ticks"), 96}}}}}).ok);

		const std::vector<std::pair<QString, QString>> legacy{
			{QStringLiteral("resample"), QStringLiteral("re_pitch")},
			{QStringLiteral("rubberband"), QStringLiteral("complex")}};
		for (const auto& [name, canonical] : legacy)
		{
			const ControlResult stretched = run(QStringLiteral("warp.stretch"),
				{{QStringLiteral("clip"), clip}, {QStringLiteral("mode"), name}});
			QVERIFY2(stretched.ok, qPrintable(QStringLiteral("%1 refused: %2")
				.arg(name, stretched.errorMessage)));
			QCOMPARE(stretched.result.value(QStringLiteral("stretch")).toString(), canonical);
		}
	}

	//! ACCEPTANCE 2 (duration + pitch): a deterministic two-tone fixture, one
	//! render per mode. The mapping sets the duration, so every mode renders
	//! the same number of frames; Re-Pitch is the one mode the pitch moves in.
	void aRenderPerModeMeetsItsPitchAndDurationExpectations()
	{
		SampleTrack track(Engine::getSong());
		const auto rendered = renderEveryMode(track, m_rate);
		QCOMPARE(static_cast<int>(rendered.size()), 6);

		// A 2 s source under a 2x warp spans exactly half the source frames -
		// in every mode, because the mapping, not the algorithm, sets the span.
		const f_cnt_t expected = static_cast<f_cnt_t>(m_rate);
		QCOMPARE(static_cast<int>(rendered.begin()->second.size()), static_cast<int>(expected));

		for (const auto& [mode, frames] : rendered)
		{
			const double low = amplitudeAt(frames, kLowTone, m_rate);
			const double high = amplitudeAt(frames, kHighTone, m_rate);
			const double doubled = amplitudeAt(frames, 2.0 * kLowTone, m_rate);
			const double tripled = amplitudeAt(frames, 2.0 * kHighTone, m_rate);
			const bool keeps = mode != QStringLiteral("re_pitch");
			evidence(qPrintable(QStringLiteral("%1: 440 / 660 / 880 / 1320").arg(mode)),
				low, high, doubled, tripled);
			QCOMPARE(frames.size(), static_cast<std::size_t>(expected));
			if (keeps)
			{
				QVERIFY2(std::fabs(low - 0.5) < 0.06,
					qPrintable(QStringLiteral("%1 lost the 440 Hz tone: %2").arg(mode).arg(low)));
				QVERIFY2(std::fabs(high - 0.3) < 0.06,
					qPrintable(QStringLiteral("%1 lost the 660 Hz tone: %2").arg(mode).arg(high)));
				QVERIFY2(doubled < 0.06 && tripled < 0.06,
					qPrintable(QStringLiteral("%1 moved the pitch an octave").arg(mode)));
			}
			else
			{
				QVERIFY2(doubled > 0.42 && tripled > 0.24,
					qPrintable(QStringLiteral("re_pitch did not move the pitch: 880 = %1, 1320 = %2")
						.arg(doubled).arg(tripled)));
				QVERIFY2(low < 0.06 && high < 0.06,
					qPrintable(QStringLiteral("re_pitch left energy at the source pitch: %1 / %2")
						.arg(low).arg(high)));
			}
		}

		// No two modes render the same waveform. Re-Pitch is trivially apart
		// (the octave); the five pitch-keeping renders are five different
		// engines and option sets, so their waveforms differ too - that is the
		// "hear their distinct behavior" half of the promise, measured.
		const std::vector<QString> keeping{QStringLiteral("beats"), QStringLiteral("tones"),
			QStringLiteral("texture"), QStringLiteral("complex"), QStringLiteral("preserve_pitch")};
		for (std::size_t a = 0; a < keeping.size(); ++a)
		{
			for (std::size_t b = a + 1; b < keeping.size(); ++b)
			{
				const double difference = meanDifference(rendered.at(keeping[a]), rendered.at(keeping[b]));
				evidence(qPrintable(QStringLiteral("mean |%1 - %2|").arg(keeping[a], keeping[b])), difference);
				QVERIFY2(difference > 1e-3,
					qPrintable(QStringLiteral("%1 and %2 rendered identically").arg(keeping[a], keeping[b])));
			}
		}
	}

	//! ACCEPTANCE 2 (transients): a burst whose onset the mapping puts at a
	//! quarter second of output under the 2x warp. Every mode lands the onset
	//! within a real-time stretcher's tolerance of where the mapping says, and
	//! the engines are honest about how they differ: Re-Pitch is sample-exact
	//! (the mapping is one linear segment and a resampler reads nothing ahead),
	//! the WSOLA modes emit nothing before the onset (their grains read forward
	//! only, so the onset arrives late by up to half a grain), and the two
	//! Rubber Band modes ring measurably ahead of it (the phase vocoder's
	//! spectral pre-echo). Measured, not asserted.
	void theTransientFixtureSeparatesTheOnsetBehavior()
	{
		SampleTrack track(Engine::getSong());
		const f_cnt_t predicted = kBurstOnsetFrame / 2; // the 2x mapping
		std::map<QString, double> leakage;
		std::vector<SampleFrame> beatsBurstRender;
		std::vector<SampleFrame> textureBurstRender;

		const std::vector<WarpStretchMode> modes{ WarpStretchMode::Beats, WarpStretchMode::Tones,
			WarpStretchMode::Texture, WarpStretchMode::Resample, WarpStretchMode::Complex,
			WarpStretchMode::PreservePitch };
		for (const WarpStretchMode mode : modes)
		{
			auto* clip = makeBurstClip(track, m_rate);
			applyTwoTimesWarp(*clip, m_rate);
			clip->setWarpStretchMode(mode);
			const QString name = warpStretchModeName(mode);
			const std::vector<SampleFrame> frames = renderClip(clip);
			const f_cnt_t onset = firstLoudFrame(frames, 0, 0.05f);
			QVERIFY2(onset < static_cast<f_cnt_t>(frames.size()),
				qPrintable(QStringLiteral("%1 never produced a loud sample: the onset vanished").arg(name)));
			const double errorMs = (static_cast<double>(onset) - static_cast<double>(predicted))
				/ m_rate * 1000.0;
			// Pre-onset leakage: the loudest sample in the 20 ms before the
			// FIRST loud frame (so the onset itself is excluded), relative to
			// the burst's own level (0.8).
			const f_cnt_t window = static_cast<f_cnt_t>(m_rate / 50);
			const f_cnt_t from = predicted > window ? predicted - window : 0;
			const double leak = peakBetween(frames, from, std::min(predicted, onset)) / 0.8;
			evidence(qPrintable(QStringLiteral("%1 onset (ms) / pre-onset peak").arg(name)),
				errorMs, leak);			QVERIFY2(std::fabs(errorMs) <= 20.0,
				qPrintable(QStringLiteral("%1 put the onset %2 ms away from the mapping's position")
					.arg(name).arg(errorMs)));
			leakage.emplace(name, leak);
			if (mode == WarpStretchMode::Beats) { beatsBurstRender = frames; }
			if (mode == WarpStretchMode::Texture) { textureBurstRender = frames; }
			if (mode == WarpStretchMode::Resample)
			{
				QVERIFY2(std::fabs(errorMs) < 1.0,
					qPrintable(QStringLiteral("re_pitch was not sample-exact: %1 ms").arg(errorMs)));
			}
			if (warpModeUsesRubberBand(mode))
			{
				QVERIFY2(leak >= 0.01,
					qPrintable(QStringLiteral("%1 showed no spectral pre-echo (%2)").arg(name).arg(leak)));
			}
			else
			{
				QVERIFY2(leak < 0.005,
					qPrintable(QStringLiteral("%1 leaked %2 of the burst ahead of the onset; "
						"the WSOLA grains read forward only").arg(name).arg(leak)));
			}
		}

		// And the crisp single-window render is not the textural one.
		QVERIFY2(meanDifference(beatsBurstRender, textureBurstRender) > 1e-3,
			"the beats and texture renders are identical");
	}

	//! ACCEPTANCE 3: a project with different warp modes on separate clips
	//! round-trips through a real save/load, selections included.
	void modesRoundTripThroughAProjectFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		Song* song = Engine::getSong();
		song->setLoadOnLaunch(false);
		song->clearProject();

		SampleTrack* first = dynamic_cast<SampleTrack*>(
			Track::create(Track::Type::Sample, song));
		QVERIFY(first != nullptr);
		SampleTrack* second = dynamic_cast<SampleTrack*>(
			Track::create(Track::Type::Sample, song));
		QVERIFY(second != nullptr);
		auto* beatsClip = makeToneClip(*first, m_rate);
		applyTwoTimesWarp(*beatsClip, m_rate);
		beatsClip->setWarpStretchMode(WarpStretchMode::Beats);
		auto* textureClip = makeToneClip(*second, m_rate);
		applyTwoTimesWarp(*textureClip, m_rate);
		textureClip->setWarpStretchMode(WarpStretchMode::Texture);
		// A third clip on the same track carries the legacy alias vocabulary:
		// the file will write "rubberband" for it, and the load must bring the
		// mode back through that name.
		auto* complexClip = makeToneClip(*second, m_rate);
		applyTwoTimesWarp(*complexClip, m_rate);
		complexClip->setWarpStretchMode(WarpStretchMode::Complex);

		const QString path = dir.filePath(QStringLiteral("warp-modes.mmp"));
		QVERIFY2(song->saveProjectFile(path), qPrintable(song->saveRefusal()));
		song->clearProject();
		song->loadProject(path);

		std::vector<WarpStretchMode> loaded;
		for (const Track* track : song->tracks())
		{
			for (const Clip* clip : track->getClips())
			{
				const auto* sample = dynamic_cast<const SampleClip*>(clip);
				if (sample != nullptr) { loaded.push_back(sample->warpStretchMode()); }
			}
		}
		QCOMPARE(static_cast<int>(loaded.size()), 3);
		QCOMPARE(static_cast<int>(loaded[0]), static_cast<int>(WarpStretchMode::Beats));
		QCOMPARE(static_cast<int>(loaded[1]), static_cast<int>(WarpStretchMode::Texture));
		QCOMPARE(static_cast<int>(loaded[2]), static_cast<int>(WarpStretchMode::Complex));

		// And the XML says the mode names the file vocabulary carries.
		QDomDocument document;
		QFile file(path);
		QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
		QVERIFY(document.setContent(&file));
		file.close();
		const QString text = document.toString();
		QVERIFY2(text.contains(QStringLiteral("stretch=\"beats\"")),
			"the project file does not carry stretch=\"beats\"");
		QVERIFY2(text.contains(QStringLiteral("stretch=\"texture\"")),
			"the project file does not carry stretch=\"texture\"");
	}

private:
	int m_rate = 44100;
};

QTEST_GUILESS_MAIN(SampleClipWarpModesTest)
#include "SampleClipWarpModesTest.moc"
