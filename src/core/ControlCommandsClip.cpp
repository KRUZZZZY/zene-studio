/*
 * ControlCommandsClip.cpp - the clip.* (song editor) command group (SPEC A11-A16).
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

#include <QJsonArray>
#include <QJsonObject>

#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

const QString ClauseTrackJournalled = QStringLiteral("ProjectJournal (Track checkpoint: "
	"Track::restoreState re-loads the track's serialized clips, which is how the GUI's own "
	"clip add/delete/split paths reverse themselves)");
const QString ClauseClipJournalled = QStringLiteral("ProjectJournal (Clip checkpoint: "
	"Clip::restoreState re-loads the clip's position/length)");

//! Ordinal of \p clip in the arrangement, or -1 when it is not in the song.
int ordinalOf(const Clip* clip)
{
	for (const control::ClipRef& ref : control::enumerateClips())
	{
		if (ref.clip == clip) { return ref.ordinal; }
	}
	return -1;
}

QJsonObject clipStateOf(Clip* clip, ControlResult* error)
{
	const int ordinal = ordinalOf(clip);
	if (ordinal < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("the clip is not in the song's arrangement"));
		return QJsonObject();
	}
	control::ClipRef ref;
	if (!control::resolveClip(control::clipId(ordinal), &ref, error)) { return QJsonObject(); }
	return control::clipState(ref);
}

//! Length of the clip after the mutation, with the explicit-length rule applied:
//! an explicit length makes the clip manually resized, so a later note edit no
//! longer auto-shrinks it (the GUI sets the same flag when a user resizes).
void applyLength(Clip* clip, const QJsonObject& args)
{
	if (!args.contains(QStringLiteral("length"))) { return; }
	clip->changeLength(TimePos(static_cast<tick_t>(args.value(QStringLiteral("length")).toDouble())));
	clip->setAutoResize(false);
}

void registerClipAdd(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.add");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("add");
	cmd.description = QStringLiteral("Create a clip on a track at a tick position and return its stable "
		"clip-<n> id. Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), control::integerProperty(1, MaxSongLength)},
		{QStringLiteral("name"), control::stringProperty()},
	}, {QStringLiteral("track"), QStringLiteral("position")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		Track* track = control::resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
		if (track == nullptr) { return error; }
		const int clipCount = track->numOfClips();
		QJsonObject before;
		before.insert(QStringLiteral("track"), args.value(QStringLiteral("track")).toString());
		before.insert(QStringLiteral("clip_count"), clipCount);

		// TrackContentWidget::mousePressEvent checkpoints the track and then calls
		// Track::createClip(); do the same so the journal owns a real inverse.
		track->addJournalCheckPoint();
		Clip* clip = track->createClip(TimePos(static_cast<tick_t>(
			args.value(QStringLiteral("position")).toDouble())));
		if (!args.value(QStringLiteral("name")).toString().isEmpty())
		{
			clip->setName(args.value(QStringLiteral("name")).toString());
		}
		applyLength(clip, args);

		const int ordinal = ordinalOf(clip);
		if (ordinal < 0)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("the new clip is not in the song's arrangement"));
		}
		QJsonObject result = clipStateOf(clip, &error);
		if (result.isEmpty()) { return error; }
		result.insert(QStringLiteral("clip"), control::clipId(ordinal));
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(before, QStringLiteral("clip.delete"),
				QJsonObject{{QStringLiteral("clip"), control::clipId(ordinal)}},
				true, ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipMove(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.move");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("move");
	cmd.description = QStringLiteral("Move a clip to an absolute tick position. Reversible through the "
		"ProjectJournal (Clip checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
	}, {QStringLiteral("clip"), QStringLiteral("position")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		if (!control::resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
		{
			return error;
		}
		const tick_t previous = ref.clip->startPosition().getTicks();
		ref.clip->addJournalCheckPoint();
		ref.clip->movePosition(TimePos(static_cast<tick_t>(
			args.value(QStringLiteral("position")).toDouble())));

		QJsonObject result;
		result.insert(QStringLiteral("clip"), control::clipId(ref.ordinal));
		result.insert(QStringLiteral("position"), ref.clip->startPosition().getTicks());
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), control::clipId(ref.ordinal));
		inverseArgs.insert(QStringLiteral("position"), previous);
		QJsonObject before;
		before.insert(QStringLiteral("clip"), control::clipId(ref.ordinal));
		before.insert(QStringLiteral("position"), previous);
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(before, QStringLiteral("clip.move"), inverseArgs,
				true, ClauseClipJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipResize(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.resize");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("resize");
	cmd.description = QStringLiteral("Set a clip's length in ticks (the clip becomes manually resized). "
		"Reversible through the ProjectJournal (Clip checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("length"), control::integerProperty(1, MaxSongLength)},
	}, {QStringLiteral("clip"), QStringLiteral("length")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("length"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		if (!control::resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
		{
			return error;
		}
		const tick_t previous = ref.clip->length().getTicks();
		ref.clip->addJournalCheckPoint();
		ref.clip->changeLength(TimePos(static_cast<tick_t>(
			args.value(QStringLiteral("length")).toDouble())));
		ref.clip->setAutoResize(false);

		QJsonObject result;
		result.insert(QStringLiteral("clip"), control::clipId(ref.ordinal));
		result.insert(QStringLiteral("length"), ref.clip->length().getTicks());
		QJsonObject before;
		before.insert(QStringLiteral("clip"), control::clipId(ref.ordinal));
		before.insert(QStringLiteral("length"), previous);
		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("clip"), control::clipId(ref.ordinal));
		inverseArgs.insert(QStringLiteral("length"), previous);
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(before, QStringLiteral("clip.resize"), inverseArgs,
				true, ClauseClipJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipSplit(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.split");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("split");
	cmd.description = QStringLiteral("Split a clip at an absolute tick position, leaving the left part in "
		"place and returning both ids. Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(1, MaxSongLength)},
	}, {QStringLiteral("clip"), QStringLiteral("position")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("left"), control::stringProperty()},
		{QStringLiteral("right"), control::stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		if (!control::resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
		{
			return error;
		}
		const tick_t start = ref.clip->startPosition().getTicks();
		const tick_t end = ref.clip->endPosition().getTicks();
		const tick_t splitPos = static_cast<tick_t>(args.value(QStringLiteral("position")).toDouble());
		// The GUI refuses the same two cuts (ClipView::splitClip): they would make
		// a zero-length clip or a copy the length of the original.
		if (splitPos <= start || splitPos >= end)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'position' %1 is not strictly inside the clip (%2..%3)")
					.arg(splitPos).arg(start).arg(end));
		}

		QJsonObject before;
		before.insert(QStringLiteral("clip"), control::clipId(ref.ordinal));
		before.insert(QStringLiteral("position"), start);
		before.insert(QStringLiteral("length"), end - start);

		// ClipView::splitClip: checkpoint the track, stop the new clip from
		// journalling itself, clone, then resize both halves.
		Track* track = ref.track;
		track->addJournalCheckPoint();
		track->saveJournallingState(false);
		Clip* right = ref.clip->clone();
		ref.clip->changeLength(TimePos(splitPos - start));
		ref.clip->setAutoResize(false);
		right->movePosition(TimePos(splitPos));
		right->changeLength(TimePos(end - splitPos));
		right->setStartTimeOffset(ref.clip->startTimeOffset() - ref.clip->length());
		right->setAutoResize(false);
		track->restoreJournallingState();

		const int rightOrdinal = ordinalOf(right);
		QJsonObject result;
		result.insert(QStringLiteral("left"), control::clipId(ref.ordinal));
		result.insert(QStringLiteral("right"), control::clipId(rightOrdinal));
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(before, QStringLiteral("UNIMPLEMENTED: re-join the two halves"),
				QJsonObject{{QStringLiteral("left"), control::clipId(ref.ordinal)},
					{QStringLiteral("right"), control::clipId(rightOrdinal)}},
				true, ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipDelete(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.delete");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("delete");
	cmd.description = QStringLiteral("Delete a clip from its track. dry_run previews it. Reversible "
		"through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("dry_run"), control::booleanProperty()},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("deleted"), control::stringProperty()},
		{QStringLiteral("track"), control::stringProperty()},
		{QStringLiteral("dry_run"), control::booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		if (!control::resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
		{
			return error;
		}
		QJsonObject snapshot = control::clipState(ref);
		if (args.value(QStringLiteral("dry_run")).toBool(false))
		{
			QJsonObject preview = snapshot;
			preview.insert(QStringLiteral("dry_run"), true);
			preview.insert(QStringLiteral("deleted"), control::clipId(ref.ordinal));
			preview.insert(QStringLiteral("__transaction"),
				control::transactionPayload(snapshot, QStringLiteral("UNIMPLEMENTED: re-create the clip"),
					QJsonObject(), false, QStringLiteral("dry_run preview: nothing was changed")));
			return ControlResult::success(preview);
		}

		Track* track = ref.track;
		const QString trackIdText = control::trackId(ref.trackIndex);
		// ClipView::remove: checkpoint the track, detach, delete.
		track->addJournalCheckPoint();
		track->removeClip(ref.clip);
		delete ref.clip;

		QJsonObject result;
		result.insert(QStringLiteral("deleted"), control::clipId(ref.ordinal));
		result.insert(QStringLiteral("track"), trackIdText);
		result.insert(QStringLiteral("dry_run"), false);
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(snapshot, QStringLiteral("UNIMPLEMENTED: re-create the clip"),
				QJsonObject(), true, ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipDuplicate(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.duplicate");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("duplicate");
	cmd.description = QStringLiteral("Copy a clip on its own track (notes included) and return the copy's "
		"id. Reversible through the ProjectJournal (Track checkpoint).");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
		{QStringLiteral("source"), control::stringProperty()},
		{QStringLiteral("position"), control::integerProperty(0, MaxSongLength)},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		control::ClipRef ref;
		if (!control::resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error))
		{
			return error;
		}
		const tick_t target = args.contains(QStringLiteral("position"))
			? static_cast<tick_t>(args.value(QStringLiteral("position")).toDouble())
			: ref.clip->endPosition().getTicks();
		QJsonObject before;
		before.insert(QStringLiteral("clip"), control::clipId(ref.ordinal));
		before.insert(QStringLiteral("clip_count"), ref.track->numOfClips());

		Track* track = ref.track;
		track->addJournalCheckPoint();
		Clip* copy = ref.clip->clone();
		copy->movePosition(TimePos(target));

		const int ordinal = ordinalOf(copy);
		QJsonObject result;
		result.insert(QStringLiteral("clip"), control::clipId(ordinal));
		result.insert(QStringLiteral("source"), control::clipId(ref.ordinal));
		result.insert(QStringLiteral("position"), copy->startPosition().getTicks());
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(before, QStringLiteral("clip.delete"),
				QJsonObject{{QStringLiteral("clip"), control::clipId(ordinal)}},
				true, ClauseTrackJournalled));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerClipSelect(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("clip.select");
	cmd.group = QStringLiteral("clip");
	cmd.verb = QStringLiteral("select");
	cmd.description = QStringLiteral("Select (or clear with an empty id) a clip for roll.get_state and the "
		"get_states' selection flags. Control-surface state, not project state.");
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("clip"), control::stringProperty()},
	}, {QStringLiteral("clip")});
	cmd.resultSchema = control::objectSchema({
		{QStringLiteral("selected_clip"), control::stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString previous = control::selectedClipId();
		const QString id = args.value(QStringLiteral("clip")).toString();
		ControlResult error;
		control::ClipRef ref;
		if (!id.isEmpty() && !control::resolveClip(id, &ref, &error)) { return error; }
		control::selectClip(id);

		QJsonObject result;
		result.insert(QStringLiteral("selected_clip"), id);
		QJsonObject before;
		before.insert(QStringLiteral("selected_clip"), previous);
		result.insert(QStringLiteral("__transaction"),
			control::transactionPayload(before, QStringLiteral("clip.select"),
				QJsonObject{{QStringLiteral("clip"), previous}}, false,
				QStringLiteral("selection is control-surface state, not project state: the GUI keeps its "
					"selection in views (QGraphicsItem state) and the project file has no field for it, so "
					"the ProjectJournal has no checkpoint to reverse")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerClipCommands(ControlRegistry& registry)
{
	registerClipAdd(registry);
	registerClipMove(registry);
	registerClipResize(registry);
	registerClipSplit(registry);
	registerClipDelete(registry);
	registerClipDuplicate(registry);
	registerClipSelect(registry);
}

} // namespace lmms
