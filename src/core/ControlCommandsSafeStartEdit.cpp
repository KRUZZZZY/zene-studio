/*
 * ControlCommandsSafeStartEdit.cpp - the EDIT half of the safestart.* surface
 *                                    (SPEC A11-A16).
 *
 * Feature row 77 of docs/FEATURE-LIST-0.3.0.md ("Safe-start mode after a crash -
 * launch with third-party plugins disabled", OWNER-31 item 31, board task #666).
 * The engine half is include/SafeStart.h; the READ half and the pair's shared
 * vocabulary are ControlCommandsSafeStart.cpp / ControlCommandsSafeStartShared.h.
 *
 * THE THREE WRITERS ARE THE STATE MACHINE'S OWN, nothing more:
 *   * safestart.acknowledge - accept the offer of a normal start. Writes the
 *     acknowledgement file the NEXT launch consumes and keeps the marker, so the
 *     crash is still there to look at. It does NOT un-skip anything already
 *     loaded, because instances that were skipped were never created.
 *   * safestart.clear - the marker cleared now, and this session out of
 *     safe-start mode. The verb for "the cause is known and fixed".
 *   * safestart.set_skip - the SESSION-SCOPED half of the load-time predicate:
 *     with it false, a project loaded now loads its third-party plugins, while
 *     the marker stays on disk and the mode stays reported. It is what an operator
 *     reaches for when the blamed plugin is known and they want it back without
 *     restarting.
 *
 * A16: all three are IRREVERSIBLE and each says so, naming the fallback the
 * engine leaves (src/core/ControlReversibilityTableSafeStart.cpp argues every
 * row). This module is a plain C API over files outside the project - nothing in
 * it is a JournallingObject and no ProjectJournal checkpoint can hold a marker or
 * a session switch - and no command writes a crash marker from a caller's bytes,
 * so a cleared marker cannot be put back. control.undo therefore FAILS, typed,
 * naming the fallback, which is the contract the crash reporter's two writers
 * already have.
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

/*! safestart.acknowledge - accept the offer of the normal start.
 *  The engine operation is safestart::acknowledge(): it writes the
 *  acknowledgement the NEXT launch consumes.
 */
ControlResult safeStartHandleAcknowledge()
{
	if (!isInstalled()) { return safeStartNoModule(SafeStartAcknowledgeId); }
	if (!safeStartActive() && !markerExists())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: there is no crash marker, so this session did not start safe and "
				"there is nothing to acknowledge (%2)").arg(SafeStartAcknowledgeId,
				safeStartWire(markerPath())));
	}

	const QJsonObject before = safeStartBeforeState(markerPath(), acknowledgedPath());
	const bool wasSafe = safeStartActive();
	acknowledge();

	// The engine operation returns void, so the effect is READ BACK rather than
	// assumed: an acknowledgement that could not be written (a read-only working
	// directory) must not be reported as a decision that was recorded.
	if (!acknowledged())
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("%1: the acknowledgement at %2 could not be written, so the next launch "
				"will start safe again. Nothing else was changed")
				.arg(SafeStartAcknowledgeId, safeStartWire(acknowledgedPath())));
	}

	QJsonObject result;
	result.insert(QStringLiteral("acknowledged"), true);
	result.insert(QStringLiteral("marker_path"), safeStartWire(markerPath()));
	result.insert(QStringLiteral("acknowledgement_path"), safeStartWire(acknowledgedPath()));
	result.insert(QStringLiteral("acknowledgement"), safeStartFileJson(acknowledgedPath()));
	result.insert(QStringLiteral("safe_start"), safeStartActive());
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
				QStringLiteral("none: this writes the acknowledgement the next launch CONSUMES, and no "
					"function in this engine removes it except safestart.clear - which removes the "
					"marker with it, so it is not an inverse. FALLBACK: delete the file %1 by hand and "
					"the next launch starts safe again; the marker is untouched by this command")
					.arg(safeStartWire(acknowledgedPath()))}});
	return ControlResult::success(result);
}

/*! safestart.clear - the marker cleared now, and this session out of safe-start
 *  mode. The engine operation is safestart::clear().
 */
ControlResult safeStartHandleClear()
{
	if (!isInstalled()) { return safeStartNoModule(SafeStartClearId); }

	const QJsonObject before = safeStartBeforeState(markerPath(), acknowledgedPath());
	const bool hadMarker = markerExists();
	const bool hadAcknowledged = acknowledged();
	const bool wasSafe = safeStartActive();
	if (!hadMarker && !hadAcknowledged)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: there is no crash marker and no acknowledgement to clear (%2), so "
				"nothing was written").arg(SafeStartClearId, safeStartWire(markerPath())));
	}

	clear();

	QJsonArray removed;
	if (hadMarker) { removed.append(safeStartWire(markerPath())); }
	if (hadAcknowledged) { removed.append(safeStartWire(acknowledgedPath())); }

	QJsonObject result;
	result.insert(QStringLiteral("cleared"), true);
	result.insert(QStringLiteral("removed"), removed);
	result.insert(QStringLiteral("removed_count"), removed.size());
	result.insert(QStringLiteral("marker"), safeStartFileJson(markerPath()));
	result.insert(QStringLiteral("acknowledgement"), safeStartFileJson(acknowledgedPath()));
	result.insert(QStringLiteral("marker_present"), markerExists());
	result.insert(QStringLiteral("safe_start"), safeStartActive());
	result.insert(QStringLiteral("skip_enabled"), skipEnabled());
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
ControlResult safeStartHandleSetSkip(const QJsonObject& args)
{
	if (!isInstalled()) { return safeStartNoModule(SafeStartSetSkipId); }
	const QJsonValue value = args.value(QStringLiteral("enabled"));
	if (value.isUndefined() || !value.isBool())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1: 'enabled' is required and must be a boolean (true skips third-party "
				"plugin instances at load time, false loads them)").arg(SafeStartSetSkipId));
	}

	const bool enabled = value.toBool();
	const bool before = skipEnabled();
	setSkipEnabled(enabled);

	QJsonObject result;
	result.insert(QStringLiteral("skip_enabled"), skipEnabled());
	result.insert(QStringLiteral("was"), before);
	result.insert(QStringLiteral("changed"), before != enabled);
	result.insert(QStringLiteral("safe_start"), safeStartActive());
	result.insert(QStringLiteral("marker_present"), markerExists());
	result.insert(QStringLiteral("note"),
		QStringLiteral("This is the SESSION's switch, not the marker's: with it off, a project loaded "
			"now loads its third-party plugin instances, while the marker stays on disk and the next "
			"launch still starts safe. beginSession() re-arms it for the next run. Instances that were "
			"already skipped are not created by this call."));
	QJsonObject switchBefore;
	switchBefore.insert(QStringLiteral("skip_enabled"), before);
	result.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"), switchBefore},
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

void registerSafeStartEditCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = SafeStartAcknowledgeId;
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
		cmd.handler = [](const QJsonObject&) { return safeStartHandleAcknowledge(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = SafeStartClearId;
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
		cmd.handler = [](const QJsonObject&) { return safeStartHandleClear(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = SafeStartSetSkipId;
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
		cmd.handler = [](const QJsonObject& args) { return safeStartHandleSetSkip(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace control
} // namespace lmms
