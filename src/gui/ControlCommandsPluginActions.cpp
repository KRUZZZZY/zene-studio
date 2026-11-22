/*
 * ControlCommandsPluginActions.cpp - the keyboard's plug-in actions: what the selection's device does on a key
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

/*! Q050-055.2 (0.5.0 L13): plugin.editor_open and plugin.load take an explicit target; a key does not know one. These
 *  commands read the shell's selection when they run - never remembered, so a key cannot act on a stale track - and
 *  then run the explicit command, so its refusals, its result and its undo step are the key's too. They are
 *  non-mutating wrappers (the arrangement.nudge pattern): the nested command records itself, and on a collaboration
 *  peer it is the explicit command, with its explicit target, that goes to the host. GUI thread only (ControlRegistry). */

#include <QJsonArray>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ControlWindowCommands.h"
#include "DevicePanel.h"
#include "Effect.h"
#include "EffectChain.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "PanelShortcuts.h"
#include "PluginOrganiser.h"
#include "Shell.h"
#include "ShellPanel.h"
#include "ShellSelection.h"

namespace lmms
{

using namespace control;
namespace ps = gui::panelshortcuts;

namespace
{

QJsonObject deviceProperty()
{
	QJsonObject spec = stringProperty();
	spec.insert(QStringLiteral("format"), QStringLiteral("device"));  // the command form lists the catalogue by name
	return spec;
}

//! The selection's target id, or a typed refusal: a key with nothing selected acts on nothing, not on some other track.
QString selectedTarget(ControlResult* error)
{
	const gui::ShellSelection* selection = gui::ShellSelection::instance();
	const QString id = selection->track() != nullptr ? trackIdOf(selection->track()) : selection->channel();
	if (id.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("no track or mixer channel is selected: click one first (window.select)"));
	}
	return id;
}

//! The device @a which ("" = the instrument, else the first effect; "effect" = the first effect) of @a target, or a typed
//! refusal naming what it lacks. Never another target's device.
QString deviceOn(const ControlTarget& target, const QString& which, ControlResult* error)
{
	if (which != QLatin1String("effect") && target.instrumentTrack != nullptr) { return QStringLiteral("inst"); }
	if (target.chain != nullptr && !target.chain->effects().empty()) { return effectIdOf(target.chain->effects().front()); }
	*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("'%1' has %2: there is no editor to open")
		.arg(target.id, which == QLatin1String("effect") ? QStringLiteral("no effect") : QStringLiteral("no instrument and no effect")));
	return {};
}

gui::DevicePanel* devicePanel()
{
	gui::MainWindow* window = controlMainWindow();
	gui::ShellPanel* panel = window != nullptr && window->shell() != nullptr ? window->shell()->panel(QStringLiteral("devices")) : nullptr;
	return panel != nullptr ? qobject_cast<gui::DevicePanel*>(panel->content()) : nullptr;
}

ControlResult selectedEditorOpen(const QJsonObject& args)
{
	if (controlMainWindow() == nullptr) { return noInterface(QStringLiteral("plugin.selected_editor_open")); }
	ControlResult error;
	const QString targetId = selectedTarget(&error);
	if (targetId.isEmpty()) { return error; }
	ControlTarget target;
	if (!resolveControlTarget(targetId, &target, &error)) { return error; }
	const QString plugin = deviceOn(target, args.value(QStringLiteral("device")).toString(), &error);
	if (plugin.isEmpty()) { return error; }
	// As the device card does: a hosted plug-in's own window first, the host's own view when it has none.
	const ControlResult native = ControlRegistry::instance()->invoke(QStringLiteral("plugin.editor_open"),
		{{QStringLiteral("target"), targetId}, {QStringLiteral("plugin"), plugin}});
	if (native.ok)
	{
		QJsonObject result = native.result;
		result.insert(QStringLiteral("surface"), QStringLiteral("native"));
		return ControlResult::success(result);
	}
	gui::DevicePanel* panel = devicePanel();
	if (panel == nullptr || !panel->openGenericEditor(targetId, plugin)) { return native; }
	return ControlResult::success({{QStringLiteral("target"), targetId}, {QStringLiteral("plugin"), plugin},
		{QStringLiteral("open"), true}, {QStringLiteral("surface"), QStringLiteral("generic")}});
}

//! The dev-<n> of @a named: a dev-<n> itself, or the stable key a binding stores (pluginorganiser::keyOf).
QString catalogueId(const QString& named, ControlResult* error)
{
	if (named.startsWith(QLatin1String("dev-"))) { return named; }
	const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
	for (int i = 0; i < catalogue.size(); ++i)
	{
		if (pluginorganiser::keyOf(catalogue[i]) == named) { return deviceId(i); }
	}
	*error = ControlResult::failure(ControlErrorKind::NotFound,
		QStringLiteral("no device '%1' in this build's catalogue (plugin.list names them)").arg(named));
	return {};
}

ControlResult selectedLoad(const QJsonObject& args)
{
	if (controlMainWindow() == nullptr) { return noInterface(QStringLiteral("plugin.selected_load")); }
	ControlResult error;
	const QString device = catalogueId(args.value(QStringLiteral("device")).toString(), &error);
	if (device.isEmpty()) { return error; }
	const QString target = selectedTarget(&error);
	if (target.isEmpty()) { return error; }
	return ControlRegistry::instance()->invoke(QStringLiteral("plugin.load"),
		{{QStringLiteral("target"), target}, {QStringLiteral("device"), device}});
}

ControlResult bindPlugin(const QJsonObject& args)
{
	const QString scope = args.value(QStringLiteral("scope")).toString();
	const QString keys = args.value(QStringLiteral("keys")).toString();
	if (keys.trimmed().isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("name the keys (Unbind on its row removes a binding)"));
	}
	ControlDeviceEntry entry;
	int index = -1;
	ControlResult error;
	if (!controlDeviceById(args.value(QStringLiteral("device")).toString(), &entry, &index, &error)) { return error; }
	if (!entry.loadable)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("'%1' cannot be loaded in this build").arg(entry.displayName));
	}
	const QString key = pluginorganiser::keyOf(entry);
	QJsonObject set{{QStringLiteral("id"), ps::pluginActionId(scope, key)}, {QStringLiteral("scope"), scope}, {QStringLiteral("keys"), keys}};
	if (args.contains(QStringLiteral("force"))) { set.insert(QStringLiteral("force"), args.value(QStringLiteral("force"))); }
	ControlResult bound = ControlRegistry::instance()->invoke(QStringLiteral("shortcuts.set"), set);
	if (bound.ok)
	{
		bound.result.insert(QStringLiteral("device"), key);
		bound.result.insert(QStringLiteral("display_name"), entry.displayName);
	}
	return bound;
}

void add(ControlRegistry& registry, const char* group, const char* verb, const QString& description, const QJsonObject& args,
	const QJsonObject& result, ControlResult (*handler)(const QJsonObject&), bool needsDisplay = false)
{
	ControlCommand cmd = guiCommand(group, verb);
	if (needsDisplay) { cmd.requiresDecl = QStringList{QStringLiteral("display")}; }
	cmd.description = description;
	cmd.argsSchema = args;
	cmd.resultSchema = result;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerPluginActionCommands(ControlRegistry& registry)
{
	QJsonObject keys = stringProperty();
	keys.insert(QStringLiteral("format"), QStringLiteral("keys"));
	QJsonObject which = stringProperty();
	which.insert(QStringLiteral("enum"), QJsonArray{QStringLiteral("main"), QStringLiteral("effect")});
	QJsonObject scope = stringProperty();
	scope.insert(QStringLiteral("enum"), QJsonArray::fromStringList(ps::scopes()));
	add(registry, "plugin", "selected_editor_open", QStringLiteral("Open the editor of the selected track's device: its instrument, or "
		"on a track with none (and a selected mixer channel) its first effect; 'device': effect asks for the first effect. "
		"A plug-in's own window when it can open one (plugin.editor_open), else the host's view - 'surface' says which: native "
		"or generic. Refused, typed, with nothing selected or no such device on the selection; it never goes to another track. "
		"Needs a display. Interface state: nothing is saved or journalled."),
		objectSchema({{QStringLiteral("device"), which}}),
		objectSchema({{QStringLiteral("target"), stringProperty()}, {QStringLiteral("plugin"), stringProperty()},
			{QStringLiteral("name"), stringProperty()}, {QStringLiteral("open"), booleanProperty()},
			{QStringLiteral("surface"), stringProperty()}}), selectedEditorOpen, true);
	add(registry, "plugin", "selected_load", QStringLiteral("Load a device onto what is selected: plugin.load with the selected track "
		"or mixer channel as its 'target', so the reply, the refusals (an instrument onto a target with no instrument, a track "
		"with no chain) and the undo are plugin.load's - an effect is one undo step, replacing an instrument reports "
		"reversible:false. 'device' is a dev-<n> or the stable key a binding stores. Needs an interface: the selection is one."),
		objectSchema({{QStringLiteral("device"), deviceProperty()}}, {QStringLiteral("device")}),
		objectSchema({{QStringLiteral("target"), stringProperty()}, {QStringLiteral("kind"), stringProperty()},
			{QStringLiteral("id"), stringProperty()}, {QStringLiteral("plugin"), stringProperty()},
			{QStringLiteral("device"), stringProperty()}, {QStringLiteral("display_name"), stringProperty()},
			{QStringLiteral("index"), numberProperty()}, {QStringLiteral("chain"), integerProperty()},
			{QStringLiteral("previous_plugin"), stringProperty()}}), selectedLoad);
	add(registry, "shortcuts", "bind_plugin", QStringLiteral("Give a chosen loadable plug-in ('device', dev-<n>) a key in one panel "
		"scope: pressing it there runs plugin.selected_load on it. The binding appears in shortcuts.list as '<Panel> ▸ Load "
		"Plug-in ▸ <stable key>', and behaves as any panel binding: shortcuts.set changes it (empty keys remove it), a same-scope "
		"clash is refused unless 'force', export and import carry it, one undo step. A rescan does not break it."),
		objectSchema({{QStringLiteral("scope"), scope}, {QStringLiteral("device"), deviceProperty()}, {QStringLiteral("keys"), keys},
			{QStringLiteral("force"), booleanProperty()}}, {QStringLiteral("scope"), QStringLiteral("device"), QStringLiteral("keys")}),
		objectSchema({{QStringLiteral("id"), stringProperty()}, {QStringLiteral("scope"), stringProperty()},
			{QStringLiteral("keys"), stringProperty()}, {QStringLiteral("previous"), stringProperty()},
			{QStringLiteral("cleared"), arrayProperty()}, {QStringLiteral("device"), stringProperty()},
			{QStringLiteral("display_name"), stringProperty()}}), bindPlugin);
}

} // namespace lmms
