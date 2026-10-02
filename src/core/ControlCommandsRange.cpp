/*
 * ControlCommandsRange.cpp - range.*: a time range over tracks and the edits it carries
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

/*! ED-011/ED-012 (0.5.0 L04): the arranger's time-range selection and the edits over it - split
 *  at its edges, delete (closing the gap with ripple), crop, copy and paste, duplicate, insert
 *  time. Each edit checkpoints every track the range covers first, so it is ONE undo step
 *  (Track checkpoints carry their clips). The range itself is interface state. */

#include <QDomDocument>
#include <QJsonArray>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

//! range.copy's clipboard: each clip's serialised state, its track's place in the range and its
//! position relative to the range start.
struct CopiedClip
{
	int trackIndex;
	tick_t offset;
	QString xml;
};
QVector<CopiedClip> g_clipboard;
tick_t g_clipboardLength = 0;

QJsonObject rangeJson()
{
	const ArrangerRange& range = ArrangerRange::instance();
	QJsonArray tracks;
	for (Track* track : range.tracks()) { tracks.append(trackIdOf(track)); }
	return {{QStringLiteral("active"), range.active()}, {QStringLiteral("start"), range.start()},
		{QStringLiteral("end"), range.end()}, {QStringLiteral("all_tracks"), range.allTracks()},
		{QStringLiteral("tracks"), tracks}, {QStringLiteral("clipboard"), g_clipboard.size()}};
}

//! The active range's tracks, checkpointed as one undo step; false (with @a error) when there is
//! no range.
bool beginEdit(QVector<Track*>* tracks, ControlResult* error)
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (!range.active())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no range is selected (range.set)"));
		return false;
	}
	*tracks = range.tracks();
	QVector<JournallingObject*> objects;
	for (Track* track : *tracks) { objects.append(track); }
	control::addUndoStep(objects);
	return true;
}

ControlResult finishEdit(const QString& command, QJsonObject result)
{
	Engine::getSong()->setModified();
	result.insert(QStringLiteral("range"), rangeJson());
	result.insert(QStringLiteral("__transaction"), transactionPayload(rangeJson(), command, {}, true,
		QStringLiteral("Track checkpoints: every track the range covers was checkpointed before the edit, as one step")));
	return ControlResult::success(result);
}

ControlResult setRange(const QJsonObject& args)
{
	QVector<int> ids;
	for (const QJsonValue& value : args.value(QStringLiteral("tracks")).toArray())
	{
		ControlResult error;
		Track* track = resolveTrack(value.toString(), &error);
		if (track == nullptr) { return error; }
		ids.append(track->id());
	}
	ArrangerRange::instance().set(args.value(QStringLiteral("start")).toInt(), args.value(QStringLiteral("end")).toInt(), ids,
		args.value(QStringLiteral("all")).toBool(ids.isEmpty()));
	return ControlResult::success(rangeJson());
}

ControlResult splitRange(const QJsonObject&)
{
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const ArrangerRange& range = ArrangerRange::instance();
	int split = 0;
	for (Track* track : tracks) { split += arrangement::splitTrackAt(track, range.start()) + arrangement::splitTrackAt(track, range.end()); }
	return finishEdit(QStringLiteral("range.split"), {{QStringLiteral("split"), split}});
}

ControlResult deleteRange(const QJsonObject& args)
{
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const ArrangerRange& range = ArrangerRange::instance();
	const bool ripple = args.value(QStringLiteral("ripple")).toBool();
	int removed = 0;
	for (Track* track : tracks) { removed += arrangement::deleteRange(track, range.start(), range.end(), ripple); }
	return finishEdit(QStringLiteral("range.delete"), {{QStringLiteral("removed"), removed}, {QStringLiteral("ripple"), ripple}});
}

ControlResult cropRange(const QJsonObject&)
{
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const ArrangerRange& range = ArrangerRange::instance();
	int removed = 0;
	for (Track* track : tracks) { removed += arrangement::cropToRange(track, range.start(), range.end()); }
	return finishEdit(QStringLiteral("range.crop"), {{QStringLiteral("removed"), removed}});
}

ControlResult insertTime(const QJsonObject& args)
{
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const ArrangerRange& range = ArrangerRange::instance();
	const tick_t length = args.value(QStringLiteral("length")).toInt(range.end() - range.start());
	int moved = 0;
	for (Track* track : tracks)
	{
		arrangement::splitTrackAt(track, range.start());
		moved += arrangement::shiftClipsFrom(track, range.start(), length);
	}
	return finishEdit(QStringLiteral("range.insert_time"), {{QStringLiteral("moved"), moved}, {QStringLiteral("length"), length}});
}

ControlResult copyRange(const QJsonObject&)
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (!range.active()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no range is selected (range.set)")); }
	g_clipboard.clear();
	const QVector<Track*> tracks = range.tracks();
	for (int i = 0; i < tracks.size(); ++i)
	{
		for (Clip* clip : arrangement::clipsWithin(tracks.at(i), range.start(), range.end()))
		{
			QDomDocument doc;
			QDomElement root = doc.createElement(QStringLiteral("clip"));
			doc.appendChild(root);
			clip->saveState(doc, root);
			g_clipboard.append({i, clip->startPosition().getTicks() - range.start(), doc.toString()});
		}
	}
	g_clipboardLength = range.end() - range.start();
	return ControlResult::success({{QStringLiteral("copied"), g_clipboard.size()}, {QStringLiteral("length"), g_clipboardLength}});
}

//! Recreate the clipboard's clips at @a position on @a tracks (by their place in the range).
int pasteAt(const QVector<Track*>& tracks, tick_t position)
{
	int pasted = 0;
	for (const CopiedClip& copied : g_clipboard)
	{
		if (copied.trackIndex >= tracks.size()) { continue; }
		QDomDocument doc;
		doc.setContent(copied.xml);
		Track* track = tracks.at(copied.trackIndex);
		Clip* clip = track->createClip(TimePos(position + copied.offset));
		clip->restoreState(doc.documentElement().firstChildElement());
		clip->movePosition(TimePos(position + copied.offset));
		++pasted;
	}
	return pasted;
}

ControlResult pasteRange(const QJsonObject& args)
{
	if (g_clipboard.isEmpty()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("nothing copied (range.copy)")); }
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const tick_t position = args.value(QStringLiteral("position")).toInt(ArrangerRange::instance().start());
	return finishEdit(QStringLiteral("range.paste"), {{QStringLiteral("pasted"), pasteAt(tracks, position)}, {QStringLiteral("position"), position}});
}

ControlResult duplicateRange(const QJsonObject&)
{
	const ControlResult copied = copyRange({});
	if (!copied.ok) { return copied; }
	QVector<Track*> tracks;
	ControlResult error;
	if (!beginEdit(&tracks, &error)) { return error; }
	const ArrangerRange& range = ArrangerRange::instance();
	const int pasted = pasteAt(tracks, range.end());
	const tick_t length = range.end() - range.start();
	ArrangerRange::instance().set(range.end(), range.end() + length, range.trackIds(), range.allTracks());
	return finishEdit(QStringLiteral("range.duplicate"), {{QStringLiteral("pasted"), pasted}});
}

//! ED-031: clip.consolidate over the range on each covered sample track (it renders and records itself).
ControlResult consolidateRange(const QJsonObject&)
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (!range.active()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no range is selected (range.set)")); }
	QJsonArray done;
	QJsonArray refused;
	for (Track* track : range.tracks())
	{
		if (track->type() != Track::Type::Sample) { continue; }
		const ControlResult ran = ControlRegistry::instance()->invoke(QStringLiteral("clip.consolidate"),
			{{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("start"), range.start()}, {QStringLiteral("end"), range.end()}});
		(ran.ok ? done : refused).append(QJsonObject{{QStringLiteral("track"), trackIdOf(track)},
			{QStringLiteral("detail"), ran.ok ? QString() : ran.errorMessage}});
	}
	return ControlResult::success({{QStringLiteral("consolidated"), done}, {QStringLiteral("refused"), refused}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("range");
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerRangeCommands(ControlRegistry& registry)
{
	const QString oneStep = QStringLiteral(" One undo step (every covered track is checkpointed).");
	add(registry, "set", QStringLiteral("Select a time range from 'start' to 'end' (ticks) over 'tracks' (trk ids), or "
		"over every track ('all', the default with no tracks). Interface state, drawn in the arranger."),
		objectSchema({{QStringLiteral("start"), tickProperty()}, {QStringLiteral("end"), tickProperty()},
			{QStringLiteral("tracks"), arrayProperty()}, {QStringLiteral("all"), booleanProperty()}},
			{QStringLiteral("start"), QStringLiteral("end")}), false, setRange);
	add(registry, "get", QStringLiteral("The range: active, start, end, its tracks, and how many clips are copied."),
		objectSchema({}), false, [](const QJsonObject&) { return ControlResult::success(rangeJson()); });
	add(registry, "clear", QStringLiteral("Clear the range."), objectSchema({}), false, [](const QJsonObject&) {
		ArrangerRange::instance().clear();
		return ControlResult::success(rangeJson());
	});
	add(registry, "split", QStringLiteral("Split every clip on the range's tracks at its start and its end.") + oneStep,
		objectSchema({}), true, splitRange);
	add(registry, "delete", QStringLiteral("Remove what is inside the range on its tracks (clips crossing an edge are "
		"split there first); with 'ripple', later clips move left to close the gap.") + oneStep,
		objectSchema({{QStringLiteral("ripple"), booleanProperty()}}), true, deleteRange);
	add(registry, "crop", QStringLiteral("Keep only what is inside the range on its tracks.") + oneStep,
		objectSchema({}), true, cropRange);
	add(registry, "insert_time", QStringLiteral("Insert empty time at the range's start on its tracks - 'length' ticks, "
		"default the range's length - moving everything from there later.") + oneStep,
		objectSchema({{QStringLiteral("length"), tickProperty()}}), true, insertTime);
	add(registry, "copy", QStringLiteral("Copy the clips that lie inside the range (range.split first to include the "
		"parts of clips crossing its edges). Changes nothing."), objectSchema({}), false, copyRange);
	add(registry, "paste", QStringLiteral("Paste the copied clips at 'position' (default the range's start) onto the "
		"range's tracks, in the order they were copied from.") + oneStep,
		objectSchema({{QStringLiteral("position"), tickProperty()}}), true, pasteRange);
	add(registry, "consolidate", QStringLiteral("Render each covered sample track's clips inside the range into ONE clip "
		"(clip.consolidate per track - clips crossing an edge are refused: range.split first). Each track records "
		"and undoes as its own consolidation."), objectSchema({}), false, consolidateRange);
	add(registry, "duplicate", QStringLiteral("Copy what is inside the range and paste it right after it; the range moves "
		"onto the copy, so repeating it repeats the pattern.") + oneStep, objectSchema({}), true, duplicateRange);
}

} // namespace lmms
