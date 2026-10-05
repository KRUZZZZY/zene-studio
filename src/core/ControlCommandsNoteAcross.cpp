/*
 * ControlCommandsNoteAcross.cpp - note.across: one note edit over several clips, one undo step
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

/*! ED-078 (0.5.0 L07): edit several clips at once. note.across runs ONE clip-scoped note command -
 *  transpose, velocity, humanise, time scale, ratchet, find and change, quantise, groove, restore
 *  timing, snap to scale - on every clip it names, with the same arguments. The handlers run inside
 *  this one, so the registry merges every clip's checkpoint into ONE undo step. Every clip and the
 *  arguments for each are checked before anything is written; a clip the command finds nothing to do
 *  in (no notes in scope) is reported, not fatal. */

#include <QJsonArray>
#include <QSet>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxClips = 64;

const QStringList& acrossCommands()
{
	static const QStringList ids{QStringLiteral("note.transpose"), QStringLiteral("note.velocity_scale"),
		QStringLiteral("note.velocity_offset"), QStringLiteral("note.randomize"), QStringLiteral("note.scale"),
		QStringLiteral("note.ratchet"), QStringLiteral("note.transform_where"), QStringLiteral("note.restore_timing"),
		QStringLiteral("groove.quantize"), QStringLiteral("groove.apply"), QStringLiteral("scale.snap_notes")};
	return ids;
}

//! Every clip resolved and the command's arguments for it checked; false (with @a error) at the first that fails.
bool prepare(const ControlCommand& command, const QJsonArray& clips, const QJsonObject& args, QVector<QJsonObject>* calls,
	ControlResult* error)
{
	if (clips.isEmpty() || clips.size() > MaxClips)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'clips' names 1..%1 MIDI clips").arg(MaxClips));
		return false;
	}
	QSet<QString> seen;
	for (const QJsonValue& value : clips)
	{
		if (seen.contains(value.toString())) { continue; }  // a clip named twice is edited once
		seen.insert(value.toString());
		ClipRef ref;
		if (resolveMidiClip(value.toString(), &ref, error) == nullptr) { return false; }
		QJsonObject call = args;
		call.insert(QStringLiteral("clip"), value.toString());
		const QString invalid = ControlRegistry::validateArgs(command.argsSchema, call);
		if (!invalid.isEmpty())
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("%1: %2").arg(command.id, invalid));
			return false;
		}
		calls->append(call);
	}
	return true;
}

//! 'clips', plus every MIDI clip of each track in 'tracks' (edit a whole track, or several, at once).
bool clipsNamed(const QJsonObject& args, QJsonArray* clips, ControlResult* error)
{
	*clips = args.value(QStringLiteral("clips")).toArray();
	for (const QJsonValue& value : args.value(QStringLiteral("tracks")).toArray())
	{
		Track* track = resolveTrack(value.toString(), error);
		if (track == nullptr) { return false; }
		for (Clip* clip : track->getClips())
		{
			if (dynamic_cast<MidiClip*>(clip) != nullptr) { clips->append(clipIdOf(clip)); }
		}
	}
	return true;
}

ControlResult across(const QJsonObject& args)
{
	const QString id = args.value(QStringLiteral("command")).toString();
	const ControlCommand* command = ControlRegistry::instance()->command(id);
	if (command == nullptr || !acrossCommands().contains(id))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'command' is one of %1").arg(acrossCommands().join(QStringLiteral(", "))));
	}
	ControlResult error;
	QJsonArray clips;
	if (!clipsNamed(args, &clips, &error)) { return error; }
	QVector<QJsonObject> calls;
	if (!prepare(*command, clips, args.value(QStringLiteral("args")).toObject(), &calls, &error)) { return error; }
	QJsonArray results;
	int edited = 0;
	for (const QJsonObject& call : calls)
	{
		const ControlResult done = command->handler(call);
		QJsonObject row{{QStringLiteral("clip"), call.value(QStringLiteral("clip"))}, {QStringLiteral("ok"), done.ok}};
		// A handler that succeeded WITHOUT changing anything left no journal checkpoint and
		// described no transaction (note.restore_timing over notes that keep no original timing
		// answers 'restored: 0'). Counting it as an edit would report clips as edited and return a
		// control.undo transaction that no undo step backs - replaying it would undo something else.
		if (done.ok && done.result.contains(QStringLiteral("__transaction"))) { ++edited; }
		else if (!done.ok) { row.insert(QStringLiteral("error"), done.errorMessage); }
		results.append(row);
	}
	if (edited == 0) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("%1 changed none of the clips").arg(id)); }
	QJsonObject result{{QStringLiteral("command"), id}, {QStringLiteral("edited"), edited}, {QStringLiteral("clips"), results}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("clips"), args.value(QStringLiteral("clips"))}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("each clip's MidiClip checkpoint, merged into one step")));
	return ControlResult::success(result);
}

} // namespace

void registerNoteAcrossCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("note");
	cmd.verb = QStringLiteral("across");
	cmd.id = QStringLiteral("note.across");
	cmd.description = QStringLiteral("Edit several clips at once: run one clip-scoped note command ('command': note.transpose, "
		"note.velocity_scale, note.velocity_offset, note.randomize, note.scale, note.ratchet, note.transform_where, "
		"note.restore_timing, groove.quantize, groove.apply or scale.snap_notes) with the same 'args' on every clip in "
		"'clips' and every MIDI clip of the tracks in 'tracks' (1..64 clips in all; 'clip' is filled in for each). Every clip and its arguments are checked before "
		"anything is written; 'clips' in the reply says which ones changed. ONE undo step for all of them.");
	cmd.argsSchema = objectSchema({{QStringLiteral("clips"), arrayProperty()}, {QStringLiteral("tracks"), arrayProperty()},
		{QStringLiteral("command"), stringProperty()}, {QStringLiteral("args"), objectProperty()}}, {QStringLiteral("command")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = true;
	cmd.handler = across;
	registry.registerCommand(cmd);
}

} // namespace lmms
