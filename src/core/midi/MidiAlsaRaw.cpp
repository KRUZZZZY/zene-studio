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
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>


namespace lmms
{


//! Shutdown order (Q050-488): stopWorker() sets m_quit and writes one byte to the
//! wake pipe, whose read end is the last entry of the input worker's poll set, so a
//! worker blocked waiting for input returns from poll() at once instead of after its
//! 10 s timeout. The device is non-blocking in both directions, so nothing else in the
//! input loop can block; the join is therefore bounded by one 5 ms sleep and the
//! parsing of at most one 128-byte read. Output is a second thread of its own
//! (OutputWriter, BUG 12), stopped and joined separately, so an input poll or read
//! error - which ends the input worker - no longer ends output delivery. Only after
//! both threads are joined are the device handles, the poll array and the wake pipe
//! released. No thread is ever terminated: the old 1 s wait was always shorter than
//! the 10 s poll, so every close of an idle device ended in QThread::terminate() with
//! the worker inside the ALSA/parser code.
static constexpr unsigned long StopWarnAfterMs = 2000;


//! The writer's pace while it has nothing to do, and while the device is behind (ms).
static constexpr int OutputIdlePaceMs = 2;
static constexpr int OutputBusyPaceMs = 1;

//! The bounded shutdown budget for a device that keeps refusing a message, and the pause between
//! those retries (ms). A device that cannot take the message within it is counted as undelivered.
static constexpr int OutputCloseRetryMs = 100;
static constexpr int OutputCloseRetryPaceMs = 1;

//! The idle poll timeout: with no input, the wake pipe ends the wait.
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

	// Output gets a thread of its own, started only once the device is fully set up: the failure paths
	// above close the device, and they must leave no writer running.
	if (m_output != nullptr)
	{
		m_outputWriter.start( QThread::LowPriority );
	}
	start( QThread::LowPriority );
}




MidiAlsaRaw::~MidiAlsaRaw()
{
	stopWorker();

	// Output has its own thread: it flushes and counts on its way out. Run the bounded flush again
	// here, after the join, so nothing else writes the device - and so a client whose writer never
	// started (the device failed to open or to start) has its queued messages counted, not lost.
	stopOutput();
	finishOutput();

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
	// Collected into the sending thread's message; processOutEvent() queues it and the output writer is
	// the only writer of the device. Writing here is what blocked the audio path (BUG 12, new-p12-118).
	if (t_outLength < sizeof( t_outBytes )) { t_outBytes[t_outLength++] = c; }
}




void MidiAlsaRaw::processOutEvent( const MidiEvent& event, const TimePos& time, const MidiPort* port )
{
	// Called from the audio path (a render period, an instrument's note), so this may do nothing but
	// queue: no device call, no pipe, no system call of any kind - nothing a full or stalled device can
	// hold up. The event is encoded on this thread by MidiClientRaw's encoder, which hands its bytes to
	// sendByte(), and the whole message is pushed into the queue the output writer drains. Both
	// producers (this one and the GUI's patch change) push into one multi-producer queue.
	t_outLength = 0;
	MidiClientRaw::processOutEvent( event, time, port );
	if (t_outLength == 0) { return; }

	OutMessage message;
	std::copy( t_outBytes, t_outBytes + t_outLength, message.bytes.begin() );
	message.length = t_outLength;
	t_outLength = 0;

	if (!m_outQueue.push( message ))
	{
		// Full: refuse and count. The writer is the faster side of this hand-off, so this is what a
		// device stalled for long enough to fill 1024 messages looks like - and no audio period waits.
		m_outDropped.fetch_add( 1, std::memory_order_relaxed );
	}
}




void MidiAlsaRaw::OutputWriter::run()
{
	// Nothing wakes this thread: a sender must not perform I/O, so the writer paces itself. While the
	// device keeps up, one pass every OutputIdlePaceMs is enough for MIDI; while it is behind (a
	// refused message) the shorter pace retries as soon as the buffer is likely to have drained.
	while (!m_client->m_outputQuit.load( std::memory_order_acquire ))
	{
		const bool nothingWaiting = m_client->drainOutQueue();
		if (m_client->m_outputQuit.load( std::memory_order_acquire )) { break; }
		msleep( nothingWaiting ? OutputIdlePaceMs : OutputBusyPaceMs );
	}
	// Leaving because the client is being destroyed: the bounded flush below retries a refused message
	// and counts what the device never took.
	m_client->finishOutput();
}




void MidiAlsaRaw::stopOutput()
{
	if (!m_outputWriter.isRunning()) { return; }
	m_outputQuit.store( true, std::memory_order_release );
	// The writer's own finishOutput() runs before run() returns, so the join is bounded by its pace
	// plus the shutdown retry budget.
	m_outputWriter.wait();
}




void MidiAlsaRaw::finishOutput()
{
	// Bounded: a device that keeps refusing past this deadline has stalled or gone. What it never took
	// is counted (outputQueueOverflowCount()), so undelivered output is reported instead of vanishing.
	const auto deadline = std::chrono::steady_clock::now()
				+ std::chrono::milliseconds( OutputCloseRetryMs );
	for (;;)
	{
		if (drainOutQueue()) { return; }
		if (std::chrono::steady_clock::now() >= deadline) { break; }
		msleep( OutputCloseRetryPaceMs );
	}

	std::uint64_t undelivered = m_written < m_writing.length ? 1 : 0;
	m_written = m_writing.length;
	OutMessage message;
	while (m_outQueue.pop( &message )) { ++undelivered; }
	if (undelivered > 0)
	{
		m_outUndelivered.fetch_add( undelivered, std::memory_order_relaxed );
	}
}




bool MidiAlsaRaw::drainOutQueue()
{
	for (;;)
	{
		// A message the device took only partly goes first: the next one must not overtake it.
		if (m_written < m_writing.length && !writePending()) { return false; }

		OutMessage message;
		if (!m_outQueue.pop( &message )) { return true; }
		m_writing = message;
		m_written = 0;
		if (!writePending()) { return false; }
	}
}




bool MidiAlsaRaw::writePending()
{
	if (m_output == nullptr)
	{
		// No device (it never opened, or its startup failed): the message cannot be delivered, so it is
		// counted instead of vanishing, and the hand-off moves on.
		m_outUndelivered.fetch_add( 1, std::memory_order_relaxed );
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
			// the next round. Any other failure means the device cannot take it at all, so the message
			// is counted as undelivered rather than retried forever.
			if (written == -EAGAIN) { return false; }
			m_outUndelivered.fetch_add( 1, std::memory_order_relaxed );
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
		// Input only: output is the OutputWriter's, so a poll or read error below (which ends this
		// worker) cannot stop output delivery.
		msleep( 5 );	// must do that, otherwise this thread takes
				// too much CPU-time, even with LowPriority...
		// The last entry is the wake pipe: stopWorker() makes it readable, so this
		// returns at once on shutdown and the 10 s timeout only paces an idle device.
		if (int err = poll(m_pfds, m_npfds + 1, IdleTimeoutMs); err < 0 && errno == EINTR)
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
		// The wake pipe (the last descriptor) is readable: stopWorker() asked for the stop. Consuming
		// the byte keeps the pipe from staying readable, which would turn every poll into an
		// immediate return and spin this low-priority thread.
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
