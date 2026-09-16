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
 * measured against). This file is the surface's two READS - `oop.get_state`
 * (every device chain in the song, each device's hosting resolved) and
 * `oop.list_families` (the table itself) - plus the group's one registration
 * point. The three WRITERS are in ControlCommandsOutOfProcessEdit.cpp and the
 * shared helpers are in ControlCommandsOutOfProcessSupport.cpp.
 *
 * The surface is the ONLY way the choice is made in a headless instance, and it
 * is the only place the crash accounting is readable at all. There is no
 * out-of-process page, dialog or column in the interface
 * (docs/KNOWN-LIMITATIONS.md and docs/RELEASE-NOTES-v0.3.0-alpha.md carry the
 * absence line; docs/OUT-OF-PROCESS-BEYOND-ZYN.md is the feature's document).
 *
 * The resolution order a device's state is answered by, and why, is documented
 * where it is implemented (`hostedDeviceState`, in the support file): a live
 * pid, then this session's record, then the plugin's own answer, then the family
 * table. The record outranking the plugin matters: a plugin re-instantiated
 * after a crash reports a healthy mode and would otherwise hide the crash that
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

namespace
{

//! One chain's devices, resolved the way dsp.get_state walks them so a caller
//! can chain `oop.get_state` and `plugin.*` on the same ids.
QJsonObject chainJson(const ControlTarget& target)
{
	QJsonArray devices;
	const std::vector<Effect*>& chain = target.chain->effects();
	for (std::size_t index = 0; index < chain.size(); ++index)
	{
		Effect* effect = chain[index];
		HostedDevice device;
		device.targetId = target.id;
		device.deviceId = effectIdOf(effect);
		device.effect = effect;
		device.pluginKey = QString::fromUtf8(effect->descriptor()->name);
		device.pluginLabel = QString::fromUtf8(effect->descriptor()->displayName);
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

//! Appends one chain, resolving its id through the control vocabulary the way
//! plugin.* addresses it; a target that does not resolve is skipped.
void appendChain(QJsonArray* chains, const QString& targetId)
{
	ControlTarget target;
	ControlResult ignored;
	if (!resolveControlTarget(targetId, &target, &ignored)) { return; }
	chains->append(chainJson(target));
}

//! Every track's chain, then every mixer channel's - the same walk
//! dsp.get_state makes, so `oop.get_state` answers for exactly the devices
//! `plugin.*` can address.
QJsonArray allChains()
{
	QJsonArray chains;
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
	for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
	{
		appendChain(&chains, control::trackIdOf(tracks[i]));
	}

	Mixer* mixer = Engine::mixer();
	if (mixer != nullptr)
	{
		for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
		{
			appendChain(&chains, control::channelIdOf(mixer->mixerChannel(i)));
		}
	}
	return chains;
}

//! The devices of one chain: its effect instances plus, on an instrument track,
//! the instrument itself (reported under the "inst" address).
QJsonArray chainDevices(const QJsonObject& chain)
{
	QJsonArray devices = chain.value(QStringLiteral("devices")).toArray();
	if (chain.contains(QStringLiteral("instrument")))
	{
		devices.append(chain.value(QStringLiteral("instrument")));
	}
	return devices;
}

//! How many devices are out of process and how many are refused, derived from
//! the chain objects just built rather than from a second walk: one source of
//! truth for what the surface reported.
void countHostedDevices(const QJsonArray& chains, int* separateCount, int* refusedCount)
{
	for (const QJsonValue& chain : chains)
	{
		for (const QJsonValue& device : chainDevices(chain.toObject()))
		{
			const QString state = device.toObject().value(QStringLiteral("hosting")).toObject()
				.value(QStringLiteral("state")).toString();
			if (state == stateName(State::SeparateProcess)) { ++(*separateCount); }
			if (state.startsWith(QStringLiteral("refused"))) { ++(*refusedCount); }
		}
	}
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

QJsonObject boundsJson()
{
	QJsonObject bounds;
	bounds.insert(QStringLiteral("max_crashes_per_client"), HostTracker::maxCrashesPerClient());
	return bounds;
}

//! The invokable-method convention, on the wire, so a client can tell what makes
//! a family drivable without reading this repository.
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
 *  resolved, the build's family table and every client executable this session
 *  has started. Read-only, and it answers in every configuration.
 */
ControlResult handleGetState()
{
	int separateCount = 0;
	int refusedCount = 0;
	const QJsonArray chains = allChains();
	countHostedDevices(chains, &separateCount, &refusedCount);

	QJsonObject result;
	result.insert(QStringLiteral("chains"), chains);
	result.insert(QStringLiteral("family_count"), families().size());
	result.insert(QStringLiteral("families"), familiesJson());
	result.insert(QStringLiteral("clients"), clientRecordsJson());
	result.insert(QStringLiteral("separate_process_count"), separateCount);
	result.insert(QStringLiteral("refused_count"), refusedCount);
	result.insert(QStringLiteral("convention"), conventionJson());
	result.insert(QStringLiteral("bounds"), boundsJson());
	result.insert(QStringLiteral("realtime_safe"), true);
	result.insert(QStringLiteral("note"), outOfProcessNote());
	return ControlResult::success(result);
}

/*! oop.list_families - the family table on its own: what each family's
 *  out-of-process story is in THIS build, the client executable when it has one,
 *  and the reason when it does not. Read-only.
 */
ControlResult handleListFamilies()
{
	QJsonObject result;
	result.insert(QStringLiteral("families"), familiesJson());
	result.insert(QStringLiteral("family_count"), families().size());
	result.insert(QStringLiteral("clients"), clientRecordsJson());
	result.insert(QStringLiteral("bounds"), boundsJson());
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
 *  makes, or the link fails with an undefined reference at the composition site
 *  (registerControlCommands).
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
