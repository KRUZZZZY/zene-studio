/*
 * ControlCommandsPluginEditor.cpp - R4.4: plugin.editor_open / plugin.editor_close
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

/*! A plug-in's OWN editor (a VST3 IPlugView, or a CLAP plug-in's embedded clap.gui)
 *  opened in a window of its own through Plugin::openNativeEditor, so an agent can drive and
 *  test an editor the way a person opens one. They need a display - a process with no
 *  interface refuses them, typed - and they are interface state, not project state: nothing
 *  is saved and nothing is journalled. The generic parameter view stays; an editor is an
 *  addition beside it.
 */

#include <QJsonObject>

#include "ControlCommandsOutOfProcessShared.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Plugin.h"

namespace lmms
{

namespace
{

QJsonObject editorState(const control::HostedDevice& device, bool open)
{
	return QJsonObject{{QStringLiteral("target"), device.targetId}, {QStringLiteral("plugin"), device.deviceId},
		{QStringLiteral("name"), device.pluginLabel}, {QStringLiteral("open"), open}};
}

ControlResult editorOpen(const QJsonObject& args)
{
	control::HostedDevice device;
	ControlResult error;
	if (!control::resolveHostedDevice(args, &device, &error)) { return error; }
	QString why;
	if (!device.plugin()->openNativeEditor(&why))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1/%2 (%3): %4").arg(device.targetId, device.deviceId, device.pluginLabel, why));
	}
	return ControlResult::success(editorState(device, device.plugin()->nativeEditorOpen()));
}

ControlResult editorClose(const QJsonObject& args)
{
	control::HostedDevice device;
	ControlResult error;
	if (!control::resolveHostedDevice(args, &device, &error)) { return error; }
	const bool wasOpen = device.plugin()->nativeEditorOpen();
	device.plugin()->closeNativeEditor();
	QJsonObject result = editorState(device, device.plugin()->nativeEditorOpen());
	result.insert(QStringLiteral("was_open"), wasOpen);
	return ControlResult::success(result);
}

void registerOne(ControlRegistry& registry, const char* verb, const QString& description,
	ControlResult (*handler)(const QJsonObject&), bool reportsWasOpen)
{
	ControlCommand cmd;
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.description = description;
	cmd.argsSchema = control::objectSchema({
		{QStringLiteral("target"), control::stringProperty()},
		{QStringLiteral("plugin"), control::stringProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin")});
	QJsonObject result{
		{QStringLiteral("target"), control::stringProperty()},
		{QStringLiteral("plugin"), control::stringProperty()},
		{QStringLiteral("name"), control::stringProperty()},
		{QStringLiteral("open"), control::booleanProperty()},
	};
	if (reportsWasOpen) { result.insert(QStringLiteral("was_open"), control::booleanProperty()); }
	cmd.resultSchema = control::objectSchema(result);
	cmd.mutating = false;
	cmd.requiresDecl = QStringList{QStringLiteral("display")};
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

} // namespace

void registerPluginEditorCommands(ControlRegistry& registry)
{
	registerOne(registry, "editor_open", QStringLiteral("Open a device's OWN editor - the plug-in's "
		"interface, not the host's generic knob view (which stays) - in a window of its own. 'target' "
		"is the track or channel (trk-<n>/ch-<n>) and 'plugin' the device on it (fx-<n>, or 'inst' for "
		"a track's instrument), as plugin.param_set names them. VST3 plug-ins with an editor view "
		"and CLAP plug-ins whose gui embeds in X11 open; a plug-in with no editor, or one that can "
		"only float, refuses with the reason. An editor already open is brought forward. Needs a display; interface state, "
		"not project state: nothing is saved or journalled."), &editorOpen, false);
	registerOne(registry, "editor_close", QStringLiteral("Close a device's own editor window, if "
		"it has one open. 'was_open' says whether there was one; closing a closed editor is not an "
		"error. Needs a display; nothing is saved or journalled."), &editorClose, true);
}

} // namespace lmms
