/*
 * ControlHardwareIoSupport.h - what the hardware I/O commands share: the device's channels and routing replies
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

#ifndef LMMS_CONTROL_HARDWARE_IO_SUPPORT_H
#define LMMS_CONTROL_HARDWARE_IO_SUPPORT_H

#include <array>
#include <functional>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "AudioIoSetup.h"
#include "HardwareIo.h"
#include "ControlRegistry.h"
#include "lmms_export.h"

namespace lmms
{

class MixerChannel;

//! 0.5.0 F1 (MX-040, MX-041, MX-036, MX-061): audio.io_list / io_setup, mixer.hardware_output / hardware_get,
//! and through it the cue commands (ControlCommandsCueMix.cpp) and the insert commands
//! (ControlCommandsHardwareInsert.cpp).
LMMS_EXPORT void registerHardwareIoCommands(ControlRegistry& registry);
void registerCueMixCommands(ControlRegistry& registry);
void registerHardwareInsertCommands(ControlRegistry& registry);
//! 0.5.0 F7 (MX-060): mixer.channel_format / surround_pan / downmix_options / surround_get
//! (ControlCommandsMixerSurround.cpp), registered with the hardware I/O commands they play through.
void registerSurroundCommands(ControlRegistry& registry);

//! 0.5.0 F1: the hardware I/O commands' shared pieces (ControlCommandsHardwareIo*.cpp, ControlCommandsCueMix.cpp).
namespace control::hardware
{

//! The running device's channels, as far as the commands need them.
struct DeviceChannels
{
	QString key;             //!< its I/O setup entry (audioio::currentDeviceKey)
	QString backend;
	int outputs = 0;         //!< the outputs the device opened
	bool multichannel = false;
	int inputs = 0;          //!< the inputs it delivers: the capture's channels, or the stereo bus's two
	bool capture = false;    //!< a capture device is open
};

DeviceChannels deviceChannels();
//! The running device's I/O setup, its inputs counted as the capture plan has them.
audioio::Setup currentSetup();

/*! A routing target as a reply: its label (a bus name where one matches), its channels, and whether the
 *  device can carry it - "ok", "none", "beyond_device" (the device opened fewer channels: raise the
 *  count with audio.io_setup and reopen the device) or "stereo_backend" (this backend opens a stereo
 *  pair only; the route plays nowhere). @a inputs reads it against the inputs instead (then "no_capture"
 *  when nothing captures). */
QJsonObject destinationJson(const audioio::Destination& destination, const audioio::Setup& setup,
	const DeviceChannels& device, bool inputs = false);

//! A hardware insert as a reply: its settings, the send and return against the device, latency, ping state.
QJsonObject insertJson(const HardwareInsert& insert, const audioio::Setup& setup, const DeviceChannels& device);
//! The cue masters as a reply.
QJsonArray cueMastersJson(const std::array<hardwareio::CueMaster, hardwareio::MaxCues>& masters, const audioio::Setup& setup,
	const DeviceChannels& device);

//! Read @a args' @a key as a routing target on the current setup's outputs (or inputs, for @a inputs).
bool readDestination(const QJsonObject& args, const QString& key, bool inputs, audioio::Destination* out,
	ControlResult* error);

//! "A".."D" or 1..4 into 0..3.
bool readCue(const QJsonObject& args, int* cue, ControlResult* error);

//! Run @a change with the engine's render held off (the change lock), so the audio thread never sees a
//! half-written setting.
void underChangeLock(const std::function<void()>& change);

//! The channel whose stable id is @a id, or nullptr (an undo step names its channel by id, not position).
MixerChannel* channelById(const QString& id);

} // namespace control::hardware

} // namespace lmms

#endif // LMMS_CONTROL_HARDWARE_IO_SUPPORT_H
