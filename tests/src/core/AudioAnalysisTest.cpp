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
 *  1 kHz tone (ED-158); every clip of a folder moves together, one undo step (ED-035). */

#include <QtTest>

#include <QJsonArray>

#include <cmath>
#include <vector>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "PitchTrack.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TrackFolder.h"

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

std::vector<float> toneWithSilence(int rate, std::size_t start, std::size_t end)
{
	std::vector<float> out(static_cast<std::size_t>(rate), 0.f);
	for (std::size_t i = start; i < end; ++i) { out[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 220.0 * static_cast<double>(i) / rate)); }
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

	void aNoteStartsNearItsAudioOnset()
	{
		constexpr int rate = 44100;
		constexpr std::size_t onset = rate * 3 / 40;
		const std::vector<float> audio = toneWithSilence(rate, onset, rate * 3 / 4);
		const std::vector<pitchtrack::PitchNote> notes = pitchtrack::detectNotes(audio.data(), audio.size(), rate);
		QCOMPARE(notes.size(), std::size_t{1});
		const f_cnt_t tolerance = static_cast<f_cnt_t>(rate / 50);
		QVERIFY2(notes.front().start >= onset - tolerance && notes.front().start <= onset + tolerance,
			qPrintable(QStringLiteral("detected onset %1, expected %2").arg(static_cast<qulonglong>(notes.front().start))
				.arg(static_cast<qulonglong>(onset))));
	}

	void aNoteEndsNearItsAudioOffset()
	{
		constexpr int rate = 44100;
		constexpr std::size_t offset = rate * 3 / 4;
		const std::vector<float> audio = toneWithSilence(rate, rate / 4, offset);
		const std::vector<pitchtrack::PitchNote> notes = pitchtrack::detectNotes(audio.data(), audio.size(), rate);
		QCOMPARE(notes.size(), std::size_t{1});
		const f_cnt_t tolerance = static_cast<f_cnt_t>(rate / 50);
		QVERIFY2(notes.front().end >= offset - tolerance && notes.front().end <= offset + tolerance,
			qPrintable(QStringLiteral("detected offset %1, expected %2").arg(static_cast<qulonglong>(notes.front().end))
				.arg(static_cast<qulonglong>(offset))));
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
};

QTEST_MAIN(AudioAnalysisTest)
#include "AudioAnalysisTest.moc"
