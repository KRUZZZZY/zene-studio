/*
 * ControlCommandsSessionRecordLand.cpp - Arrangement Record's one writing verb:
 *                                        the pass that turns the recorded
 *                                        performance into arrangement clips
 *                                        (SPEC-zene-studio §4.1; board task
 *                                        #641, the #596 half).
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
 */

/* WHY THIS IS ITS OWN TRANSLATION UNIT. The group measured 518 lines against
 * this fork's 500-line file-length ratchet as one file, and a fork-new file over
 * the limit fails the gate - so the tap (arm / status / back-to-arrangement) and
 * the pass are two files, on the same read/edit seam the automation, warp, vca
 * and chain-preset groups use. The pairing the two share is in
 * ControlCommandsSessionRecordInternal.h.
 *
 * WHAT A LANDED CLIP CARRIES. Position and length are the recorded span, and
 * that is all this build can fill in: `pattern` reports the PatternStore
 * reference the session slot names, but the notes are NOT copied into the clip
 * and a session slot does not render audio in 0.3.0 (there is no session-clip
 * playback path - #597). So the landed span is a real, editable arrangement clip
 * in the right place for the right length, and the "rendered audio matches the
 * session playback" half of #596's acceptance is UNMET in this tree rather than
 * faked; docs/KNOWN-LIMITATIONS.md states exactly that.
 *
 * THE INVERSE. The pass creates clips over one Track journal checkpoint per
 * touched track - the clip.add and midi.retro_capture_to_clip shape - so ONE
 * control.undo takes the whole pass back. `before`/`inverse` in the recorded
 * transaction describe the FIRST clip the pass created, which is a complete and
 * exact inverse for that clip (clip.delete removes it); the mechanism names the
 * checkpoints that carry the rest, because a pass creates one clip per completed
 * launch and a transaction carries one inverse.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlCommandsSessionRecordInternal.h"
#include "ControlCommandsSessionShared.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "SessionArrangementRecorder.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace sessioncontrol;
using namespace sessionrecord;

namespace
{

void registerRecordLand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.arrangement_record_land");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("arrangement_record_land");
	cmd.description = QStringLiteral("Land the recorded performance: pair every recorded launch "
		"with the stop that ended it and create ONE arrangement clip per pair on that column's "
		"song track, at the recorded ticks. REFUSED - consuming nothing - while any recorded start "
		"is still open, because a half-landed performance would lose starts or invent their ends; "
		"stop the session first (session.stop_all, or session.back_to_arrangement). Reversible: "
		"one Track journal checkpoint per touched track, so one control.undo takes the whole pass "
		"back. The clip's CONTENT is not copied from the session slot (there is no session-clip "
		"playback path in 0.3.0): the reply names the slot's pattern reference per clip.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("landed"), arrayProperty()},
		{QStringLiteral("clips"), integerProperty()},
		{QStringLiteral("pairs"), integerProperty()},
		{QStringLiteral("events"), integerProperty()},
		{QStringLiteral("skipped_columns"), integerProperty()},
		{QStringLiteral("collapsed"), integerProperty()},
		{QStringLiteral("unmatched_stops"), integerProperty()},
		{QStringLiteral("recorded"), integerProperty()},
		{QStringLiteral("dropped"), integerProperty()},
		{QStringLiteral("armed"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject&) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		Song* song = Engine::getSong();
		SessionArrangementRecorder& recorder = song->sessionScheduler().arrangementRecorder();
		SessionArrangementRecorder::Event events[MaxLandEvents];
		const std::size_t count = recorder.snapshot(events, MaxLandEvents);
		if (count == 0)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("nothing has been recorded: arm session.arrangement_record_arm, "
					"perform, and stop the session before landing"));
		}
		LandPair pairs[MaxLandEvents];
		OpenStart open[MaxLandEvents];
		int openCount = 0;
		int unmatchedStops = 0;
		const int pairCount = landPairEvents(events, count, pairs, MaxLandEvents,
			open, &openCount, MaxLandEvents, &unmatchedStops);
		if (openCount > 0)
		{
			// The refusal names what is still playing, because that is what the
			// client has to stop - and it consumes NOTHING, so the performance it
			// names is still there to land afterwards.
			QStringList stillPlaying;
			for (int i = 0; i < openCount; ++i)
			{
				stillPlaying << QStringLiteral("(track %1, scene %2) from tick %3")
					.arg(open[i].track).arg(open[i].scene).arg(open[i].tick);
			}
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("%1 recorded launch(es) have not stopped yet: %2. End the "
					"session (session.stop_all, or session.back_to_arrangement) and land "
					"again - nothing was consumed.").arg(openCount)
					.arg(stillPlaying.join(QStringLiteral(", "))));
		}

		// Every event is paired or an unmatched stop (which has no clip to make),
		// so the pass can consume the whole ring.
		consumeLandEvents(recorder, count);

		QJsonArray landed;
		int clips = 0;
		int skippedColumns = 0;
		int collapsed = 0;
		for (int i = 0; i < pairCount; ++i)
		{
			const QJsonObject entry = landPairState(*song, *model, pairs[i]);
			if (!entry.value(QStringLiteral("landed")).toBool())
			{
				if (entry.value(QStringLiteral("reason")).toString()
					== QStringLiteral("column_has_no_song_track")) { ++skippedColumns; }
				else { ++collapsed; }
			}
			else { ++clips; }
			landed.append(entry);
		}

		QJsonObject result;
		result.insert(QStringLiteral("landed"), landed);
		result.insert(QStringLiteral("clips"), clips);
		result.insert(QStringLiteral("pairs"), pairCount);
		result.insert(QStringLiteral("events"), static_cast<int>(count));
		result.insert(QStringLiteral("skipped_columns"), skippedColumns);
		result.insert(QStringLiteral("collapsed"), collapsed);
		result.insert(QStringLiteral("unmatched_stops"), unmatchedStops);
		result.insert(QStringLiteral("recorded"), static_cast<double>(recorder.recorded()));
		result.insert(QStringLiteral("dropped"), static_cast<double>(recorder.dropped()));
		result.insert(QStringLiteral("armed"), recorder.armed());

		int firstLanded = -1;
		for (int i = 0; i < landed.size(); ++i)
		{
			if (landed.at(i).toObject().value(QStringLiteral("landed")).toBool())
			{
				firstLanded = i;
				break;
			}
		}
		if (firstLanded >= 0)
		{
			const QJsonObject firstClip = landed.at(firstLanded).toObject();
			result.insert(QStringLiteral("__transaction"),
				transactionPayload(
					QJsonObject{{QStringLiteral("track"),
							firstClip.value(QStringLiteral("track"))},
						{QStringLiteral("track_index"),
							firstClip.value(QStringLiteral("track_index"))},
						{QStringLiteral("lands_created"), clips}},
					QStringLiteral("clip.delete"),
					QJsonObject{{QStringLiteral("clip"),
						firstClip.value(QStringLiteral("clip"))}},
					true,
					QStringLiteral("ProjectJournal (one Track checkpoint per touched track, "
						"taken before the pass created its clips: Track::restoreState re-loads "
						"the track's serialized clips, which is how the GUI's own clip "
						"add/delete paths reverse themselves). The recorded inverse names the "
						"FIRST clip of the pass, which clip.delete removes exactly; "
						"control.undo restores every touched track, which removes them all.")));
		}
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerSessionRecordLandCommands(ControlRegistry& registry)
{
	registerRecordLand(registry);
}

} // namespace lmms
