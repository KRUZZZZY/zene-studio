/*
 * ControlCommandsSelection.cpp - window.select: what the Inspector and the Device panel show
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

#include <QJsonObject>
#include <tuple>

#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "DevicePanel.h"
#include "GuiApplication.h"
#include "InspectorPanel.h"
#include "MainWindow.h"
#include "Shell.h"
#include "ShellPanel.h"
#include "ShellSelection.h"

namespace lmms
{

using namespace control;

namespace
{

template <class T>
T* mounted(gui::GuiApplication* g, const char* id)
{
	gui::ShellPanel* panel = g->mainWindow()->shell()->panel(QLatin1String(id));
	return panel != nullptr ? qobject_cast<T*>(panel->content()) : nullptr;
}

QJsonObject selectionState(gui::GuiApplication* g)
{
	gui::ShellSelection* selection = gui::ShellSelection::instance();
	QJsonObject out{{QStringLiteral("track"), trackIdOf(selection->track())},
		{QStringLiteral("clip"), clipIdOf(selection->clip())}, {QStringLiteral("channel"), selection->channel()}};
	if (auto* inspector = mounted<gui::InspectorPanel>(g, "inspector")) { out.insert(QStringLiteral("inspector"), inspector->describe()); }
	if (auto* devices = mounted<gui::DevicePanel>(g, "devices")) { out.insert(QStringLiteral("devices"), devices->describe()); }
	return out;
}

ControlResult select(const QJsonObject& args)
{
	gui::GuiApplication* g = gui::getGUI();
	if (g == nullptr || g->mainWindow() == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Requires,
			QStringLiteral("command 'window.select' requires an interface: this instance has no main window"));
	}
	gui::ShellSelection* selection = gui::ShellSelection::instance();
	const auto before = std::make_tuple(selection->track(), selection->clip(), selection->channel());
	ControlResult error;
	if (args.contains(QStringLiteral("clip")))
	{
		ClipRef ref;
		if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
		gui::ShellSelection::instance()->setClip(ref.clip);
	}
	else if (args.contains(QStringLiteral("channel")))
	{
		const QString id = args.value(QStringLiteral("channel")).toString();
		if (resolveMixerChannel(id, &error) == nullptr) { return error; }
		selection->setChannel(id);
	}
	else if (args.contains(QStringLiteral("track")))
	{
		const QString id = args.value(QStringLiteral("track")).toString();
		Track* track = id.isEmpty() ? nullptr : resolveTrack(id, &error);
		if (!id.isEmpty() && track == nullptr) { return error; }
		gui::ShellSelection::instance()->setTrack(track);
	}
	// A changed selection queues the panels' redraw for the next event-loop turn; draw them now,
	// so the reply describes what they show.
	if (before != std::make_tuple(selection->track(), selection->clip(), selection->channel()))
	{
		if (auto* inspector = mounted<gui::InspectorPanel>(g, "inspector")) { inspector->rebuild(); }
		if (auto* devices = mounted<gui::DevicePanel>(g, "devices")) { devices->rebuild(); }
	}
	return ControlResult::success(selectionState(g));
}

} // namespace

QJsonObject shellSelectionState()
{
	gui::GuiApplication* g = gui::getGUI();
	return g != nullptr && g->mainWindow() != nullptr ? selectionState(g) : QJsonObject();
}

void registerSelectionWindowCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("window");
	cmd.verb = QStringLiteral("select");
	cmd.id = QStringLiteral("window.select");
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.description = QStringLiteral("Select what the Inspector and the Device panel show - what clicking a "
		"track header, a clip or a mixer strip does. `clip` (clip-<n>) selects that clip and its track; "
		"otherwise `channel` (ch-<n>) selects a mixer channel on its own; otherwise `track` (trk-<n>) selects "
		"a track, and an empty `track` clears the selection; no argument just answers. The reply is "
		"{track, clip, channel, inspector, devices}: the Inspector's {track, clip, channel, name, muted, "
		"soloed} and the Device panel's {track, channel, cards: [{target, plugin, name, enabled, "
		"parameters, shown}], modulators} - a track's own mixer channel (not the master) is drawn after "
		"its devices, with the modulators beside the inserts they drive. Interface state, not project state: nothing is journalled. Refused `requires` in a "
		"process with no interface.");
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("channel"), stringProperty()}});
	cmd.resultSchema = objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("inspector"), objectProperty()}, {QStringLiteral("devices"), objectProperty()}});
	cmd.handler = select;
	registry.registerCommand(cmd);
}

} // namespace lmms
