/*
 * AudioAnalysisTest.cpp - the YIN pitch tracker, audio to MIDI, the spectrogram, a folder moved as one
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
/*! 0.5.0 L05 (W3): a held 220 Hz tone reads as one A3 and silence as nothing (ED-160); audio to MIDI
 *  writes the line onto a new track and one undo removes it; the spectrogram's loudest band holds a
 *  1 kHz tone (ED-158); every clip of a folder moves together, one undo step (ED-035), and so does one of its
 *  summary clips alone. */

#include <QtTest>

#include <QJsonArray>

#include <array>
#include <cmath>
#include <span>
#include <vector>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "FolderSummary.h"
#include "fft_helpers.h"
#include "fft_helpers.h"
#include "PitchTrack.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TrackFolder.h"

// Compile the private plugin analyser into this test so it can be exercised directly.
#include "GuiApplication.h"
#include "MainWindow.h"
#include "../../../plugins/Eq/EqCurve.h"
#pragma push_macro("Q_OBJECT")
#undef Q_OBJECT
#define Q_OBJECT
#include "../../../plugins/Eq/EqSpectrumView.cpp"
#pragma pop_macro("Q_OBJECT")

namespace lmms::gui
{

// EqAnalyser does not use the view's coordinate mapper; satisfy the unused view code.
float EqHandle::freqToXPixel(float, int) { return 0.0f; }

} // namespace lmms::gui

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

std::vector<float> tone(double hz, int rate, double seconds)
{
	std::vector<float> out(static_cast<std::size_t>(rate * seconds));
	for (std::size_t i = 0; i < out.size(); ++i) { out[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / rate)); }
	return out;
}

SampleClip* toneClip(Track* track, double hz, tick_t at)
{
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	const std::vector<float> mono = tone(hz, rate, 1.0);
	std::vector<SampleFrame> frames;
	for (const float v : mono) { frames.emplace_back(v, v); }
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(at)));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(frames.data(), frames.size(), rate));
	clip->movePosition(TimePos(at));
	return clip;
}

//! A tone clip whose sample buffer carries @a rate as its own rate, so a clip can
//! be built in the malformed state @a rate names (a buffer that never recorded one).
SampleClip* toneClipAtRate(Track* track, double hz, tick_t at, int rate)
{
	const std::vector<float> mono = tone(hz, 44100, 1.0);
	std::vector<SampleFrame> frames;
	for (const float v : mono) { frames.emplace_back(v, v); }
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(at)));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(frames.data(), frames.size(), rate));
	clip->movePosition(TimePos(at));
	return clip;
}

} // namespace

class AudioAnalysisTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aHeldToneIsOneNote()
	{
		const std::vector<float> a3 = tone(220.0, 44100, 1.0);
		const std::vector<pitchtrack::PitchNote> notes = pitchtrack::detectNotes(a3.data(), a3.size(), 44100);
		QCOMPARE(notes.size(), std::size_t{1});
		QCOMPARE(notes.front().key, 57);
		const std::vector<float> silence(44100, 0.f);
		QVERIFY(pitchtrack::detectNotes(silence.data(), silence.size(), 44100).empty());
	}

	void rejectsShortCosineSumWindows()
	{
		constexpr FFTWindow windows[] = {FFTWindow::BlackmanHarris, FFTWindow::Hamming, FFTWindow::Hanning};
		float sample = 1.0f;
		for (const FFTWindow window : windows)
		{
			QCOMPARE(precomputeWindow(&sample, 0, window), -1);
			QCOMPARE(precomputeWindow(&sample, 1, window), -1);
		}
	}

	void audioBecomesATrackOfNotes()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		SampleClip* clip = toneClip(track, 220.0, 0);
		const std::size_t before = Engine::getSong()->tracks().size();
		const ControlResult made = run(QStringLiteral("clip.audio_to_midi"), {{QStringLiteral("clip"), control::clipIdOf(clip)}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QVERIFY(made.result.value(QStringLiteral("notes")).toInt() >= 1);
		QCOMPARE(Engine::getSong()->tracks().size(), before + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), before);
	}

	void theSpectrogramFindsTheTone()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		SampleClip* clip = toneClip(track, 1000.0, 1920);
		const ControlResult got = run(QStringLiteral("sample.spectrogram"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("rows"), 4}, {QStringLiteral("bands"), 32}});
		QVERIFY2(got.ok, qPrintable(got.errorMessage));
		const QJsonArray rows = got.result.value(QStringLiteral("rows")).toArray();
		QCOMPARE(rows.size(), 4);
		const QJsonArray row = rows.at(1).toArray();
		int loudest = 0;
		for (int i = 1; i < row.size(); ++i) { if (row.at(i).toDouble() > row.at(loudest).toDouble()) { loudest = i; } }
		// 32 log bands from 30 Hz to Nyquist: 1 kHz sits a little past the middle.
		const double nyquist = Engine::audioEngine()->outputSampleRate() / 2.0;
		const int expected = static_cast<int>(32.0 * std::log(1000.0 / 30.0) / std::log(nyquist / 30.0));
		QVERIFY2(std::abs(loudest - expected) <= 1, qPrintable(QStringLiteral("loudest band %1, expected %2").arg(loudest).arg(expected)));
	}

	//! A clip whose sample buffer names no rate has no Nyquist to work from: the
	//! spectrogram's band bins divide by it and clip.audio_to_midi hands it straight
	//! to the pitch detector. Both refuse the clip instead of analysing an impossible rate.
	void anAudioClipWithNoSampleRateIsRefused()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		SampleClip* clip = toneClipAtRate(track, 1000.0, 0, 0);
		QCOMPARE(clip->sample().sampleRate(), 0);
		QVERIFY(clip->sample().data() != nullptr);  // the malformed part is the rate, not the data
		const QString id = control::clipIdOf(clip);

		const ControlResult midi = run(QStringLiteral("clip.audio_to_midi"), {{QStringLiteral("clip"), id}});
		QVERIFY2(!midi.ok, "a clip with no sample rate was analysed as if it had one");
		QCOMPARE(midi.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult spec = run(QStringLiteral("sample.spectrogram"),
			{{QStringLiteral("clip"), id}, {QStringLiteral("rows"), 4}, {QStringLiteral("bands"), 32}});
		QVERIFY2(!spec.ok, "a spectrogram was computed from a clip with no sample rate");
		QCOMPARE(spec.errorKind, ControlErrorKind::InvalidArgs);
	}

	void antiPhaseStereoHasZeroEnergy()
	{
		constexpr auto frameCount = static_cast<f_cnt_t>(FFT_BUFFER_SIZE);
		const std::vector<SampleFrame> input(frameCount, SampleFrame{0.5f, -0.5f});
		EqAnalyser analyser;
		analyser.analyze(InterleavedBufferView<const float, 2>{input.data(), frameCount});
		QVERIFY(std::isfinite(analyser.getEnergy()));
		QCOMPARE(analyser.getEnergy(), 0.0f);
	}

	void normalizationRejectsAOneSampleBlock()
	{
		const std::vector<float> spectrum{1.0f};
		std::vector<float> normalized{42.0f};
		QCOMPARE(normalize(spectrum, normalized, 1), -1);
		QCOMPARE(normalized.front(), 42.0f);
	}

	void aRangeIsEdited()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		SampleClip* clip = toneClip(track, 440.0, 3840);
		const auto size = static_cast<int>(clip->sample().sampleSize());
		QVERIFY(run(QStringLiteral("sample.edit_range"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("from"), 0},
			{QStringLiteral("to"), size / 2}, {QStringLiteral("op"), QStringLiteral("silence")}}).ok);
		QCOMPARE(clip->sample().data()[10][0], 0.f);
		QVERIFY(run(QStringLiteral("sample.edit_range"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("from"), 0},
			{QStringLiteral("to"), size / 2}, {QStringLiteral("op"), QStringLiteral("delete")}}).ok);
		QCOMPARE(static_cast<int>(clip->sample().sampleSize()), size - size / 2);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(dynamic_cast<SampleClip*>(track->getClips().front())->sample().sampleSize()), size);
		QCOMPARE(run(QStringLiteral("sample.edit_range"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("from"), 10},
			{QStringLiteral("to"), 5}, {QStringLiteral("op"), QStringLiteral("gain")}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	//! Two warp markers inside a one-second tone: source frames at its quarter and half.
	void setMarkers(SampleClip* clip)
	{
		const auto size = static_cast<int>(clip->sample().sampleSize());
		const std::array<WarpMarker, 2> markers{WarpMarker{static_cast<f_cnt_t>(size / 4), 24},
			WarpMarker{static_cast<f_cnt_t>(size / 2), 96}};
		QVERIFY2(clip->setWarpMarkers(std::span<const WarpMarker>(markers.data(), markers.size())),
			"the two-marker set was rejected");
	}

	void aRangeEditRespectsWarpMarkers()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		SampleClip* clip = toneClip(track, 440.0, 3840);
		const auto size = static_cast<int>(clip->sample().sampleSize());
		setMarkers(clip);
		// A crop from inside the source makes the new frame 0 the OLD frame `from`, so every marker
		// would be on the wrong audio - the same refusal sample.crop makes for its own window.
		const ControlResult cropped = run(QStringLiteral("sample.edit_range"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("from"), size / 8}, {QStringLiteral("to"), size / 2}, {QStringLiteral("op"), QStringLiteral("crop")}});
		QCOMPARE(cropped.errorKind, ControlErrorKind::Refused);
		QCOMPARE(static_cast<int>(clip->sample().sampleSize()), size);
		QCOMPARE(clip->warpMarkers().size(), 2);
		// A delete over the first marker drops it, and one after it shifts the frame it pins.
		const ControlResult deleted = run(QStringLiteral("sample.edit_range"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("from"), 0}, {QStringLiteral("to"), size / 2}, {QStringLiteral("op"), QStringLiteral("delete")}});
		QCOMPARE(deleted.errorKind, ControlErrorKind::Refused);
		QCOMPARE(static_cast<int>(clip->sample().sampleSize()), size);
		QCOMPARE(clip->warpMarkers().size(), 2);
		// A frame-for-frame op moves no source frame, so the same clip still takes them.
		QVERIFY(run(QStringLiteral("sample.edit_range"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("from"), size / 8}, {QStringLiteral("to"), size / 4}, {QStringLiteral("op"), QStringLiteral("silence")}}).ok);
		QCOMPARE(clip->warpMarkers().size(), 2);
	}

	void aRangeFadeReachesItsEndpoint()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		SampleClip* clip = toneClip(track, 440.0, 3840);
		const auto size = static_cast<int>(clip->sample().sampleSize());
		const int frame = size / 8;
		QVERIFY2(std::abs(clip->sample().data()[frame][0]) > 0.001f, "the frame under test is silent");
		// A one-frame range is all endpoint: the fade_out's last (and only) frame is 0, not unity.
		QVERIFY(run(QStringLiteral("sample.edit_range"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("from"), frame}, {QStringLiteral("to"), frame + 1}, {QStringLiteral("op"), QStringLiteral("fade_out")}}).ok);
		QCOMPARE(clip->sample().data()[frame][0], 0.f);
		QCOMPARE(clip->sample().data()[frame][1], 0.f);
	}

	void aRangeIgnoresAnUnrelatedGain()
	{
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		SampleClip* clip = toneClip(track, 440.0, 3840);
		const auto size = static_cast<int>(clip->sample().sampleSize());
		// gain_db is 'gain's parameter; a crop that ignores it must not be refused over it.
		QVERIFY2(run(QStringLiteral("sample.edit_range"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("from"), size / 4}, {QStringLiteral("to"), size / 2}, {QStringLiteral("op"), QStringLiteral("crop")},
			{QStringLiteral("gain_db"), 30.0}}).ok, "a crop was refused over an unused gain_db");
		QCOMPARE(static_cast<int>(clip->sample().sampleSize()), size / 4);
		// 'gain' itself still validates the parameter it applies.
		QCOMPARE(run(QStringLiteral("sample.edit_range"), {{QStringLiteral("clip"), control::clipIdOf(clip)},
			{QStringLiteral("from"), 0}, {QStringLiteral("to"), size / 4}, {QStringLiteral("op"), QStringLiteral("gain")},
			{QStringLiteral("gain_db"), 30.0}}).errorKind, ControlErrorKind::InvalidArgs);
	}

	void aFolderMovesAsOne()
	{
		Song* song = Engine::getSong();
		Track* folder = Track::create(Track::Type::Folder, song);
		auto* child = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		QVERIFY(run(QStringLiteral("track.set_folder"), {{QStringLiteral("track"), control::trackIdOf(child)},
			{QStringLiteral("folder"), control::trackIdOf(folder)}}).ok);
		SampleClip* clip = toneClip(child, 440.0, 384);
		QVERIFY(run(QStringLiteral("track.folder_move"), {{QStringLiteral("folder"), control::trackIdOf(folder)}, {QStringLiteral("delta"), 192}}).ok);
		QCOMPARE(clip->startPosition().getTicks(), tick_t{576});
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(dynamic_cast<SampleClip*>(child->getClips().front())->startPosition().getTicks(), tick_t{384});
		QCOMPARE(run(QStringLiteral("track.folder_move"), {{QStringLiteral("folder"), control::trackIdOf(child)}, {QStringLiteral("delta"), 1}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	//! 0.5.0 ED-035: the folder's summary clips - the merged spans of its children's clips - and a span moved
	//! alone through track.folder_move's 'start'/'end', one undo step; the other span stays where it was.
	void aFolderSummarySpanMovesAlone()
	{
		Song* song = Engine::getSong();
		auto* folder = dynamic_cast<TrackFolder*>(Track::create(Track::Type::Folder, song));
		auto* a = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		auto* b = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		for (Track* child : {static_cast<Track*>(a), static_cast<Track*>(b)})
		{
			QVERIFY(run(QStringLiteral("track.set_folder"), {{QStringLiteral("track"), control::trackIdOf(child)},
				{QStringLiteral("folder"), control::trackIdOf(folder)}}).ok);
		}
		SampleClip* first = toneClip(a, 440.0, 0);
		const tick_t length = first->length().getTicks();
		SampleClip* overlapping = toneClip(b, 440.0, length / 2);       // overlaps the first: one span
		SampleClip* later = toneClip(a, 440.0, 4 * length);            // a gap before it: a second span
		const std::vector<FolderSpan> spans = folderSummary(*folder);
		QCOMPARE(spans.size(), std::size_t{2});
		QCOMPARE(spans[0].start, tick_t{0});
		QCOMPARE(spans[0].end, length / 2 + length);
		QCOMPARE(spans[0].clips, 2);
		QCOMPARE(spans[1].start, 4 * length);
		QCOMPARE(spans[1].clips, 1);

		const QJsonObject span{{QStringLiteral("folder"), control::trackIdOf(folder)}, {QStringLiteral("delta"), 192},
			{QStringLiteral("start"), static_cast<int>(spans[0].start)}, {QStringLiteral("end"), static_cast<int>(spans[0].end)}};
		const ControlResult moved = run(QStringLiteral("track.folder_move"), span);
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(moved.result.value(QStringLiteral("moved")).toInt(), 2);
		QCOMPARE(first->startPosition().getTicks(), tick_t{192});
		QCOMPARE(overlapping->startPosition().getTicks(), length / 2 + 192);
		QCOMPARE(later->startPosition().getTicks(), 4 * length);  // outside the span: untouched
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(folderSummary(*folder).front().start, tick_t{0});
		QCOMPARE(a->getClips().front()->startPosition().getTicks(), tick_t{0});

		QJsonObject halfSpan = span;
		halfSpan.remove(QStringLiteral("end"));
		QCOMPARE(run(QStringLiteral("track.folder_move"), halfSpan).errorKind, ControlErrorKind::InvalidArgs);
		QJsonObject empty = span;
		empty.insert(QStringLiteral("start"), static_cast<int>(2 * length + length / 2));
		empty.insert(QStringLiteral("end"), static_cast<int>(3 * length));
		QCOMPARE(run(QStringLiteral("track.folder_move"), empty).errorKind, ControlErrorKind::NotFound);
	}
};

QTEST_MAIN(AudioAnalysisTest)
#include "AudioAnalysisTest.moc"
