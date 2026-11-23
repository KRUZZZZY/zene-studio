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

#include <algorithm>
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
//! timeout. The same pipe carries the output wake (requestWake(), BUG 12): a sender
//! that queued a message asks for one byte, and the worker drains at the top of its
//! loop. The device is non-blocking in both directions, so nothing else in the loop
//! can block: a drain writes what the device takes and keeps the rest for the next
//! round. The join is therefore bounded by one 5 ms sleep, one drain of the queued
//! messages and the parsing of at most one 128-byte read. Only after the join are the
//! device handles, the poll array and the wake pipe released. The thread is never
//! terminated: the old 1 s wait was always shorter than the 10 s poll, so every close
//! of an idle device ended in QThread::terminate() with the worker inside the
//! ALSA/parser code.
static constexpr unsigned long StopWarnAfterMs = 2000;


//! How long the worker waits for the device to accept the rest of a partly written message.
static constexpr int OutputRetryTimeoutMs = 5;

//! The idle poll timeout: with no input and no queued output, the wake pipe ends the wait.
static constexpr int IdleTimeoutMs = 10000;


namespace
{

//! The message processOutEvent() is assembling on this thread (MidiClientRaw writes it byte by
//! byte through sendByte()); per thread, so concurrent senders never interleave their bytes.
//! Three bytes: the longest message this encoder produces (a note-on, a control change, a pitch
//! bend); a longer one (SysEx) is not produced here and is truncated rather than overflowing.
thread_local unsigned char t_outBytes[3];
thread_local std::uint8_t t_outLength = 0;

} // namespace


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

	// The output is written by the worker and must not block it either: a device whose buffer is
	// full answers -EAGAIN, and writePending() keeps the rest of the message for the next round.
	if (m_output != nullptr)
	{
		snd_rawmidi_nonblock( m_output, 1 );
	}

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

	// The worker was the only writer, so what it had queued but not yet written is written here,
	// before the device is closed: output produced during shutdown is delayed, never lost.
	drainOutQueue();

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
	// Collected into the sending thread's message; processOutEvent() queues it and the worker is the
	// only writer of the device. Writing here is what blocked the audio path (BUG 12, new-p12-118).
	if (t_outLength < sizeof( t_outBytes )) { t_outBytes[t_outLength++] = c; }
}




void MidiAlsaRaw::processOutEvent( const MidiEvent& event, const TimePos& time, const MidiPort* port )
{
	// Called from the audio path (a render period, an instrument's note): nothing here may write the
	// device. The event is encoded on this thread by MidiClientRaw's encoder, which hands its bytes to
	// sendByte(), and the whole message is queued for the worker. Both producers - this one and the
	// GUI's patch change - push into one multi-producer queue, so neither has to know about the other.
	t_outLength = 0;
	MidiClientRaw::processOutEvent( event, time, port );
	if (t_outLength == 0) { return; }

	OutMessage message;
	std::copy( t_outBytes, t_outBytes + t_outLength, message.bytes.begin() );
	message.length = t_outLength;
	t_outLength = 0;

	if (!m_outQueue.push( message ))
	{
		// Full: refuse and count, exactly as a full device buffer is refused and counted. Nothing
		// waits here, so a stalled device or a stopped worker cannot hold an audio period.
		m_outDropped.fetch_add( 1, std::memory_order_relaxed );
		return;
	}
	requestWake();
}




void MidiAlsaRaw::requestWake()
{
	// One byte per drain, not per event: a byte still in the pipe is a wake that has not been served
	// yet, so two senders asking at once cannot lose one. The pipe is non-blocking, so a full pipe
	// refuses the byte without waiting - and it already holds the wake that was needed.
	if (m_wakeRequested.exchange( true, std::memory_order_seq_cst )) { return; }
	if (m_wakePipe[1] < 0) { return; }
	const char wake = 1;
	[[maybe_unused]] const ssize_t written = ::write( m_wakePipe[1], &wake, 1 );
}




void MidiAlsaRaw::acknowledgeWake()
{
	m_wakeRequested.store( false, std::memory_order_seq_cst );
}




void MidiAlsaRaw::drainOutQueue()
{
	for (;;)
	{
		// A message the device took only partly goes first: the next one must not overtake it.
		if (m_written < m_writing.length && !writePending()) { return; }

		OutMessage message;
		if (!m_outQueue.pop( &message )) { return; }
		m_writing = message;
		m_written = 0;
		if (!writePending()) { return; }
	}
}




bool MidiAlsaRaw::writePending()
{
	if (m_output == nullptr)
	{
		// No device: the message is dropped, exactly as it was when sendByte() had no handle.
		m_written = m_writing.length;
		return true;
	}

	while (m_written < m_writing.length)
	{
		const ssize_t written = snd_rawmidi_write( m_output, m_writing.bytes.data() + m_written,
					static_cast<std::size_t>( m_writing.length - m_written ) );
		if (written < 0)
		{
			// A full device buffer is not an error: keep the rest of the message and offer it again on
			// the next round. Any other failure drops the message, which is what the unprocessed
			// return value of the blocking write did before.
			if (written == -EAGAIN) { return false; }
			m_written = m_writing.length;
			return true;
		}
		if (written == 0) { return false; }
		m_written = static_cast<std::uint8_t>( m_written + written );
	}
	return true;
}




void MidiAlsaRaw::run()
{
	auto buf = std::array<unsigned char, 128>{};
	//int cnt = 0;
	while (!m_quit.load(std::memory_order_acquire))
	{
		// Output first: acknowledge before draining, so a sender that queues after this point asks for
		// a wake again (the same order MidiAlsaSeq's worker uses). The drain writes what the device
		// takes and keeps the rest, so it never waits.
		acknowledgeWake();
		drainOutQueue();

		msleep( 5 );	// must do that, otherwise this thread takes
				// too much CPU-time, even with LowPriority...
		// The last entry is the wake pipe: stopWorker() makes it readable, so this
		// returns at once on shutdown and the 10 s timeout only paces an idle device.
		// While a message is only partly written the timeout is short, so the rest of it
		// reaches a device that has drained its buffer promptly.
		const int timeoutMs = m_written < m_writing.length ? OutputRetryTimeoutMs : IdleTimeoutMs;
		if (int err = poll(m_pfds, m_npfds + 1, timeoutMs); err < 0 && errno == EINTR)
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
		// The wake pipe (the last descriptor) is readable: a sender queued output, or stopWorker()
		// asked for the stop. Consuming the bytes keeps the pipe from staying readable, which would
		// turn every poll into an immediate return and spin this low-priority thread.
		if (m_pfds[m_npfds].revents & POLLIN)
		{
			char wakeBytes[64];
			[[maybe_unused]] const ssize_t consumed = ::read( m_wakePipe[0], wakeBytes, sizeof( wakeBytes ) );
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
