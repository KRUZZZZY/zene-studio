/*
 * AudioDevice.h - base-class for audio-devices, used by LMMS audio engine
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_AUDIO_DEVICE_H
#define LMMS_AUDIO_DEVICE_H

#include <vector>

#include <QMutex>
#include <samplerate.h>

#include "LmmsTypes.h"
#include "lmms_constants.h"
#include "SampleFrame.h"

class QThread;

namespace lmms
{

class AudioEngine;
class AudioBusHandle;
class SampleFrame;


class AudioDevice
{
public:
	AudioDevice( const ch_cnt_t _channels, AudioEngine* audioEngine );
	virtual ~AudioDevice();

	// if audio-driver supports ports, classes inheriting AudioBusHandle
	// (e.g. channel-tracks) can register themselves for making
	// audio-driver able to collect their individual output and provide
	// them at a specific port - currently only supported by JACK
	virtual void registerPort(AudioBusHandle* port);
	virtual void unregisterPort(AudioBusHandle* port);
	virtual void renamePort(AudioBusHandle* port);

	inline bool supportsCapture() const
	{
		return m_supportsCapture;
	}

	inline sample_rate_t sampleRate() const
	{
		return m_sampleRate;
	}

	//! 0.5.0 F1 (MX-040): the output channels this device opened - the master's pair and any beyond it.
	ch_cnt_t outputChannels() const { return m_channels; }
	//! 0.5.0 F1: whether the backend can open more than a stereo pair. JACK, ALSA and PortAudio can
	//! (they override this); the others are stereo, unless they opened more (the Dummy, configured to).
	virtual bool supportsMultichannelOutput() const { return m_channels > DEFAULT_CHANNELS; }

#ifdef LMMS_TESTING
	//! Test-only: pin the device sample rate to a spec value.
	//! The production setter is protected because the backend owns the rate;
	//! unit tests that must run at a fixed rate (e.g. the 48 kHz sidechain
	//! performance gate) need an explicit hook. Inline and non-virtual, so it
	//! adds no ABI surface to the production build.
	void setSampleRateForTesting(const sample_rate_t _new_sr) { setSampleRate(_new_sr); }
#endif

	/*! R2.2: the device's own latency, in frames at its rate - input jack to the engine's
	 *  input buffer, and the engine's output buffer to the output jack. 0 when the backend
	 *  reports none (in this build JACK reports both; the Dummy reports what its loopback
	 *  is configured with; the others report nothing). */
	virtual f_cnt_t inputLatencyFrames() const { return 0; }
	virtual f_cnt_t outputLatencyFrames() const { return 0; }
	/*! R2.2: the frames one capture push carries. Input captured during one push is read by
	 *  the NEXT render (the engine's staging ring), so a take lags the output by this much
	 *  beyond the two latencies above. The device's buffer by default; the Dummy pushes one
	 *  engine period per render. */
	virtual f_cnt_t captureBlockFrames() const;

	void startProcessing();

	void stopProcessing();

	bool isRunning() const { return m_running.test(std::memory_order_acquire); }

protected:
	// convert a given audio-buffer to a buffer in signed 16-bit samples
	// returns num of bytes in outbuf
	int convertToS16(const SampleFrame* _ab, const f_cnt_t _frames, int_sample_t* _output_buffer,
		const bool _convert_endian = false);

	// clear given signed-int-16-buffer
	void clearS16Buffer(int_sample_t* _outbuf, const f_cnt_t _frames);

	ch_cnt_t channels() const { return m_channels; }

	AudioEngine* audioEngine() { return m_audioEngine; }
	const AudioEngine* audioEngine() const { return m_audioEngine; }

	void setSampleRate(const sample_rate_t _new_sr) { m_sampleRate = _new_sr; }
	void setChannels(const ch_cnt_t channels) { m_channels = channels; }

	static void stopProcessingThread( QThread * thread );

	/*! R2.3: sizes the stereo scratch bus for pushes of up to @a maxFrames and selects the
	 *  captured pair (@a left, @a right) that bus carries. Off the audio thread - at open, or
	 *  in a backend's buffer-size callback - because publishCaptured() must not allocate.
	 *  A negative @a left or @a right means "the configured pair"
	 *  (AudioInputPath::configuredPlan()), which is what a backend that passes no pair of its
	 *  own gets: only ALSA knows its granted width when it opens, so the others cannot
	 *  clamp the configured pair themselves. */
	void prepareCapture( f_cnt_t maxFrames, int left = -1, int right = -1 );
	/*! R2.3: THE capture publisher, one for every backend (it was AudioAlsa's alone, and JACK
	 *  and SDL each fed a different subset). One interleaved block of @a channels channels
	 *  feeds all three consumers: the N-channel path (record routes), the stereo bus (the
	 *  selected pair - SampleRecordHandle, the monitor, every play handle) and the capture
	 *  counter AudioInputPath reports. Capture thread; allocation-free - a block longer than
	 *  the prepared bus is carried in bus-sized pieces. */
	void publishCaptured( const float* interleaved, int channels, f_cnt_t frames ) noexcept;

	/*! A backend callback that can run after stopProcessingImpl() has returned holds one of these
	 *  for its whole body (render and capture). JACK's process callback is one: it runs for as long
	 *  as its client stays active, whether or not the device runs. stopProcessing() waits until no
	 *  such scope is open, so once it returns no render and no capture publish from a callback is
	 *  still running - the precondition of resetInputStream() (cluster new-p14-402). */
	class CallbackScope
	{
	public:
		explicit CallbackScope(AudioDevice& device) noexcept : m_device(device)
		{
			m_device.m_openCallbacks.fetch_add(1, std::memory_order_seq_cst);
			// Ordered before the isRunning() reads in the body: see waitForOpenCallbacks().
			std::atomic_thread_fence(std::memory_order_seq_cst);
		}
		~CallbackScope() { m_device.m_openCallbacks.fetch_sub(1, std::memory_order_seq_cst); }

		CallbackScope(const CallbackScope&) = delete;
		CallbackScope& operator=(const CallbackScope&) = delete;

	private:
		AudioDevice& m_device;
	};

	/*! Called on every iteration while stopProcessing() waits for an open CallbackScope. Empty,
	 *  except in tests, which use it to see the wait begin without a clock. */
	virtual void onStopWait() {}

protected:
	bool m_supportsCapture;

private:
	virtual void startProcessingImpl() = 0;
	virtual void stopProcessingImpl() = 0;

	//! Waits until every CallbackScope that began before m_running was cleared has ended.
	void waitForOpenCallbacks();

	sample_rate_t m_sampleRate;
	ch_cnt_t m_channels;
	std::vector<SampleFrame> m_captureBus;
	int m_captureLeft = 0;
	int m_captureRight = 1;
	AudioEngine* m_audioEngine = nullptr;
	std::atomic_flag m_running = ATOMIC_FLAG_INIT;
	//! Open CallbackScopes. Dekker pair with m_running: see waitForOpenCallbacks().
	std::atomic<int> m_openCallbacks{0};
};

} // namespace lmms

#endif // LMMS_AUDIO_DEVICE_H
