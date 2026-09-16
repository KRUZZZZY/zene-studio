/*
 * ControlCommandsOutOfProcess.cpp - the READ half of the `oop.*` surface
 *                                    (SPEC A11-A16)
 *
 * Feature row 80 of docs/FEATURE-LIST-0.3.0.md ("Out-of-process plugin hosting /
 * crash isolation beyond ZynAddSubFx", board card #670). The engine half is
 * include/OutOfProcessHosting.h + src/core/OutOfProcessHosting.cpp: the family
 * table (what each plugin family's out-of-process story IS in this build, with
 * the reason when it has none) and the client-process record (what the client
 * executables have done this session, which is what a crash-loop refusal is
 * measured against). This file is the surface: `oop.get_state` (every device
 * chain in the song, each device's hosting resolved) and `oop.list_families`
 * (the table itself). The group's three WRITERS are in
 * ControlCommandsOutOfProcessEdit.cpp.
 *
 * The surface is the ONLY way the choice is made in a headless instance, and it
 * is the only place the crash accounting is readable at all. There is no
 * out-of-process page, dialog or column in the interface
 * (docs/KNOWN-LIMITATIONS.md and docs/RELEASE-NOTES-v0.3.0-alpha.md carry the
 * absence line).
 *
 * HOW A DEVICE'S HOSTING IS ANSWERED, and why it is not a lookup: four sources
 * are read in order and the FIRST one that speaks decides -
 *
 *   1. a live client process (the plugin's own `hostingProcessId()`, or the
 *      RemotePlugin's QProcess when the device plugin IS one) - it is running,
 *      so "separate-process" is a fact about a pid;
 *   2. this session's record for the family's client executable: a count of
 *      deaths at or over the bound is `refused-crash-loop` (the refusal
 *      oop.set_mode and oop.restart obey), any death at all is
 *      `client-exited`, and the crash is still named in the object;
 *   3. the plugin's own `hostingState()` (the invokable convention in
 *      include/OutOfProcessHosting.h);
 *   4. the family table: a family with no client executable in this build is
 *      `refused-no-client` - reported, not hidden, which is the whole point of
 *      the row beyond ZynAddSubFx.
 *
 * The order matters: the record outranks the plugin's own answer because a
 * plugin that has been re-instantiated after a crash reports a healthy
 * `in-process`/`separate-process` and would otherwise hide the crash that
 * emptied the slot.
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

#include "ControlDeviceSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "Plugin.h"
#include "RemotePlugin.h"
#include "Song.h"

namespace lmms
{

namespace control
{

using namespace lmms::oop;

const QString OutOfProcessGetStateId = QStringLiteral("oop.get_state");
const QString OutOfProcessListFamiliesId = QStringLiteral("oop.list_families");
const QString OutOfProcessSetModeId = QStringLiteral("oop.set_mode");
const QString OutOfProcessRestartId = QStringLiteral("oop.restart");
const QString OutOfProcessResetCrashesId = QStringLiteral("oop.reset_crashes");

const QString HostingModeInProcess = QStringLiteral("in-process");
const QString HostingModeSeparate = QStringLiteral("separate-process");

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

namespace
{

//! The invokable-method probes. Each one asks the meta-object and answers
//! "does this plugin implement the convention", so the call sites below are
//! three lines each and a plugin that implements the convention is drivable
//! with nothing in this file changing (include/OutOfProcessHosting.h).
bool hasInvokable(Plugin* plugin, const char* name)
{
	return plugin != nullptr && plugin->metaObject()->indexOfMethod(
		QMetaObject::normalizedSignature(name).constData()) >= 0;
}

QString invokeHostingState(Plugin* plugin)
{
	if (!hasInvokable(plugin, "hostingState()"))
	{
		return {};
	}
	QString state;
	if (!QMetaObject::invokeMethod(plugin, HostingStateInvokable, Q_RETURN_ARG(QString, state)))
	{
		return {};
	}
	return state;
}

qint64 invokeHostingProcessId(Plugin* plugin)
{
	if (!hasInvokable(plugin, "hostingProcessId()"))
	{
		return 0;
	}
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

} // namespace

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
	else
	{
		device->effect = resolveControlEffect(target, deviceId, error);
		if (device->effect == nullptr) { return false; }
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
	Plugin* plugin = device.plugin();
	if (!deviceCanChooseHostingMode(device)) { return false; }
	bool moved = false;
	if (!QMetaObject::invokeMethod(plugin, SetHostingModeInvokable, Q_RETURN_ARG(bool, moved),
			Q_ARG(bool, separate)))
	{
		return false;
	}
	if (changed != nullptr) { *changed = moved; }
	return true;
}

bool reloadHostedDevice(const HostedDevice& device)
{
	Plugin* plugin = device.plugin();
	if (!hasInvokable(plugin, "reloadPlugin()")) { return false; }
	// Directly, like the instrument view's own reload: the call re-instantiates
	// the plugin through the plugin's own path, and a queued call would leave
	// the caller reading a state that has not changed yet.
	return QMetaObject::invokeMethod(plugin, ReloadInvokable, Qt::DirectConnection);
}

QString deviceClientExecutable(const HostedDevice& device)
{
	return familyFor(device.pluginKey).client;
}

State hostedDeviceState(const HostedDevice& device)
{
	const Family family = familyFor(device.pluginKey);

	if (deviceHostingProcessId(device) > 0) { return State::SeparateProcess; }

	if (!family.client.isEmpty())
	{
		HostTracker& tracker = HostTracker::instance();
		const ClientRecord record = tracker.record(family.client);
		QString refusal;
		if (tracker.refusedByCrashLoop(family.client, &refusal)) { return State::RefusedCrashLoop; }
		if (record.exits > 0) { return State::ClientExited; }
	}

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
	const ClientRecord record = family.client.isEmpty() ? ClientRecord{} : tracker.record(family.client);
	QString refusal;
	const bool refused = !family.client.isEmpty()
		&& tracker.refusedByCrashLoop(family.client, &refusal);
	const State state = hostedDeviceState(device);

	QJsonObject hosting;
	hosting.insert(QStringLiteral("state"), stateName(state));
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
	hosting.insert(QStringLiteral("record"), clientRecordJson(record));

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
	HostTracker& tracker = HostTracker::instance();
	QString refusal;
	const bool refused = !family.client.isEmpty() && tracker.refusedByCrashLoop(family.client, &refusal);
	out.insert(QStringLiteral("hostable"), family.availability != Availability::NoClientInBuild);
	out.insert(QStringLiteral("refused"), refused);
	out.insert(QStringLiteral("refused_reason"), refused ? refusal : QString());
	out.insert(QStringLiteral("record"), family.client.isEmpty()
		? clientRecordJson(ClientRecord{}) : clientRecordJson(tracker.record(family.client)));
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

namespace
{

//! One chain's devices, resolved the way dsp.get_state walks them so a caller
//! can chain `oop.get_state` and `plugin.*` on the same ids.
QJsonObject chainJson(const ControlTarget& target)
{
	QJsonArray devices;
	const std::vector<Effect*>& chain = target.chain->effects();
	for (int i = 0; i < static_cast<int>(chain.size()); ++i)
	{
		std::vector<Effect*>::size_type index = static_cast<std::vector<Effect*>::size_type>(i);
		HostedDevice device;
		device.targetId = target.id;
		device.deviceId = effectIdOf(chain[index]);
		device.effect = chain[index];
		device.pluginKey = QString::fromUtf8(chain[index]->descriptor()->name);
		device.pluginLabel = QString::fromUtf8(chain[index]->descriptor()->displayName);
		devices.append(hostedDeviceJson(device));
	}

	QJsonObject out;
	out.insert(QStringLiteral("id"), target.id);
	out.insert(QStringLiteral("kind"), target.kind);
	out.insert(QStringLiteral("type"), target.typeName);
	out.insert(QStringLiteral("count"), devices.size());
	out.insert(QStringLiteral("devices"), devices);

	InstrumentTrack* track = target.instrumentTrack;
	if (track != nullptr && track->instrument() != nullptr)
	{
		HostedDevice device;
		device.targetId = target.id;
		device.deviceId = QStringLiteral("inst");
		device.isInstrument = true;
		device.instrument = track->instrument();
		device.pluginKey = QString::fromUtf8(track->instrument()->descriptor()->name);
		device.pluginLabel = QString::fromUtf8(track->instrument()->descriptor()->displayName);
		out.insert(QStringLiteral("instrument"), hostedDeviceJson(device));
	}
	return out;
}

void appendChain(QJsonArray* chains, const ControlTarget& target)
{
	chains->append(chainJson(target));
}

//! Every track's chain, then every mixer channel's - the same walk dsp.get_state
//! makes, so `oop.get_state` answers for exactly the devices `plugin.*` can
//! address.
QJsonArray allChains(int* separateCount, int* refusedCount)
{
	QJsonArray chains;
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
	for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
	{
		ControlTarget target;
		ControlResult ignored;
		if (!resolveControlTarget(control::trackIdOf(tracks[i]), &target, &ignored)) { continue; }
		appendChain(&chains, target);
	}

	Mixer* mixer = Engine::mixer();
	if (mixer != nullptr)
	{
		for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
		{
			ControlTarget target;
			ControlResult ignored;
			if (!resolveControlTarget(control::channelIdOf(mixer->mixerChannel(i)), &target, &ignored))
			{
				continue;
			}
			appendChain(&chains, target);
		}
	}

	// The two counters are derived from the chain objects just built rather
	// than from a second walk: one source of truth for what is out of process.
	for (const QJsonValue& chain : chains)
	{
		const QJsonObject object = chain.toObject();
		QJsonArray devices = object.value(QStringLiteral("devices")).toArray();
		if (object.contains(QStringLiteral("instrument")))
		{
			devices.append(object.value(QStringLiteral("instrument")));
		}
		for (const QJsonValue& device : devices)
		{
			const QJsonObject hosting = device.toObject().value(QStringLiteral("hosting")).toObject();
			const QString state = hosting.value(QStringLiteral("state")).toString();
			if (state == stateName(State::SeparateProcess)) { ++(*separateCount); }
			if (state.startsWith(QStringLiteral("refused"))) { ++(*refusedCount); }
		}
	}
	return chains;
}

QJsonArray familiesJson()
{
	QJsonArray out;
	for (const Family& family : families()) { out.append(familyJson(family)); }
	return out;
}

QJsonArray clientRecordsJson()
{
	QJsonArray out;
	for (const ClientRecord& record : HostTracker::instance().records())
	{
		out.append(clientRecordJson(record));
	}
	return out;
}

QJsonObject conventionJson()
{
	QJsonObject out;
	out.insert(QStringLiteral("hosting_state"), QString::fromLatin1(HostingStateInvokable));
	out.insert(QStringLiteral("set_hosting_mode"), QString::fromLatin1(SetHostingModeInvokable));
	out.insert(QStringLiteral("reload"), QString::fromLatin1(ReloadInvokable));
	out.insert(QStringLiteral("why"),
		QStringLiteral("a plugin is drivable out of process when it implements these as Q_INVOKABLE "
			"members (see include/OutOfProcessHosting.h): the group asks the meta-object, so a family "
			"that grows them needs no change in the control surface"));
	return out;
}

/*! oop.get_state - every device chain in the song, each device's hosting
 *  resolved (see the header note for the order the four sources are read in),
 *  the build's family table, and every client executable this session has
 *  started. Read-only, and it answers in every configuration.
 */
ControlResult handleGetState()
{
	int separateCount = 0;
	int refusedCount = 0;
	const QJsonArray chains = allChains(&separateCount, &refusedCount);

	QJsonObject bounds;
	bounds.insert(QStringLiteral("max_crashes_per_client"), HostTracker::maxCrashesPerClient());

	QJsonObject result;
	result.insert(QStringLiteral("chains"), chains);
	result.insert(QStringLiteral("family_count"), families().size());
	result.insert(QStringLiteral("families"), familiesJson());
	result.insert(QStringLiteral("clients"), clientRecordsJson());
	result.insert(QStringLiteral("separate_process_count"), separateCount);
	result.insert(QStringLiteral("refused_count"), refusedCount);
	result.insert(QStringLiteral("convention"), conventionJson());
	result.insert(QStringLiteral("bounds"), bounds);
	result.insert(QStringLiteral("realtime_safe"), true);
	result.insert(QStringLiteral("note"), outOfProcessNote());
	return ControlResult::success(result);
}

/*! oop.list_families - the family table on its own: what each family's
 *  out-of-process story is in THIS build, the client executable when it has
 *  one, and the reason when it does not. Read-only.
 */
ControlResult handleListFamilies()
{
	QJsonObject result;
	result.insert(QStringLiteral("families"), familiesJson());
	result.insert(QStringLiteral("family_count"), families().size());
	result.insert(QStringLiteral("clients"), clientRecordsJson());
	QJsonObject bounds;
	bounds.insert(QStringLiteral("max_crashes_per_client"), HostTracker::maxCrashesPerClient());
	result.insert(QStringLiteral("bounds"), bounds);
	result.insert(QStringLiteral("convention"), conventionJson());
	result.insert(QStringLiteral("note"), outOfProcessNote());
	return ControlResult::success(result);
}

} // namespace

} // namespace control

/*! The group's ONE registration point (declared in
 *  include/ControlRegistryGroups.h, beside the other groups that outgrew
 *  include/ControlRegistry.h). Defined in namespace lmms, like every other
 *  register*Commands - the definition has to match the declaration the header
 *  makes, or the link fails with an undefined reference at the composition
 *  site (registerControlCommands).
 */
void registerOutOfProcessCommands(ControlRegistry& registry)
{
	using namespace control; // the group's ids, its verbs and the shared vocabulary

	{
		ControlCommand cmd;
		cmd.id = OutOfProcessGetStateId;
		cmd.group = QStringLiteral("oop");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("Every device chain in the song with each device's "
			"out-of-process hosting resolved: the state (in-process / separate-process / client-exited / "
			"refused-no-client / refused-crash-loop), the client executable and its live pid, what the "
			"device plugin reports itself, whether it can be driven (mode choice, restart), and this "
			"session's record for that client (starts, exits, crashes, restarts, last exit code). Also "
			"the build's whole family table and every client executable seen. Read-only.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("chains"), arrayProperty()},
			{QStringLiteral("family_count"), integerProperty()},
			{QStringLiteral("families"), arrayProperty()},
			{QStringLiteral("clients"), arrayProperty()},
			{QStringLiteral("separate_process_count"), integerProperty()},
			{QStringLiteral("refused_count"), integerProperty()},
			{QStringLiteral("convention"), objectProperty()},
			{QStringLiteral("bounds"), objectProperty()},
			{QStringLiteral("realtime_safe"), booleanProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleGetState(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = OutOfProcessListFamiliesId;
		cmd.group = QStringLiteral("oop");
		cmd.verb = QStringLiteral("list_families");
		cmd.description = QStringLiteral("The build's out-of-process family table: for every plugin "
			"family this build classifies, its availability (client-available / always-separate / "
			"no-client), the client executable when it has one, and the one-sentence reason when it does "
			"not - so a family that cannot be hosted out of process is refused by NAME rather than "
			"silently run in the wrong place. Read-only.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("families"), arrayProperty()},
			{QStringLiteral("family_count"), integerProperty()},
			{QStringLiteral("clients"), arrayProperty()},
			{QStringLiteral("bounds"), objectProperty()},
			{QStringLiteral("convention"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleListFamilies(); };
		registry.registerCommand(cmd);
	}

	// The group's three writers, in their own translation unit: the registry has
	// exactly one `oop.*` registration point, and it is this one.
	control::registerOutOfProcessEditCommands(registry);
}

} // namespace lmms
