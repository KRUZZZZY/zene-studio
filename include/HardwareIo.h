/*
 * HardwareIo.h - a mixer channel's hardware side: its direct out, its cue sends, its hardware insert
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

#ifndef LMMS_HARDWARE_IO_H
#define LMMS_HARDWARE_IO_H

#include <array>

#include <QString>

#include "AudioIoSetup.h"
#include "HardwareInsert.h"
#include "LmmsTypes.h"
#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

class Mixer;
class MixerChannel;
class SampleFrame;

namespace hardwareio
{

//! The cue (headphone) mixes a project can have: A, B, C, D.
constexpr int MaxCues = 4;

//! One channel's send to one cue mix. Level 0 is no send.
struct CueSend
{
	float level = 0.f;       //!< linear, 0..2
	float pan = 0.f;         //!< -1..1, a balance on top of the channel's own pan
	bool preFader = true;    //!< the cue mix's default: the performer's mix does not follow the engineer's faders
	bool operator==(const CueSend&) const = default;
};

//! A cue mix's master: what its sends sum to, and where that goes.
struct CueMaster
{
	QString name;                     //!< empty: "Cue A" etc.
	float level = 1.f;                //!< linear, 0..2
	audioio::Destination output;      //!< none until routed
	bool followListen = true;         //!< while a channel listens (AFL/PFL), the cue carries the listened signal
	bool operator==(const CueMaster&) const = default;
};

} // namespace hardwareio

/*! 0.5.0 F1 (MX-040, MX-036, MX-061): what one mixer channel sends to the hardware. A member of
 *  MixerChannel, so it moves, is deleted and is cleared with its channel.
 *
 *  - The direct out (MX-040): the channel after its fader, added to a device output pair (or one output),
 *    as FL's mixer track output does - its sends, the one to the master included, are unchanged, so a
 *    channel goes ONLY to the hardware when its master send is also removed. The master's own direct out
 *    is where the main mix plays: outputs 1/2 unless routed elsewhere (or nowhere).
 *  - The cue sends (MX-036): one per cue mix, see hardwareio::CueSend.
 *  - The hardware insert (MX-061): see HardwareInsert.
 *
 *  Written by the commands under the engine's change lock, read by the render thread
 *  (hardwareio::mixOutputs). Saved as one <hardwareio> element under the channel, only while it differs
 *  from the defaults, so a project that uses none of this saves exactly as before. */
struct LMMS_EXPORT ChannelHardwareIo
{
	bool outputSet = false;           //!< false: the default (the master on 1/2, any other channel nowhere)
	audioio::Destination output;
	std::array<hardwareio::CueSend, hardwareio::MaxCues> cues{};
	HardwareInsert insert;

	//! The direct out in force: the set one, or the default for the master (1/2) or another channel (none).
	audioio::Destination effectiveOutput(bool master) const;
	bool usesCues() const;

	void save(QDomDocument& doc, QDomElement& channelElement) const;
	//! Read it back; absent: the defaults (and the insert's buffers for periods of @a frames, if enabled).
	void load(const QDomElement& channelElement, f_cnt_t frames);
	void clear();
};

namespace hardwareio
{

//! The cue masters (project state; the commands write them under the engine's change lock).
LMMS_EXPORT const CueMaster& cueMaster(int cue);
LMMS_EXPORT void setCueMaster(int cue, const CueMaster& master);
//! The name a cue answers to: its own, or "Cue A".. when it has none.
LMMS_EXPORT QString cueName(int cue);
//! Ableton's Solo/Cue switch: listening goes to the cue mixes that follow it instead of the main output.
LMMS_EXPORT bool listenToCue();
LMMS_EXPORT void setListenToCue(bool toCue);
//! Whether the main output keeps the mix while channels listen: listenToCue() and a cue mix that
//! follows the listen is routed somewhere (otherwise the listen falls back to the main output).
LMMS_EXPORT bool listenSparesMain();

/*! The channel's worker (MixerChannel::doProcessing), after its devices and before its fader: the hardware
 *  insert, while it is on. Its return may carry a tail the channel's input no longer has, so the channel
 *  is kept awake (its quiet flags cleared) for as long as the insert runs. */
LMMS_EXPORT void processInsert(MixerChannel& channel, f_cnt_t frames);

//! The latency the channel's hardware insert adds to its output, for delay compensation: 0 while it is
//! off, and while an export bypasses it.
LMMS_EXPORT int insertLatency(const MixerChannel& channel);

//! Where insertLatency()'s figure comes from, as pdc.report and the strip tooltip name it: "off" (the
//! insert is disabled), "bypassed" (an export runs), "override" (a typed figure), "measured" (the last
//! ping, at this rate and on this route) or "unmeasured" (on, but no figure: it adds 0).
LMMS_EXPORT QString insertLatencySource(const MixerChannel& channel);

/*! The render thread, at the end of Mixer::masterMix, after every worker and before the listen bus
 *  replaces the main output: place the master (@a master, the main mix after its fader) and add every
 *  direct out, cue mix and hardware insert send to the engine's hardware bus. Muted channels send
 *  nothing (so the cues follow solo, which mutes). No allocation, no lock: the scratch is static and
 *  sized for the engine's largest period. */
LMMS_EXPORT void mixOutputs(Mixer& mixer, const SampleFrame* master, f_cnt_t frames);

//! The render thread, after mixOutputs: the listen bus replaces the main output (MixerListen.h), unless
//! listenSparesMain() - the listen then reaches only the cue mixes that follow it.
LMMS_EXPORT void listenOnMain(SampleFrame* master, Mixer& mixer, f_cnt_t frames);

//! <cuemix> elements and the listen target under the mixer's element, only what differs from the defaults.
LMMS_EXPORT void saveMixer(QDomDocument& doc, QDomElement& mixerElement);
LMMS_EXPORT void loadMixer(const QDomElement& mixerElement);
//! A new or cleared project: every cue master back to its defaults, listening to the main output.
LMMS_EXPORT void resetMixer();

} // namespace hardwareio

} // namespace lmms

#endif // LMMS_HARDWARE_IO_H
