/*
 * SurroundExport.h - the multichannel WAV export of a surround master (0.5.0 F7)
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

#ifndef LMMS_SURROUND_EXPORT_H
#define LMMS_SURROUND_EXPORT_H

#include <array>
#include <cstdint>

#include <QString>
#include <QStringList>

#include "LmmsTypes.h"
#include "MixerSurround.h"
#include "OutputSettings.h"
#include "lmms_export.h"

namespace lmms::surroundexport
{

/*! 0.5.0 F7 (MX-060): an export can write a surround master as it is - one file channel per speaker -
 *  instead of folding it to stereo.
 *
 *  - The choice is OutputSettings::ChannelMode. Stereo (the default) is the export every caller had
 *    before: a surround master folded with the BS.775 downmix. Surround writes the master's layout:
 *    3 channels for LCR, 4 for quad, 6 for 5.1, 8 for 7.1, in the WAVE_FORMAT_EXTENSIBLE order the
 *    mixer already keeps (L R C LFE Ls Rs; 7.1: L R C LFE Lrs Rrs Ls Rs) with the matching channel mask.
 *  - Only WAV carries it (libsndfile's SF_FORMAT_WAVEX: SF_FORMAT_WAV writes a plain PCM header for more
 *    than two channels, with no mask), at 16-bit, 24-bit and 32-bit float. Everything else is refused,
 *    with the reason: a stereo master, another file format, the loudness report (it measures stereo).
 *  - The master's side channels reach the file through a TAP: while a surround export runs, the master
 *    mix (surround::masterOutput) hands them, at the master fader and gain, to a buffer this module
 *    owns instead of folding them into the pair. The tap is double-buffered like the engine's output,
 *    so the renderer reads the period that matches the stereo pair it was handed.
 *
 *  Threads: arm() and disarm() run on the control thread, before the render starts and after it ended
 *  (ProjectRenderer's constructor and destructor); they are the only calls that allocate. beginPeriod()
 *  and writePlane() run on the render thread inside the master mix and only touch that storage;
 *  readPlane() runs on the same thread after the period. Nothing on the period path allocates or locks. */

using ChannelMode = OutputSettings::ChannelMode;

//! "stereo" / "surround": the names the CLI, the render command and the docs use.
LMMS_EXPORT QString channelModeName(ChannelMode mode);
LMMS_EXPORT bool channelModeFromName(const QString& name, ChannelMode* mode);
LMMS_EXPORT QStringList channelModeNames();

//! The master bus's format (control thread).
LMMS_EXPORT surround::Format masterFormat();
//! The channels an export with @a settings writes: 2 for Stereo, the master's for Surround.
LMMS_EXPORT int channelsFor(const OutputSettings& settings);
//! Why @a settings cannot be exported to @a wave (true: a WAV file) with the master as it is, or empty.
//! A Stereo export is never refused here.
LMMS_EXPORT QString refusal(const OutputSettings& settings, bool wave);

//! The surround format a file of @a channels carries (3 LCR, 4 quad, 6 5.1, 8 7.1), else Stereo.
LMMS_EXPORT surround::Format formatForChannels(int channels);
//! libsndfile's channel map (SF_CHANNEL_MAP_*) of @a format, in its channel order; unused entries 0.
LMMS_EXPORT std::array<int, surround::MaxChannels> sndfileChannelMap(surround::Format format);
//! The WAVE_FORMAT_EXTENSIBLE dwChannelMask of @a format (5.1: 0x3F, 7.1: 0x63F).
LMMS_EXPORT std::uint32_t channelMask(surround::Format format);

//! Control thread, before the render: the tap's storage for periods of @a frames, and armed.
LMMS_EXPORT void arm(f_cnt_t frames);
//! Control thread, after the render: disarmed and freed.
LMMS_EXPORT void disarm();
LMMS_EXPORT bool armed() noexcept;
//! Render thread, once per master mix: the sides flip and the new write side is silenced.
LMMS_EXPORT void beginPeriod() noexcept;
//! Render thread: side channel @a channel (2..7) of the period being mixed, frames() floats.
LMMS_EXPORT float* writePlane(int channel) noexcept;
//! Render thread, after the period: side channel @a channel of the period the renderer was handed.
LMMS_EXPORT const float* readPlane(int channel) noexcept;
LMMS_EXPORT f_cnt_t frames() noexcept;

} // namespace lmms::surroundexport

#endif // LMMS_SURROUND_EXPORT_H
