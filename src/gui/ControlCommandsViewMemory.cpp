/*
 * ControlCommandsViewMemory.cpp - location.* and selection.*: named places and selections
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

/*! 0.5.0 W3 L13:
 *  - location.* (WF-098 memory locations): a named play position with the range and the arranger zoom that
 *    went with it - saved, recalled, listed, removed;
 *  - selection.* (WF-106 saved selections): a named range and the clips selected in the arranger - saved,
 *    applied again, listed, removed.
 *  Both are kept with the project (ViewMemory.h, <view-memory>); saving and removing are one undo step each.
 *  Recalling moves the play position through transport.seek and the range through range.set, which record
 *  themselves; the zoom and the clip selection are interface state. Without an interface the position and
 *  the range still work.
 *  Design theme 22: each save says what it kept ('captures') and what a memory of its kind never keeps
 *  ('not_kept'), and each recall what came back ('restored') and what did not ('skipped'); 'replace' false
 *  refuses a name in use, so the Window menu's Save never overwrites silently (its Replace asks first). */

#include <utility>
#include <vector>

#include <QJsonArray>

#include "ClipView.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "Rubberband.h"
#include "Song.h"
#include "SongEditor.h"
#include "ViewMemory.h"

namespace lmms
{

using namespace control;

namespace
{

using Kind = ViewMemory::Kind;

constexpr int MaxEntries = 99;

QJsonObject read(const char* id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(QLatin1String(id), args).result;
}

gui::SongEditor* arranger()
{
	gui::GuiApplication* g = gui::getGUI();
	return g != nullptr && g->songEditor() != nullptr ? g->songEditor()->m_editor : nullptr;
}

//! The active range as range.set takes it, or an empty object when there is none.
QJsonObject currentRange()
{
	const QJsonObject range = read("range.get");  // range.get's reply is the range itself (active, start, end, tracks)
	if (!range.value(QStringLiteral("active")).toBool()) { return {}; }
	QJsonObject out{{QStringLiteral("start"), range.value(QStringLiteral("start"))}, {QStringLiteral("end"), range.value(QStringLiteral("end"))}};
	if (range.value(QStringLiteral("all_tracks")).toBool()) { out.insert(QStringLiteral("all"), true); }
	else { out.insert(QStringLiteral("tracks"), range.value(QStringLiteral("tracks"))); }
	return out;
}

//! The clips selected in the arranger, or the control surface's selected clip.
QJsonArray selectedClips()
{
	QJsonArray ids;
	if (gui::SongEditor* editor = arranger())
	{
		for (gui::selectableObject* object : editor->selectedObjects())
		{
			if (auto* view = dynamic_cast<gui::ClipView*>(object)) { ids.append(clipIdOf(view->getClip())); }
		}
	}
	if (ids.isEmpty() && !selectedClipId().isEmpty()) { ids.append(selectedClipId()); }
	return ids;
}

bool selectInArranger(const QJsonArray& ids)
{
	gui::SongEditor* editor = arranger();
	if (editor == nullptr) { return false; }
	for (gui::selectableObject* object : editor->rubberBand()->selectableObjects())
	{
		auto* view = dynamic_cast<gui::ClipView*>(object);
		if (view != nullptr) { view->setSelected(ids.contains(clipIdOf(view->getClip()))); }
	}
	return true;
}

//! The 'name' argument as it is stored: save, recall, apply and remove all trim it the same way.
QString nameOf(const QJsonObject& args)
{
	return args.value(QStringLiteral("name")).toString().trimmed();
}

//! Theme 22: the parts a memory of @a kind never keeps - said by every save and in the Window menu.
QJsonArray notKept(Kind kind)
{
	if (kind == Kind::Location)
	{
		return {QStringLiteral("scroll"), QStringLiteral("vertical_zoom"), QStringLiteral("track_heights"),
			QStringLiteral("selection"), QStringLiteral("layout")};
	}
	return {QStringLiteral("notes"), QStringLiteral("automation_points"), QStringLiteral("mixer_channels"),
		QStringLiteral("play_position")};
}

//! The parts @a entry holds, by name: position (its tick), range, zoom, clips.
QJsonArray partsOf(const QJsonObject& entry)
{
	QJsonArray parts;
	if (entry.contains(QStringLiteral("tick"))) { parts.append(QStringLiteral("position")); }
	for (const char* part : {"range", "zoom"})
	{
		if (entry.contains(QLatin1String(part))) { parts.append(QLatin1String(part)); }
	}
	if (!entry.value(QStringLiteral("clips")).toArray().isEmpty()) { parts.append(QStringLiteral("clips")); }
	return parts;
}

QString kindNoun(Kind kind)
{
	return kind == Kind::Location ? QStringLiteral("memory location") : QStringLiteral("saved selection");
}

//! The result key a list of @a kind is published under, here and in the list command.
QString keyOf(Kind kind)
{
	return kind == Kind::Location ? QStringLiteral("locations") : QStringLiteral("selections");
}

//! The honest transaction of one stored-entry change: the list as it was before, the undo step
//! that puts it back, and @a changed false for a call that recorded none.
QJsonObject entryPayload(Kind kind, const QJsonArray& previous, bool changed)
{
	return transactionPayload({{keyOf(kind), previous}}, QStringLiteral("control.undo"), {}, changed,
		changed
			? QStringLiteral("action checkpoint: the recorded undo step puts the previous list back")
			: QStringLiteral("nothing changed: this call left no undo step, and there is nothing to take back"));
}

//! Replace the entries of @a kind with @a next as ONE undo step.
void storeEntries(Kind kind, const QJsonArray& next)
{
	const QJsonArray before = ViewMemory::instance().entries(kind);
	ViewMemory::instance().setEntries(kind, next);
	addUndoStep([kind, before] { ViewMemory::instance().setEntries(kind, before); },
		[kind, next] { ViewMemory::instance().setEntries(kind, next); });
	Engine::getSong()->setModified();
}

//! Whether a name is in use among the entries of @a kind.
bool isSaved(Kind kind, const QString& name)
{
	return !ViewMemory::instance().entry(kind, name).isEmpty();
}

//! @a entry stored under its name - replacing one of that name unless @a replace is false - or a refusal when the
//! name is in use and may not be replaced, or the list is full.
ControlResult saveEntry(Kind kind, const QJsonObject& entry, bool replace)
{
	const QString name = entry.value(QStringLiteral("name")).toString();
	if (!replace && isSaved(kind, name))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a %1 named '%2' is saved already: choose another "
			"name, or replace it (Window > Replace)").arg(kindNoun(kind), name));
	}
	QJsonArray next;
	bool replaced = false;
	for (const QJsonValue& value : ViewMemory::instance().entries(kind))
	{
		const bool same = value.toObject().value(QStringLiteral("name")).toString() == name;
		next.append(same ? QJsonValue(entry) : value);
		replaced = replaced || same;
	}
	if (!replaced && next.size() >= MaxEntries)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("at most %1 are kept; remove one first").arg(MaxEntries));
	}
	if (!replaced) { next.append(entry); }
	// Saving what is already saved changes nothing: no undo step, the project stays unmodified.
	const QJsonArray previous = ViewMemory::instance().entries(kind);
	const bool changed = next != previous;
	if (changed) { storeEntries(kind, next); }
	QJsonObject result = entry;
	// The list of this kind is a bounded recorded state, and the recorded undo step puts the
	// previous one back - so SAY SO, as range.set and the copy commands do, rather than leaving
	// the client to infer it from a bare 'changed'.
	result.insert(QStringLiteral("__transaction"), entryPayload(kind, previous, changed));
	result.insert(QStringLiteral("replaced"), replaced);
	result.insert(QStringLiteral("changed"), changed);
	result.insert(QStringLiteral("captures"), partsOf(entry));
	result.insert(QStringLiteral("not_kept"), notKept(kind));
	result.insert(QStringLiteral("max_entries"), MaxEntries);
	return ControlResult::success(result);
}

ControlResult removeEntry(Kind kind, const QJsonObject& args)
{
	const QString name = nameOf(args);
	QJsonArray next;
	for (const QJsonValue& value : ViewMemory::instance().entries(kind))
	{
		if (value.toObject().value(QStringLiteral("name")).toString() != name) { next.append(value); }
	}
	if (next.size() == ViewMemory::instance().entries(kind).size())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("nothing is saved as '%1'").arg(name));
	}
	const QJsonArray previous = ViewMemory::instance().entries(kind);
	storeEntries(kind, next);
	QJsonObject result = {{QStringLiteral("name"), name}, {QStringLiteral("count"), next.size()}};
	result.insert(QStringLiteral("__transaction"), entryPayload(kind, previous, true));
	return ControlResult::success(result);
}

bool namedOrRefuse(const QJsonObject& args, QString* name, ControlResult* error)
{
	*name = nameOf(args);
	if (name->isEmpty()) { *error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("give it a name")); }
	return !name->isEmpty();
}

ControlResult saveLocation(const QJsonObject& args)
{
	QString name;
	ControlResult error;
	if (!namedOrRefuse(args, &name, &error)) { return error; }
	QJsonObject entry{{QStringLiteral("name"), name},
		{QStringLiteral("tick"), args.contains(QStringLiteral("tick")) ? args.value(QStringLiteral("tick"))
			: read("transport.get_state").value(QStringLiteral("position_ticks"))}};
	if (const QJsonObject range = currentRange(); !range.isEmpty()) { entry.insert(QStringLiteral("range"), range); }
	if (gui::SongEditor* editor = arranger()) { entry.insert(QStringLiteral("zoom"), static_cast<double>(editor->pixelsPerBar())); }
	return saveEntry(Kind::Location, entry, args.value(QStringLiteral("replace")).toBool(true));
}

//! Theme 22: @a result plus 'restored' and 'skipped' - of the parts @a entry holds (partsOf), those that came back and
//! those that did not, @a outcome saying which did.
QJsonObject withOutcome(QJsonObject result, const QJsonObject& entry, const std::vector<std::pair<const char*, bool>>& outcome)
{
	QJsonArray restored;
	QJsonArray skipped;
	const QJsonArray held = partsOf(entry);
	for (const auto& [part, done] : outcome)
	{
		if (held.contains(QLatin1String(part))) { (done ? restored : skipped).append(QLatin1String(part)); }
	}
	result.insert(QStringLiteral("restored"), restored);
	result.insert(QStringLiteral("skipped"), skipped);
	return result;
}

//! Put a saved range back; whether there was one.
bool restoreRange(const QJsonObject& entry)
{
	const QJsonObject range = entry.value(QStringLiteral("range")).toObject();
	return !range.isEmpty() && ControlRegistry::instance()->invoke(QStringLiteral("range.set"), range).ok;
}

ControlResult recallLocation(const QJsonObject& args)
{
	const QString name = nameOf(args);
	const QJsonObject entry = ViewMemory::instance().entry(Kind::Location, name);
	if (entry.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no memory location '%1'").arg(name)); }
	const ControlResult seek = ControlRegistry::instance()->invoke(QStringLiteral("transport.seek"),
		{{QStringLiteral("ticks"), entry.value(QStringLiteral("tick")).toInt()}});
	if (!seek.ok) { return seek; }
	const bool range = restoreRange(entry);
	const bool zoom = entry.contains(QStringLiteral("zoom")) && arranger() != nullptr
		&& ControlRegistry::instance()->invoke(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), entry.value(QStringLiteral("zoom"))}}).ok;
	return ControlResult::success(withOutcome({{QStringLiteral("name"), name}, {QStringLiteral("tick"), entry.value(QStringLiteral("tick"))},
		{QStringLiteral("range_restored"), range}, {QStringLiteral("zoom_restored"), zoom}}, entry,
		{{"position", true}, {"range", range}, {"zoom", zoom}}));
}

//! Whether one of @a ids names no clip in the project; @a unknown receives the first such id. The
//! answer is a flag and NOT the id itself: the empty string is a valid argument that resolves no
//! clip, so returning it as the "nothing unknown" sentinel let such an id through unvalidated.
bool firstUnknownClip(const QJsonArray& ids, QString* unknown)
{
	for (const QJsonValue& id : ids)
	{
		ClipRef ref;
		ControlResult missing;
		if (!resolveClip(id.toString(), &ref, &missing))
		{
			*unknown = id.isString() ? id.toString() : QStringLiteral("?");
			return true;
		}
	}
	return false;
}

ControlResult saveSelection(const QJsonObject& args)
{
	QString name;
	ControlResult error;
	if (!namedOrRefuse(args, &name, &error)) { return error; }
	const QJsonObject range = currentRange();
	const bool given = args.contains(QStringLiteral("clips"));
	const QJsonArray clips = given ? args.value(QStringLiteral("clips")).toArray() : selectedClips();
	// The schema validator checks the array itself, but not its items; reject them before toString() can coerce one.
	for (const QJsonValue& id : clips)
	{
		if (!id.isString())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("clips: every clip id must be a string"));
		}
	}
	QString unknown;
	if (given && firstUnknownClip(clips, &unknown))
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no clip '%1'").arg(unknown));
	}
	if (range.isEmpty() && clips.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("nothing is selected: select clips or a range first"));
	}
	QJsonObject entry{{QStringLiteral("name"), name}, {QStringLiteral("clips"), clips}};
	if (!range.isEmpty()) { entry.insert(QStringLiteral("range"), range); }
	return saveEntry(Kind::Selection, entry, args.value(QStringLiteral("replace")).toBool(true));
}

ControlResult applySelection(const QJsonObject& args)
{
	const QString name = nameOf(args);
	const QJsonObject entry = ViewMemory::instance().entry(Kind::Selection, name);
	if (entry.isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no saved selection '%1'").arg(name)); }
	const bool range = restoreRange(entry);
	QJsonArray live;
	for (const QJsonValue& id : entry.value(QStringLiteral("clips")).toArray())
	{
		ClipRef ref;
		ControlResult missing;
		if (resolveClip(id.toString(), &ref, &missing)) { live.append(id); }
	}
	const bool clipsRestored = !live.isEmpty() && selectInArranger(live);
	if (!live.isEmpty()) { ControlRegistry::instance()->invoke(QStringLiteral("clip.select"), {{QStringLiteral("clip"), live.first()}}); }
	else { selectClip(QString()); }  // the arranger's selection is empty now, so the surface's is too
	return ControlResult::success(withOutcome({{QStringLiteral("name"), name}, {QStringLiteral("clips"), live},
		{QStringLiteral("missing"), entry.value(QStringLiteral("clips")).toArray().size() - live.size()}, {QStringLiteral("range_restored"), range}},
		entry, {{"range", range}, {"clips", clipsRestored}}));
}

void add(ControlRegistry& registry, const char* group, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, std::function<ControlResult(const QJsonObject&)> handler)
{
	ControlCommand cmd = guiCommand(group, verb);
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = std::move(handler);
	registry.registerCommand(cmd);
}

void registerKind(ControlRegistry& registry, const char* group, Kind kind, const QString& what)
{
	const QJsonObject named = objectSchema({{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("name")});
	const QString key = kind == Kind::Location ? QStringLiteral("locations") : QStringLiteral("selections");
	add(registry, group, "list", QStringLiteral("The project's %1, in the order they were saved, their 'count' and limit "
		"('max_entries'), and what one never keeps ('not_kept').").arg(what), objectSchema({}), false,
		[kind, key](const QJsonObject&) {
			const QJsonArray entries = ViewMemory::instance().entries(kind);
			return ControlResult::success({{key, entries}, {QStringLiteral("count"), entries.size()},
				{QStringLiteral("max_entries"), MaxEntries}, {QStringLiteral("not_kept"), notKept(kind)}});
		});
	add(registry, group, "remove", QStringLiteral("Forget one of the project's %1 ('name'). One undo step.").arg(what), named, true,
		[kind](const QJsonObject& args) { return removeEntry(kind, args); });
}

} // namespace

void registerViewMemoryCommands(ControlRegistry& registry)
{
	ViewMemory::instance();  // registers the section before any project loads
	add(registry, "location", "save", QStringLiteral("Save a memory location 'name': the play position (or 'tick'), the range "
		"and the arranger's zoom as they are now ('captures' lists the parts kept; 'not_kept' what a location never keeps: the "
		"scroll, the vertical zoom, track heights, the selection and the window layout) - replacing one of that name ('replaced') "
		"unless 'replace' is false, when a name in use is refused; at most 99 ('max_entries'). Saved with the project; one undo "
		"step ('changed' false, and no step, when it was already saved just so). Names are trimmed here and in every lookup."),
		objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("tick"), tickProperty()},
		{QStringLiteral("replace"), booleanProperty()}}, {QStringLiteral("name")}), true, saveLocation);
	add(registry, "location", "recall", QStringLiteral("Go to memory location 'name': the play position (transport.seek), its "
		"range (range.set) and its zoom come back - 'restored' lists the parts that did, 'skipped' those it holds that did not "
		"(the zoom without an arranger, a range refused)."), objectSchema({{QStringLiteral("name"), stringProperty()}},
		{QStringLiteral("name")}), false, recallLocation);
	registerKind(registry, "location", Kind::Location, QStringLiteral("memory locations"));
	add(registry, "selection", "save", QStringLiteral("Save the selection as 'name': the range and the clips selected in the "
		"arranger (or 'clips', a list of clip ids; refused NotFound naming one that is not in the project) - 'captures' lists the "
		"parts kept, 'not_kept' what a saved selection never keeps (notes, automation points, mixer channels, the play position) - "
		"replacing one of that name ('replaced') unless 'replace' is false, when a name in use is refused; at most 99 ('changed' "
		"false when it was already saved just so: no undo step). Refused when nothing is selected. Saved with the project; one "
		"undo step."),
		objectSchema({{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("clips"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
			{QStringLiteral("items"), stringProperty()}}},
		{QStringLiteral("replace"), booleanProperty()}}, {QStringLiteral("name")}), true, saveSelection);
	add(registry, "selection", "apply", QStringLiteral("Select saved selection 'name' again: its range (range.set) and those of "
		"its clips that still exist ('missing' counts the rest; 'restored' and 'skipped' name the parts that came back and those "
		"that did not)."), objectSchema({{QStringLiteral("name"), stringProperty()}}, {QStringLiteral("name")}), false, applySelection);
	registerKind(registry, "selection", Kind::Selection, QStringLiteral("saved selections"));
}

} // namespace lmms
