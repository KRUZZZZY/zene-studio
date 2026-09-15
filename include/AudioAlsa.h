/*
 * AudioAlsa.h - device-class that implements ALSA-PCM-output
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_AUDIO_ALSA_H
#define LMMS_AUDIO_ALSA_H

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_ALSA

// older ALSA-versions might require this
#define ALSA_PCM_NEW_HW_PARAMS_API

#include <alsa/asoundlib.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <QThread>

#include "AudioDevice.h"
#include "SampleFrame.h"

namespace lmms
{

class AudioAlsa : public QThread, public AudioDevice
{
	Q_OBJECT
public:
	/**
	 * @brief Contains the relevant information about available ALSA devices
	 */
	class DeviceInfo
	{
	public:
		DeviceInfo(QString const & deviceName, QString const & deviceDescription) :
			m_deviceName(deviceName),
			m_deviceDescription(deviceDescription)
		{}
		~DeviceInfo() = default;

		QString const & getDeviceName() const { return m_deviceName; }
		QString const & getDeviceDescription() const { return m_deviceDescription; }

	private:
		QString m_deviceName;
		QString m_deviceDescription;

	};

	using DeviceInfoCollection = std::vector<DeviceInfo>;

public:
	AudioAlsa( bool & _success_ful, AudioEngine* audioEngine );
	~AudioAlsa() override;

	inline static QString name()
	{
		return QT_TRANSLATE_NOOP( "AudioDeviceSetupWidget",
			"ALSA (Advanced Linux Sound Architecture)" );
	}

	static QString probeDevice();

	static DeviceInfoCollection getAvailableDevices();

private:
	void startProcessingImpl() override;
	void stopProcessingImpl() override;
	void run() override;

	int setHWParams( const ch_cnt_t _channels, snd_pcm_access_t _access );
	int setSWParams();
	int handleError( int _err );

	// -----------------------------------------------------------------------
	// The CAPTURE path (0.3.0, feature row 64 "Arbitrary input count / multiple
	// simultaneous inputs"). This backend used to be playback-only - there was
	// no snd_pcm_readi anywhere in the file - so under ALSA
	// AudioEngine::inputBufferFrames() was always 0 and every record route took
	// zero inputs (docs/FEATURE-LIST-0.3.0.md rows 14/16/64).
	//
	// The capture device is a SECOND PCM opened on its own thread: the read is
	// blocking, and a blocking read on the playback thread would stall the
	// render. The plan (device, channel count, which pair rides the stereo bus)
	// comes from AudioInputPath, and what the device granted is published back
	// there so record.input_get_state can report it.
	// -----------------------------------------------------------------------
	//! Opens the capture device for the configured plan. False with the reason
	//! published when it cannot be opened; the playback path is unaffected.
	bool openCapture();
	//! Closes it: stops the thread, drops the PCM, publishes the closure.
	void closeCapture();
	//! The capture thread: read a period, publish it to both engine input paths.
	void captureLoop();
	//! Publishes one interleaved block of \a frames frames into the engine.
	void publishCaptured( const float* interleaved, int channels, snd_pcm_uframes_t frames );
	//! Error recovery for the capture PCM (overrun/suspend), the read-side twin
	//! of handleError()'s write-side one.
	int handleCaptureError( int _err );

	snd_pcm_t * m_handle;

	snd_pcm_uframes_t m_bufferSize;
	snd_pcm_uframes_t m_periodSize;

	snd_pcm_hw_params_t * m_hwParams;
	snd_pcm_sw_params_t * m_swParams;

	// --- capture state -----------------------------------------------------
	snd_pcm_t * m_captureHandle;
	snd_pcm_hw_params_t * m_captureHwParams;
	snd_pcm_uframes_t m_capturePeriodSize;
	//! Device channels the capture PCM granted (the plan's count, or what the
	//! driver accepted instead).
	int m_captureChannels;
	//! True when the PCM hands over SND_PCM_FORMAT_FLOAT, false for S16_LE -
	//! the two formats this path opens, in that order of preference.
	bool m_captureFloat;
	//! The raw device block (FLOAT in place, or S16_LE before conversion).
	std::vector<float> m_captureWide;
	std::vector<std::int16_t> m_captureRaw16;
	//! The pair of captured channels the STEREO engine bus carries.
	std::vector<SampleFrame> m_captureBus;
	//! The bus routing in force, cached at open time: the capture thread must
	//! not read the published state (that takes a lock) once per period.
	int m_captureLeft;
	int m_captureRight;
	std::thread m_captureThread;
	std::atomic<bool> m_captureStop;
	std::atomic<bool> m_captureOpen;
} ;

} // namespace lmms

#endif // LMMS_HAVE_ALSA

#endif // LMMS_AUDIO_ALSA_H
