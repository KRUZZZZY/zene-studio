/*
 * ControlCommandsAudioAnalysis.cpp - audio to MIDI and a clip's spectrogram
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
/*! 0.5.0 L05 (W3), what is in a sample clip:
 *  - clip.audio_to_midi (ED-160): a monophonic line's notes (YIN, PitchTrack.h) onto a new instrument
 *    track, where the audio sits - one undo step removes it;
 *  - sample.spectrogram (ED-158): a clip's frequency content as rows of band magnitudes over time, for
 *    a spectrogram view or an agent reading where the noise is. */

#include <algorithm>
#include <cmath>
#include <vector>

#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ImportDetection.h"
#include "ImportDetectionSpectrum.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "PitchTrack.h"
#include "SampleClip.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

SampleClip* audioClipOf(const QJsonObject& args, ClipRef* ref, ControlResult* error)
{
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), ref, error)) { return nullptr; }
	auto* clip = dynamic_cast<SampleClip*>(ref->clip);
	const SampleWindow window = clip != nullptr ? clip->sampleWindow() : SampleWindow();
	if (clip == nullptr || clip->sample().data() == nullptr || window.sourceOut <= window.sourceIn)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'clip' is an audio clip with audio"));
		return nullptr;
	}
	return clip;
}

//! The clip's window as mono samples.
std::vector<float> windowMono(SampleClip& clip)
{
	const SampleWindow window = clip.sampleWindow();
	return monoMix(clip.sample().data() + window.sourceIn, static_cast<std::size_t>(window.sourceOut - window.sourceIn));
}

ControlResult audioToMidi(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = audioClipOf(args, &ref, &error);
	if (clip == nullptr) { return error; }
	const std::vector<float> mono = windowMono(*clip);
	const SampleWindow window = clip->sampleWindow();
	const std::vector<pitchtrack::PitchNote> found = pitchtrack::detectNotes(mono.data(), mono.size(), clip->sample().sampleRate());
	if (found.empty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no pitched line was found in the clip")); }
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	track->setName(QStringLiteral("%1 notes").arg(clip->name().isEmpty() ? QStringLiteral("Audio") : clip->name()));
	auto* notes = dynamic_cast<MidiClip*>(track->createClip(clip->startPosition()));
	const tick_t origin = clip->startPosition().getTicks();
	for (const pitchtrack::PitchNote& note : found)
	{
		const tick_t from = clip->timelinePosAt(window.sourceIn + note.start).getTicks() - origin;
		const tick_t to = clip->timelinePosAt(window.sourceIn + note.end).getTicks() - origin;
		if (from < 0 || to <= from) { continue; }
		const auto velocity = static_cast<volume_t>(std::clamp(std::lround(note.level * 400.f), 10L, 200L));
		notes->addNote(Note(TimePos(to - from), TimePos(from), std::clamp(note.key, 0, NumKeys - 1), velocity), false);
	}
	if (notes->notes().empty())
	{
		// Every note found lay outside the clip's span: no empty track is left behind as a success.
		removeTrack(track);
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no pitched note falls inside the clip"));
	}
	notes->rearrangeAllNotes();
	notes->updateLength();
	recordCreatedTrack(track);
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("track"), trackIdOf(track)},
		{QStringLiteral("notes"), static_cast<int>(notes->notes().size())}});
}

//! Band @a band of @a bands, log-spaced from 30 Hz to Nyquist: the FFT bins [first, last) it averages.
std::pair<int, int> bandBins(int band, int bands, int size, int rate)
{
	const double nyquist = rate / 2.0;
	const auto binOf = [&](double hz) { return std::clamp(static_cast<int>(hz / nyquist * (size / 2)), 1, size / 2); };
	const double low = 30.0 * std::pow(nyquist / 30.0, static_cast<double>(band) / bands);
	const double high = 30.0 * std::pow(nyquist / 30.0, static_cast<double>(band + 1) / bands);
	return {binOf(low), std::max(binOf(low) + 1, binOf(high))};
}

QJsonArray spectrumRow(const SampleFrame* frames, std::size_t at, const std::vector<float>& hann, int bands, int rate)
{
	const int size = static_cast<int>(hann.size());
	std::vector<float> re(static_cast<std::size_t>(size));
	std::vector<float> im(static_cast<std::size_t>(size), 0.f);
	for (int i = 0; i < size; ++i)
	{
		const SampleFrame& frame = frames[at + static_cast<std::size_t>(i)];
		re[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * (frame.left() + frame.right())) * hann[static_cast<std::size_t>(i)];
	}
	detection::detail::fftRadix2(re, im);
	QJsonArray row;
	for (int band = 0; band < bands; ++band)
	{
		const auto [first, last] = bandBins(band, bands, size, rate);
		double sum = 0.0;
		for (int bin = first; bin < last; ++bin) { sum += std::hypot(re[static_cast<std::size_t>(bin)], im[static_cast<std::size_t>(bin)]); }
		row.append(20.0 * std::log10(std::max(1e-9, sum / (last - first) / size)));  // dB
	}
	return row;
}

ControlResult spectrogram(const QJsonObject& args)
{
	ClipRef ref;
	ControlResult error;
	SampleClip* clip = audioClipOf(args, &ref, &error);
	if (clip == nullptr) { return error; }
	const int rows = std::clamp(args.value(QStringLiteral("rows")).toInt(128), 1, 1024);
	const int bands = std::clamp(args.value(QStringLiteral("bands")).toInt(64), 4, 256);
	const std::vector<float> hann = detection::detail::hannWindow(2048);
	const SampleWindow window = clip->sampleWindow();
	const std::size_t frameCount = static_cast<std::size_t>(window.sourceOut - window.sourceIn);
	if (frameCount < hann.size()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the clip is shorter than one analysis window")); }
	const SampleFrame* frames = clip->sample().data() + window.sourceIn;
	const std::size_t span = frameCount - hann.size();
	QJsonArray out;
	for (int r = 0; r < rows; ++r)
	{
		out.append(spectrumRow(frames, span * static_cast<std::size_t>(r) / static_cast<std::size_t>(std::max(1, rows - 1)), hann, bands,
			clip->sample().sampleRate()));
	}
	return ControlResult::success({{QStringLiteral("clip"), clipId(ref.id)}, {QStringLiteral("rows"), out}, {QStringLiteral("bands"), bands},
		{QStringLiteral("low_hz"), 30}, {QStringLiteral("high_hz"), clip->sample().sampleRate() / 2}});
}

} // namespace

void registerAudioAnalysisCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.audio_to_midi");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("audio_to_midi");
	cmd.description = QStringLiteral("Find the notes of a monophonic line in an audio clip (a voice, a bass, a lead; YIN pitch "
		"tracking - a strong harmonic can read an octave out) and write them, with their loudness as velocity, onto a new "
		"instrument track where the audio sits. One undo step removes the track.");
	cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}}, QJsonArray{QStringLiteral("clip")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = audioToMidi;
	registry.registerCommand(cmd);
	cmd.id = QStringLiteral("sample.spectrogram");
	cmd.group = QStringLiteral("sample");
	cmd.verb = QStringLiteral("spectrogram");
	cmd.description = QStringLiteral("An audio clip's frequency content: 'rows' (128) moments evenly over the clip, each 'bands' (64) "
		"log-spaced bands from 30 Hz to Nyquist, in dB - what a spectrogram view draws. Read only.");
	cmd.argsSchema = objectSchema({{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("rows"), integerProperty(1, 1024)},
		{QStringLiteral("bands"), integerProperty(4, 256)}}, QJsonArray{QStringLiteral("clip")});
	cmd.mutating = false;
	cmd.handler = spectrogram;
	registry.registerCommand(cmd);
}

} // namespace lmms
