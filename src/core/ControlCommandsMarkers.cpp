/*
 * ControlCommandsMarkers.cpp - marker.*: add, edit, remove, list and jump to the project's markers
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

/*! ED-048 (0.5.0 L04): the project's markers over the control surface. Each edit is one undo
 *  step that puts the previous list back (an action checkpoint - the list is not a
 *  JournallingObject, the groove pool's precedent) and marks the project modified. */

#include <QColor>
#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectMarkers.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

QString markerId(int id)
{
	return QStringLiteral("mk-%1").arg(id);
}

int markerIndex(const QJsonObject& args)
{
	const QString text = args.value(QStringLiteral("marker")).toString();
	return text.startsWith(QStringLiteral("mk-")) ? text.mid(3).toInt() : -1;
}

QJsonObject markerJson(const Marker& marker)
{
	return {{QStringLiteral("marker"), markerId(marker.id)}, {QStringLiteral("tick"), marker.tick},
		{QStringLiteral("name"), marker.name}, {QStringLiteral("color"), marker.color}};
}

QJsonArray listJson()
{
	QJsonArray out;
	for (const Marker& marker : ProjectMarkers::instance().markers()) { out.append(markerJson(marker)); }
	return out;
}

ControlResult badColour(const QString& color)
{
	return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is not a colour (\"#rrggbb\")").arg(color));
}

//! Run @a edit on the markers as ONE undo step that restores the list before it.
ControlResult recorded(const QString& command, const std::function<ControlResult()>& edit)
{
	const QVector<Marker> before = ProjectMarkers::instance().markers();
	ControlResult result = edit();
	if (!result.ok) { return result; }
	const QVector<Marker> after = ProjectMarkers::instance().markers();
	control::addUndoStep([before] { ProjectMarkers::instance().restore(before); },
		[after] { ProjectMarkers::instance().restore(after); });
	Engine::getSong()->setModified();
	result.result.insert(QStringLiteral("markers"), listJson());
	result.result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("count"), before.size()}},
		command, {}, true, QStringLiteral("action checkpoint: the recorded undo step puts the previous marker list back")));
	return result;
}

ControlResult addMarker(const QJsonObject& args)
{
	const QString color = args.value(QStringLiteral("color")).toString();
	if (!color.isEmpty() && !QColor::isValidColorName(color)) { return badColour(color); }
	const tick_t tick = args.contains(QStringLiteral("tick")) ? args.value(QStringLiteral("tick")).toInt()
		: Engine::getSong()->getPlayPos().getTicks();
	return recorded(QStringLiteral("marker.remove"), [&] {
		const int id = ProjectMarkers::instance().add(tick, args.value(QStringLiteral("name")).toString(),
			color.isEmpty() ? color : QColor(color).name());
		if (id < 0)
		{
			return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("at most %1 markers").arg(ProjectMarkers::MaxMarkers));
		}
		return ControlResult::success(markerJson(*ProjectMarkers::instance().find(id)));
	});
}

ControlResult setMarker(const QJsonObject& args)
{
	const Marker* found = ProjectMarkers::instance().find(markerIndex(args));
	if (found == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no marker '%1'").arg(args.value(QStringLiteral("marker")).toString())); }
	Marker changed = *found;
	if (args.contains(QStringLiteral("tick"))) { changed.tick = args.value(QStringLiteral("tick")).toInt(); }
	if (args.contains(QStringLiteral("name"))) { changed.name = args.value(QStringLiteral("name")).toString(); }
	if (args.contains(QStringLiteral("color")))
	{
		const QString color = args.value(QStringLiteral("color")).toString();
		if (!color.isEmpty() && !QColor::isValidColorName(color)) { return badColour(color); }
		changed.color = color.isEmpty() ? color : QColor(color).name();
	}
	return recorded(QStringLiteral("marker.set"), [changed] {
		ProjectMarkers::instance().replace(changed.id, changed);
		return ControlResult::success(markerJson(*ProjectMarkers::instance().find(changed.id)));
	});
}

ControlResult removeMarker(const QJsonObject& args)
{
	const int id = markerIndex(args);
	if (ProjectMarkers::instance().find(id) == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no marker '%1'").arg(args.value(QStringLiteral("marker")).toString()));
	}
	return recorded(QStringLiteral("marker.add"), [id] {
		ProjectMarkers::instance().remove(id);
		return ControlResult::success({{QStringLiteral("removed"), markerId(id)}});
	});
}

ControlResult jumpMarker(const QJsonObject& args)
{
	Song* song = Engine::getSong();
	const tick_t now = song->getPlayPos().getTicks();
	const QString direction = args.value(QStringLiteral("direction")).toString();
	const Marker* target = args.contains(QStringLiteral("marker")) ? ProjectMarkers::instance().find(markerIndex(args))
		: direction == QStringLiteral("previous") ? ProjectMarkers::instance().previous(now) : ProjectMarkers::instance().next(now);
	if (target == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no marker to jump to")); }
	const ControlResult sought = ControlRegistry::instance()->invoke(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), target->tick}});
	if (!sought.ok) { return sought; }
	return ControlResult::success(markerJson(*target));
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("marker");
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

void registerMarkerCommands(ControlRegistry& registry)
{
	ProjectMarkers::instance();  // registers the section before any project loads
	add(registry, "list", QStringLiteral("The project's markers, in time order: id (mk-<n>), tick, name, colour."),
		objectSchema({}), false, [](const QJsonObject&) {
			return ControlResult::success({{QStringLiteral("markers"), listJson()}, {QStringLiteral("max_markers"), ProjectMarkers::MaxMarkers}});
		});
	add(registry, "add", QStringLiteral("Add a marker at 'tick' (default: the play position), with an optional "
		"'name' and 'color' (\"#rrggbb\"). Saved with the project; one undo step."),
		objectSchema({{QStringLiteral("tick"), tickProperty()}, {QStringLiteral("name"), stringProperty()},
			{QStringLiteral("color"), stringProperty()}}), true, addMarker);
	add(registry, "set", QStringLiteral("Move, rename or recolour marker 'marker' (only the fields given change). "
		"One undo step."), objectSchema({{QStringLiteral("marker"), stringProperty()}, {QStringLiteral("tick"), tickProperty()},
		{QStringLiteral("name"), stringProperty()}, {QStringLiteral("color"), stringProperty()}}, {QStringLiteral("marker")}),
		true, setMarker);
	add(registry, "remove", QStringLiteral("Remove marker 'marker'. One undo step."),
		objectSchema({{QStringLiteral("marker"), stringProperty()}}, {QStringLiteral("marker")}), true, removeMarker);
	add(registry, "jump", QStringLiteral("Move the play position to marker 'marker', or to the next / previous "
		"marker from it ('direction', default next). Refused when there is none."),
		objectSchema({{QStringLiteral("marker"), stringProperty()},
			{QStringLiteral("direction"), enumProperty({QStringLiteral("next"), QStringLiteral("previous")})}}), false, jumpMarker);
}

} // namespace lmms
