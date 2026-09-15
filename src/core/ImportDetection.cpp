/*
 * ImportDetection.cpp - the file and the vocabulary half of import detection
 *                       (see include/ImportDetection.h for the design).
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

#include "ImportDetection.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "InstrumentFunctions.h" // InstrumentFunctionNoteStacking::ChordTable
#include "SampleDecoder.h"
#include "SampleFrame.h"

namespace lmms
{

using namespace detection;

namespace
{

//! The mask of one ChordTable entry: its semitones as a 12-bit pitch-class set.
std::uint32_t maskOf(const InstrumentFunctionNoteStacking::Chord& chord)
{
	std::uint32_t mask = 0;
	for (int i = 0; i < chord.size(); ++i)
	{
		const int semiTone = chord[i];
		// A degree the pitch-class model cannot express is not silently wrapped
		// into another note: the entry is skipped by the caller (mask == 0).
		if (semiTone < 0 || semiTone > 11) { return 0; }
		mask |= (1u << semiTone);
	}
	return mask;
}

/*! The vocabulary, built once.
 *
 *  The filter is the table's OWN definition of a scale (Chord::isScale() is
 *  `size() > 6`) plus this feature's declared information rule
 *  (detection::MaxTemplateDegrees): a template covering ten or more of the
 *  twelve pitch classes carries no key information, and the 12-note entry the
 *  table holds would otherwise win on any noisy recording. */
QVector<ScaleCandidate> buildCandidates()
{
	QVector<ScaleCandidate> candidates;
	const auto& table = InstrumentFunctionNoteStacking::ChordTable::getInstance();
	for (const auto& chord : table.chords())
	{
		if (!chord.isScale()) { continue; }
		const std::uint32_t mask = maskOf(chord);
		if (mask == 0 || maskDegreeCount(mask) > MaxTemplateDegrees) { continue; }
		const bool seen = std::any_of(candidates.cbegin(), candidates.cend(),
			[mask](const ScaleCandidate& candidate) { return candidate.mask == mask; });
		if (seen) { continue; } // the FIRST name the table holds for a mask wins
		ScaleCandidate candidate;
		candidate.mask = mask;
		candidate.name = chord.getName();
		candidates.append(candidate);
	}
	return candidates;
}

} // namespace


const QVector<ScaleCandidate>& candidateScales()
{
	// Built on first use: the table is static data and the list never changes,
	// and nothing here is on an audio-thread path.
	static const QVector<ScaleCandidate> candidates = buildCandidates();
	return candidates;
}


bool scaleNameIsKnown(const QString& name)
{
	if (name.isEmpty()) { return false; }
	return !InstrumentFunctionNoteStacking::ChordTable::getInstance()
		.getScaleByName(name).isEmpty();
}


ImportDetectionResult analyseAudioFile(const QString& path, double maxSeconds)
{
	ImportDetectionResult result;
	result.path = path;
	result.tempoMethod = QString::fromLatin1(TempoMethodName);
	result.keyMethod = QString::fromLatin1(KeyMethodName);

	if (path.isEmpty())
	{
		result.error = QStringLiteral("name an audio file to analyse");
		return result;
	}

	// The engine's OWN decoder: the same libsndfile path (plus DrumSynth and Ogg
	// Vorbis) an import goes through, so a file the importer takes is a file this
	// can analyse, and a file it refuses is refused here with the same shape.
	const std::optional<SampleDecoder::Result> decoded = SampleDecoder::decode(path);
	if (!decoded || decoded->data.empty())
	{
		result.error = QStringLiteral("no decoder in this build could read `%1`").arg(path);
		return result;
	}

	const int sampleRate = decoded->sampleRate;
	if (sampleRate <= 0)
	{
		result.error = QStringLiteral("`%1` reports no sample rate").arg(path);
		return result;
	}

	// Mono: the arithmetic takes one channel, and 0.5 * (L + R) is what an
	// import's own audition path would hand a listener's single ear.
	const std::vector<SampleFrame>& frames = decoded->data;
	std::vector<float> mono(frames.size(), 0.0f);
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		mono[i] = static_cast<float>(0.5 * (frames[i].left() + frames[i].right()));
	}

	const double bound = std::clamp(maxSeconds, 1.0, MaxAnalysisSeconds);
	result.sampleRate = sampleRate;
	result.frames = static_cast<qint64>(frames.size());
	result.durationSeconds = static_cast<double>(frames.size()) / sampleRate;
	result.analysedSeconds = std::min(result.durationSeconds, bound);

	result.tempo = estimateTempo(mono.data(), mono.size(), sampleRate, bound);

	const QVector<ScaleCandidate>& candidates = candidateScales();
	std::vector<std::uint32_t> masks;
	masks.reserve(static_cast<std::size_t>(candidates.size()));
	for (const ScaleCandidate& candidate : candidates) { masks.push_back(candidate.mask); }
	result.key = estimateKey(mono.data(), mono.size(), sampleRate, masks, bound);

	if (result.key.found && result.key.candidateIndex >= 0
		&& result.key.candidateIndex < candidates.size())
	{
		result.tonicName = QString::fromLatin1(pitchClassName(result.key.tonicPitchClass));
		result.scaleName = candidates[result.key.candidateIndex].name;
		result.scaleKnown = scaleNameIsKnown(result.scaleName);
		result.tonicPitchClass = result.key.tonicPitchClass;
	}

	result.ok = true;
	return result;
}

} // namespace lmms
