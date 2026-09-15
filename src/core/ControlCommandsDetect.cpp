/*
 * ControlCommandsDetect.cpp - the `detect.*` command group (SPEC-zene-studio
 *                              A11-A16): import detection over the control
 *                              surface.
 *
 * THE ITEM THIS CLOSES. "Transient / BPM / key detection on import" was a
 * `to build` row of docs/FEATURE-LIST-0.3.0.md (row 34, section 6) whose two
 * dependencies - the tempo map (OWNER-31 item 16/26) and the scale machinery -
 * are both in this line. BACKLOG.md item 10 states what the smallest honest
 * version is, and this group follows it rather than exceeding it:
 *
 *   "on import, one offline pass filling (a) tempo (BPM) and (b) the first
 *    transient, shown as SUGGESTIONS the user accepts - never applied silently."
 *
 * So: `detect.analyze` is the suggestion (three reads of what a file implies,
 * writing nothing), and `detect.apply` is the acceptance - the one verb that
 * writes, and it writes only when a caller asks for it by name. Nothing here
 * runs on import by itself; there is no UI to run it from either
 * (docs/KNOWN-LIMITATIONS.md), which is why the socket IS the feature's surface.
 *
 * THE THREE IDS:
 *   detect.analyze    - analyse a file: tempo, first transient, key. Read-only.
 *   detect.apply      - write the detected tempo into the TEMPO MAP and the
 *                       detected key into the project's own key field, as ONE
 *                       undoable step.
 *   detect.get_state  - what the project currently holds (the key field, the
 *                       map's tempo at tick 0), the two method names, the
 *                       bounds - and the accuracy sentence, so an agent that
 *                       reads the surface cannot mistake a suggestion for a
 *                       measurement.
 *
 * REVERSIBILITY (SPEC A16), honestly. `detect.analyze` and `detect.get_state`
 * write nothing: not_mutating. `detect.apply` writes TWO things that no single
 * live object carries - the tempo map (not a JournallingObject, and not inside
 * the Song's own checkpoint, the finding ControlCommandsTransportMap.cpp
 * records) and the project key (a plain value on the Song, the shape
 * GroovePool has) - so its inverse is a recorded ACTION checkpoint that writes
 * both captures back, and `control.undo` takes a detection off in ONE step.
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

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The tick the applied tempo event lands on: tick 0, the map's own total
//! override (include/TempoMap.h, decision 2) - every tick from the start of the
//! timeline answers the detected tempo, and nothing before tick 0 exists.
constexpr tick_t kAppliedEventTick = 0;

//! The one-line accuracy statement every read of this group carries. It says
//! what was measured and what was not, in the words the release documents use.
QString accuracyNote()
{
	return QStringLiteral(
		"unverified on real music. What is measured is synthesised input with a known "
		"answer (a click track at a chosen BPM, a scale at a chosen root): "
		"tools/import-detection-proof.cpp and the registered ImportDetectionTest. No "
		"real-music corpus was measured on this box, and the confidence numbers are the "
		"detector's own scores, not probabilities. docs/IMPORT-DETECTION.md is the record");
}

QJsonObject boundsJson()
{
	QJsonObject bounds;
	bounds.insert(QStringLiteral("min_bpm"), detection::MinDetectionBpm);
	bounds.insert(QStringLiteral("max_bpm"), detection::MaxDetectionBpm);
	bounds.insert(QStringLiteral("default_seconds"), detection::DefaultAnalysisSeconds);
	bounds.insert(QStringLiteral("max_seconds"), detection::MaxAnalysisSeconds);
	bounds.insert(QStringLiteral("chroma_min_hz"), detection::ChromaMinHz);
	bounds.insert(QStringLiteral("chroma_max_hz"), detection::ChromaMaxHz);
	bounds.insert(QStringLiteral("max_template_degrees"), detection::MaxTemplateDegrees);
	bounds.insert(QStringLiteral("tempo_event_tick"), static_cast<qint64>(kAppliedEventTick));
	bounds.insert(QStringLiteral("tempo_rounding"),
		QStringLiteral("the tempo map holds an INTEGER bpm, so an applied tempo is rounded "
			"to the nearest whole BPM and the exact estimate is reported beside it"));
	return bounds;
}

QJsonObject tempoJson(const detection::TempoEstimate& tempo)
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

QJsonObject keyJson(const ImportDetectionResult& analysis)
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
QJsonObject methodJson()
{
	QJsonObject methods;
	methods.insert(QStringLiteral("tempo"), QString::fromLatin1(detection::TempoMethodName));
	methods.insert(QStringLiteral("key"), QString::fromLatin1(detection::KeyMethodName));
	methods.insert(QStringLiteral("accuracy_note"), accuracyNote());
	return methods;
}

//! The whole analysis, as the wire reports it.
QJsonObject analysisJson(const ImportDetectionResult& analysis)
{
	QJsonObject result;
	result.insert(QStringLiteral("path"), analysis.path);
	result.insert(QStringLiteral("sample_rate"), analysis.sampleRate);
	result.insert(QStringLiteral("frames"), analysis.frames);
	result.insert(QStringLiteral("duration_seconds"), analysis.durationSeconds);
	result.insert(QStringLiteral("analysed_seconds"), analysis.analysedSeconds);
	result.insert(QStringLiteral("tempo"), tempoJson(analysis.tempo));
	result.insert(QStringLiteral("key"), keyJson(analysis));
	result.insert(QStringLiteral("method"), methodJson());
	result.insert(QStringLiteral("bounds"), boundsJson());
	return result;
}

//! The project's OWN key field, as it stands now.
QJsonObject projectKeyJson(const ProjectKey& key)
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
bool readBound(const QJsonObject& args, double* out, ControlResult* error)
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
void recordApplyRestore(const TempoMap& before, const QString& keyBefore)
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
QJsonObject detectState(Song* song)
{
	const TempoMap& map = song->tempoMap().map();
	QJsonObject tempoMap;
	tempoMap.insert(QStringLiteral("active"), map.active());
	tempoMap.insert(QStringLiteral("event_count"), map.size());
	tempoMap.insert(QStringLiteral("global_tempo"),
		static_cast<int>(song->getTempo()));
	tempoMap.insert(QStringLiteral("tempo_at_tick_0"),
		map.tempoAtTick(kAppliedEventTick, static_cast<int>(song->getTempo())));
	tempoMap.insert(QStringLiteral("event_at_tick_0"), map.hasEventAt(kAppliedEventTick));

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
	state.insert(QStringLiteral("key"), projectKeyJson(song->projectKey()));
	state.insert(QStringLiteral("tempo_map"), tempoMap);
	state.insert(QStringLiteral("method"), methodJson());
	state.insert(QStringLiteral("bounds"), boundsJson());
	state.insert(QStringLiteral("scale_vocabulary"), vocabulary);
	return state;
}

//! `detect.analyze` - the suggestion. Reads one file; writes nothing.
void registerAnalyzeCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("detect.analyze");
	cmd.group = QStringLiteral("detect");
	cmd.verb = QStringLiteral("analyze");
	cmd.description = QStringLiteral("Analyse an audio file the importer can read and "
		"report what it implies: the tempo (BPM) over the declared 40..240 BPM band, the "
		"FIRST transient (in seconds and frames), and the key as a tonic pitch class plus "
		"a scale name from the pre-existing scale vocabulary - each with the detector's "
		"own score and the method that produced it. Writes NOTHING: this is the "
		"suggestion BACKLOG.md item 10 asks for, and `detect.apply` is the acceptance. "
		"Reads at most `max_seconds` (default 60) from the START of the file.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("max_seconds"), numberProperty()},
	}, {QStringLiteral("path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("duration_seconds"), numberProperty()},
		{QStringLiteral("analysed_seconds"), numberProperty()},
		{QStringLiteral("tempo"), objectProperty()},
		{QStringLiteral("key"), objectProperty()},
		{QStringLiteral("method"), objectProperty()},
		{QStringLiteral("bounds"), objectProperty()},
	});
	cmd.handler = [](const QJsonObject& args) {
		double bound = 0.0;
		ControlResult error;
		if (!readBound(args, &bound, &error)) { return error; }
		const ImportDetectionResult analysis =
			analyseAudioFile(args.value(QStringLiteral("path")).toString(), bound);
		if (!analysis.ok)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, analysis.error);
		}
		return ControlResult::success(analysisJson(analysis));
	};
	registry.registerCommand(cmd);
}

/*! `detect.apply` - the acceptance. ONE command writes both halves and records
 *  ONE inverse; a call that names a half the file gave nothing for is REFUSED
 *  and writes neither (no half-applied detection). */
void registerApplyCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("detect.apply");
	cmd.group = QStringLiteral("detect");
	cmd.verb = QStringLiteral("apply");
	cmd.description = QStringLiteral("Analyse an audio file and WRITE the result into the "
		"project's own fields: the detected tempo becomes a tempo-map event at tick 0 (the "
		"map is switched on, and the whole BPM is the map's integer - the exact estimate "
		"is reported back beside it), and the detected key is stored in the project's "
		"`<detected-key>` field under a scale name the pre-existing vocabulary knows. "
		"`tempo` and `key` select the halves (both default to true); a half the analysis "
		"found nothing for is REFUSED and NOTHING is written. Both writes are ONE undoable "
		"step: control.undo takes the whole detection off.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("tempo"), booleanProperty()},
		{QStringLiteral("key"), booleanProperty()},
		{QStringLiteral("max_seconds"), numberProperty()},
	}, {QStringLiteral("path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("tempo"), objectProperty()},
		{QStringLiteral("key"), objectProperty()},
		{QStringLiteral("method"), objectProperty()},
		{QStringLiteral("bounds"), objectProperty()},
		{QStringLiteral("key_state"), objectProperty()},
		{QStringLiteral("tempo_map"), objectProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		Song* song = Engine::getSong();
		double bound = 0.0;
		ControlResult error;
		if (!readBound(args, &bound, &error)) { return error; }

		const bool writeTempo = !args.contains(QStringLiteral("tempo"))
			|| args.value(QStringLiteral("tempo")).toBool();
		const bool writeKey = !args.contains(QStringLiteral("key"))
			|| args.value(QStringLiteral("key")).toBool();
		if (!writeTempo && !writeKey)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("detect.apply was asked to write neither half: set at least "
					"one of `tempo` or `key`"));
		}

		const QString path = args.value(QStringLiteral("path")).toString();
		const ImportDetectionResult analysis = analyseAudioFile(path, bound);
		if (!analysis.ok)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, analysis.error);
		}

		// Everything that can refuse is decided BEFORE the first write, so a
		// refusal never leaves half a detection in the project.
		int tempoForMap = -1;
		if (writeTempo)
		{
			if (!analysis.tempo.found)
			{
				return ControlResult::failure(ControlErrorKind::Refused,
					QStringLiteral("no tempo was found in `%1`: the onset envelope carries "
						"fewer than four transients, or correlates with itself below the "
						"0.2 floor. Nothing was written").arg(path));
			}
			tempoForMap = roundedTempoForMap(analysis.tempo.bpm);
			if (tempoForMap < 0)
			{
				return ControlResult::failure(ControlErrorKind::Refused,
					QStringLiteral("the detected tempo (%1 BPM) is outside the tempo map's "
						"own range %2..%3; nothing was written")
						.arg(analysis.tempo.bpm).arg(TempoMapMinTempo).arg(TempoMapMaxTempo));
			}
		}
		if (writeKey)
		{
			if (!analysis.key.found || !analysis.scaleKnown)
			{
				return ControlResult::failure(ControlErrorKind::Refused,
					QStringLiteral("no key the pre-existing scale vocabulary can name was "
						"found in `%1`; nothing was written").arg(path));
			}
		}

		const TempoMap before = song->tempoMap().map();
		const QString keyBefore = song->projectKey().toXml();

		QJsonObject tempoResult;
		if (writeTempo)
		{
			const bool edited = song->tempoMap().edit([tempoForMap](TempoMap& map) {
				TempoMap candidate = map;
				candidate.setActive(true);
				TempoMapEvent event;
				event.tick = kAppliedEventTick;
				event.hasTempo = true;
				event.tempo = tempoForMap;
				if (!candidate.addEvent(event)) { return false; }
				map = candidate;
				return true;
			});
			if (!edited)
			{
				return ControlResult::failure(ControlErrorKind::Refused,
					QStringLiteral("the engine refused the tempo-map event; nothing was "
						"written"));
			}
			const TempoMap& map = song->tempoMap().map();
			tempoResult.insert(QStringLiteral("applied"), true);
			tempoResult.insert(QStringLiteral("detected_bpm"), analysis.tempo.bpm);
			tempoResult.insert(QStringLiteral("bpm"), tempoForMap);
			tempoResult.insert(QStringLiteral("rounded"),
				std::abs(analysis.tempo.bpm - tempoForMap) > 1e-9);
			tempoResult.insert(QStringLiteral("event_tick"), static_cast<qint64>(kAppliedEventTick));
			tempoResult.insert(QStringLiteral("events"), map.size());
			tempoResult.insert(QStringLiteral("active"), map.active());
			tempoResult.insert(QStringLiteral("confidence"), analysis.tempo.confidence);
		}
		else
		{
			tempoResult.insert(QStringLiteral("applied"), false);
		}

		QJsonObject keyResult;
		if (writeKey)
		{
			song->projectKey().set(analysis.tonicName, analysis.key.tonicPitchClass,
				analysis.scaleName, analysis.key.score, analysis.key.margin,
				analysis.keyMethod, analysis.path);
			keyResult = projectKeyJson(song->projectKey());
			keyResult.insert(QStringLiteral("applied"), true);
		}
		else
		{
			keyResult.insert(QStringLiteral("applied"), false);
		}

		recordApplyRestore(before, keyBefore);

		QJsonObject beforePayload;
		beforePayload.insert(QStringLiteral("tempo_map_active"), before.active());
		beforePayload.insert(QStringLiteral("tempo_map_events"), before.size());
		beforePayload.insert(QStringLiteral("key_present"), !keyBefore.isEmpty());
		// The key field is ONE small element, so the record carries it whole: a
		// reader of the A16 transaction can see exactly what an undo puts back
		// without opening the project.
		beforePayload.insert(QStringLiteral("key_xml"), keyBefore);

		QJsonObject result;
		result.insert(QStringLiteral("path"), analysis.path);
		result.insert(QStringLiteral("tempo"), tempoResult);
		result.insert(QStringLiteral("key"), keyResult);
		result.insert(QStringLiteral("method"), methodJson());
		result.insert(QStringLiteral("bounds"), boundsJson());
		result.insert(QStringLiteral("key_state"), projectKeyJson(song->projectKey()));
		result.insert(QStringLiteral("__transaction"), transactionPayload(beforePayload,
			QStringLiteral("detect.apply"),
			QJsonObject{{QStringLiteral("path"), analysis.path}},
			true,
			QStringLiteral("action checkpoint: BOTH halves of a detection are captured "
				"before the first write - the tempo map (not a JournallingObject, and not "
				"inside the Song's own checkpoint) and the project's `<detected-key>` "
				"element - and the recorded step writes both back, so control.undo takes "
				"the whole detection off in one step. Re-issuing detect.apply is the "
				"forward path.")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

//! `detect.get_state` - what the project holds, the methods and the bounds.
void registerGetStateCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("detect.get_state");
	cmd.group = QStringLiteral("detect");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("Read back what the project holds: the detected key "
		"(the `<detected-key>` field, when a detection has been applied), the tempo map's "
		"own state and what it answers at tick 0, the scale vocabulary a key name can come "
		"from, the two detection method names, the bounds - and the accuracy sentence "
		"(`method.accuracy_note`), which says plainly that real-world accuracy is "
		"unverified and that the confidence numbers are scores, not probabilities.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("key"), objectProperty()},
		{QStringLiteral("tempo_map"), objectProperty()},
		{QStringLiteral("method"), objectProperty()},
		{QStringLiteral("bounds"), objectProperty()},
		{QStringLiteral("scale_vocabulary"), objectProperty()},
	});
	cmd.handler = [](const QJsonObject&) {
		return ControlResult::success(detectState(Engine::getSong()));
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerDetectCommands(ControlRegistry& registry)
{
	registerAnalyzeCommand(registry);
	registerApplyCommand(registry);
	registerGetStateCommand(registry);
}

} // namespace lmms
