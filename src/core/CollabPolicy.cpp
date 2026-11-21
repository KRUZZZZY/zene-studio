/*
 * CollabPolicy.cpp - which commands a collaboration peer may send, which run
 *                    locally, and which the host streams (0.5.0 F6, WF-050/052)
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

/*! The rules, in one place, as data. The host is the single authority: a peer's edit is applied by the host
 *  and reaches everyone - the peer included - in the host's order, so there is nothing to merge. What this
 *  file decides is which commands take part in that, which belong to one machine, and which the host must
 *  never run for somebody else (a script, a file path on its disk, a dialog). */

#include "CollabPolicy.h"

#include <QJsonArray>
#include <QSet>

#include "ControlRegistry.h"

namespace lmms::collab
{

namespace
{

QString groupOf(const QString& id)
{
	return id.section(QLatin1Char('.'), 0, 0);
}

//! The session's own commands, and every command that edits one machine rather than the project: preferences,
//! devices, the library, the window. They run where they are invoked and are never streamed.
const QSet<QString>& localGroups()
{
	static const QSet<QString> groups{QStringLiteral("collab"), QStringLiteral("settings"), QStringLiteral("audio"),
		QStringLiteral("midi"), QStringLiteral("crash"), QStringLiteral("telemetry"), QStringLiteral("feedback"),
		QStringLiteral("safestart"), QStringLiteral("window"), QStringLiteral("shortcuts"), QStringLiteral("theme"),
		QStringLiteral("app"), QStringLiteral("oop"), QStringLiteral("link"), QStringLiteral("clock"),
		QStringLiteral("export"), QStringLiteral("browser"), QStringLiteral("workflow"), QStringLiteral("render"),
		QStringLiteral("mastering"), QStringLiteral("view"), QStringLiteral("remote")};
	return groups;
}

const QSet<QString>& localIds()
{
	static const QSet<QString> ids{QStringLiteral("transport.seek"), QStringLiteral("transport.time_format"),
		QStringLiteral("transport.metronome_options"), QStringLiteral("transport.record_options"),
		QStringLiteral("transport.midi_options"), QStringLiteral("control.set_undo_depth"),
		QStringLiteral("control.set_undo_coalescing"), QStringLiteral("plugin.paths_set"), QStringLiteral("plugin.organise"),
		QStringLiteral("plugin.prefer_format"), QStringLiteral("plugin.rescan"),
		QStringLiteral("plugin.scan_cache_quarantine_add"), QStringLiteral("plugin.scan_cache_quarantine_remove"),
		QStringLiteral("plugin.state_save"), QStringLiteral("plugin.preset_save"), QStringLiteral("plugin.preset_remove"),
		QStringLiteral("plugin.preset_rename"), QStringLiteral("project.save"), QStringLiteral("project.save_version"),
		QStringLiteral("project.save_as_template"), QStringLiteral("project.export_midi"), QStringLiteral("chain.save"),
		QStringLiteral("chain.remove"), QStringLiteral("chain.rename"), QStringLiteral("scale.define"),
		// Q050-217.1: the controller's bindings are the machine's, like the launcher's.
		QStringLiteral("session.bind"), QStringLiteral("session.unbind"), QStringLiteral("session.setlist"),
		QStringLiteral("session.bind_command"), QStringLiteral("session.unbind_command"),
		QStringLiteral("controller.template_save"), QStringLiteral("controller.template_delete"),
		QStringLiteral("track.preset_save"), QStringLiteral("meter.arm"), QStringLiteral("record.journal_begin"),
		QStringLiteral("record.journal_update"), QStringLiteral("record.journal_finish")};
	return ids;
}

//! Undo and redo walk the HOST's history, which every participant shares (per-originator undo is not offered).
const QSet<QString>& historyIds()
{
	static const QSet<QString> ids{QStringLiteral("control.undo"), QStringLiteral("control.redo"),
		QStringLiteral("control.history_jump")};
	return ids;
}

//! Commands that replace the project, run code, or read a file on the invoking machine: a peer cannot take
//! part in them, and the host shares their outcome as a fresh snapshot.
const QSet<QString>& wholeProjectIds()
{
	static const QSet<QString> ids{QStringLiteral("project.open"), QStringLiteral("project.new"),
		QStringLiteral("project.import"), QStringLiteral("project.restore_revision"), QStringLiteral("project.merge"),
		QStringLiteral("project.relink"), QStringLiteral("project.collect_media"), QStringLiteral("project.import_tracks"),
		QStringLiteral("project.alternative_switch"), QStringLiteral("project.alternative_save"),
		QStringLiteral("project.alternative_remove"), QStringLiteral("project.subproject_add"),
		QStringLiteral("project.subproject_refresh"), QStringLiteral("revisions.restore"),
		QStringLiteral("session.setlist_go"), QStringLiteral("dawproject.import"), QStringLiteral("interchange.smf_import"),
		QStringLiteral("script.run"), QStringLiteral("script.set_memory_budget"), QStringLiteral("workflow.run"),
		QStringLiteral("workflow.resume"),
		QStringLiteral("livecode.schedule"), QStringLiteral("livecode.unschedule"), QStringLiteral("record.recovery_restore"),
		QStringLiteral("record.recovery_discard")};
	return ids;
}

//! A host never runs these for a peer, whatever its role: they quit, run code or touch the host's machine.
const QSet<QString>& hostDeniedIds()
{
	static const QSet<QString> ids{QStringLiteral("control.quit"), QStringLiteral("record.arm_track"),
		QStringLiteral("record.input_set"), QStringLiteral("record.route_source"), QStringLiteral("track.set_arm"),
		QStringLiteral("track.set_arm_many"), QStringLiteral("track.set_monitor"), QStringLiteral("plugin.state_load")};
	return ids;
}

//! Argument names that hold a file, folder or URL (measured over the registered mutating commands' schemas).
bool isPathKey(const QString& key)
{
	static const QSet<QString> keys{QStringLiteral("path"), QStringLiteral("file"), QStringLiteral("dir"),
		QStringLiteral("dirs"), QStringLiteral("directory"), QStringLiteral("out_dir"), QStringLiteral("url"),
		QStringLiteral("uri"), QStringLiteral("sample"), QStringLiteral("project")};
	return keys.contains(key) || key.endsWith(QLatin1String("_path")) || key.endsWith(QLatin1String("_file"))
		|| key.endsWith(QLatin1String("_dir"));
}

bool pathLike(const QJsonValue& value, int depth);

bool arrayPathLike(const QJsonArray& array, int depth)
{
	for (const QJsonValue& item : array)
	{
		if (pathLike(item, depth + 1)) { return true; }
	}
	return false;
}

bool pathLike(const QJsonValue& value, int depth)
{
	// Bounded: a hostile peer's deeply nested arguments cost at most four levels here, and the
	// registry's own schema check refuses what the command does not declare.
	if (depth > 4) { return false; }
	if (value.isArray()) { return arrayPathLike(value.toArray(), depth); }
	if (!value.isObject()) { return false; }
	const QJsonObject object = value.toObject();
	for (auto it = object.begin(); it != object.end(); ++it)
	{
		if (isPathKey(it.key()) || pathLike(it.value(), depth + 1)) { return true; }
	}
	return false;
}

bool isLocal(const QString& id)
{
	return localGroups().contains(groupOf(id)) || localIds().contains(id);
}

RouteDecision refuse(const QString& reason)
{
	return RouteDecision{PeerRoute::Refuse, reason};
}

RouteDecision routeHistory(CollabRole role)
{
	if (role == CollabRole::Editor) { return RouteDecision{PeerRoute::Forward, {}}; }
	return refuse(QStringLiteral("you joined as a %1: undo and redo walk the host's history").arg(roleName(role)));
}

} // namespace

QString roleName(CollabRole role)
{
	switch (role)
	{
	case CollabRole::Editor: return QStringLiteral("editor");
	case CollabRole::Viewer: return QStringLiteral("viewer");
	case CollabRole::Remote: return QStringLiteral("remote");
	}
	return QString();
}

bool roleFromName(const QString& name, CollabRole* role)
{
	for (const CollabRole candidate : {CollabRole::Editor, CollabRole::Viewer, CollabRole::Remote})
	{
		if (roleName(candidate) == name) { *role = candidate; return true; }
	}
	return false;
}

bool hasPathLikeArgument(const QJsonObject& args)
{
	return pathLike(QJsonValue(args), 0);
}

namespace
{

RouteDecision routeRemote(const QString& id, bool mutating)
{
	// A Zene instance joined with a remote invite holds no project: what the allow-list names runs on the host.
	if (remoteAllowList().contains(id) && groupOf(id) != QLatin1String("collab")) { return RouteDecision{PeerRoute::Forward, {}}; }
	if (!mutating || isLocal(id)) { return RouteDecision{PeerRoute::Local, {}}; }
	return refuse(QStringLiteral("you joined as a remote control: '%1' is not on its allow-list").arg(id));
}

} // namespace

RouteDecision peerRoute(const QString& id, bool mutating, const QJsonObject& args, CollabRole role)
{
	if (role == CollabRole::Remote) { return routeRemote(id, mutating); }
	if (historyIds().contains(id)) { return routeHistory(role); }
	if (wholeProjectIds().contains(id))
	{
		return refuse(QStringLiteral("'%1' replaces or reloads the shared project, runs code, or reads a file on this "
			"machine; ask the host, or collab.leave first").arg(id));
	}
	if (!mutating || isLocal(id)) { return RouteDecision{PeerRoute::Local, {}}; }
	if (hasPathLikeArgument(args))
	{
		return refuse(QStringLiteral("'%1' names a file on this machine, which the host cannot read; ask the host to "
			"do it with its own copy of the file").arg(id));
	}
	if (role != CollabRole::Editor)
	{
		return refuse(QStringLiteral("you joined as a %1: only editors change the shared project").arg(roleName(role)));
	}
	return RouteDecision{PeerRoute::Forward, {}};
}

HostBroadcast hostBroadcast(const QString& id, bool mutating, bool changed, const QJsonObject& args)
{
	if (!changed || groupOf(id) == QLatin1String("collab")) { return HostBroadcast::None; }
	if (historyIds().contains(id) || wholeProjectIds().contains(id)) { return HostBroadcast::Resync; }
	if (!mutating || isLocal(id)) { return HostBroadcast::None; }
	// A file the host read cannot be read again on a peer: share the outcome instead of the command.
	if (hasPathLikeArgument(args)) { return HostBroadcast::Resync; }
	return HostBroadcast::Stream;
}

QStringList remoteAllowList()
{
	return {QStringLiteral("control.ping"), QStringLiteral("collab.get_state"), QStringLiteral("transport.play"),
		QStringLiteral("transport.stop"), QStringLiteral("transport.seek"), QStringLiteral("transport.get_state"),
		QStringLiteral("transport.panic"), QStringLiteral("marker.list"), QStringLiteral("marker.jump"),
		QStringLiteral("marker.add"), QStringLiteral("marker.set"), QStringLiteral("marker.remove"),
		QStringLiteral("mixer.get_state"), QStringLiteral("mixer.set_volume"), QStringLiteral("mixer.set_pan")};
}

namespace
{

QString editorRefusal(const QString& id, const ControlCommand& command, const QJsonObject& args)
{
	if (!command.requiresDecl.isEmpty())
	{
		return QStringLiteral("'%1' needs the host's %2, so a peer cannot run it").arg(id, command.requiresDecl.join(QLatin1Char('/')));
	}
	if (historyIds().contains(id) || id == QLatin1String("control.ping")) { return QString(); }
	if (!command.mutating) { return QStringLiteral("'%1' is a read: you hold the project, so read your own copy").arg(id); }
	if (isLocal(id) || wholeProjectIds().contains(id) || hostDeniedIds().contains(id))
	{
		return QStringLiteral("'%1' acts on the host's machine (a preference, a device, a file, a script or the whole "
			"project); the host does not run it for a peer").arg(id);
	}
	if (hasPathLikeArgument(args)) { return QStringLiteral("'%1' names a file; a peer cannot reach the host's disk").arg(id); }
	return QString();
}

} // namespace

QString hostRefusal(const QString& id, const ControlCommand* command, const QJsonObject& args, CollabRole role)
{
	if (command == nullptr) { return QString(); }
	switch (role)
	{
	case CollabRole::Editor: return editorRefusal(id, *command, args);
	case CollabRole::Viewer:
		return id == QLatin1String("control.ping") ? QString() : QStringLiteral("viewers do not send commands");
	case CollabRole::Remote:
		return remoteAllowList().contains(id) ? QString()
			: QStringLiteral("'%1' is not on the remote-control allow-list (transport, markers, mixer faders, state)").arg(id);
	}
	return QStringLiteral("unknown role");
}

} // namespace lmms::collab
