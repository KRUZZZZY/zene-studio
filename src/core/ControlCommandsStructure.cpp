/*
 * ControlCommandsStructure.cpp - track.move: the arrangement's ORDER as a
 *                                drivable, undoable operation (Zene Studio,
 *                                SPEC A16 deliverable 5, task #664, feature
 *                                row 75).
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

/*
 * WHY THIS VERB, AND WHY IT IS NOT A WRAPPER
 *
 * Row 75's other half is that the structural operations were "not journalled
 * and several paths bypass addJournalCheckPoint". `track.move` is the one that
 * did not exist AT ALL: the engine can reorder a container
 * (`TrackContainer::moveTrack`) and the product's own interface calls it from a
 * drag or the up/down arrows, but no command did - so the arrangement's order
 * was the one structural property of a song an agent could not change, and the
 * reorder a user made by dragging was not undoable either.
 *
 * The engine half is `control::journalTrackMove` + `control::moveTrackToIndex`
 * (src/core/ControlStructuralSupport.cpp): a step whose undo puts the track back
 * where it was. The order of a container is not a serialized property of any
 * track, so no object checkpoint can express it and the recorded pair IS the
 * mechanism - the same shape the tempo map's rows take for a value type the
 * engine does not journal.
 *
 * The reorder is also the one structural edit that is NOT bounded by
 * StructuralSnapshotLimit: it captures no document (payload 0 bytes), because
 * there is nothing to capture beyond two integers.
 */

#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

void registerTrackMove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.move");
	cmd.group = QStringLiteral("track");
	cmd.verb = QStringLiteral("move");
	cmd.description = QStringLiteral("Reorder a track: put it at `index` in the song's track list "
		"(0-based, the index track.list prints). Reversible through one recorded action step - "
		"control.undo puts the track back where it was, and the redo sends it forward again. "
		"Refuses an index outside the song rather than clamping it. A track that is not in the "
		"song container is refused too: the engine's own reorder erases-then-inserts, so a "
		"foreign pointer would be DUPLICATED into the song.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("index"), integerProperty(0, MaxSongLength)},
	}, {QStringLiteral("track"), QStringLiteral("index")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("index"), integerProperty()},
		{QStringLiteral("previous_index"), integerProperty()},
		{QStringLiteral("moved"), booleanProperty()},
		{QStringLiteral("track_count"), integerProperty(0, MaxSongLength)},
		// The song's track order AFTER the move, by stable id - the read that
		// makes "the reorder happened" checkable in the same reply, and the one
		// a client compares against `control.undo`'s.
		{QStringLiteral("order"), arrayProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
		if (track == nullptr) { return error; }

		Song* song = Engine::getSong();
		const int from = trackIndexIn(song, track);
		if (from < 0)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("track.move: the track is not in the song container"));
		}
		const int count = static_cast<int>(song->tracks().size());
		const int to = args.value(QStringLiteral("index")).toInt();
		if (to < 0 || to >= count)
		{
			// Refused, never clamped: "move it to the end" and "move it to track
			// 400" are different requests, and a clamped one would silently do
			// something nobody asked for.
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("track.move: index %1 is outside the song's %2 track(s) "
					"[0, %3]; it is refused rather than clamped")
					.arg(to).arg(count).arg(count - 1));
		}

		const QString id = args.value(QStringLiteral("track")).toString();
		if (to != from)
		{
			// The recorded step BEFORE the move: the same order, one place
			// earlier, is the inverse - and it is pushed before the edit so a
			// handler that fails after it cannot leave a step describing an
			// operation that did not happen.
			journalTrackMove(track, from, to);
			if (!moveTrackToIndex(track, to))
			{
				return ControlResult::failure(ControlErrorKind::Busy,
					QStringLiteral("track.move: the engine refused the reorder of %1").arg(id));
			}
		}

		auto order = [song]() {
			QJsonArray ids;
			for (Track* t : song->tracks()) { ids.append(trackIdOf(t)); }
			return ids;
		};

		QJsonObject result;
		result.insert(QStringLiteral("track"), id);
		result.insert(QStringLiteral("index"), trackIndexIn(song, track));
		result.insert(QStringLiteral("previous_index"), from);
		result.insert(QStringLiteral("moved"), to != from);
		result.insert(QStringLiteral("track_count"), count);
		result.insert(QStringLiteral("order"), order());

		QJsonObject before;
		before.insert(QStringLiteral("track"), id);
		before.insert(QStringLiteral("index"), from);
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("track"), id);
		inverseArgs.insert(QStringLiteral("index"), from);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(before, QStringLiteral("track.move"), inverseArgs,
				to != from,
				to != from
					? QStringLiteral("action checkpoint: the recorded undo step puts the track "
						"back at the index it came from, through the engine's own "
						"TrackContainer::moveTrack. A container's order is not a serialized "
						"property of any track, so there is no object checkpoint that could "
						"express it")
					: QStringLiteral("no-op: the track is already at that index, so nothing "
						"was changed and no step was recorded")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerTrackStructureCommands(ControlRegistry& registry)
{
	registerTrackMove(registry);
}

} // namespace lmms
