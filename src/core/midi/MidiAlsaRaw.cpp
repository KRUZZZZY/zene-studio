/*
 * MidiAlsaRaw.cpp - MIDI client for RawMIDI via ALSA
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "MidiAlsaRaw.h"
#include "ConfigManager.h"


#ifdef LMMS_HAVE_ALSA

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>


namespace lmms
{


//! Shutdown order (Q050-488): stopWorker() sets m_quit and writes one byte to the
//! wake pipe, whose read end is the last entry of the worker's poll set, so a worker
//! blocked waiting for input returns from poll() at once instead of after its 10 s
//! timeout. The device is non-blocking, so nothing else in the loop can block; the
//! join is therefore bounded by one 5 ms sleep plus the parsing of at most one
//! 128-byte read. Only after the join are the device handles, the poll array and
//! the wake pipe released. The thread is never terminated: the old 1 s wait was
//! always shorter than the 10 s poll, so every close of an idle device ended in
//! QThread::terminate() with the worker inside the ALSA/parser code.
static constexpr unsigned long StopWarnAfterMs = 2000;


MidiAlsaRaw::MidiAlsaRaw() :
	MidiClientRaw(),
	m_input(nullptr),
	m_inputp( &m_input ),
	m_output(nullptr),
	m_outputp( &m_output ),
	m_npfds(0),
	m_pfds(nullptr),
	m_wakePipe{ -1, -1 },
	m_quit( false )
{
	if (int err = snd_rawmidi_open(m_inputp, m_outputp, probeDevice().toLatin1().constData(), 0); err < 0)
	{
		printf( "cannot open MIDI-device: %s\n", snd_strerror( err ) );
		m_input = nullptr;
		m_output = nullptr;
		return;
	}

	snd_rawmidi_read( m_input, nullptr, 0 );

	snd_rawmidi_nonblock( m_input, 1 );

	// A failure after the device opened leaves no worker: everything acquired so far
	// is released here, and the destructor finds nothing running.
	const int count = snd_rawmidi_poll_descriptors_count( m_input );
	if (count <= 0 || ::pipe(m_wakePipe) != 0)
	{
		m_wakePipe[0] = m_wakePipe[1] = -1;
		printf( "cannot watch MIDI-device: no poll descriptors or wake pipe\n" );
		closeDevice();
		return;
	}
	for (const int fd : m_wakePipe)
	{
		::fcntl( fd, F_SETFD, FD_CLOEXEC );
		::fcntl( fd, F_SETFL, ::fcntl( fd, F_GETFL ) | O_NONBLOCK );
	}

	m_npfds = count;
	m_pfds = new pollfd[m_npfds + 1];
	if (snd_rawmidi_poll_descriptors(m_input, m_pfds, m_npfds) != m_npfds)
	{
		printf( "cannot get MIDI-device poll descriptors\n" );
		delete[] m_pfds;
		m_pfds = nullptr;
		m_npfds = 0;
		closeDevice();
		closeWakePipe();
		return;
	}
	m_pfds[m_npfds].fd = m_wakePipe[0];
	m_pfds[m_npfds].events = POLLIN;
	m_pfds[m_npfds].revents = 0;

	start( QThread::LowPriority );
}




MidiAlsaRaw::~MidiAlsaRaw()
{
	stopWorker();

	closeDevice();
	delete[] m_pfds;
	closeWakePipe();
}




void MidiAlsaRaw::stopWorker()
{
	m_quit.store( true, std::memory_order_release );
	if (m_wakePipe[1] >= 0)
	{
		// Non-blocking: a full pipe already holds a wake-up, so a short write is fine.
		const char wake = 1;
		[[maybe_unused]] const ssize_t written = ::write( m_wakePipe[1], &wake, 1 );
	}
	if (!wait( StopWarnAfterMs ))
	{
		fprintf( stderr, "MidiAlsaRaw: worker still stopping after %lu ms\n", StopWarnAfterMs );
		wait();
	}
}




void MidiAlsaRaw::closeDevice()
{
	if (m_input != nullptr)
	{
		snd_rawmidi_close( m_input );
		m_input = nullptr;
	}
	if (m_output != nullptr)
	{
		snd_rawmidi_close( m_output );
		m_output = nullptr;
	}
}




void MidiAlsaRaw::closeWakePipe()
{
	for (int& fd : m_wakePipe)
	{
		if (fd >= 0)
		{
			::close( fd );
			fd = -1;
		}
	}
}




QString MidiAlsaRaw::probeDevice()
{
	QString dev = ConfigManager::inst()->value( "MidiAlsaRaw", "device" );
	if( dev == "" )
	{
		if( getenv( "MIDIDEV" ) != nullptr )
		{
			return getenv( "MIDIDEV" );
		}
		return "default";
	}
	return dev;
}




void MidiAlsaRaw::sendByte( unsigned char c )
{
	// A device that failed to open has no output handle; ALSA must not be handed null.
	if (m_output == nullptr) { return; }
	snd_rawmidi_write( m_output, &c, sizeof( c ) );
}




void MidiAlsaRaw::run()
{
	auto buf = std::array<unsigned char, 128>{};
	//int cnt = 0;
	while (!m_quit.load(std::memory_order_acquire))
	{
		msleep( 5 );	// must do that, otherwise this thread takes
				// too much CPU-time, even with LowPriority...
		// The last entry is the wake pipe: stopWorker() makes it readable, so this
		// returns at once on shutdown and the 10 s timeout only paces an idle device.
		if (int err = poll(m_pfds, m_npfds + 1, 10000); err < 0 && errno == EINTR)
		{
			printf( "MidiAlsaRaw::run(): Got EINTR while "
				"polling. Will stop polling MIDI-events from "
				"MIDI-port.\n" );
			break;
		}
		else if (err < 0)
		{
			printf( "poll failed: %s\nWill stop polling "
				"MIDI-events from MIDI-port.\n",
							strerror( errno ) );
			break;
		}
		else if (err == 0)
		{
			//printf( "there seems to be no active MIDI-device %d\n", ++cnt );
			continue;
		}
		if (m_quit.load(std::memory_order_acquire)) { break; }
		unsigned short revents = 0;
		if (int err = snd_rawmidi_poll_descriptors_revents(m_input, m_pfds, m_npfds, &revents); err < 0)
		{
			printf( "cannot get poll events: %s\nWill stop polling "
				"MIDI-events from MIDI-port.\n",
							snd_strerror( errno ) );
			break;
		}
		if( revents & ( POLLERR | POLLHUP ) )
		{
			printf( "POLLERR or POLLHUP\n" );
			break;
		}
		if( !( revents & POLLIN ) )
		{
			continue;
		}

		if (int err = snd_rawmidi_read(m_input, buf.data(), buf.size()); err == -EAGAIN) { continue; }
		else if (err < 0)
		{
			printf( "cannot read from port \"%s\": %s\nWill stop "
				"polling MIDI-events from MIDI-port.\n",
				/*port_name*/"default", snd_strerror( err ) );
			break;
		}
		else if (err == 0) { continue; }
		else
		{
			for (int i = 0; i < err; ++i) { parseData(buf[i]); }
		}
	}

}


} // namespace lmms

#endif // LMMS_HAVE_ALSA
