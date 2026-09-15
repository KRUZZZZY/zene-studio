/*
 * ControlCommandsSafeStart.cpp - the safestart.* command group (SPEC A11-A16).
 *
 * Feature row 77 of docs/FEATURE-LIST-0.3.0.md ("Safe-start mode after a crash -
 * launch with third-party plugins disabled", OWNER-31 item 31). The engine half
 * is include/SafeStart.h and src/core/SafeStart.cpp; this file is the surface
 * that makes it observable and operable, and - per the 0.3.0 scope contract - it
 * is the ONLY way it is operated in this release: there is no dialog, no banner
 * and no toolbar button for safe-start mode (docs/KNOWN-LIMITATIONS.md carries
 * the absence line).
 *
 * WHAT THE ENGINE ACTUALLY SUPPORTS, read off the header rather than assumed:
 *   install, isInstalled, workingDirectory, markerPath, acknowledgedPath,
 *   beginSession/endSession, markerExists, acknowledged, previousRunExitedCleanly,
 *   safeStartActive, lastSession, safeStartRunCount, acknowledge, clear,
 *   isThirdPartyPluginFile, shouldSkipPluginInstance, noteSkippedInstance,
 *   skippedInstances/skippedCount/resetSkippedInstances, skipEnabled/setSkipEnabled,
 *   ownPluginDirectories.
 * Of those, the READ is the state (`safestart.get_state`: the marker, the
 * decision, the skipped instances and the classification's own inputs), and the
 * three WRITES are the ones the state machine actually has:
 *   * safestart.acknowledge - accept the offer of a normal start. Writes the
 *     acknowledgment file the NEXT launch consumes; keeps the marker.
 *   * safestart.clear       - the marker cleared now, and this session out of
 *     safe-start mode. The verb for "the cause is known and fixed".
 *   * safestart.set_skip    - the SESSION-SCOPED half of the load-time
 *     predicate: with it false, a project loaded now loads its third-party
 *     plugins, while the marker stays on disk and the mode stays reported.
 * beginSession()/endSession() are NOT registered, and that is a decision rather
 * than an omission: the lifecycle belongs to the process (main() calls them
 * around the run, exactly as it calls the crash reporter's), and a command that
 * could start a session on a running instance, or end one without the process
 * exiting, would be a way to make the marker lie about what happened. install()
 * is not registered for the same reason the crash reporter's is not: the working
 * directory is the process's, and there is exactly one.
 *
 * A16: the three writers are `irreversible` and each says so, naming the fallback
 * the engine leaves. This module is a plain C API over files outside the project
 * (nothing in it is a JournallingObject and no ProjectJournal checkpoint can hold
 * a marker), and the marker records ONE crashed session, which no command can
 * bring back once the file is gone. control.undo therefore FAILS, typed, naming
 * the fallback - the contract the crash reporter's two writers already have.
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

#include "ControlRegistry.h"

#include "ControlVocabulary.h"
#include "SafeStart.h"

namespace lmms
{

namespace
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

const QString GetStateName = QStringLiteral("safestart.get_state");
const QString AcknowledgeName = QStringLiteral("safestart.acknowledge");
const QString ClearName = QStringLiteral("safestart.clear");
const QString SetSkipName = QStringLiteral("safestart.set_skip");

QString wire(const std::string& text) { return QString::fromStdString(text); }

//! One of the module's own files, as it stands: its path, whether it is there,
//! how big it is and when it was last written. The shape the crash group's
//! report listing uses (src/core/ControlCommandsCrash.cpp: fileJson).
QJsonObject fileJson(const std::string& path)
{
	QJsonObject out;
	out.insert(QStringLiteral("path"), wire(path));
	if (path.empty())
	{
		out.insert(QStringLiteral("exists"), false);
		out.insert(QStringLiteral("bytes"), 0);
		out.insert(QStringLiteral("modified_unix"), 0);
		return out;
	}
	const QFileInfo info(wire(path));
	out.insert(QStringLiteral("exists"), info.exists());
	out.insert(QStringLiteral("bytes"), info.isFile() ? qint64(info.size()) : qint64(0));
	out.insert(QStringLiteral("modified_unix"),
		info.exists() ? qint64(info.lastModified().toSecsSinceEpoch()) : qint64(0));
	return out;
}

//! The "this instance has no safe-start module" refusal. The module is a plain
//! file API, so an instance whose working directory could not be opened simply
//! has no state at all; the read still answers and the writers refuse.
ControlResult noModule(const QString& command)
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1: safe-start mode is not installed in this instance (it has no working "
			"directory), so it holds no marker and nothing was written. main() installs it beside the "
			"crash reporter before this surface is reachable").arg(command));
}

/*! safestart.get_state - the whole state, from the module's own accessors: the
 *  marker and the acknowledgement as files (path, existence, size, time), the
 *  previous session's record, whether THIS session is a safe start, the
 *  session-scoped skip switch, every instance the mode skipped, the directories
 *  the third-party classification treats as this build's own, and the offer.
 *  Read-only, and it answers in every configuration.
 */
ControlResult handleGetState()
{
	const bool installed = safestart::isInstalled();
	const std::string marker = safestart::markerPath();
	const std::string acknowledged = safestart::acknowledgedPath();
	const safestart::SessionRecord previous = safestart::lastSession();

	QJsonArray own;
	for (const std::string& directory : safestart::ownPluginDirectories())
	{
		own.append(wire(directory));
	}

	QJsonArray skipped;
	for (const safestart::SkippedInstance& instance : safestart::skippedInstances())
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("plugin"), wire(instance.pluginName));
		entry.insert(QStringLiteral("file"), wire(instance.file));
		entry.insert(QStringLiteral("reason"), wire(instance.reason));
		skipped.append(entry);
	}

	const bool safeStart = safestart::safeStartActive();
	const bool markerPresent = safestart::markerExists();

	QJsonObject session;
	session.insert(QStringLiteral("present"), previous.present);
	session.insert(QStringLiteral("process_id"), qint64(previous.processId));
	session.insert(QStringLiteral("time_unix"), qint64(previous.unixTime));
	session.insert(QStringLiteral("project"), wire(previous.projectPath));

	// The offer of the normal start, as the state machine actually holds it: the
	// safe session is the one that can accept it, and accepting it is
	// safestart.acknowledge. There is no dialog in this release - the offer is
	// reported HERE and on stderr by main().
	QJsonObject offer;
	offer.insert(QStringLiteral("made"), safeStart);
	offer.insert(QStringLiteral("accepted"), safestart::acknowledged());
	offer.insert(QStringLiteral("restart_required"), safeStart);
	offer.insert(QStringLiteral("accept_with"), QStringLiteral("safestart.acknowledge"));
	offer.insert(QStringLiteral("decline_with"), QStringLiteral("safestart.clear"));
	offer.insert(QStringLiteral("text"), safeStart
		? QStringLiteral("The previous session ended unexpectedly, so this one started with THIRD-PARTY "
			"plugin instances skipped. Accept the offer (safestart.acknowledge) and the NEXT launch "
			"loads them again; safestart.clear drops the marker now; safestart.set_skip false loads "
			"them in THIS session.")
		: QStringLiteral("Nothing is offered: this session did not start safe."));

	QJsonObject bounds;
	bounds.insert(QStringLiteral("max_marker_bytes"), qint64(safestart::kMaxMarkerBytes));
	bounds.insert(QStringLiteral("max_project_path_bytes"), qint64(safestart::kMaxProjectPathBytes));

	QJsonObject result;
	result.insert(QStringLiteral("installed"), installed);
	result.insert(QStringLiteral("working_directory"), wire(safestart::workingDirectory()));
	result.insert(QStringLiteral("marker_path"), wire(marker));
	result.insert(QStringLiteral("acknowledgement_path"), wire(acknowledged));
	result.insert(QStringLiteral("marker"), fileJson(marker));
	result.insert(QStringLiteral("acknowledgement"), fileJson(acknowledged));
	result.insert(QStringLiteral("marker_present"), markerPresent);
	result.insert(QStringLiteral("acknowledged"), safestart::acknowledged());
	// The module's OWN predicate, not a re-derivation of it.
	result.insert(QStringLiteral("previous_run_exited_cleanly"),
		safestart::previousRunExitedCleanly());
	result.insert(QStringLiteral("safe_start"), safeStart);
	result.insert(QStringLiteral("safe_start_runs"),
		qint64(safestart::safeStartRunCount()));
	result.insert(QStringLiteral("skip_enabled"), safestart::skipEnabled());
	result.insert(QStringLiteral("skipped"), skipped);
	result.insert(QStringLiteral("skipped_count"), skipped.size());
	result.insert(QStringLiteral("own_plugin_directories"), own);
	result.insert(QStringLiteral("session"), session);
	result.insert(QStringLiteral("project_path"), wire(safestart::projectPath()));
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

//! The before-state of the writers: what the module's files held.
QJsonObject safeStartBeforeState(const std::string& marker, const std::string& acknowledged)
{
	QJsonObject out;
	out.insert(QStringLiteral("marker"), fileJson(marker));
	out.insert(QStringLiteral("acknowledgement"), fileJson(acknowledged));
	out.insert(QStringLiteral("safe_start"), safestart::safeStartActive());
	out.insert(QStringLiteral("skip_enabled"), safestart::skipEnabled());
	return out;
}

/*! safestart.acknowledge - accept the offer of the normal start.
 *  The engine operation is safestart::acknowledge(): it writes the
 *  acknowledgement the NEXT launch consumes.
 */
ControlResult handleAcknowledge()
{
	if (!safestart::isInstalled()) { return noModule(AcknowledgeName); }
	if (!safestart::safeStartActive() && !safestart::markerExists())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: there is no crash marker, so this session did not start safe and "
				"there is nothing to acknowledge (%2)").arg(AcknowledgeName,
				wire(safestart::markerPath())));
	}

	const QJsonObject before = safeStartBeforeState(safestart::markerPath(),
		safestart::acknowledgedPath());
	const bool wasSafe = safestart::safeStartActive();
	safestart::acknowledge();

	// The engine operation returns void, so the effect is READ BACK rather than
	// assumed: an acknowledgement that could not be written (a read-only working
	// directory) must not be reported as a decision that was recorded.
	const bool written = safestart::acknowledged();
	if (!written)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("%1: the acknowledgement at %2 could not be written, so the next launch "
				"will start safe again. Nothing else was changed")
				.arg(AcknowledgeName, wire(safestart::acknowledgedPath())));
	}

	QJsonObject result;
	result.insert(QStringLiteral("acknowledged"), true);
	result.insert(QStringLiteral("marker_path"), wire(safestart::markerPath()));
	result.insert(QStringLiteral("acknowledgement_path"), wire(safestart::acknowledgedPath()));
	result.insert(QStringLiteral("acknowledgement"), fileJson(safestart::acknowledgedPath()));
	result.insert(QStringLiteral("safe_start"), safestart::safeStartActive());
	result.insert(QStringLiteral("next_launch"), QStringLiteral("normal"));
	result.insert(QStringLiteral("accepted_a_safe_start"), wasSafe);
	result.insert(QStringLiteral("note"),
		QStringLiteral("The NEXT launch loads with third-party plugins enabled; this session is NOT "
			"changed by this call - instances already skipped were never created. The marker stays on "
			"disk (the crash is still there to look at) and a clean exit clears it."));
	result.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"), before},
			{QStringLiteral("reversible"), false},
			{QStringLiteral("mechanism"),
				QStringLiteral("none: this writes the reporter-style acknowledgement the next launch "
					"CONSUMES, and no function in this engine removes it except safestart.clear - which "
					"removes the marker with it, so it is not an inverse. FALLBACK: delete the file %1 "
					"by hand and the next launch starts safe again; the marker is untouched by this "
					"command").arg(wire(safestart::acknowledgedPath()))}});
	return ControlResult::success(result);
}

/*! safestart.clear - the marker cleared now, and this session out of safe-start
 *  mode. The engine operation is safestart::clear().
 */
ControlResult handleClear()
{
	if (!safestart::isInstalled()) { return noModule(ClearName); }

	const QJsonObject before = safeStartBeforeState(safestart::markerPath(),
		safestart::acknowledgedPath());
	const bool hadMarker = safestart::markerExists();
	const bool hadAcknowledged = safestart::acknowledged();
	const bool wasSafe = safestart::safeStartActive();
	if (!hadMarker && !hadAcknowledged)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: there is no crash marker and no acknowledgement to clear (%2), so "
				"nothing was written").arg(ClearName, wire(safestart::markerPath())));
	}

	safestart::clear();

	QJsonArray removed;
	if (hadMarker) { removed.append(wire(safestart::markerPath())); }
	if (hadAcknowledged) { removed.append(wire(safestart::acknowledgedPath())); }

	QJsonObject result;
	result.insert(QStringLiteral("cleared"), true);
	result.insert(QStringLiteral("removed"), removed);
	result.insert(QStringLiteral("removed_count"), removed.size());
	result.insert(QStringLiteral("marker"), fileJson(safestart::markerPath()));
	result.insert(QStringLiteral("acknowledgement"), fileJson(safestart::acknowledgedPath()));
	result.insert(QStringLiteral("marker_present"), safestart::markerExists());
	result.insert(QStringLiteral("safe_start"), safestart::safeStartActive());
	result.insert(QStringLiteral("skip_enabled"), safestart::skipEnabled());
	result.insert(QStringLiteral("was_safe_start"), wasSafe);
	result.insert(QStringLiteral("note"),
		QStringLiteral("The next launch is a normal one. THIS session also left safe-start mode, so a "
			"project loaded after this call loads its third-party plugins; instances skipped before "
			"it stay skipped, because they were never created and nothing here creates them "
			"retroactively."));
	result.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"), before},
			{QStringLiteral("reversible"), false},
			{QStringLiteral("mechanism"),
				QStringLiteral("none: the marker describes ONE crashed session (its pid, its time, the "
					"project that was open) and the file is gone; nothing in this engine writes a "
					"marker from a caller's bytes - beginSession() composes one from the process it is "
					"running in. FALLBACK: the crash itself is still recoverable - crash.list_reports "
					"names the report the signal handler wrote, and the reporter's own session marker "
					"still says the last run was unclean")}});
	return ControlResult::success(result);
}

/*! safestart.set_skip - the session-scoped half of the load-time predicate.
 *  The engine operation is safestart::setSkipEnabled().
 */
ControlResult handleSetSkip(const QJsonObject& args)
{
	if (!safestart::isInstalled()) { return noModule(SetSkipName); }
	const QJsonValue value = args.value(QStringLiteral("enabled"));
	if (value.isUndefined() || !value.isBool())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1: 'enabled' is required and must be a boolean (true skips third-party "
				"plugin instances at load time, false loads them)").arg(SetSkipName));
	}

	const bool enabled = value.toBool();
	const bool before = safestart::skipEnabled();
	safestart::setSkipEnabled(enabled);

	QJsonObject result;
	result.insert(QStringLiteral("skip_enabled"), safestart::skipEnabled());
	result.insert(QStringLiteral("was"), before);
	result.insert(QStringLiteral("changed"), before != enabled);
	result.insert(QStringLiteral("safe_start"), safestart::safeStartActive());
	result.insert(QStringLiteral("marker_present"), safestart::markerExists());
	result.insert(QStringLiteral("note"),
		QStringLiteral("This is the SESSION's switch, not the marker's: with it off, a project loaded "
			"now loads its third-party plugin instances, while the marker stays on disk and the next "
			"launch still starts safe. beginSession() re-arms it for the next run. Instances that were "
			"already skipped are not created by this call."));
	result.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"),
				QJsonObject{{QStringLiteral("skip_enabled"), before}}},
			{QStringLiteral("reversible"), false},
			{QStringLiteral("mechanism"),
				QStringLiteral("none: the switch is a bool on this module's process state - no model, "
					"no song, no journal checkpoint - and setting it back is not an inverse of what "
					"already happened, because instances skipped while it was on were never created. "
					"FALLBACK: pass 'enabled': false and load the project again; the instances load "
					"then. The state is reported by safestart.get_state and re-armed by the next "
					"session")}});
	return ControlResult::success(result);
}

} // namespace

void registerSafeStartCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = GetStateName;
		cmd.group = QStringLiteral("safestart");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("Safe-start mode's state: whether the module is installed, "
			"the crash marker and the acknowledgement as files (path, existence, size, time), whether "
			"a marker says the previous run did not exit cleanly, whether THIS session started safe, "
			"how many sessions in a row have, the session-scoped skip switch, every plugin instance "
			"the mode skipped and why, the directories the third-party classification treats as this "
			"build's own, the previous session's own record and the offer of a normal start. "
			"Read-only, and it answers in every configuration.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("installed"), booleanProperty()},
			{QStringLiteral("working_directory"), stringProperty()},
			{QStringLiteral("marker_path"), stringProperty()},
			{QStringLiteral("acknowledgement_path"), stringProperty()},
			// {path, exists, bytes, modified_unix}
			{QStringLiteral("marker"), objectProperty()},
			{QStringLiteral("acknowledgement"), objectProperty()},
			{QStringLiteral("marker_present"), booleanProperty()},
			{QStringLiteral("acknowledged"), booleanProperty()},
			{QStringLiteral("previous_run_exited_cleanly"), booleanProperty()},
			{QStringLiteral("safe_start"), booleanProperty()},
			{QStringLiteral("safe_start_runs"), integerProperty()},
			{QStringLiteral("skip_enabled"), booleanProperty()},
			{QStringLiteral("skipped"), arrayProperty()},
			{QStringLiteral("skipped_count"), integerProperty()},
			{QStringLiteral("own_plugin_directories"), arrayProperty()},
			// {present, process_id, time_unix, project}
			{QStringLiteral("session"), objectProperty()},
			QStringLiteral("project_path"), stringProperty()},
			// {made, accepted, restart_required, accept_with, decline_with, text}
			{QStringLiteral("offer"), objectProperty()},
			// {max_marker_bytes, max_project_path_bytes}
			{QStringLiteral("bounds"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleGetState(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = AcknowledgeName;
		cmd.group = QStringLiteral("safestart");
		cmd.verb = QStringLiteral("acknowledge");
		cmd.description = QStringLiteral("Accept the offer of a normal start: write the acknowledgement "
			"the NEXT launch consumes, so it loads with third-party plugins enabled. Keeps the marker "
			"and does not change this session (instances already skipped were never created). Refuses "
			"when there is no marker. Not reversible - no function in this engine removes the "
			"acknowledgement except safestart.clear, which removes the marker with it.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("acknowledged"), booleanProperty()},
			{QStringLiteral("marker_path"), stringProperty()},
			{QStringLiteral("acknowledgement_path"), stringProperty()},
			{QStringLiteral("acknowledgement"), objectProperty()},
			{QStringLiteral("safe_start"), booleanProperty()},
			{QStringLiteral("next_launch"), stringProperty()},
			{QStringLiteral("accepted_a_safe_start"), booleanProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleAcknowledge(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = ClearName;
		cmd.group = QStringLiteral("safestart");
		cmd.verb = QStringLiteral("clear");
		cmd.description = QStringLiteral("Clear the crash marker: delete the marker and the "
			"acknowledgement, so the next launch is a normal one, and leave safe-start mode in this "
			"session. The verb for 'the cause is known and fixed'. Refuses when there is neither file, "
			"so a call that would change nothing writes nothing. Not reversible - the marker's own "
			"record of the crashed session is gone; control.undo names the fallback.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("cleared"), booleanProperty()},
			{QStringLiteral("removed"), arrayProperty()},
			{QStringLiteral("removed_count"), integerProperty()},
			{QStringLiteral("marker"), objectProperty()},
			{QStringLiteral("acknowledgement"), objectProperty()},
			{QStringLiteral("marker_present"), booleanProperty()},
			{QStringLiteral("safe_start"), booleanProperty()},
			{QStringLiteral("skip_enabled"), booleanProperty()},
			{QStringLiteral("was_safe_start"), booleanProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleClear(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = SetSkipName;
		cmd.group = QStringLiteral("safestart");
		cmd.verb = QStringLiteral("set_skip");
		cmd.description = QStringLiteral("Turn the load-time skip of third-party plugin INSTANCES on "
			"or off for THIS session - the session-scoped half of the predicate Plugin::instantiate "
			"consults. Does not touch the marker: with it off, a project loaded now loads its "
			"third-party plugins when it is opened (already-skipped instances are not created by this "
			"call). Re-armed by the next session. Not reversible - process-scoped mode state, no "
			"journal checkpoint, and nothing puts an already-skipped instance back.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
		}, {QStringLiteral("enabled")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("skip_enabled"), booleanProperty()},
			{QStringLiteral("was"), booleanProperty()},
			{QStringLiteral("changed"), booleanProperty()},
			{QStringLiteral("safe_start"), booleanProperty()},
			{QStringLiteral("marker_present"), booleanProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handleSetSkip(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
