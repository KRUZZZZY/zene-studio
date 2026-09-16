/*
 * ControlCommandsOutOfProcessShared.h - the vocabulary the `oop.*` group's two
 *                                       translation units share (feature row 80,
 *                                       board card #670)
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

#ifndef LMMS_CONTROL_COMMANDS_OUT_OF_PROCESS_SHARED_H
#define LMMS_CONTROL_COMMANDS_OUT_OF_PROCESS_SHARED_H

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "OutOfProcessHosting.h"

namespace lmms
{

class Effect;
class Instrument;
class Plugin;

namespace control
{

//! The five ids of the `oop.*` group.
extern const QString OutOfProcessGetStateId;       //!< oop.get_state
extern const QString OutOfProcessListFamiliesId;   //!< oop.list_families
extern const QString OutOfProcessSetModeId;        //!< oop.set_mode
extern const QString OutOfProcessRestartId;        //!< oop.restart
extern const QString OutOfProcessResetCrashesId;   //!< oop.reset_crashes

//! The two mode names `oop.set_mode` accepts. They are the vocabulary of the
//! invokable convention in include/OutOfProcessHosting.h, not an enum of this
//! group's own, so a family that implements the convention is drivable without
//! anything in this file changing.
extern const QString HostingModeInProcess;  //!< "in-process"
extern const QString HostingModeSeparate;   //!< "separate-process"

/*! One device as this group addresses it: the `plugin.*` addressing convention
 *  (`target` = trk-<n>/ch-<n>, `plugin` = fx-<n>/inst) plus the two things the
 *  hosting layer needs to answer about it - its family key and its own plugin.
 */
struct HostedDevice
{
	QString targetId;             //!< "trk-<n>" | "ch-<n>"
	QString deviceId;             //!< "fx-<n>" | "inst"
	QString pluginKey;            //!< the hosting module's Plugin::Descriptor::name
	QString pluginLabel;          //!< its display name
	bool isInstrument = false;
	Effect* effect = nullptr;     //!< set when the device is an effect instance
	Instrument* instrument = nullptr; //!< set when it is the track's instrument

	//! The Plugin both kinds are (Effect and Instrument both derive from it).
	Plugin* plugin() const;
};

//! Resolves `target` and `plugin` out of \a args; false and *error set on
//! failure, with the same messages the rest of the `plugin.*` group uses.
bool resolveHostedDevice(const QJsonObject& args, HostedDevice* device, ControlResult* error);

//! The device plugin's own answer for how it is hosted, through the invokable
//! convention; empty when the plugin does not implement it (a family with no
//! out-of-process story at all).
QString deviceHostingState(const HostedDevice& device);

//! pid of the device's client process, 0 when there is none: the plugin's own
//! `hostingProcessId()` when it implements the convention, otherwise the
//! RemotePlugin's own QProcess when the device's plugin is one.
qint64 deviceHostingProcessId(const HostedDevice& device);

//! Whether the device's plugin implements the `setHostingMode` half of the
//! convention (i.e. whether `oop.set_mode` can do anything with it).
bool deviceCanChooseHostingMode(const HostedDevice& device);

//! Calls `setHostingMode`. Returns false when the plugin does not implement it.
//! *changed is the plugin's own answer for whether the mode moved.
bool chooseDeviceHostingMode(const HostedDevice& device, bool separate, bool* changed);

//! Calls `reloadPlugin` (the convention's re-host). False when unimplemented.
bool reloadHostedDevice(const HostedDevice& device);

//! The client executable that belongs to the device's family ("" when the
//! family has none in this build).
QString deviceClientExecutable(const HostedDevice& device);

//! The device's hosting state, resolved: a running client wins, then the crash
//! record, then the plugin's own answer, then the family's allowance.
oop::State hostedDeviceState(const HostedDevice& device);

//! The device's hosting, as the wire object (see oop.get_state).
QJsonObject hostedDeviceJson(const HostedDevice& device);

//! One family of the build's table, as the wire object.
QJsonObject familyJson(const oop::Family& family);

//! One client executable's record for this session, as the wire object.
QJsonObject clientRecordJson(const oop::ClientRecord& record);

//! The group's own one-sentence honesty line, repeated in both reads so a
//! caller that only ever calls one of them still reads it.
QString outOfProcessNote();

//! Registers the group's three writers - oop.set_mode, oop.restart and
//! oop.reset_crashes (ControlCommandsOutOfProcessEdit.cpp). Called by
//! registerOutOfProcessCommands(), which is the group's one registration point.
void registerOutOfProcessEditCommands(ControlRegistry& registry);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_OUT_OF_PROCESS_SHARED_H
