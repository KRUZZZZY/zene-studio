/*
 * ControlCommandsOutOfProcessSupport.cpp - the helpers the `oop.*` group's two
 *                                          command translation units share
 *                                          (feature row 80, board card #670)
 *
 * Split out of ControlCommandsOutOfProcess.cpp because that file crossed the
 * 500-line file ratchet the moment the device resolution, the four-source state
 * resolution and the wire shapes were in it - and the seam is real rather than
 * arithmetic: this file answers "what is this device's hosting, and how is one
 * addressed", the two command files answer "what does a verb do with that
 * answer". Nothing here is registered; every declaration it defines is in
 * ControlCommandsOutOfProcessShared.h.
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

#include <QJsonObject>

#include "ControlCommandsOutOfProcessShared.h"

#include "ControlDeviceSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Plugin.h"
#include "RemotePlugin.h"

namespace lmms
{

namespace control
{

using namespace lmms::oop;

namespace
{

//! The invokable-method probes. Each one asks the meta-object and answers "does
//! this plugin implement the convention" (include/OutOfProcessHosting.h), so a
//! plugin that implements it is drivable with nothing in this repository
//! changing - and one that does not is refused by NAME rather than guessed at.
bool hasInvokable(Plugin* plugin, const char* name)
{
	return plugin != nullptr && plugin->metaObject()->indexOfMethod(
		QMetaObject::normalizedSignature(name).constData()) >= 0;
}

QString invokeHostingState(Plugin* plugin)
{
	if (!hasInvokable(plugin, "hostingState()")) { return {}; }
	QString state;
	if (!QMetaObject::invokeMethod(plugin, HostingStateInvokable, Q_RETURN_ARG(QString, state)))
	{
		return {};
	}
	return state;
}

qint64 invokeHostingProcessId(Plugin* plugin)
{
	if (!hasInvokable(plugin, "hostingProcessId()")) { return 0; }
	qint64 pid = 0;
	if (!QMetaObject::invokeMethod(plugin, "hostingProcessId", Q_RETURN_ARG(qint64, pid)))
	{
		return 0;
	}
	return pid;
}

QJsonObject familyObject(const Family& family)
{
	QJsonObject out;
	out.insert(QStringLiteral("key"), family.key);
	out.insert(QStringLiteral("label"), family.label);
	out.insert(QStringLiteral("availability"), availabilityName(family.availability));
	out.insert(QStringLiteral("client"), family.client);
	out.insert(QStringLiteral("reason"), family.reason);
	return out;
}

//! The client record this device's family keeps, or a default one when the
//! family has no client executable at all.
ClientRecord familyRecord(const Family& family)
{
	return family.client.isEmpty()
		? ClientRecord{}
		: HostTracker::instance().record(family.client);
}

} // namespace

Plugin* HostedDevice::plugin() const
{
	return effect != nullptr ? static_cast<Plugin*>(effect) : static_cast<Plugin*>(instrument);
}

QString outOfProcessNote()
{
	return QStringLiteral("Out-of-process hosting is a per-family property of the BUILD, not a wish: "
		"`oop.list_families` reports what each family's story is here, and a family with no client "
		"executable is refused with that reason rather than silently run in-process. A client that dies "
		"does not take this process down (RemotePlugin zero-fills the slot's output planes); the death is "
		"counted under the CLIENT EXECUTABLE's name, and at the bound `oop.set_mode`/`oop.restart` refuse "
		"that family's out-of-process path until oop.reset_crashes clears it. No auto-restart: a slot that "
		"lost its client stays silent until a caller asks for it, which is the honest behaviour for a "
		"plugin that is crashing.");
}

bool resolveHostedDevice(const QJsonObject& args, HostedDevice* device, ControlResult* error)
{
	const QString targetId = args.value(QStringLiteral("target")).toString();
	const QString deviceId = args.value(QStringLiteral("plugin")).toString();

	ControlTarget target;
	if (!resolveControlTarget(targetId, &target, error)) { return false; }

	device->targetId = target.id;
	device->deviceId = deviceId;

	if (deviceId == QLatin1String("inst"))
	{
		if (target.instrumentTrack == nullptr || target.instrumentTrack->instrument() == nullptr)
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("target %1 carries no instrument").arg(target.id));
			return false;
		}
		device->isInstrument = true;
		device->instrument = target.instrumentTrack->instrument();
	}
	else if ((device->effect = resolveControlEffect(target, deviceId, error)) == nullptr)
	{
		return false;
	}

	Plugin* plugin = device->plugin();
	if (plugin == nullptr || plugin->descriptor() == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1/%2 has no plugin behind it").arg(target.id, deviceId));
		return false;
	}
	device->pluginKey = QString::fromUtf8(plugin->descriptor()->name);
	device->pluginLabel = QString::fromUtf8(plugin->descriptor()->displayName);
	return true;
}

QString deviceHostingState(const HostedDevice& device)
{
	return invokeHostingState(device.plugin());
}

qint64 deviceHostingProcessId(const HostedDevice& device)
{
	const qint64 invited = invokeHostingProcessId(device.plugin());
	if (invited != 0) { return invited; }

	// A device whose plugin IS a RemotePlugin (the VST2 halves) has no need of
	// the convention: its client is the plugin's own QProcess.
	if (auto* remote = dynamic_cast<RemotePlugin*>(device.plugin()))
	{
		return remote->isRunning() ? remote->hostProcessId() : 0;
	}
	return 0;
}

bool deviceCanChooseHostingMode(const HostedDevice& device)
{
	return hasInvokable(device.plugin(), "setHostingMode(bool)");
}

bool chooseDeviceHostingMode(const HostedDevice& device, bool separate, bool* changed)
{
	if (changed != nullptr) { *changed = false; }
	if (!deviceCanChooseHostingMode(device)) { return false; }

	bool moved = false;
	if (!QMetaObject::invokeMethod(device.plugin(), SetHostingModeInvokable, Q_RETURN_ARG(bool, moved),
			Q_ARG(bool, separate)))
	{
		return false;
	}
	if (changed != nullptr) { *changed = moved; }
	return true;
}

bool reloadHostedDevice(const HostedDevice& device)
{
	if (!hasInvokable(device.plugin(), "reloadPlugin()")) { return false; }
	// Directly, like the instrument view's own reload: the call re-instantiates
	// the plugin through the plugin's own path, and a queued call would leave the
	// caller reading a state that has not changed yet.
	return QMetaObject::invokeMethod(device.plugin(), ReloadInvokable, Qt::DirectConnection);
}

QString deviceClientExecutable(const HostedDevice& device)
{
	return familyFor(device.pluginKey).client;
}

State hostedDeviceState(const HostedDevice& device)
{
	const Family family = familyFor(device.pluginKey);

	// 1. a live client is a fact about a pid.
	if (deviceHostingProcessId(device) > 0) { return State::SeparateProcess; }

	// 2. this session's record outranks the plugin's own answer: a plugin that
	//    was re-instantiated after a crash reports a healthy mode and would hide
	//    the crash that emptied the slot.
	if (!family.client.isEmpty())
	{
		HostTracker& tracker = HostTracker::instance();
		if (tracker.refusedByCrashLoop(family.client, nullptr)) { return State::RefusedCrashLoop; }
		if (tracker.record(family.client).exits > 0) { return State::ClientExited; }
	}

	// 3. what the plugin says about itself, then 4. what the family allows.
	const QString reported = deviceHostingState(device);
	if (reported == HostingModeSeparate) { return State::SeparateProcess; }
	if (reported == QStringLiteral("separate-process-exited")) { return State::ClientExited; }
	if (reported.isEmpty() && family.availability == Availability::NoClientInBuild)
	{
		return State::RefusedNoClient;
	}
	return State::InProcess;
}

QJsonObject hostedDeviceJson(const HostedDevice& device)
{
	const Family family = familyFor(device.pluginKey);
	HostTracker& tracker = HostTracker::instance();
	QString refusal;
	const bool refused = !family.client.isEmpty()
		&& tracker.refusedByCrashLoop(family.client, &refusal);

	QJsonObject hosting;
	hosting.insert(QStringLiteral("state"), stateName(hostedDeviceState(device)));
	hosting.insert(QStringLiteral("family"), familyObject(family));
	hosting.insert(QStringLiteral("client"), family.client);
	hosting.insert(QStringLiteral("client_process_id"), deviceHostingProcessId(device));
	hosting.insert(QStringLiteral("plugin_reports"), deviceHostingState(device));
	// What this device can be DRIVEN with, asked of the plugin rather than
	// assumed: a family in the table has a client, not necessarily an
	// implementation of the convention.
	hosting.insert(QStringLiteral("can_choose_mode"), deviceCanChooseHostingMode(device));
	hosting.insert(QStringLiteral("can_restart"), hasInvokable(device.plugin(), "reloadPlugin()"));
	hosting.insert(QStringLiteral("refused"), refused);
	hosting.insert(QStringLiteral("refused_reason"), refused ? refusal : QString());
	hosting.insert(QStringLiteral("record"), clientRecordJson(familyRecord(family)));

	QJsonObject out;
	out.insert(QStringLiteral("target"), device.targetId);
	out.insert(QStringLiteral("device"), device.deviceId);
	out.insert(QStringLiteral("kind"), device.isInstrument
		? QStringLiteral("instrument") : QStringLiteral("effect"));
	out.insert(QStringLiteral("plugin"), device.pluginKey);
	out.insert(QStringLiteral("label"), device.pluginLabel);
	out.insert(QStringLiteral("hosting"), hosting);
	return out;
}

QJsonObject familyJson(const Family& family)
{
	QJsonObject out = familyObject(family);
	QString refusal;
	const bool refused = !family.client.isEmpty()
		&& HostTracker::instance().refusedByCrashLoop(family.client, &refusal);
	out.insert(QStringLiteral("hostable"), family.availability != Availability::NoClientInBuild);
	out.insert(QStringLiteral("refused"), refused);
	out.insert(QStringLiteral("refused_reason"), refused ? refusal : QString());
	out.insert(QStringLiteral("record"), clientRecordJson(familyRecord(family)));
	return out;
}

QJsonObject clientRecordJson(const ClientRecord& record)
{
	QJsonObject out;
	out.insert(QStringLiteral("client"), record.client);
	out.insert(QStringLiteral("starts"), record.starts);
	out.insert(QStringLiteral("exits"), record.exits);
	out.insert(QStringLiteral("crashes"), record.crashes);
	out.insert(QStringLiteral("restarts"), record.restarts);
	out.insert(QStringLiteral("last_process_id"), record.lastPid);
	out.insert(QStringLiteral("last_exit_code"), record.lastExitCode);
	out.insert(QStringLiteral("last_exit_was_crash"), record.lastExitWasCrash);
	out.insert(QStringLiteral("last_state"), record.lastState);
	return out;
}

} // namespace control

} // namespace lmms
