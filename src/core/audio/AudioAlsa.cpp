/*
 * AudioAlsa.cpp - device-class which implements ALSA-PCM-output
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


#include "AudioAlsa.h"

#ifdef LMMS_HAVE_ALSA

#include "endian_handling.h"
#include "AudioEngine.h"
#include "AudioInputPath.h"
#include "ConfigManager.h"

namespace lmms
{

namespace
{

//! How long the capture thread waits on the device before it re-checks the
//! stop flag (0.3.0). Bounded on purpose: a bare blocking snd_pcm_readi() on a
//! device that has stopped delivering would never return, and the join in
//! closeCapture() would hang on it.
constexpr int kCaptureWaitMs = 200;

} // namespace

AudioAlsa::AudioAlsa( bool & _success_ful, AudioEngine*  _audioEngine ) :
	AudioDevice(std::clamp<ch_cnt_t>(
		ConfigManager::inst()->value("audioalsa", "channels").toInt(),
		DEFAULT_CHANNELS,
		DEFAULT_CHANNELS), _audioEngine),
	m_handle( nullptr ),
	m_hwParams( nullptr ),
	m_swParams(nullptr),
	m_captureHandle( nullptr ),
	m_captureHwParams( nullptr ),
	m_capturePeriodSize( 0 ),
	m_captureChannels( 0 ),
	m_captureFloat( false ),
	m_captureLeft( 0 ),
	m_captureRight( 1 ),
	m_captureStop( false ),
	m_captureOpen( false )
{
	_success_ful = false;

	if( setenv( "PULSE_ALSA_HOOK_CONF", "/dev/null", 0 ) )
	{
		fprintf( stderr,
		"Could not avoid possible interception by PulseAudio\n" );
	}

	if (int err = snd_pcm_open(&m_handle, probeDevice().toLatin1().constData(), SND_PCM_STREAM_PLAYBACK, 0); err < 0)
	{
		printf( "Playback open error: %s\n", snd_strerror( err ) );
		return;
	}

	snd_pcm_hw_params_malloc( &m_hwParams );
	snd_pcm_sw_params_malloc( &m_swParams );

	if (int err = setHWParams(channels(), SND_PCM_ACCESS_RW_INTERLEAVED); err < 0)
	{
		printf( "Setting of hwparams failed: %s\n",
							snd_strerror( err ) );
		return;
	}
	if (int err = setSWParams(); err < 0)
	{
		printf( "Setting of swparams failed: %s\n",
							snd_strerror( err ) );
		return;
	}

	// set FD_CLOEXEC flag for all file descriptors so forked processes
	// do not inherit them
	int count = snd_pcm_poll_descriptors_count( m_handle );
	auto ufds = new pollfd[count];
	snd_pcm_poll_descriptors( m_handle, ufds, count );
	for (int i = 0; i < std::max(3, count); ++i)
	{
		const int fd = ( i >= count ) ? ufds[0].fd+i : ufds[i].fd;
		int oldflags = fcntl( fd, F_GETFD, 0 );
		if( oldflags < 0 )
			continue;
		oldflags |= FD_CLOEXEC;
		fcntl( fd, F_SETFD, oldflags );
	}
	delete[] ufds;
	_success_ful = true;
}




AudioAlsa::~AudioAlsa()
{
	// The capture side first: it must be stopped and joined before the class's
	// own state goes away (0.3.0).
	closeCapture();

	if( m_handle != nullptr )
	{
		snd_pcm_close( m_handle );
	}

	if( m_hwParams != nullptr )
	{
		snd_pcm_hw_params_free( m_hwParams );
	}

	if( m_swParams != nullptr )
	{
		snd_pcm_sw_params_free( m_swParams );
	}
}




QString AudioAlsa::probeDevice()
{
	QString dev = ConfigManager::inst()->value( "audioalsa", "device" );
	if( dev == "" )
	{
		if( getenv( "AUDIODEV" ) != nullptr )
		{
			return getenv( "AUDIODEV" );
		}
		return "default";
	}
	return dev;
}




/**
 * @brief Creates a list of all available devices.
 *
 * Uses the hints API of ALSA to collect all devices. This also includes plug
 * devices. The reason to collect these and not the raw hardware devices
 * (e.g. hw:0,0) is that hardware devices often have a very limited number of
 * supported formats, etc. Plugs on the other hand are software components that
 * map all types of formats and inputs to the hardware and therefore they are
 * much more flexible and more what we want.
 *
 * Further helpful info http://jan.newmarch.name/LinuxSound/Sampled/Alsa/.
 *
 * @return A collection of devices found on the system.
 */
AudioAlsa::DeviceInfoCollection AudioAlsa::getAvailableDevices()
{
	DeviceInfoCollection deviceInfos;

	char** hints = nullptr;

	/* Enumerate sound devices */
	int err = snd_device_name_hint(-1, "pcm", (void***)&hints);
	if (err != 0)
	{
		return deviceInfos;
	}

	char** n = hints;
	while (*n != nullptr)
	{
		char *name = snd_device_name_get_hint(*n, "NAME");
		char *description = snd_device_name_get_hint(*n, "DESC");

		if (name != 0 && description != 0)
		{
			deviceInfos.push_back(DeviceInfo(QString(name), QString(description)));
		}

		free(name);
		free(description);

		n++;
	}

	//Free the hint buffer
	snd_device_name_free_hint((void**)hints);

	return deviceInfos;
}




int AudioAlsa::handleError( int _err )
{
	if( _err == -EPIPE )
	{
		// under-run
		_err = snd_pcm_prepare( m_handle );
		if( _err < 0 )
			printf( "Can't recover from underrun, prepare "
					"failed: %s\n", snd_strerror( _err ) );
		return ( 0 );
	}
#ifdef ESTRPIPE
	else if( _err == -ESTRPIPE )
	{
		while( ( _err = snd_pcm_resume( m_handle ) ) == -EAGAIN )
		{
			sleep( 1 );	// wait until the suspend flag
					// is released
		}

		if( _err < 0 )
		{
			_err = snd_pcm_prepare( m_handle );
			if( _err < 0 )
				printf( "Can't recover from suspend, prepare "
					"failed: %s\n", snd_strerror( _err ) );
		}
		return ( 0 );
	}
#endif
	return _err;
}




void AudioAlsa::startProcessingImpl()
{
	// The capture side is opened and started BEFORE the playback thread, so the
	// engine never renders a period with the input path half-configured: by the
	// time renderNextPeriod() runs, AudioInputPath reports the device state and
	// the capture thread is producing frames (0.3.0, feature row 64).
	if( openCapture() )
	{
		m_captureStop.store( false, std::memory_order_release );
		m_captureOpen.store( true, std::memory_order_release );
		m_captureThread = std::thread( &AudioAlsa::captureLoop, this );
	}
	start(QThread::HighPriority);
}




void AudioAlsa::stopProcessingImpl()
{
	stopProcessingThread( this );
	closeCapture();
}

void AudioAlsa::run()
{
	const auto framesPerAudioBuffer = audioEngine()->framesPerAudioBuffer();
	auto buf = std::vector<float>(framesPerAudioBuffer * channels());
	while (AudioDevice::isRunning())
	{
		audioEngine()->renderNextBuffer({buf.data(), channels(), framesPerAudioBuffer});

		if (const auto framesWritten = snd_pcm_writei(m_handle, buf.data(), framesPerAudioBuffer); framesWritten < 0)
		{
			handleError(framesWritten);
			continue;
		}
	}
}




// ===========================================================================
// The capture path (0.3.0, feature row 64).
//
// WHY A SECOND PCM AND A SECOND THREAD. The playback half of this backend
// renders one period and then writes it; snd_pcm_readi() BLOCKS until the
// device has a period to hand over, so reading on the playback thread would
// stall the render for as long as the capture device is late. The fork already
// has the right shape for a capture that is not the render thread: the JACK and
// SDL backends push their input from their own callbacks through
// AudioEngine::pushInputFrames()/pushInputFramesWide(). This is the same
// handover, from a thread this class owns.
// ===========================================================================


bool AudioAlsa::openCapture()
{
	// This backend HAS a capture path, whether or not the device below opens.
	// The distinction matters: "no capture exists in this build" and "the
	// capture device refused to open" are different facts, and an agent that
	// cannot tell them apart will keep re-trying the second one.
	AudioInputPath::setCaptureCapable( true );

	const AudioInputPath::Plan plan = AudioInputPath::configuredPlan();
	if( plan.channels < AudioInputPath::MinChannels )
	{
		AudioInputPath::publishUnavailable( plan.device,
			QStringLiteral("no input channels configured") );
		return false;
	}

	// An empty configured device means "the same device the playback path
	// uses", which is what a duplex card wants; naming one explicitly is how a
	// separate capture interface is selected.
	const QString device = plan.device.isEmpty() ? probeDevice() : plan.device;
	const QByteArray deviceName = device.toUtf8();

	if( int err = snd_pcm_open( &m_captureHandle, deviceName.constData(),
			SND_PCM_STREAM_CAPTURE, 0 ); err < 0 )
	{
		printf( "Capture open error on %s: %s\n", deviceName.constData(), snd_strerror( err ) );
		m_captureHandle = nullptr;
		AudioInputPath::publishUnavailable( device, QString::fromUtf8( snd_strerror( err ) ) );
		return false;
	}

	snd_pcm_hw_params_malloc( &m_captureHwParams );
	snd_pcm_hw_params_any( m_captureHandle, m_captureHwParams );
	snd_pcm_hw_params_set_access( m_captureHandle, m_captureHwParams,
		SND_PCM_ACCESS_RW_INTERLEAVED );

	// FLOAT first (it is what the engine's own samples are), S16_LE as the
	// fallback every driver accepts. What was granted is published, so a
	// recording made at S16_LE is never reported as a float capture.
	m_captureFloat = true;
	if( snd_pcm_hw_params_set_format( m_captureHandle, m_captureHwParams,
			SND_PCM_FORMAT_FLOAT ) < 0 )
	{
		m_captureFloat = false;
		if( int err = snd_pcm_hw_params_set_format( m_captureHandle, m_captureHwParams,
				SND_PCM_FORMAT_S16_LE ); err < 0 )
		{
			printf( "Capture format not available on %s: %s\n",
				deviceName.constData(), snd_strerror( err ) );
			closeCapture();
			AudioInputPath::publishUnavailable( device, QString::fromUtf8( snd_strerror( err ) ) );
			return false;
		}
	}

	// THE ARBITRARY INPUT COUNT: the channel count is the configured one, not
	// the stereo bus's two. A driver that will not take it is reported as what
	// it actually granted, never as a silent downmix.
	unsigned int channels = static_cast<unsigned int>( plan.channels );
	if( int err = snd_pcm_hw_params_set_channels( m_captureHandle, m_captureHwParams,
			channels ); err < 0 )
	{
		printf( "Capture channel count (%i) not available on %s: %s\n",
			plan.channels, deviceName.constData(), snd_strerror( err ) );
		closeCapture();
		AudioInputPath::publishUnavailable( device, QString::fromUtf8( snd_strerror( err ) ) );
		return false;
	}

	unsigned int rate = audioEngine() != nullptr
		? static_cast<unsigned int>( audioEngine()->baseSampleRate() )
		: static_cast<unsigned int>( sampleRate() );
	snd_pcm_hw_params_set_rate_near( m_captureHandle, m_captureHwParams, &rate, nullptr );

	m_capturePeriodSize = audioEngine() != nullptr
		? static_cast<snd_pcm_uframes_t>( audioEngine()->framesPerAudioBuffer() )
		: static_cast<snd_pcm_uframes_t>( 512 );
	snd_pcm_uframes_t bufferSize = m_capturePeriodSize * 4;
	snd_pcm_hw_params_set_period_size_near( m_captureHandle, m_captureHwParams,
		&m_capturePeriodSize, nullptr );
	snd_pcm_hw_params_set_buffer_size_near( m_captureHandle, m_captureHwParams, &bufferSize );

	if( int err = snd_pcm_hw_params( m_captureHandle, m_captureHwParams ); err < 0 )
	{
		printf( "Capture hwparams failed on %s: %s\n", deviceName.constData(), snd_strerror( err ) );
		closeCapture();
		AudioInputPath::publishUnavailable( device, QString::fromUtf8( snd_strerror( err ) ) );
		return false;
	}

	// What the device GRANTED (it may be fewer channels or a different rate
	// than the plan asked for; the plan is an intent, this is the measurement).
	unsigned int grantedChannels = 0;
	unsigned int grantedRate = 0;
	snd_pcm_hw_params_get_channels( m_captureHwParams, &grantedChannels );
	snd_pcm_hw_params_get_rate( m_captureHwParams, &grantedRate, nullptr );
	snd_pcm_hw_params_get_period_size( m_captureHwParams, &m_capturePeriodSize, nullptr );
	if( grantedChannels == 0 ) { grantedChannels = 1; }

	m_captureChannels = static_cast<int>( grantedChannels );
	m_captureLeft = std::clamp( plan.left, 0, m_captureChannels - 1 );
	m_captureRight = std::clamp( plan.right, 0, m_captureChannels - 1 );

	// Everything the capture thread touches is allocated HERE, once, off that
	// thread: the loop below must not allocate (the realtime rule).
	const auto blockFrames = static_cast<std::size_t>( m_capturePeriodSize );
	m_captureWide.assign( blockFrames * static_cast<std::size_t>( m_captureChannels ), 0.f );
	m_captureBus.assign( blockFrames, SampleFrame{} );
	if( !m_captureFloat )
	{
		m_captureRaw16.assign( blockFrames * static_cast<std::size_t>( m_captureChannels ), 0 );
	}

	// Software parameters, the read-side mirror of setSWParams(): start when a
	// period is full, wake at a period's availability.
	snd_pcm_sw_params_t* swParams = nullptr;
	snd_pcm_sw_params_malloc( &swParams );
	snd_pcm_sw_params_current( m_captureHandle, swParams );
	snd_pcm_sw_params_set_start_threshold( m_captureHandle, swParams, m_capturePeriodSize );
	snd_pcm_sw_params_set_avail_min( m_captureHandle, swParams, m_capturePeriodSize );
	snd_pcm_sw_params( m_captureHandle, swParams );
	snd_pcm_sw_params_free( swParams );

	if( int err = snd_pcm_prepare( m_captureHandle ); err < 0 )
	{
		printf( "Capture prepare failed on %s: %s\n", deviceName.constData(), snd_strerror( err ) );
		closeCapture();
		AudioInputPath::publishUnavailable( device, QString::fromUtf8( snd_strerror( err ) ) );
		return false;
	}
	if( int err = snd_pcm_start( m_captureHandle ); err < 0 )
	{
		printf( "Capture start failed on %s: %s\n", deviceName.constData(), snd_strerror( err ) );
		closeCapture();
		AudioInputPath::publishUnavailable( device, QString::fromUtf8( snd_strerror( err ) ) );
		return false;
	}

	printf( "ALSA capture: %s, %u channels at %u Hz (%s), period %lu\n",
		deviceName.constData(), grantedChannels, grantedRate,
		m_captureFloat ? "float" : "s16_le",
		static_cast<unsigned long>( m_capturePeriodSize ) );
	AudioInputPath::publishOpen( plan, m_captureChannels, static_cast<int>( grantedRate ),
		m_captureFloat ? QStringLiteral("float") : QStringLiteral("s16_le") );
	return true;
}




void AudioAlsa::closeCapture()
{
	if( m_captureOpen.load( std::memory_order_acquire ) )
	{
		m_captureStop.store( true, std::memory_order_release );
		// Unblock a read that is already waiting on the device. The loop also
		// waits with a bounded timeout, so a device that never delivers still
		// lets the thread notice the stop flag - this is the fast path, not the
		// only one.
		if( m_captureHandle != nullptr )
		{
			snd_pcm_drop( m_captureHandle );
		}
		if( m_captureThread.joinable() )
		{
			m_captureThread.join();
		}
		m_captureOpen.store( false, std::memory_order_release );
		AudioInputPath::publishClosed( QStringLiteral("closed") );
	}
	else if( m_captureThread.joinable() )
	{
		// Opened on the public path but never marked open (a failure between the
		// two): still join, so a half-started thread can never outlive this.
		m_captureStop.store( true, std::memory_order_release );
		m_captureThread.join();
	}

	if( m_captureHandle != nullptr )
	{
		snd_pcm_close( m_captureHandle );
		m_captureHandle = nullptr;
	}
	if( m_captureHwParams != nullptr )
	{
		snd_pcm_hw_params_free( m_captureHwParams );
		m_captureHwParams = nullptr;
	}
	m_captureChannels = 0;
}




int AudioAlsa::handleCaptureError( int _err )
{
	if( _err == -EPIPE )
	{
		// Overrun: the capture thread was too late. Recoverable, counted.
		AudioInputPath::addOverrun();
		const int err = snd_pcm_prepare( m_captureHandle );
		if( err < 0 )
		{
			printf( "Can't recover capture from overrun, prepare failed: %s\n",
				snd_strerror( err ) );
		}
		else
		{
			snd_pcm_start( m_captureHandle );
		}
		return 0;
	}
#ifdef ESTRPIPE
	if( _err == -ESTRPIPE )
	{
		AudioInputPath::addOverrun();
		int err = 0;
		while( ( err = snd_pcm_resume( m_captureHandle ) ) == -EAGAIN )
		{
			sleep( 1 );
		}
		if( err < 0 )
		{
			err = snd_pcm_prepare( m_captureHandle );
			if( err < 0 )
			{
				printf( "Can't recover capture from suspend, prepare failed: %s\n",
					snd_strerror( err ) );
			}
		}
		return 0;
	}
#endif
	return _err;
}




void AudioAlsa::publishCaptured( const float* interleaved, int channels, snd_pcm_uframes_t frames )
{
	AudioEngine* engine = audioEngine();
	if( engine == nullptr || interleaved == nullptr || frames == 0 )
	{
		return;
	}

	// 1. The N-CHANNEL path (feature row 64): every captured channel, so a
	//    record route can select any of them - including channels 2..N-1, which
	//    the stereo bus below cannot carry.
	engine->pushInputFramesWide( interleaved, channels, static_cast<f_cnt_t>( frames ) );

	// 2. The STEREO bus the rest of the engine already reads
	//    (AudioEngine::inputBuffer(), SampleRecordHandle, any play handle):
	//    the configured pair of captured channels, so the existing input
	//    consumers see the interface the user selected rather than a fixed
	//    first-two-channels mapping.
	const auto width = static_cast<snd_pcm_uframes_t>( channels );
	const auto left = static_cast<snd_pcm_uframes_t>( m_captureLeft );
	const auto right = static_cast<snd_pcm_uframes_t>( m_captureRight );
	for( snd_pcm_uframes_t i = 0; i < frames; ++i )
	{
		m_captureBus[i] = SampleFrame( interleaved[i * width + left],
			interleaved[i * width + right] );
	}
	engine->pushInputFrames( m_captureBus.data(), static_cast<f_cnt_t>( frames ) );

	AudioInputPath::addCapturedFrames( static_cast<std::uint64_t>( frames ) );
}




void AudioAlsa::captureLoop()
{
	// Realtime rules, kept: no allocation, no locks, no QString and no logging
	// in this loop - the buffers were allocated in openCapture(), and the
	// counters it touches are relaxed atomics.
	while( !m_captureStop.load( std::memory_order_acquire ) )
	{
		// A BOUNDED wait rather than a bare blocking read, so the stop flag is
		// honoured even when the device hands over nothing: the 200 ms bound is
		// what closeCapture()'s join relies on when a device goes quiet.
		const int ready = snd_pcm_wait( m_captureHandle, kCaptureWaitMs );
		if( m_captureStop.load( std::memory_order_acquire ) )
		{
			break;
		}
		if( ready < 0 )
		{
			handleCaptureError( ready );
			continue;
		}
		if( ready == 0 )
		{
			continue;  // timeout: nothing to read yet
		}

		snd_pcm_sframes_t got = 0;
		if( m_captureFloat )
		{
			got = snd_pcm_readi( m_captureHandle, m_captureWide.data(), m_capturePeriodSize );
		}
		else
		{
			got = snd_pcm_readi( m_captureHandle, m_captureRaw16.data(), m_capturePeriodSize );
			if( got > 0 )
			{
				const auto samples = static_cast<std::size_t>( got )
					* static_cast<std::size_t>( m_captureChannels );
				for( std::size_t i = 0; i < samples; ++i )
				{
					m_captureWide[i] = static_cast<float>( m_captureRaw16[i] ) / 32768.0f;
				}
			}
		}

		if( got < 0 )
		{
			handleCaptureError( static_cast<int>( got ) );
			continue;
		}
		if( got == 0 )
		{
			continue;
		}
		publishCaptured( m_captureWide.data(), m_captureChannels,
			static_cast<snd_pcm_uframes_t>( got ) );
	}
}




int AudioAlsa::setHWParams( const ch_cnt_t _channels, snd_pcm_access_t _access )
{
	// choose all parameters
	if (int err = snd_pcm_hw_params_any(m_handle, m_hwParams); err < 0)
	{
		printf( "Broken configuration for playback: no configurations "
				"available: %s\n", snd_strerror( err ) );
		return err;
	}

	// set the interleaved read/write format
	if (int err = snd_pcm_hw_params_set_access(m_handle, m_hwParams, _access); err < 0)
	{
		printf( "Access type not available for playback: %s\n",
							snd_strerror( err ) );
		return err;
	}

	// set the sample format
	if (int err = snd_pcm_hw_params_set_format(m_handle, m_hwParams, SND_PCM_FORMAT_FLOAT); err < 0)
	{
		printf("Failed to set PCM format: %s", snd_strerror(err));
		return err;
	}

	// set the count of channels
	if (int err = snd_pcm_hw_params_set_channels(m_handle, m_hwParams, _channels); err < 0)
	{
		printf( "Channel count (%i) not available for playbacks: %s\n"
				"(Does your soundcard not support surround?)\n",
					_channels, snd_strerror( err ) );
		return err;
	}

	// set the sample rate
	if (int err = snd_pcm_hw_params_set_rate(m_handle, m_hwParams, sampleRate(), 0); err < 0)
	{
		if (int err = snd_pcm_hw_params_set_rate(m_handle, m_hwParams, audioEngine()->baseSampleRate(), 0); err < 0)
		{
			printf( "Could not set sample rate: %s\n",
							snd_strerror( err ) );
			return err;
		}
	}

	m_periodSize = audioEngine()->framesPerPeriod();
	m_bufferSize = m_periodSize * 8;
	int dir;
	if (int err = snd_pcm_hw_params_set_period_size_near(m_handle, m_hwParams, &m_periodSize, &dir); err < 0)
	{
		printf( "Unable to set period size %lu for playback: %s\n",
					m_periodSize, snd_strerror( err ) );
		return err;
	}
	dir = 0;
	if (int err = snd_pcm_hw_params_get_period_size(m_hwParams, &m_periodSize, &dir); err < 0)
	{
		printf( "Unable to get period size for playback: %s\n",
							snd_strerror( err ) );
	}

	dir = 0;
	if (int err = snd_pcm_hw_params_set_buffer_size_near(m_handle, m_hwParams, &m_bufferSize); err < 0)
	{
		printf( "Unable to set buffer size %lu for playback: %s\n",
					m_bufferSize, snd_strerror( err ) );
		return ( err );
	}

	if (int err = snd_pcm_hw_params_get_buffer_size(m_hwParams, &m_bufferSize); 2 * m_periodSize > m_bufferSize)
	{
		printf( "buffer to small, could not use\n" );
		return ( err );
	}


	// write the parameters to device
	if (int err = snd_pcm_hw_params(m_handle, m_hwParams); err < 0)
	{
		printf( "Unable to set hw params for playback: %s\n",
							snd_strerror( err ) );
		return ( err );
	}

	return ( 0 );	// all ok
}




int AudioAlsa::setSWParams()
{
	// get the current swparams
	if (int err = snd_pcm_sw_params_current(m_handle, m_swParams); err < 0)
	{
		printf( "Unable to determine current swparams for playback: %s"
						"\n", snd_strerror( err ) );
		return err;
	}

	// start the transfer when a period is full
	if (int err = snd_pcm_sw_params_set_start_threshold(m_handle, m_swParams, m_periodSize); err < 0)
	{
		printf( "Unable to set start threshold mode for playback: %s\n",
							snd_strerror( err ) );
		return err;
	}

	// allow the transfer when at least m_periodSize samples can be
	// processed
	if (int err = snd_pcm_sw_params_set_avail_min(m_handle, m_swParams, m_periodSize); err < 0)
	{
		printf( "Unable to set avail min for playback: %s\n",
							snd_strerror( err ) );
		return err;
	}

	// align all transfers to 1 sample
	
#if SND_LIB_VERSION < ((1<<16)|(0)|16)
	if( ( err = snd_pcm_sw_params_set_xfer_align( m_handle,
							m_swParams, 1 ) ) < 0 )
	{
		printf( "Unable to set transfer align for playback: %s\n",
							snd_strerror( err ) );
		return err;
	}
#endif

	// write the parameters to the playback device
	if (int err = snd_pcm_sw_params(m_handle, m_swParams); err < 0)
	{
		printf( "Unable to set sw params for playback: %s\n",
							snd_strerror( err ) );
		return err;
	}

	return 0;	// all ok
}

} // namespace lmms

#endif // LMMS_HAVE_ALSA
