/*
 * ControlCommandsDetect.cpp - the `detect.*` command group's READ half
 *                              (SPEC-zene-studio A11-A16): import detection
 *                              over the control surface, and the group's one
 *                              registration entry point. The writer lives in
 *                              ControlCommandsDetectApply.cpp and the shared
 *                              vocabulary in ControlDetectSupport.h.
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

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

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
		if (!readDetectBound(args, &bound, &error)) { return error; }
		const ImportDetectionResult analysis =
			analyseAudioFile(args.value(QStringLiteral("path")).toString(), bound);
		if (!analysis.ok)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, analysis.error);
		}
		return ControlResult::success(detectAnalysisJson(analysis));
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
		return ControlResult::success(detectStateJson(Engine::getSong()));
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerDetectCommands(ControlRegistry& registry)
{
	registerAnalyzeCommand(registry);
	registerGetStateCommand(registry);
	// The group's one writing verb, in its own translation unit (the automation,
	// warp and mastering groups' read/edit split): one entry point per group, so
	// the registry's composition names `detect` once.
	registerDetectApplyCommands(registry);
}

} // namespace lmms
