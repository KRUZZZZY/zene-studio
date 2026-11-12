/*
 * ControlCommandsFolderSpans.cpp - track.folder_span*: a folder's summary clips, selected and edited as groups
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

/*! Q050-255: the group edits over a folder's summary clips. Each edit takes its targets the same way - 'spans' (or, without
 *  it, the selected summary clips) and 'clips' (ordinary clips edited with them) - and resolves them to one set of clips,
 *  so a clip reached twice (a child selected beside its summary clip, a sub-folder's span inside its parent's) changes
 *  once. Every track holding one of those clips is checkpointed as ONE undo step; the arranger, its keys and an agent all
 *  come through here. A summary clip stays derived: nothing new is saved. */

#include <algorithm>
#include <iterator>

#include <QJsonArray>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ClipGroupEdits.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "TrackFolder.h"

namespace lmms
{

using namespace control;

namespace
{

//! One summary clip an edit is aimed at, and whether it was selected (it stays so afterwards).
struct Target
{
	TrackFolder* folder = nullptr;
	FolderSpan span;
	bool selected = false;
};

struct Targets
{
	std::vector<Target> spans;
	std::vector<Clip*> clips;  //!< the ordinary clips named beside them

	//! Every clip of the targets, each once.
	std::vector<Clip*> allClips() const
	{
		std::vector<Clip*> out;
		const auto addOnce = [&out](Clip* clip) {
			if (std::find(out.begin(), out.end(), clip) == out.end()) { out.push_back(clip); }
		};
		for (const Target& target : spans)
		{
			for (Clip* clip : folderSpanClips(*target.folder, target.span)) { addOnce(clip); }
		}
		for (Clip* clip : clips) { addOnce(clip); }
		return out;
	}
};

ControlResult invalid(const QString& why) { return ControlResult::failure(ControlErrorKind::InvalidArgs, why); }
ControlResult refused(const QString& why) { return ControlResult::failure(ControlErrorKind::Refused, why); }

TrackFolder* folderOf(const QJsonValue& id, ControlResult* error)
{
	ControlResult missing;
	Track* track = resolveTrack(id.toString(), &missing);
	auto* folder = dynamic_cast<TrackFolder*>(track);
	if (folder == nullptr) { *error = track == nullptr ? missing : invalid(QStringLiteral("'folder' is a folder track")); }
	return folder;
}

bool namedSpan(const QJsonObject& each, Target* target, ControlResult* error)
{
	target->folder = folderOf(each.value(QStringLiteral("folder")), error);
	if (target->folder == nullptr) { return false; }
	const tick_t start = each.value(QStringLiteral("start")).toInt(-1);
	const tick_t end = each.value(QStringLiteral("end")).toInt(-1);
	for (const FolderSpan& span : folderSummary(*target->folder))
	{
		if (span.start == start && span.end == end)
		{
			target->span = span;
			target->selected = FolderSpanSelection::instance().holds(target->folder, span);
			return true;
		}
	}
	*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("%1 has no summary clip from %2 to %3 (track.folder_spans lists them)")
		.arg(each.value(QStringLiteral("folder")).toString()).arg(start).arg(end));
	return false;
}

bool targetsOf(const QJsonObject& args, Targets* targets, ControlResult* error)
{
	if (args.contains(QStringLiteral("spans")))
	{
		for (const QJsonValue& each : args.value(QStringLiteral("spans")).toArray())
		{
			Target target;
			if (!namedSpan(each.toObject(), &target, error)) { return false; }
			targets->spans.push_back(target);
		}
	}
	else
	{
		for (const FolderSpanSelection::Selected& each : FolderSpanSelection::instance().selected())
		{
			targets->spans.push_back({each.folder, each.span, true});
		}
	}
	for (const QJsonValue& each : args.value(QStringLiteral("clips")).toArray())
	{
		ClipRef ref;
		if (!resolveClip(each.toString(), &ref, error)) { return false; }
		targets->clips.push_back(ref.clip);
	}
	if (targets->spans.empty() && targets->clips.empty())
	{
		*error = refused(QStringLiteral("no summary clip is selected (track.folder_span_select) or named ('spans', 'clips')"));
		return false;
	}
	return true;
}

//! Targets that a lock leaves free; the refusal otherwise, before anything is checkpointed.
bool editableTargets(const QJsonObject& args, Targets* targets, ControlResult* error)
{
	if (!targetsOf(args, targets, error)) { return false; }
	if (const Clip* locked = arrangement::firstLocked(targets->allClips()))
	{
		*error = refused(QStringLiteral("%1 is locked against editing (track.set_locked / clip.set_locked unlocks it)").arg(clipIdOf(locked)));
		return false;
	}
	return true;
}

QJsonObject spanJson(const TrackFolder* folder, const FolderSpan& span)
{
	QJsonArray tracks;
	for (const Track* track : folderTracksWithin(*folder, span.start, span.end)) { tracks.append(trackIdOf(track)); }
	return {{QStringLiteral("folder"), trackIdOf(folder)}, {QStringLiteral("start"), static_cast<int>(span.start)},
		{QStringLiteral("end"), static_cast<int>(span.end)}, {QStringLiteral("clips"), span.clips}, {QStringLiteral("tracks"), tracks},
		{QStringLiteral("selected"), FolderSpanSelection::instance().holds(folder, span)}};
}

QJsonArray selectionJson()
{
	QJsonArray out;
	for (const FolderSpanSelection::Selected& each : FolderSpanSelection::instance().selected()) { out.append(spanJson(each.folder, each.span)); }
	return out;
}

//! The reply of an edit: how much changed, and every folder it aimed at as its summary clips now stand.
ControlResult finished(const Targets& targets, QJsonObject result)
{
	Engine::getSong()->setModified();
	QJsonArray spans;
	std::vector<const TrackFolder*> seen;
	for (const Target& target : targets.spans)
	{
		if (std::find(seen.begin(), seen.end(), target.folder) != seen.end()) { continue; }
		seen.push_back(target.folder);
		for (const FolderSpan& span : folderSummary(*target.folder)) { spans.append(spanJson(target.folder, span)); }
	}
	result.insert(QStringLiteral("spans"), spans);
	result.insert(QStringLiteral("__transaction"), transactionPayload(QJsonObject{}, QStringLiteral("control.undo"), {}, true,
		QStringLiteral("Track checkpoints: every track holding an edited clip was checkpointed before the edit, as one step")));
	return ControlResult::success(result);
}

//! Take the selected targets out of the selection for the edit; @a tickAfter says where each is afterwards.
template<typename TickAfter>
void reselect(const std::vector<Target>& spans, TickAfter tickAfter)
{
	for (const Target& target : spans)
	{
		if (target.selected) { FolderSpanSelection::instance().select(target.folder, tickAfter(target.span), true); }
	}
}

void deselectAll(const std::vector<Target>& spans)
{
	for (const Target& target : spans)
	{
		if (target.selected) { FolderSpanSelection::instance().deselect(target.folder, target.span); }
	}
}

//! A summary clip's new edge must stay inside the song and short of its own other edge and of its neighbours.
bool edgeFits(const Target& target, arrangement::SpanEdge edge, tick_t delta, ControlResult* error)
{
	const std::vector<FolderSpan> all = folderSummary(*target.folder);
	const auto here = std::find_if(all.begin(), all.end(), [&](const FolderSpan& s) { return s.start == target.span.start; });
	const bool atEnd = edge == arrangement::SpanEdge::End;
	const tick_t moved = (atEnd ? target.span.end : target.span.start) + delta;
	const tick_t floor = atEnd ? target.span.start + 1 : (here != all.begin() ? std::prev(here)->end : 0);
	const tick_t ceiling = atEnd ? (std::next(here) != all.end() ? std::next(here)->start : MaxSongLength) : target.span.end - 1;
	if (moved >= floor && moved <= ceiling) { return true; }
	*error = refused(QStringLiteral("the summary clip's edge can go from %1 to %2 here, not to %3").arg(floor).arg(ceiling).arg(moved));
	return false;
}

bool planResize(const Targets& targets, arrangement::SpanEdge edge, tick_t delta, std::vector<arrangement::ClipEdit>* edits, ControlResult* error)
{
	for (const Target& target : targets.spans)
	{
		if (!edgeFits(target, edge, delta, error)) { return false; }
		arrangement::planSpanResize(*target.folder, target.span, edge, delta, edits);
	}
	for (Clip* clip : targets.clips)
	{
		if (arrangement::planClipResize(clip, edge, delta, edits)) { continue; }
		*error = refused(QStringLiteral("%1 would vanish or start before the song").arg(clipIdOf(clip)));
		return false;
	}
	return true;
}

ControlResult resizeSpans(const QJsonObject& args)
{
	Targets targets;
	ControlResult error;
	if (!editableTargets(args, &targets, &error)) { return error; }
	const arrangement::SpanEdge edge = args.value(QStringLiteral("edge")).toString() == QStringLiteral("start")
		? arrangement::SpanEdge::Start : arrangement::SpanEdge::End;
	const tick_t delta = args.value(QStringLiteral("delta")).toInt();
	if (delta == 0) { return invalid(QStringLiteral("'delta' moves the edge")); }
	// Everything is planned, and may be refused, before anything is written.
	std::vector<arrangement::ClipEdit> edits;
	if (!planResize(targets, edge, delta, &edits, &error)) { return error; }
	if (edits.empty()) { return refused(QStringLiteral("no clip reaches that edge")); }
	std::vector<Clip*> clips;
	for (const arrangement::ClipEdit& each : edits) { clips.push_back(each.clip); }
	arrangement::checkpointOwners(clips);
	deselectAll(targets.spans);
	arrangement::applyClipEdits(edits);
	reselect(targets.spans, [&](const FolderSpan& span) { return edge == arrangement::SpanEdge::Start ? span.start + delta : span.start; });
	const auto removed = std::count_if(edits.begin(), edits.end(), [](const arrangement::ClipEdit& each) { return each.remove; });
	return finished(targets, {{QStringLiteral("edited"), static_cast<int>(edits.size())}, {QStringLiteral("removed"), static_cast<int>(removed)}});
}

ControlResult splitSpans(const QJsonObject& args)
{
	Targets targets;
	ControlResult error;
	if (!editableTargets(args, &targets, &error)) { return error; }
	const tick_t at = args.value(QStringLiteral("at")).toInt();
	std::vector<Clip*> crossing;
	for (Clip* clip : targets.allClips())
	{
		if (clip->startPosition().getTicks() < at && clip->endPosition().getTicks() > at) { crossing.push_back(clip); }
	}
	if (crossing.empty()) { return refused(QStringLiteral("no clip of those crosses tick %1").arg(at)); }
	arrangement::checkpointOwners(crossing);
	for (Clip* clip : crossing) { arrangement::splitClipAt(clip, at); }
	return finished(targets, {{QStringLiteral("split"), static_cast<int>(crossing.size())}});
}

ControlResult moveSpans(const QJsonObject& args)
{
	Targets targets;
	ControlResult error;
	if (!editableTargets(args, &targets, &error)) { return error; }
	const tick_t delta = args.value(QStringLiteral("delta")).toInt();
	const std::vector<Clip*> clips = targets.allClips();
	const auto earliest = std::min_element(clips.begin(), clips.end(), [](const Clip* a, const Clip* b) { return a->startPosition() < b->startPosition(); });
	if (delta == 0 || (*earliest)->startPosition().getTicks() + delta < 0) { return invalid(QStringLiteral("'delta' moves the clips, and not before the song's start")); }
	arrangement::checkpointOwners(clips);
	deselectAll(targets.spans);
	for (Clip* clip : clips) { clip->movePosition(TimePos(clip->startPosition().getTicks() + delta)); }
	reselect(targets.spans, [delta](const FolderSpan& span) { return span.start + delta; });
	return finished(targets, {{QStringLiteral("moved"), static_cast<int>(clips.size())}});
}

ControlResult deleteSpans(const QJsonObject& args)
{
	Targets targets;
	ControlResult error;
	if (!editableTargets(args, &targets, &error)) { return error; }
	const std::vector<Clip*> clips = targets.allClips();
	arrangement::checkpointOwners(clips);
	deselectAll(targets.spans);
	for (Clip* clip : clips) { arrangement::deleteClip(clip->getTrack(), clip); }
	return finished(targets, {{QStringLiteral("removed"), static_cast<int>(clips.size())}});
}

ControlResult listSpans(const QJsonObject& args)
{
	ControlResult error;
	const TrackFolder* folder = folderOf(args.value(QStringLiteral("folder")), &error);
	if (folder == nullptr) { return error; }
	QJsonArray spans;
	for (const FolderSpan& span : folderSummary(*folder)) { spans.append(spanJson(folder, span)); }
	return ControlResult::success({{QStringLiteral("spans"), spans}, {QStringLiteral("selected"), selectionJson()}});
}

ControlResult selectSpan(const QJsonObject& args)
{
	FolderSpanSelection& selection = FolderSpanSelection::instance();
	if (!args.contains(QStringLiteral("folder")))
	{
		selection.clear();
		return ControlResult::success({{QStringLiteral("selected"), selectionJson()}});
	}
	ControlResult error;
	TrackFolder* folder = folderOf(args.value(QStringLiteral("folder")), &error);
	if (folder == nullptr) { return error; }
	const tick_t at = args.value(QStringLiteral("at")).toInt(-1);
	const std::vector<FolderSpan> spans = folderSummary(*folder);
	const auto span = std::find_if(spans.begin(), spans.end(), [at](const FolderSpan& s) { return at >= s.start && at < s.end; });
	if (span == spans.end()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no summary clip holds tick %1").arg(at)); }
	if (args.value(QStringLiteral("selected")).toBool(true)) { selection.select(folder, at, args.value(QStringLiteral("add")).toBool(false)); }
	else { selection.deselect(folder, *span); }
	return ControlResult::success({{QStringLiteral("selected"), selectionJson()}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerFolderSpanCommands(ControlRegistry& registry)
{
	const QString aim = QStringLiteral(" Aimed at 'spans' ([{folder, start, end}], as track.folder_spans lists them) or, without it, the selected "
		"summary clips, and at 'clips' (ordinary clip ids) edited with them; a clip reached twice changes once. One undo step.");
	const auto targeted = [](QJsonObject own) {
		own.insert(QStringLiteral("spans"), arrayProperty());
		own.insert(QStringLiteral("clips"), arrayProperty());
		return own;
	};
	const QJsonObject delta{{QStringLiteral("delta"), integerProperty(-192 * 9999, 192 * 9999)}};
	QJsonObject resize = delta;
	resize.insert(QStringLiteral("edge"), enumProperty({QStringLiteral("start"), QStringLiteral("end")}));

	add(registry, "track.folder_spans", QStringLiteral("Folder track 'folder''s summary clips - the stretches its row draws, derived from its "
		"children's clips: start, end, how many clips, on which tracks, whether selected - and every selected summary clip."),
		{{QStringLiteral("folder"), stringProperty()}}, {QStringLiteral("folder")}, false, listSpans);
	add(registry, "track.folder_span_select", QStringLiteral("Select the summary clip of 'folder' holding tick 'at', replacing the selection or "
		"with 'add' adding to it; 'selected' false takes it out again; no 'folder' clears the selection. Interface state, not saved."),
		{{QStringLiteral("folder"), stringProperty()}, {QStringLiteral("at"), tickProperty()}, {QStringLiteral("add"), booleanProperty()},
			{QStringLiteral("selected"), booleanProperty()}}, {}, false, selectSpan);
	add(registry, "track.folder_span_resize", QStringLiteral("Move the 'start' or 'end' 'edge' of summary clips by 'delta' ticks, over their child "
		"clips: lengthening takes the clips at that edge out to it, shortening cuts the clips crossing the new edge and removes those "
		"wholly past it. An edge cannot pass the neighbouring summary clip; an ordinary clip's own edge moves by the same delta.") + aim,
		targeted(resize), {QStringLiteral("edge"), QStringLiteral("delta")}, true, resizeSpans);
	add(registry, "track.folder_span_split", QStringLiteral("Cut every child clip of summary clips that crosses tick 'at' there, nested tracks "
		"too. Touching clips still draw as one summary clip.") + aim,
		targeted({{QStringLiteral("at"), integerProperty(1, 192 * 9999)}}), {QStringLiteral("at")}, true, splitSpans);
	add(registry, "track.folder_span_move", QStringLiteral("Move every clip of summary clips by 'delta' ticks, together; what they land on stays.") + aim,
		targeted(delta), {QStringLiteral("delta")}, true, moveSpans);
	add(registry, "track.folder_span_delete", QStringLiteral("Delete every clip of summary clips. Later clips stay where they are.") + aim,
		targeted({}), {}, true, deleteSpans);
}

} // namespace lmms
