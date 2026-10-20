/*
 * AudioIoSetup.h - the interface's channels: their names, how they pair, and how many outputs to open
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

#ifndef LMMS_AUDIO_IO_SETUP_H
#define LMMS_AUDIO_IO_SETUP_H

#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 F1 (MX-041): the I/O setup of an audio interface - what each output and input channel is
 *  called, how the channels pair into stereo or mono buses ("Monitors" on 1/2, "Phones" on 3/4,
 *  "Talkback" on 5), and how many outputs to open. It belongs to the machine, not to a project: it is
 *  kept in the config file (class `audioio`, one JSON attribute), one entry per device - the backend's
 *  name plus the device it opens, where the backend names one - so a second interface keeps its own.
 *
 *  A project's routing (a channel's hardware output, a cue's output, a hardware insert's pairs) stores
 *  channel NUMBERS, never these names: the names are labels and a lookup aid for the commands, so a
 *  project opened on another machine plays to the same channel numbers.
 *
 *  Main/interface thread only. */
namespace audioio
{

//! The channels a side can describe: the hardware bus's bound and the capture path's.
constexpr int MaxChannels = 128;

//! A named group of one (mono) or two (stereo) consecutive channels, 0-based.
struct Bus
{
	QString name;
	int start = 0;
	int width = 2;
};

//! One direction of the interface.
struct Side
{
	int channels = 2;          //!< outputs: how many to open (the backend may grant fewer); inputs: the capture plan's
	QStringList names;         //!< one per channel
	std::vector<Bus> buses;    //!< non-overlapping, inside [0, channels)
};

struct Setup
{
	Side outputs;
	Side inputs;
};

//! Where something is routed on the outputs: first channel (negative: nowhere) and width (1 or 2).
struct Destination
{
	int start = -1;
	int width = 2;
	bool none() const { return start < 0; }
	bool operator==(const Destination&) const = default;
};

//! The config entry a backend's device is kept under: @a backend plus its device, where it names one.
LMMS_EXPORT QString deviceKey(const QString& backend);
//! The running device's entry (the engine's backend).
LMMS_EXPORT QString currentDeviceKey();

//! The stored setup for @a key, filled in: names and buses default ("Out 1", "Out 1/2", ...), the
//! outputs' count is the stored request (2 unless set) and the inputs' is @a inputChannels.
LMMS_EXPORT Setup setupFor(const QString& key, int inputChannels);
//! The output count @a key asks the backend to open (2 unless set) - read by JACK and ALSA at open.
LMMS_EXPORT int requestedOutputs(const QString& key);
//! Validate @a setup and keep it for @a key (the config file is saved). False, with @a error, when a
//! bus overlaps another, leaves the channels, has a width other than 1 or 2, or repeats a name.
LMMS_EXPORT bool writeSetup(const QString& key, const Setup& setup, QString* error);
//! The raw stored entry for @a key (empty object: nothing stored) and its restore, for undo.
LMMS_EXPORT QJsonObject storedEntry(const QString& key);
LMMS_EXPORT void restoreEntry(const QString& key, const QJsonObject& entry);

//! A side as JSON (channels with names, buses with 1-based channel labels) and back. parseSide fills
//! only what @a json names, on top of @a side.
LMMS_EXPORT QJsonObject sideJson(const Side& side, int granted);
LMMS_EXPORT bool parseSide(const QJsonObject& json, Side* side, QString* error);

/*! Read a routing target: "none"; "3" (output 3, mono); "3/4" (a stereo pair); or the name of one of
 *  @a outputs' buses (any case). False, with @a error, for anything else, a pair that is not two
 *  consecutive channels, or a number beyond @a outputs' channels - a number names a channel of that
 *  side, so @a outputs' count is its bound. */
LMMS_EXPORT bool parseDestination(const QString& text, const Side& outputs, Destination* out, QString* error);
//! The label of @a destination: its bus's name when one matches exactly, else "3/4" or "3", or "none".
LMMS_EXPORT QString destinationLabel(const Destination& destination, const Side& outputs);
//! The 1-based channel form only ("3/4", "3", "none"), the one a project stores.
LMMS_EXPORT QString channelLabel(const Destination& destination);

} // namespace audioio

} // namespace lmms

#endif // LMMS_AUDIO_IO_SETUP_H
