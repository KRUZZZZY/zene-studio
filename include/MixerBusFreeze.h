/*
 * MixerBusFreeze.h - a mixer bus's frozen take (Q050-124): the bus's own output rendered to audio
 *                    and played back in place of its live inputs and inserts until it is unfrozen
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

#ifndef LMMS_MIXER_BUS_FREEZE_H
#define LMMS_MIXER_BUS_FREEZE_H

#include <memory>
#include <vector>

#include <QString>

#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

class QDomElement;

namespace lmms
{

class Mixer;
class MixerChannel;
class SampleBuffer;

/*! The take a frozen bus plays (one per MixerChannel, MixerChannel::m_freeze).
 *
 *  WHERE THE TAKE SITS. A bus's output is its routed inputs summed, then its trim, inserts (chain or rack),
 *  hardware insert, VCA gain, polarity/mono/width and pan - MixerChannel::m_buffer just before the fader. The
 *  render captures exactly that point (busfreeze::capture) and playback writes the take back at exactly that point
 *  (busfreeze::substitute), so everything after it - the fader, the sends to other buses and to the master, the
 *  sidechain taps, the post-fader inserts and the meters - keeps working live on the take.
 *
 *  THREADS. `path` and `take` are control-thread state. The audio-thread view (`frames`, `frameCount`, `rate`) is
 *  written only while the render period is held (AudioEngine::requestChangesGuard), so a mixer worker never sees a
 *  half-installed take and never frees one: the shared_ptr that owns the decoded audio is released on the control
 *  thread. */
struct BusFreeze
{
	QString path;                               //!< the take's file; empty = not frozen
	std::shared_ptr<const SampleBuffer> take;   //!< decoded take, null when the file is missing or unreadable

	// Audio-thread view, written under the render lock.
	const SampleFrame* frames = nullptr;
	f_cnt_t frameCount = 0;
	sample_rate_t rate = 0;

	//! Render child only (busfreeze::armCapture): where this channel's pre-fader output is copied each period.
	SampleFrame* capture = nullptr;

	bool isFrozen() const { return !path.isEmpty(); }
	bool audioReady() const { return take != nullptr; }
};

namespace busfreeze
{

// ---- audio thread (mixer workers); no allocation, no lock, no I/O, no signal ----

/*! Publishes where the period about to be mixed starts on the song timeline (AudioEngine, render thread, before
 *  the song advances). Only a playing Song-mode transport plays a take. */
LMMS_EXPORT void setPeriod(bool songPlaying, f_cnt_t startFrame) noexcept;

/*! Writes the take's frames for this period into @a channel's m_buffer, replacing the summed inputs. Returns false
 *  (and touches nothing) when the bus is not frozen, its audio is not loaded, its rate is not the engine's, the
 *  transport is stopped or the period lies past the take's end - the live chain then runs as it always does. */
LMMS_EXPORT bool substitute(MixerChannel& channel, f_cnt_t frames) noexcept;

//! Render child only: copies @a channel's pre-fader output into its armed capture buffer.
LMMS_EXPORT void capture(MixerChannel& channel, f_cnt_t frames) noexcept;

//! Render child only: replaces the master output @a out with the armed channel's capture, then clears the capture.
LMMS_EXPORT void replaceOutput(SampleFrame* out, f_cnt_t frames) noexcept;

// ---- control thread ----

/*! Makes @a channel play @a take (decoded off the audio thread by the caller) from @a path. A null @a take keeps
 *  the state frozen but silent-safe: the live inputs keep playing (the file moved; project.relink repairs it). */
LMMS_EXPORT void install(MixerChannel& channel, const QString& path, std::shared_ptr<const SampleBuffer> take);

//! Drops the take: the bus plays its live inputs again. The file stays on disk.
LMMS_EXPORT void clear(MixerChannel& channel);

//! Decodes @a path (control thread). Null, with @a error filled, when it is not usable audio.
LMMS_EXPORT std::shared_ptr<const SampleBuffer> loadTake(const QString& path, QString* error);

//! Writes the `frozenAudio` attribute on the channel's own element, only while frozen.
LMMS_EXPORT void save(const MixerChannel& channel, QDomElement& element);

//! Reads it back; an element without it leaves the channel unfrozen (every older project).
LMMS_EXPORT void load(MixerChannel& channel, const QDomElement& element);

//! True when the channel is frozen and its file is not on disk.
LMMS_EXPORT bool fileMissing(const MixerChannel& channel);

/*! Render child only (`render ... --capture-channel <id>`): the render writes channel @a channelId's pre-fader
 *  output instead of the master. The channel and the master are forced unmuted in that throwaway process. */
LMMS_EXPORT bool armCapture(Mixer& mixer, int channelId, QString* error);

//! Ends a capture armCapture started: the master is the render's output again (tests; the child just exits).
LMMS_EXPORT void disarmCapture(Mixer& mixer);

#ifdef LMMS_TESTING
//! Test seam: replaces BounceInPlace::renderBus's child render (a QtTest binary is not the application).
//! @a render writes @a stagedPath.
using RenderHook = bool (*)(MixerChannel* bus, const QString& stagedPath, int sampleRate, QString* error);
LMMS_EXPORT void setRenderHookForTest(RenderHook render);
#endif

} // namespace busfreeze

} // namespace lmms

#endif // LMMS_MIXER_BUS_FREEZE_H
