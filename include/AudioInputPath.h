/*
 * AudioInputPath.h - the engine's capture-IN path as one describable thing: the
 *                    device, the input channel COUNT and the channel pair the
 *                    stereo engine bus carries (0.3.0, feature row 64
 *                    "Arbitrary input count / multiple simultaneous inputs").
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

#ifndef LMMS_AUDIO_INPUT_PATH_H
#define LMMS_AUDIO_INPUT_PATH_H

#include <cstdint>

#include <QString>

#include "lmms_export.h"

namespace lmms
{


/*! The CAPTURE-IN path, in one place: what the configuration asks for, and what
 *  this instance's backend actually granted.
 *
 * THE GAP THIS CLOSES. LMMS's default Linux backend was playback-only
 * (src/core/audio/AudioAlsa.cpp contained no snd_pcm_readi), so under ALSA
 * AudioEngine::inputBufferFrames() was ALWAYS 0 and every consumer of the input
 * path - including every record route - took zero inputs. FEATURE-LIST-0.3.0.md
 * row 64 records the same fact as the reason its status is "to build". The
 * playback half of AudioAlsa is unchanged; the capture half is new, and this
 * type is how the rest of the program can see it: how many channels were asked
 * for, how many there are, which pair of them rides the stereo engine bus, and
 * whether the device opened at all.
 *
 * WHY A PROCESS-WIDE STATE OBJECT AND NOT A MEMBER OF AudioEngine. There is
 * exactly one input device per process, and it is opened by whichever backend
 * the configuration selected - a class the control surface has no handle on
 * (AudioEngine's device pointer is private and the backend dies and is replaced
 * when the device is switched). The producer side here is the BACKEND's capture
 * thread, the consumer side is the UI thread running an agent command, and
 * neither of them owns the other. That is what this object is for.
 *
 * THREADING CONTRACT:
 *  - the CONFIG half (Plan, configuredPlan, writePlan) is read and written from
 *    the UI/main thread only;
 *  - publishOpen()/publishUnavailable()/publishClosed() are called by a backend
 *    while it opens or closes the device - never from the audio thread and
 *    never from the capture thread;
 *  - addCapturedFrames()/addOverrun() are called from the CAPTURE thread: two
 *    relaxed atomic adds and nothing else - no allocation, no lock, no syscall;
 *  - live(), and the accessors below it, are called from the UI thread.
 *
 * The configuration keys live under the `audioinput` class
 * (`audioinput/device`, `audioinput/channels`, `audioinput/left`,
 * `audioinput/right`) and are deliberately backend-neutral: the same keys
 * describe the ALSA capture device, and would describe a JACK or SDL one.
 */
class LMMS_EXPORT AudioInputPath
{
public:
	//! One channel of input is the minimum that means anything.
	static constexpr int MinChannels = 1;
	//! The bound the config, the argument schema and the staging ring share. 32
	//! in + 32 out is beyond every interface a DAW is expected to drive, and a
	//! bound is what keeps the staging buffers pre-allocatable.
	static constexpr int MaxChannels = 32;
	//! The engine's stereo bus (SampleFrame is two floats), and therefore what
	//! a route can select with no capture device open.
	static constexpr int DefaultChannels = 2;

	//! What the configuration asks the backend to capture.
	struct Plan
	{
		QString device;                   //!< empty: the backend's own default
		int channels = DefaultChannels;   //!< device channels to capture
		int left = 0;                     //!< captured channel on the bus's left
		int right = 1;                    //!< captured channel on the bus's right
	};

	//! What a backend reports this instance actually got.
	struct Live
	{
		bool captureCapable = false;   //!< does THIS backend implement capture at all
		bool open = false;             //!< is the capture device open
		QString device;                //!< what was opened (or attempted)
		QString reason;                //!< why it is not open; empty while it is
		int channels = 0;              //!< channels the device actually granted
		int rate = 0;                  //!< capture rate the device granted
		int left = 0;                  //!< bus routing in force
		int right = 1;
		QString sampleFormat;          //!< "float" or "s16_le"
		std::uint64_t framesCaptured = 0;
		std::uint64_t overruns = 0;    //!< READI failures recovered from
	};

	// -----------------------------------------------------------------------
	// Configuration (UI/main thread)
	// -----------------------------------------------------------------------
	//! The plan the config file describes. Missing or unparsable values give
	//! the defaults; out-of-range ones are clamped, so this never fails.
	static Plan configuredPlan();
	//! Validates \a plan and writes the honoured form into \a normalised.
	//! False with \a error filled (human-readable) when it cannot be honoured,
	//! which is a channel count outside [MinChannels, MaxChannels], or a
	//! left/right source outside [0, channels) that is NOT the default pair.
	static bool validatePlan(const Plan& plan, Plan* normalised, QString* error);
	//! Writes the plan's four keys into the config file and saves it. False
	//! with \a error filled when the plan does not validate or the file could
	//! not be written.
	static bool writePlan(const Plan& plan, QString* error);

	//! Channels the configuration asks for, clamped to the sane range.
	static int configuredChannelCount();
	//! How many record routes the engine prepares (MultiTrackRecorder::MaxRoutes).
	//! A route is a file plus an input channel, so this is the ROUTE bound and not
	//! the input bound: `record.arm_track`'s `route`, and the position mapping
	//! `track.set_arm` uses, are validated against it, while the channel a route
	//! may select is validated against the input count above.
	static int recordRouteCapacity();

	// -----------------------------------------------------------------------
	// Live state
	// -----------------------------------------------------------------------
	static Live live();
	//! True when the backend that is running - or, before one is selected, the
	//! backend the configuration selects - implements a capture path.
	static void setCaptureCapable(bool capable);

	//! A backend opened its capture device: \a grantedChannels and \a rate are
	//! what the device granted (not what was asked).
	static void publishOpen(const Plan& plan, int grantedChannels, int rate,
		const QString& sampleFormat);
	//! A backend could not open a capture device. \a reason is its own error
	//! text; it is reported, never swallowed - "no capture" and "capture
	//! refused" are different facts and an agent cannot tell them apart if the
	//! second is silent.
	static void publishUnavailable(const QString& device, const QString& reason);
	//! A backend closed a capture device it had opened.
	static void publishClosed(const QString& reason);

	//! Capture thread only: \a frames arrived from the device.
	static void addCapturedFrames(std::uint64_t frames) noexcept;
	//! Capture thread only: one READI failure was recovered from.
	static void addOverrun() noexcept;

	//! The channel count a record route may select from RIGHT NOW: what the open
	//! capture delivers, or the engine bus's own width when none is open. This
	//! is the number a route's `input_channel` is validated against.
	static int recordableChannelCount();

private:
	//! The one state object. Defined in the .cpp; never returned to a caller.
	struct State;
	static State& state();
} ;


} // namespace lmms

#endif // LMMS_AUDIO_INPUT_PATH_H
