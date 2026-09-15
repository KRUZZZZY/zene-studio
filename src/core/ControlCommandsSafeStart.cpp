/*
 * ControlCommandsSafeStart.cpp - the READ half of the safestart.* surface
 *                                (SPEC A11-A16).
 *
 * Feature row 77 of docs/FEATURE-LIST-0.3.0.md ("Safe-start mode after a crash -
 * launch with third-party plugins disabled", OWNER-31 item 31, board task #666).
 * The engine half is include/SafeStart.h and src/core/SafeStart.cpp; this file
 * holds safestart.get_state - the whole state, from the module's own accessors -
 * and the two halves' shared vocabulary (ControlCommandsSafeStartShared.h). The
 * group's three WRITERS are in ControlCommandsSafeStartEdit.cpp.
 *
 * The surface is the ONLY way safe-start mode is operated in this release: there
 * is no dialog, no banner and no toolbar button for it (docs/KNOWN-LIMITATIONS.md
 * and docs/RELEASE-NOTES-v0.3.0-alpha.md carry the absence line), and the offer
 * of a normal start is reported here and printed on stderr by main().
 *
 * WHAT THE ENGINE ACTUALLY SUPPORTS, read off the header rather than assumed:
 *   install, isInstalled, workingDirectory, markerPath, acknowledgedPath,
 *   beginSession/endSession, markerExists, acknowledged, previousRunExitedCleanly,
 *   safeStartActive, lastSession, safeStartRunCount, setProjectPath, projectPath,
 *   acknowledge, clear, isThirdPartyPluginFile, shouldSkipPluginInstance,
 *   noteSkippedInstance, skippedInstances/skippedCount/resetSkippedInstances,
 *   skipEnabled/setSkipEnabled, ownPluginDirectories.
 * Of those, beginSession()/endSession() are NOT registered, and that is a
 * decision rather than an omission: the lifecycle belongs to the process (main()
 * calls them around the run, exactly as it calls the crash reporter's), and a
 * command that could start a session on a running instance, or end one without
 * the process exiting, would be a way to make the marker lie about what
 * happened. install() is not registered for the same reason the crash reporter's
 * is not: the working directory is the process's, and there is exactly one.
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

#include <string>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlCommandsSafeStartShared.h"
#include "ControlRegistry.h"

#include "ControlVocabulary.h"
#include "SafeStart.h"

namespace lmms
{

namespace control
{

using namespace lmms::safestart;

const QString SafeStartGetStateId = QStringLiteral("safestart.get_state");
const QString SafeStartAcknowledgeId = QStringLiteral("safestart.acknowledge");
const QString SafeStartClearId = QStringLiteral("safestart.clear");
const QString SafeStartSetSkipId = QStringLiteral("safestart.set_skip");

QString safeStartWire(const std::string& text) { return QString::fromStdString(text); }

QJsonObject safeStartFileJson(const std::string& path)
{
	QJsonObject out;
	out.insert(QStringLiteral("path"), safeStartWire(path));
	if (path.empty())
	{
		out.insert(QStringLiteral("exists"), false);
		out.insert(QStringLiteral("bytes"), 0);
		out.insert(QStringLiteral("modified_unix"), 0);
		return out;
	}
	const QFileInfo info(safeStartWire(path));
	out.insert(QStringLiteral("exists"), info.exists());
	out.insert(QStringLiteral("bytes"), info.isFile() ? qint64(info.size()) : qint64(0));
	out.insert(QStringLiteral("modified_unix"),
		info.exists() ? qint64(info.lastModified().toSecsSinceEpoch()) : qint64(0));
	return out;
}

ControlResult safeStartNoModule(const QString& command)
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1: safe-start mode is not installed in this instance (it has no working "
			"directory), so it holds no marker and nothing was written. main() installs it beside the "
			"crash reporter before this surface is reachable").arg(command));
}

QJsonObject safeStartBeforeState(const std::string& marker, const std::string& acknowledged)
{
	QJsonObject out;
	out.insert(QStringLiteral("marker"), safeStartFileJson(marker));
	out.insert(QStringLiteral("acknowledgement"), safeStartFileJson(acknowledged));
	out.insert(QStringLiteral("safe_start"), safeStartActive());
	out.insert(QStringLiteral("skip_enabled"), skipEnabled());
	return out;
}

/*! safestart.get_state - the whole state, from the module's own accessors: the
 *  marker and the acknowledgement as files (path, existence, size, time), the
 *  previous session's record, whether THIS session is a safe start, the
 *  session-scoped skip switch, every instance the mode skipped, the directories
 *  the third-party classification treats as this build's own, and the offer.
 *  Read-only, and it answers in every configuration.
 */
ControlResult safeStartHandleGetState()
{
	const bool installed = isInstalled();
	const std::string marker = markerPath();
	const std::string acknowledgedFile = acknowledgedPath();
	const SessionRecord previous = lastSession();

	QJsonArray own;
	for (const std::string& directory : ownPluginDirectories())
	{
		own.append(safeStartWire(directory));
	}

	QJsonArray skipped;
	for (const SkippedInstance& instance : skippedInstances())
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("plugin"), safeStartWire(instance.pluginName));
		entry.insert(QStringLiteral("file"), safeStartWire(instance.file));
		entry.insert(QStringLiteral("reason"), safeStartWire(instance.reason));
		skipped.append(entry);
	}

	const bool safeStart = safeStartActive();

	QJsonObject session;
	session.insert(QStringLiteral("present"), previous.present);
	session.insert(QStringLiteral("process_id"), qint64(previous.processId));
	session.insert(QStringLiteral("time_unix"), qint64(previous.unixTime));
	session.insert(QStringLiteral("project"), safeStartWire(previous.projectPath));

	// The offer of the normal start, as the state machine actually holds it: the
	// safe session is the one that can accept it, and accepting it is
	// safestart.acknowledge. There is no dialog in this release - the offer is
	// reported HERE and on stderr by main().
	QJsonObject offer;
	offer.insert(QStringLiteral("made"), safeStart);
	offer.insert(QStringLiteral("accepted"), acknowledged());
	offer.insert(QStringLiteral("restart_required"), safeStart);
	offer.insert(QStringLiteral("accept_with"), SafeStartAcknowledgeId);
	offer.insert(QStringLiteral("decline_with"), SafeStartClearId);
	offer.insert(QStringLiteral("text"), safeStart
		? QStringLiteral("The previous session ended unexpectedly, so this one started with THIRD-PARTY "
			"plugin instances skipped. Accept the offer (safestart.acknowledge) and the NEXT launch "
			"loads them again; safestart.clear drops the marker now; safestart.set_skip false loads "
			"them in THIS session.")
		: QStringLiteral("Nothing is offered: this session did not start safe."));

	QJsonObject bounds;
	bounds.insert(QStringLiteral("max_marker_bytes"), qint64(kMaxMarkerBytes));
	bounds.insert(QStringLiteral("max_project_path_bytes"), qint64(kMaxProjectPathBytes));

	QJsonObject result;
	result.insert(QStringLiteral("installed"), installed);
	result.insert(QStringLiteral("working_directory"), safeStartWire(workingDirectory()));
	result.insert(QStringLiteral("marker_path"), safeStartWire(marker));
	result.insert(QStringLiteral("acknowledgement_path"), safeStartWire(acknowledgedFile));
	result.insert(QStringLiteral("marker"), safeStartFileJson(marker));
	result.insert(QStringLiteral("acknowledgement"), safeStartFileJson(acknowledgedFile));
	result.insert(QStringLiteral("marker_present"), markerExists());
	result.insert(QStringLiteral("acknowledged"), acknowledged());
	// The module's OWN predicate, not a re-derivation of it.
	result.insert(QStringLiteral("previous_run_exited_cleanly"), previousRunExitedCleanly());
	result.insert(QStringLiteral("safe_start"), safeStart);
	result.insert(QStringLiteral("safe_start_runs"), qint64(safeStartRunCount()));
	result.insert(QStringLiteral("skip_enabled"), skipEnabled());
	result.insert(QStringLiteral("skipped"), skipped);
	result.insert(QStringLiteral("skipped_count"), skipped.size());
	result.insert(QStringLiteral("own_plugin_directories"), own);
	result.insert(QStringLiteral("session"), session);
	result.insert(QStringLiteral("project_path"), safeStartWire(projectPath()));
	result.insert(QStringLiteral("offer"), offer);
	result.insert(QStringLiteral("bounds"), bounds);
	result.insert(QStringLiteral("note"),
		QStringLiteral("safe-start mode is a MARKER plus a load-time predicate. The marker is written "
			"when a session begins and unlinked when it exits cleanly, so a marker found at launch "
			"means the previous run did not exit cleanly (a signal, SIGKILL or a power cut - none of "
			"which can run code on the way out). While it is present and unacknowledged, every "
			"THIRD-PARTY plugin instance is replaced by the engine's DummyPlugin at load time "
			"(Plugin::instantiate, the single funnel for instruments, effects, tools and filters), and "
			"`own_plugin_directories` is the classification's own input: a module under one of those is "
			"a file this build ships and is never skipped. Nothing here is project state."));
	return ControlResult::success(result);
}

} // namespace control

void registerSafeStartCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = control::SafeStartGetStateId;
		cmd.group = QStringLiteral("safestart");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("Safe-start mode's state: whether the module is installed, "
			"the crash marker and the acknowledgement as files (path, existence, size, time), whether "
			"a marker says the previous run did not exit cleanly, whether THIS session started safe, "
			"how many sessions in a row have, the session-scoped skip switch, every plugin instance "
			"the mode skipped and why, the directories the third-party classification treats as this "
			"build's own, the previous session's own record and the offer of a normal start. "
			"Read-only, and it answers in every configuration.");
		cmd.argsSchema = control::objectSchema({});
		cmd.resultSchema = control::objectSchema({
			{QStringLiteral("installed"), control::booleanProperty()},
			{QStringLiteral("working_directory"), control::stringProperty()},
			{QStringLiteral("marker_path"), control::stringProperty()},
			{QStringLiteral("acknowledgement_path"), control::stringProperty()},
			// {path, exists, bytes, modified_unix}
			{QStringLiteral("marker"), control::objectProperty()},
			{QStringLiteral("acknowledgement"), control::objectProperty()},
			{QStringLiteral("marker_present"), control::booleanProperty()},
			{QStringLiteral("acknowledged"), control::booleanProperty()},
			{QStringLiteral("previous_run_exited_cleanly"), control::booleanProperty()},
			{QStringLiteral("safe_start"), control::booleanProperty()},
			{QStringLiteral("safe_start_runs"), control::integerProperty()},
			{QStringLiteral("skip_enabled"), control::booleanProperty()},
			{QStringLiteral("skipped"), control::arrayProperty()},
			{QStringLiteral("skipped_count"), control::integerProperty()},
			{QStringLiteral("own_plugin_directories"), control::arrayProperty()},
			// {present, process_id, time_unix, project}
			{QStringLiteral("session"), control::objectProperty()},
			{QStringLiteral("project_path"), control::stringProperty()},
			// {made, accepted, restart_required, accept_with, decline_with, text}
			{QStringLiteral("offer"), control::objectProperty()},
			// {max_marker_bytes, max_project_path_bytes}
			{QStringLiteral("bounds"), control::objectProperty()},
			{QStringLiteral("note"), control::stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return control::safeStartHandleGetState(); };
		registry.registerCommand(cmd);
	}

	// The group's three writers, in their own translation unit: the registry has
	// exactly one safestart.* registration point, and it is this one.
	control::registerSafeStartEditCommands(registry);
}

} // namespace lmms
