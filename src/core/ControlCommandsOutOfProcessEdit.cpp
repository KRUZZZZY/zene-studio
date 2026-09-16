/*
 * ControlCommandsOutOfProcessEdit.cpp - the WRITE half of the `oop.*` surface
 *                                       (SPEC A11-A16)
 *
 * Feature row 80 of docs/FEATURE-LIST-0.3.0.md (board card #670). Three verbs:
 *
 *   oop.set_mode       choose in-process / separate-process for ONE device, the
 *                      call the instrument view's checkbox and a project's
 *                      `separateprocess` attribute both end in
 *                      (ZynAddSubFxInstrument::setHostingMode);
 *   oop.restart        re-host ONE device (a fresh client process), through the
 *                      plugin's own reload path;
 *   oop.reset_crashes  clear this session's crash count for a client executable,
 *                      which is what lifts a crash-loop refusal.
 *
 * EVERY REFUSAL IS TYPED AND NAMES THE EXACT MISSING STEP. There are four of
 * them and they are the honest half of this row: a family with no client
 * executable in this build (`refused-no-client`, the family's own reason
 * quoted), a family whose only path is already a client process and so cannot
 * go in-process, a device whose plugin does not implement the invokable
 * convention (the missing half named by its own name), and a client executable
 * that has died maxCrashesPerClient() times this session (`refused-crash-loop`,
 * the count and the last exit code quoted). None of them is a silent fallback.
 *
 * NOTHING HERE RESTARTS A SLOT BY ITSELF. A client that dies stays dead until a
 * caller asks: auto-restart is how a crash loop turns into a storm, and the
 * engine's own zero-fill on a failed plugin (RemotePlugin::process()) already
 * makes the slot silent rather than wrong. That is a decision, not a gap, and
 * docs/KNOWN-LIMITATIONS.md carries it.
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

#include <QJsonArray>

#include "ControlCommandsOutOfProcessShared.h"

#include "ControlVocabulary.h"
#include "Plugin.h"

namespace lmms
{

namespace control
{

using namespace lmms::oop;

namespace
{

//! The mode the device plugin is in RIGHT NOW, as one of the two mode names.
//! The plugin's own answer when it implements the convention; "in-process"
//! otherwise, because a family with no client executable has nothing else.
QString modeOf(const HostedDevice& device)
{
	return deviceHostingState(device) == HostingModeSeparate
		? HostingModeSeparate
		: HostingModeInProcess;
}

//! The refusal for a device whose plugin does not implement the convention.
//! Names the missing member and what IS available for the family, so the reader
//! knows whether the gap is the build or the plugin.
ControlResult missingConvention(const HostedDevice& device, const QString& what)
{
	const Family family = familyFor(device.pluginKey);
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("oop.%1: the plugin behind %2/%3 (%4) does not implement %5, so its hosting "
			"cannot be changed from here. Family: %6 (availability: %7%8). This is the missing half, "
			"not a missing family")
			.arg(what, device.targetId, device.deviceId, device.pluginKey,
				QString::fromLatin1(SetHostingModeInvokable))
			.arg(family.label, availabilityName(family.availability),
				family.client.isEmpty() ? QString()
					: QStringLiteral(", client: %1").arg(family.client)));
}

//! The refusal for a family this build cannot host out of process at all.
ControlResult noClientRefusal(const HostedDevice& device, const Family& family, const QString& verb)
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("oop.%1: %2 (%3/%4) cannot run in a separate process in this build: %5. The "
			"device keeps running in this process - nothing was changed")
			.arg(verb, device.pluginLabel, device.targetId, device.deviceId, family.reason));
}

ControlResult crashLoopRefusal(const QString& reason, const QString& verb)
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("oop.%1 refused: %2").arg(verb, reason));
}

/*! The `__transaction` block every writer reports (SPEC A16). None of the three
 *  is reversible: there is no stored checkpoint behind any of them - the mode is
 *  a property of a plugin instance that the switch itself destroys and
 *  re-creates, the crash count is a measurement, and a restart is an event. Each
 *  names the call that returns the state where one exists, which is the
 *  convention the safestart.* writers already use.
 */
QJsonObject transactionBlock(const QString& command, QJsonObject before, const QString& mechanism)
{
	QJsonObject out;
	out.insert(QStringLiteral("before"), before);
	out.insert(QStringLiteral("inverse"), QJsonObject());
	out.insert(QStringLiteral("reversible"), false);
	out.insert(QStringLiteral("mechanism"), mechanism);
	out.insert(QStringLiteral("command"), command);
	return out;
}

/*! oop.set_mode - choose the hosting mode of ONE device.
 *
 *  The decision order is fixed and each step is a different sentence: the mode
 *  argument, the family's own allowance, the crash record, the plugin's
 *  implementation. Only when all four agree does anything move.
 */
ControlResult handleSetMode(const QJsonObject& args)
{
	HostedDevice device;
	ControlResult error;
	if (!resolveHostedDevice(args, &device, &error)) { return error; }

	const QString requested = args.value(QStringLiteral("mode")).toString();
	if (requested != HostingModeInProcess && requested != HostingModeSeparate)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("oop.set_mode: 'mode' must be '%1' or '%2' (got '%3')")
				.arg(HostingModeInProcess, HostingModeSeparate, requested));
	}

	const Family family = familyFor(device.pluginKey);
	const bool wantSeparate = requested == HostingModeSeparate;

	if (wantSeparate)
	{
		if (family.availability == Availability::NoClientInBuild)
		{
			return noClientRefusal(device, family, QStringLiteral("set_mode"));
		}
		QString refusal;
		if (HostTracker::instance().refusedByCrashLoop(family.client, &refusal))
		{
			return crashLoopRefusal(refusal, QStringLiteral("set_mode"));
		}
	}
	else if (family.availability == Availability::AlwaysSeparate)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("oop.set_mode: %1 (%2/%3) cannot go in-process in this build: %4")
				.arg(device.pluginLabel, device.targetId, device.deviceId, family.reason));
	}

	if (!deviceCanChooseHostingMode(device))
	{
		return missingConvention(device, QStringLiteral("set_mode"));
	}

	const QString modeBefore = modeOf(device);
	bool changed = false;
	if (!chooseDeviceHostingMode(device, wantSeparate, &changed))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("oop.set_mode: %1/%2 implements %3 but the call was not delivered")
				.arg(device.targetId, device.deviceId, QString::fromLatin1(SetHostingModeInvokable)));
	}

	QJsonObject hosting = hostedDeviceJson(device).value(QStringLiteral("hosting")).toObject();
	QJsonObject result;
	result.insert(QStringLiteral("target"), device.targetId);
	result.insert(QStringLiteral("device"), device.deviceId);
	result.insert(QStringLiteral("plugin"), device.pluginKey);
	result.insert(QStringLiteral("mode_requested"), requested);
	result.insert(QStringLiteral("mode_before"), modeBefore);
	result.insert(QStringLiteral("mode_after"), modeOf(device));
	result.insert(QStringLiteral("changed"), changed);
	result.insert(QStringLiteral("hosting"), hosting);
	result.insert(QStringLiteral("note"),
		QStringLiteral("The choice is per instance and is STORED (ZynAddSubFx keeps it as the "
			"`separateprocess` attribute of its element), so a saved and reloaded project comes back in "
			"the mode this call chose - oop.set_mode and the project file write the same value. A call "
			"that asks for the mode already in force changes nothing and says so with changed=false."));

	QJsonObject before;
	before.insert(QStringLiteral("mode"), modeBefore);
	result.insert(QStringLiteral("__transaction"),
		transactionBlock(OutOfProcessSetModeId, before,
			QStringLiteral("none: no checkpoint. The mode is a property of a plugin INSTANCE that the "
				"switch itself destroys and re-creates, so there is nothing stored to put back. "
				"FALLBACK: oop.set_mode with 'mode': \"%1\" - the recorded mode_before - which re-hosts "
				"the instance the other way (the patch is carried over by the plugin's own reload). The "
				"transaction records mode_before so a caller can see exactly what moved")
				.arg(modeBefore)));
	return ControlResult::success(result);
}

/*! oop.restart - re-host ONE device: a fresh client process through the plugin's
 *  own reload path. Refused for a family with no client (nothing to restart) and
 *  for a client executable that is already in a crash loop, which is the whole
 *  point of the accounting.
 */
ControlResult handleRestart(const QJsonObject& args)
{
	HostedDevice device;
	ControlResult error;
	if (!resolveHostedDevice(args, &device, &error)) { return error; }

	const Family family = familyFor(device.pluginKey);
	if (family.availability != Availability::AlwaysSeparate && family.client.isEmpty())
	{
		return noClientRefusal(device, family, QStringLiteral("restart"));
	}

	QString refusal;
	if (HostTracker::instance().refusedByCrashLoop(family.client, &refusal))
	{
		return crashLoopRefusal(refusal, QStringLiteral("restart"));
	}

	const State stateBefore = hostedDeviceState(device);
	const qint64 pidBefore = deviceHostingProcessId(device);

	if (device.plugin() == nullptr || device.plugin()->metaObject()->indexOfMethod(
			QMetaObject::normalizedSignature("reloadPlugin()").constData()) < 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("oop.restart: the plugin behind %1/%2 (%3) does not implement %4, so this "
				"build has no way to re-host it. The in-process path is unaffected and the device keeps "
				"running as it is; this is the missing half named rather than a restart that did nothing")
				.arg(device.targetId, device.deviceId, device.pluginKey,
					QString::fromLatin1(ReloadInvokable)));
	}

	if (!reloadHostedDevice(device))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("oop.restart: %1/%2 delivered %3 but the plugin refused it (it reports no "
				"change, e.g. the in-process mode has nothing to re-host)")
				.arg(device.targetId, device.deviceId, QString::fromLatin1(ReloadInvokable)));
	}

	HostTracker::instance().noteRestart(family.client);

	const qint64 pidAfter = deviceHostingProcessId(device);
	QJsonObject result;
	result.insert(QStringLiteral("target"), device.targetId);
	result.insert(QStringLiteral("device"), device.deviceId);
	result.insert(QStringLiteral("plugin"), device.pluginKey);
	result.insert(QStringLiteral("client"), family.client);
	result.insert(QStringLiteral("state_before"), stateName(stateBefore));
	result.insert(QStringLiteral("state_after"), stateName(hostedDeviceState(device)));
	result.insert(QStringLiteral("process_id_before"), pidBefore);
	result.insert(QStringLiteral("process_id_after"), pidAfter);
	result.insert(QStringLiteral("restarted"), pidAfter > 0 && pidAfter != pidBefore);
	result.insert(QStringLiteral("record"), clientRecordJson(
		HostTracker::instance().record(family.client)));
	result.insert(QStringLiteral("note"),
		QStringLiteral("A restart is the plugin's own reload: the instance is re-created and its patch "
			"is carried over. The client is a CHILD of this process either way, so a client that dies "
			"again is counted again - at the bound oop.restart refuses instead of feeding the loop."));

	QJsonObject before;
	before.insert(QStringLiteral("state"), stateName(stateBefore));
	before.insert(QStringLiteral("process_id"), pidBefore);
	result.insert(QStringLiteral("__transaction"),
		transactionBlock(OutOfProcessRestartId, before,
			QStringLiteral("none: a restart is an EVENT - the previous client process is gone and its pid "
				"cannot be brought back. FALLBACK: none, and none is needed: the state before the call "
				"is recorded here (state_before, process_id_before) and the client that died was already "
				"dead, which is why the call was made")));
	return ControlResult::success(result);
}

/*! oop.reset_crashes - clear this session's crash count for one client
 *  executable, or for every one of them. This is the inverse of nothing: it
 *  lifts a refusal, it does not undo a crash, and the exits/exits-code history
 *  is deliberately left in place so the record still says what happened.
 */
ControlResult handleResetCrashes(const QJsonObject& args)
{
	const QString client = args.value(QStringLiteral("client")).toString();
	HostTracker& tracker = HostTracker::instance();

	QJsonArray beforeArray;
	QJsonArray afterArray;
	QJsonArray cleared;

	if (client.isEmpty())
	{
		for (const ClientRecord& record : tracker.records())
		{
			beforeArray.append(clientRecordJson(record));
		}
		tracker.resetAll();
		for (const ClientRecord& record : tracker.records())
		{
			afterArray.append(clientRecordJson(record));
			if (record.crashes == 0 && record.exits > 0) { cleared.append(record.client); }
		}
	}
	else
	{
		const ClientRecord before = tracker.record(client);
		beforeArray.append(clientRecordJson(before));
		tracker.resetCrashes(client);
		const ClientRecord after = tracker.record(client);
		afterArray.append(clientRecordJson(after));
		if (after.crashes == 0 && after.exits > 0) { cleared.append(client); }
	}

	QJsonObject result;
	result.insert(QStringLiteral("client"), client);
	result.insert(QStringLiteral("scope"), client.isEmpty()
		? QStringLiteral("every client executable this session has started")
		: QStringLiteral("one client executable"));
	result.insert(QStringLiteral("cleared"), cleared);
	result.insert(QStringLiteral("cleared_count"), cleared.size());
	result.insert(QStringLiteral("before"), beforeArray);
	result.insert(QStringLiteral("after"), afterArray);
	result.insert(QStringLiteral("bounds"), QJsonObject{
		{QStringLiteral("max_crashes_per_client"), HostTracker::maxCrashesPerClient()}});
	result.insert(QStringLiteral("note"),
		QStringLiteral("The count is per CLIENT EXECUTABLE, not per instance, because re-instantiating "
			"the plugin is what a crash loop does and an instance-keyed count could be cleared by the "
			"very reload the crash triggers. This call is the only thing that lifts a crash-loop "
			"refusal; exits and the last exit code stay in the record, so the crash history is still "
			"readable afterwards."));

	QJsonObject before;
	before.insert(QStringLiteral("client"), client);
	before.insert(QStringLiteral("records"), beforeArray);
	result.insert(QStringLiteral("__transaction"),
		transactionBlock(OutOfProcessResetCrashesId, before,
			QStringLiteral("none: the count is a MEASUREMENT of this session, not project state, and a "
				"count that was cleared cannot be un-cleared - the crashes really happened. FALLBACK: "
				"none. The counts before the call are recorded here and oop.get_state's `clients` block "
				"reports the (still counted) exits and the last exit code")));
	return ControlResult::success(result);
}

} // namespace

void registerOutOfProcessEditCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = OutOfProcessSetModeId;
		cmd.group = QStringLiteral("oop");
		cmd.verb = QStringLiteral("set_mode");
		cmd.description = QStringLiteral("Choose the hosting mode of ONE device: 'in-process' (the "
			"plugin's own code in this process) or 'separate-process' (a client process, so a crash in "
			"it cannot take this one down). 'target' is trk-<n>/ch-<n> and 'plugin' is fx-<n> or 'inst'. "
			"Refuses with a typed reason when the family ships no client executable in this build, when "
			"the family has no in-process path to fall back to, when the plugin does not implement "
			"setHostingMode, or when that client executable is in a crash loop. The choice is stored, so "
			"it survives save and reload.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("plugin"), stringProperty()},
			{QStringLiteral("mode"), stringProperty()},
		}, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("mode")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("device"), stringProperty()},
			{QStringLiteral("plugin"), stringProperty()},
			{QStringLiteral("mode_requested"), stringProperty()},
			{QStringLiteral("mode_before"), stringProperty()},
			{QStringLiteral("mode_after"), stringProperty()},
			{QStringLiteral("changed"), booleanProperty()},
			{QStringLiteral("hosting"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handleSetMode(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = OutOfProcessRestartId;
		cmd.group = QStringLiteral("oop");
		cmd.verb = QStringLiteral("restart");
		cmd.description = QStringLiteral("Re-host ONE device: a fresh client process through the "
			"plugin's own reload path, which is what a slot that lost its client needs. Reports the "
			"state and the pid before and after. Refused when the family ships no client executable, "
			"when the plugin implements no reload, and when that client executable is in a crash loop "
			"(the count is in the refusal) - a restart never feeds a crash loop.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("plugin"), stringProperty()},
		}, {QStringLiteral("target"), QStringLiteral("plugin")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("device"), stringProperty()},
			{QStringLiteral("plugin"), stringProperty()},
			{QStringLiteral("client"), stringProperty()},
			{QStringLiteral("state_before"), stringProperty()},
			{QStringLiteral("state_after"), stringProperty()},
			{QStringLiteral("process_id_before"), integerProperty()},
			{QStringLiteral("process_id_after"), integerProperty()},
			{QStringLiteral("restarted"), booleanProperty()},
			{QStringLiteral("record"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handleRestart(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = OutOfProcessResetCrashesId;
		cmd.group = QStringLiteral("oop");
		cmd.verb = QStringLiteral("reset_crashes");
		cmd.description = QStringLiteral("Clear this session's crash count for one client executable "
			"('client'), or for every one this session has started (no argument). This is the only "
			"thing that lifts a crash-loop refusal; it does not undo a crash, and the exits and the last "
			"exit code stay in the record. The count is per client executable, not per plugin instance.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("client"), stringProperty()},
		});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("client"), stringProperty()},
			{QStringLiteral("scope"), stringProperty()},
			{QStringLiteral("cleared"), arrayProperty()},
			{QStringLiteral("cleared_count"), integerProperty()},
			{QStringLiteral("before"), arrayProperty()},
			{QStringLiteral("after"), arrayProperty()},
			{QStringLiteral("bounds"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handleResetCrashes(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace control

} // namespace lmms
