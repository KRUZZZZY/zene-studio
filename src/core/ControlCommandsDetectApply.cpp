/*
 * ControlCommandsDetectApply.cpp - the `detect.*` group's ONE WRITING VERB
 *                                  (SPEC-zene-studio A11-A16): the acceptance
 *                                  half of import detection.
 *
 * It is its own translation unit for the reason the automation, warp, vca and
 * mastering groups split their writers out: the read half answers a question and
 * this half changes the project, and the two are reviewed differently. The
 * shared vocabulary is ControlDetectSupport.h; the group's single registration
 * entry point is registerDetectCommands() in ControlCommandsDetect.cpp.
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

namespace
{

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
		if (!readDetectBound(args, &bound, &error)) { return error; }

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
				event.tick = DetectAppliedEventTick;
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
			tempoResult.insert(QStringLiteral("event_tick"), static_cast<qint64>(DetectAppliedEventTick));
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
			keyResult = detectProjectKeyJson(song->projectKey());
			keyResult.insert(QStringLiteral("applied"), true);
		}
		else
		{
			keyResult.insert(QStringLiteral("applied"), false);
		}

		recordDetectApplyRestore(before, keyBefore);

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
		result.insert(QStringLiteral("method"), detectMethodJson());
		result.insert(QStringLiteral("bounds"), detectBoundsJson());
		result.insert(QStringLiteral("key_state"), detectProjectKeyJson(song->projectKey()));
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

} // namespace

void registerDetectApplyCommands(ControlRegistry& registry)
{
	registerApplyCommand(registry);
}

} // namespace lmms
