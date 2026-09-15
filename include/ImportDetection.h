/*
 * ImportDetection.h - import detection's ENGINE half: analyse a file the
 *                     importer can read, and name what it found (feature row 34
 *                     of docs/FEATURE-LIST-0.3.0.md).
 *
 * THE THREE PIECES, and which file owns each:
 *   1. the arithmetic      include/ImportDetectionDsp.h (Qt-free, no file I/O,
 *                          no dependency);
 *   2. the file and the     this header: decode with the engine's OWN decoder
 *      VOCABULARY          (SampleDecoder, the same libsndfile path an import
 *                          uses) and name the key with the PRE-EXISTING
 *                          vocabulary (InstrumentFunctionNoteStacking::
 *                          ChordTable, the table the piano roll's scale combo is
 *                          filled from);
 *   3. the project state   include/ProjectKey.h (the `<detected-key>` element)
 *                          and the tempo map, written by the detect.* commands
 *                          (src/core/ControlCommandsDetect.cpp).
 *
 * SUGGESTIONS, NOT SILENT EDITS. BACKLOG.md item 10 is explicit: "on import, one
 * offline pass filling (a) tempo (BPM) and (b) the first transient, shown as
 * SUGGESTIONS the user accepts - never applied silently". analyseAudioFile() is
 * the suggestion: it reads a file and returns numbers, touching no project
 * state. Nothing is written until a caller issues `detect.apply`.
 *
 * ACCURACY, STATED WHERE IT IS USED. What is measured on this box is a
 * SYNTHESISED input with a known answer (tools/import-detection-proof.cpp and
 * the registered tests/src/core/ImportDetectionTest.cpp). Real-world detection
 * accuracy is UNVERIFIED here - no real-music corpus was measured, and the
 * confidence numbers this API returns are the detector's own scores, not
 * probabilities. docs/IMPORT-DETECTION.md is the record.
 *
 * THREADING. analyseAudioFile() decodes and allocates: it is an IMPORT-TIME
 * call. No audio-thread path calls it (the realtime rule of AGENTS.md section
 * 4); the commands run it on the control thread.
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

#ifndef LMMS_IMPORT_DETECTION_H
#define LMMS_IMPORT_DETECTION_H

#include <cstdint>

#include <QString>
#include <QVector>

#include "ImportDetectionDsp.h"
#include "lmms_export.h"

namespace lmms
{

/*! One scale template the key estimate may report, as a 12-bit mask of pitch
 *  classes relative to the tonic plus the NAME the pre-existing vocabulary gives
 *  it.
 *
 *  WHERE THE NAMES COME FROM: InstrumentFunctions.h's
 *  InstrumentFunctionNoteStacking::ChordTable, filtered by the table's OWN
 *  definition of a scale (Chord::isScale(), i.e. more than six semitones) and
 *  deduplicated by mask in table order. Two entries can share a mask - the table
 *  holds both "Aeolian" and "Minor" for {0,2,3,5,7,8,10} - and the FIRST name in
 *  the table's order wins, so the same input always reports the same name. */
struct LMMS_EXPORT ScaleCandidate
{
	std::uint32_t mask = 0;
	QString name;
};

//! The candidate list, built once from ChordTable (see ScaleCandidate). Masks
//! with more than detection::MaxTemplateDegrees degrees are already excluded -
//! this is the list the DSP is handed, so its index is a valid index into it.
LMMS_EXPORT const QVector<ScaleCandidate>& candidateScales();

//! Whether a name is one the PRE-EXISTING vocabulary answers to. The detect.*
//! commands refuse, typed, rather than write a name the piano roll's scale combo
//! would not recognise.
LMMS_EXPORT bool scaleNameIsKnown(const QString& name);

//! One analysis of one file. `ok` false means `error` says why and nothing else
//! here is meaningful.
struct LMMS_EXPORT ImportDetectionResult
{
	bool ok = false;
	QString error;
	QString path;
	//! The file's own rate and length, from the decoder.
	int sampleRate = 0;
	qint64 frames = 0;
	double durationSeconds = 0.0;
	//! How much of the file was actually analysed (the caller's bound).
	double analysedSeconds = 0.0;

	//! The methods, named, exactly as the control surface reports them.
	QString tempoMethod;
	QString keyMethod;

	detection::TempoEstimate tempo;
	detection::KeyEstimate key;

	//! The key in the project's own spelling: "A" plus the ChordTable name. Both
	//! empty when no key was found, or when the winning mask has no name in the
	//! vocabulary (`scaleKnown` false - a state this API reports rather than
	//! hides).
	QString tonicName;
	QString scaleName;
	bool scaleKnown = false;
	//! The tonic pitch class ("A" == 9), or -1 when no key was found.
	int tonicPitchClass = -1;
};

/*! Analyse \a path: decode it with the engine's own decoder, then estimate the
 *  tempo and the key from at most \a maxSeconds of it (the START of the file -
 *  an import's first impression, stated rather than implied). Reads nothing but
 *  the file and writes nothing at all: this is the suggestion, and a caller that
 *  wants it applied issues `detect.apply`. */
LMMS_EXPORT ImportDetectionResult analyseAudioFile(const QString& path,
	double maxSeconds = detection::DefaultAnalysisSeconds);

} // namespace lmms

#endif // LMMS_IMPORT_DETECTION_H
