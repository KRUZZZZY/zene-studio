/*
 * ControlDetectSupport.cpp - the `detect.*` group's shared helpers (see
 *                            include/ControlDetectSupport.h).
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

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlDetectSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ImportDetection.h"
#include "ProjectKey.h"
#include "Song.h"
#include "TempoMap.h"

namespace lmms
{

using namespace control;

namespace control
{

//! The one-line accuracy statement every read of this group carries. It says
//! what was measured and what was not, in the words the release documents use.
QString detectAccuracyNote()
{
	return QStringLiteral(
		"unverified on real music. What is measured is synthesised input with a known "
		"answer (a click track at a chosen BPM, a scale at a chosen root): "
		"tools/import-detection-proof.cpp and the registered ImportDetectionTest. No "
		"real-music corpus was measured on this box, and the confidence numbers are the "
		"detector's own scores, not probabilities. docs/IMPORT-DETECTION.md is the record");
}

QJsonObject detectBoundsJson()
{
	QJsonObject bounds;
	bounds.insert(QStringLiteral("min_bpm"), detection::MinDetectionBpm);
	bounds.insert(QStringLiteral("max_bpm"), detection::MaxDetectionBpm);
	bounds.insert(QStringLiteral("default_seconds"), detection::DefaultAnalysisSeconds);
	bounds.insert(QStringLiteral("max_seconds"), detection::MaxAnalysisSeconds);
	bounds.insert(QStringLiteral("chroma_band_low_hz"), detection::ChromaBandLowHz);
	bounds.insert(QStringLiteral("chroma_band_full_low_hz"), detection::ChromaBandFullLowHz);
	bounds.insert(QStringLiteral("chroma_band_full_high_hz"), detection::ChromaBandFullHighHz);
	bounds.insert(QStringLiteral("chroma_band_high_hz"), detection::ChromaBandHighHz);
	bounds.insert(QStringLiteral("max_template_degrees"), detection::MaxTemplateDegrees);
	bounds.insert(QStringLiteral("tempo_event_tick"), static_cast<qint64>(DetectAppliedEventTick));
	bounds.insert(QStringLiteral("tempo_rounding"),
		QStringLiteral("the tempo map holds an INTEGER bpm, so an applied tempo is rounded "
			"to the nearest whole BPM and the exact estimate is reported beside it"));
	return bounds;
}

QJsonObject detectTempoJson(const detection::TempoEstimate& tempo)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("found"), tempo.found);
	entry.insert(QStringLiteral("bpm"), tempo.found ? tempo.bpm : 0.0);
	entry.insert(QStringLiteral("confidence"), tempo.confidence);
	entry.insert(QStringLiteral("period_seconds"), tempo.periodSeconds);
	entry.insert(QStringLiteral("onsets"), tempo.onsets);
	entry.insert(QStringLiteral("first_onset_seconds"), tempo.firstOnsetSeconds);
	entry.insert(QStringLiteral("first_onset_frame"), static_cast<qint64>(tempo.firstOnsetFrame));
	return entry;
}

QJsonObject detectKeyJson(const ImportDetectionResult& analysis)
{
	const detection::KeyEstimate& key = analysis.key;
	QJsonObject entry;
	entry.insert(QStringLiteral("found"), key.found);
	entry.insert(QStringLiteral("tonic"), analysis.tonicName);
	entry.insert(QStringLiteral("pitch_class"), key.found ? key.tonicPitchClass : -1);
	entry.insert(QStringLiteral("scale"), analysis.scaleName);
	entry.insert(QStringLiteral("scale_known"), analysis.scaleKnown);
	entry.insert(QStringLiteral("score"), key.score);
	entry.insert(QStringLiteral("margin"), key.margin);
	QJsonArray chroma;
	for (const double bin : key.chroma) { chroma.append(bin); }
	entry.insert(QStringLiteral("chroma"), chroma);
	return entry;
}

//! The two method names plus the accuracy sentence: the header every reply
//! carries, so no caller has to guess how a number was produced.
QJsonObject detectMethodJson()
{
	QJsonObject methods;
	methods.insert(QStringLiteral("tempo"), QString::fromLatin1(detection::TempoMethodName));
	methods.insert(QStringLiteral("key"), QString::fromLatin1(detection::KeyMethodName));
	methods.insert(QStringLiteral("accuracy_note"), detectAccuracyNote());
	return methods;
}

//! The whole analysis, as the wire reports it.
QJsonObject detectAnalysisJson(const ImportDetectionResult& analysis)
{
	QJsonObject result;
	result.insert(QStringLiteral("path"), analysis.path);
	result.insert(QStringLiteral("sample_rate"), analysis.sampleRate);
	result.insert(QStringLiteral("frames"), analysis.frames);
	result.insert(QStringLiteral("duration_seconds"), analysis.durationSeconds);
	result.insert(QStringLiteral("analysed_seconds"), analysis.analysedSeconds);
	result.insert(QStringLiteral("tempo"), detectTempoJson(analysis.tempo));
	result.insert(QStringLiteral("key"), detectKeyJson(analysis));
	result.insert(QStringLiteral("method"), detectMethodJson());
	result.insert(QStringLiteral("bounds"), detectBoundsJson());
	return result;
}

//! The project's OWN key field, as it stands now.
QJsonObject detectProjectKeyJson(const ProjectKey& key)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("present"), !key.empty());
	if (key.empty()) { return entry; }
	entry.insert(QStringLiteral("tonic"), key.tonicName());
	entry.insert(QStringLiteral("pitch_class"), key.tonicPitchClass());
	entry.insert(QStringLiteral("scale"), key.scaleName());
	entry.insert(QStringLiteral("confidence"), key.confidence());
	entry.insert(QStringLiteral("margin"), key.margin());
	entry.insert(QStringLiteral("method"), key.method());
	entry.insert(QStringLiteral("source"), key.sourcePath());
	return entry;
}

/*! Reads the optional `max_seconds` bound. Absent means the default; a value
 *  outside 1..MaxAnalysisSeconds is REFUSED rather than clamped, because a
 *  caller that asked for 0 seconds asked for something that cannot be done. */
bool readDetectBound(const QJsonObject& args, double* out, ControlResult* error)
{
	*out = detection::DefaultAnalysisSeconds;
	if (!args.contains(QStringLiteral("max_seconds"))) { return true; }
	const double wanted = args.value(QStringLiteral("max_seconds")).toDouble();
	if (wanted < 1.0 || wanted > detection::MaxAnalysisSeconds)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("max_seconds must be 1..%1").arg(detection::MaxAnalysisSeconds));
		return false;
	}
	*out = wanted;
	return true;
}

//! The detected tempo as the map can hold it. -1 when it is outside the map's
//! own bounds (which the caller must refuse rather than clamp: a map event of
//! 999 BPM for a detected 1200 is a different tempo, not a rounding).
int roundedTempoForMap(double bpm)
{
	const int rounded = static_cast<int>(std::lround(bpm));
	if (rounded < TempoMapMinTempo || rounded > TempoMapMaxTempo) { return -1; }
	return rounded;
}

/*! The recorded inverse of one detect.apply: BOTH halves captured before the
 *  write and put back by the step, so one Ctrl+Z is one detection undone. */
void recordDetectApplyRestore(const TempoMap& before, const QString& keyBefore)
{
	// The redo half captures the state as it is NOW (the write has run), the
	// shape ControlGrooveSupport.cpp uses, so a redo is faithful rather than a
	// dropped step.
	Song* song = Engine::getSong();
	const TempoMap after = song != nullptr ? song->tempoMap().map() : TempoMap();
	const QString keyAfter = song != nullptr ? song->projectKey().toXml() : QString();
	addUndoStep(
		[before, keyBefore]() {
			Song* current = Engine::getSong();
			if (current == nullptr) { return; }
			current->tempoMap().edit([&before](TempoMap& map) { map = before; return true; });
			if (!keyBefore.isEmpty()) { current->projectKey().fromXml(keyBefore); }
			else { current->projectKey().clear(); }
		},
		[after, keyAfter]() {
			Song* current = Engine::getSong();
			if (current == nullptr) { return; }
			current->tempoMap().edit([&after](TempoMap& map) { map = after; return true; });
			if (!keyAfter.isEmpty()) { current->projectKey().fromXml(keyAfter); }
			else { current->projectKey().clear(); }
		});
}

//! `detect.get_state`'s payload: what the project holds now.
QJsonObject detectStateJson(Song* song)
{
	const TempoMap& map = song->tempoMap().map();
	QJsonObject tempoMap;
	tempoMap.insert(QStringLiteral("active"), map.active());
	tempoMap.insert(QStringLiteral("event_count"), map.size());
	tempoMap.insert(QStringLiteral("global_tempo"),
		static_cast<int>(song->getTempo()));
	tempoMap.insert(QStringLiteral("tempo_at_tick_0"),
		map.tempoAtTick(DetectAppliedEventTick, static_cast<int>(song->getTempo())));
	tempoMap.insert(QStringLiteral("event_at_tick_0"), map.hasEventAt(DetectAppliedEventTick));

	const QVector<ScaleCandidate>& candidates = candidateScales();
	QJsonArray names;
	for (const ScaleCandidate& candidate : candidates) { names.append(candidate.name); }
	QJsonObject vocabulary;
	vocabulary.insert(QStringLiteral("count"), static_cast<int>(candidates.size()));
	vocabulary.insert(QStringLiteral("names"), names);
	vocabulary.insert(QStringLiteral("source"),
		QStringLiteral("InstrumentFunctionNoteStacking::ChordTable, the scale names the "
			"piano roll's own scale combo is filled from"));

	QJsonObject state;
	state.insert(QStringLiteral("key"), detectProjectKeyJson(song->projectKey()));
	state.insert(QStringLiteral("tempo_map"), tempoMap);
	state.insert(QStringLiteral("method"), detectMethodJson());
	state.insert(QStringLiteral("bounds"), detectBoundsJson());
	state.insert(QStringLiteral("scale_vocabulary"), vocabulary);
	return state;
}

} // namespace control

} // namespace lmms
