/*
 * AudioDummy.h - dummy audio-device
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

#ifndef LMMS_AUDIO_DUMMY_H
#define LMMS_AUDIO_DUMMY_H

#include <chrono>
#include <span>
#include <thread>
#include <vector>

#include "AudioDevice.h"
#include "AudioDeviceSetupWidget.h"
#include "AudioEngine.h"

namespace lmms
{

class AudioDummy : public QThread, public AudioDevice
{
	Q_OBJECT
public:
	AudioDummy( bool & _success_ful, AudioEngine* audioEngine ) :
		AudioDevice( DEFAULT_CHANNELS, audioEngine )
	{
		_success_ful = true;
		configureLoopback();
	}

	//! R2.2: the loopback's configured figures (0 when it is off).
	f_cnt_t inputLatencyFrames() const override { return m_loopInLatency; }
	f_cnt_t outputLatencyFrames() const override { return m_loopOutLatency; }
	//! One capture push per rendered engine period (run() below).
	f_cnt_t captureBlockFrames() const override;

	~AudioDummy() override
	{
		stopProcessing();
	}

	inline static QString name()
	{
		return QT_TRANSLATE_NOOP( "AudioDeviceSetupWidget", "Dummy (no sound output)" );
	}


	class setupWidget : public gui::AudioDeviceSetupWidget
	{
	public:
		setupWidget( QWidget * _parent ) :
			gui::AudioDeviceSetupWidget( AudioDummy::name(), _parent )
		{
		}

		~setupWidget() override = default;

		void saveSettings() override
		{
		}

		void show() override
		{
			parentWidget()->hide();
			QWidget::show();
		}

	} ;


private:
	void startProcessingImpl() override
	{
		start();
	}

	void stopProcessingImpl() override
	{
		stopProcessingThread( this );
	}

	void run() override
	{
		// Paced against an absolute deadline, not "a period minus the render time": each sleep's
		// overshoot used to be lost for good, so a host with coarse sleep slack (macOS) drifted
		// far below real time - two bars in 25 s on the hosted macos-arm64 runner (run
		// 36720131455, ControlLivecodeCommands). Now an overshoot is paid back by the next period
		// sleeping less; a stall longer than a quarter second is not caught up in a burst.
		using Clock = std::chrono::steady_clock;
		auto due = Clock::now();
		while (AudioDevice::isRunning())
		{
			const std::span<const SampleFrame> period = audioEngine()->renderNextPeriod();
			if( m_loopback ) { feedBack( period ); }

			due += std::chrono::microseconds( static_cast<long long>(
				audioEngine()->framesPerPeriod() * 1000000.0 / audioEngine()->outputSampleRate() ) );
			const auto now = Clock::now();
			if( now < due ) { std::this_thread::sleep_until( due ); }
			else if( now - due > std::chrono::milliseconds( 250 ) ) { due = now; }
		}
	}

	/*! R2.2's LOOPBACK FIXTURE, for a box with no audio interface: with
	 *  LMMS_DUMMY_LOOPBACK set, each rendered period is fed back as the capture input after
	 *  exactly LMMS_DUMMY_OUTPUT_LATENCY + LMMS_DUMMY_INPUT_LATENCY frames - a cable from
	 *  the output jack to the input jack of a device that reports those two figures. Off
	 *  (the default) the Dummy behaves as it always has. The delay line is allocated here,
	 *  once, never on the render loop. Defined in src/core/AudioEngine.cpp. */
	void configureLoopback();
	void feedBack( std::span<const SampleFrame> period );

	bool m_loopback = false;
	f_cnt_t m_loopInLatency = 0;
	f_cnt_t m_loopOutLatency = 0;
	std::vector<SampleFrame> m_loopLine;
	std::vector<SampleFrame> m_loopBlock;
	std::size_t m_loopWrite = 0;

} ;

} // namespace lmms

#endif // LMMS_AUDIO_DUMMY_H
