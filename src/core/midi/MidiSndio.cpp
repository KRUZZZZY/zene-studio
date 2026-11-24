/*
 * MidiSndio.cpp - base-class that implements sndio midi support
 *
 * Copyright (c) 2010-2016 jackmsr@openbsd.net
 * Copyright (c) 2016-2017 David Carlier <devnexen@gmail.com>
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

#include "MidiSndio.h"

#ifdef LMMS_HAVE_SNDIO

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <sndio.h>
#include <poll.h>

#include "ConfigManager.h"


namespace lmms
{


//! How long teardown waits for the writer to drain before it abandons a stalled write.
static constexpr auto writerTeardownGrace = std::chrono::milliseconds( 1000 );
//! The writer's pace while its queue is empty. It has no wake pipe: senders must
//! never perform I/O, and one millisecond is below a MIDI event's useful timing.
static constexpr auto writerIdlePace = std::chrono::milliseconds( 1 );


namespace
{

//! The message processOutEvent() is assembling on this thread. The raw encoder
//! emits at most three bytes per event; a longer one is truncated rather than
//! overflowing. Per-thread, so the audio thread and the clock sender never mix.
thread_local unsigned char t_outBytes[3];
thread_local std::uint8_t t_outLength = 0;

} // namespace


//! The writer's share of the output path: the handle and the bounded queue, which the
//! writer owns until it closes the handle. Whoever holds the shared_ptr keeps it valid.
struct MidiSndio::OutputWriter
{
	//! One complete event. The queue moves events whole, never individual bytes.
	struct Message
	{
		std::array<unsigned char, 3> bytes{};
		std::uint8_t length = 0;
	};

	mio_hdl* hdl = nullptr;
	//! Set when MidiSndio is being torn down: the writer drains what is queued,
	//! then closes the handle and exits.
	std::atomic<bool> closing{false};
	MpscQueue<Message, MidiSndio::OutputQueueCapacity> queue;
	//! Events refused because queue was full, and events the device never took.
	std::atomic<std::uint64_t> dropped{0};
	std::atomic<std::uint64_t> undelivered{0};
	std::promise<void> finished;
};


MidiSndio::MidiSndio() :
	MidiClientRaw(),
	m_hdl( nullptr ),
	m_quit( false )
{
	QString dev = probeDevice();

	if (dev == "")
	{
		m_hdl = mio_open( nullptr, MIO_IN | MIO_OUT, 0 );
	}
	else
	{
		m_hdl = mio_open( dev.toLatin1().constData(), MIO_IN | MIO_OUT, 0 );
	}

	if( m_hdl == nullptr )
	{
		printf( "sndio: failed opening sndio midi device\n" );
		return;
	}

	m_output = std::make_shared<OutputWriter>();
	m_output->hdl = m_hdl;
	m_writerDone = m_output->finished.get_future();
	m_writer = std::thread( &MidiSndio::runWriter, m_output );
	start( QThread::LowPriority );
}


MidiSndio::~MidiSndio()
{
	if( isRunning() )
	{
		m_quit.store(true);
		if( !wait( 1000 ) )
		{
			terminate();
			wait();
		}
	}

	if( m_output )
	{
		// The reader has stopped and no producer can add work now. The writer drains what
		// is queued, then closes the handle.
		m_output->closing.store( true, std::memory_order_release );

		// A writer blocked inside mio_write cannot be stopped from outside without
		// corrupting sndio state, so it is abandoned instead of terminated. It keeps its
		// own handle and closes it when the write returns; nothing here touches it again.
		if( m_writerDone.wait_for( writerTeardownGrace ) == std::future_status::ready )
		{
			m_writer.join();
		}
		else
		{
			m_writer.detach();
		}
	}
}


QString MidiSndio::probeDevice()
{
	QString dev = ConfigManager::inst()->value( "MidiSndio", "device" );

	return dev ;
}


void MidiSndio::sendByte( const unsigned char c )
{
	// Collected into this thread's current event. processOutEvent() queues the
	// complete event, so a full queue can never split a MIDI message.
	if( t_outLength < sizeof( t_outBytes ) )
	{
		t_outBytes[t_outLength++] = c;
	}
}


void MidiSndio::processOutEvent( const MidiEvent& event, const TimePos& time, const MidiPort* port )
{
	// Called from the audio path. The encoder below only writes to this thread's
	// staging array; the fixed queue push is one compare-exchange and never waits,
	// allocates, locks, logs or performs a system call.
	t_outLength = 0;
	MidiClientRaw::processOutEvent( event, time, port );
	if( t_outLength == 0 )
	{
		return;
	}

	OutputWriter::Message message;
	std::copy( t_outBytes, t_outBytes + t_outLength, message.bytes.begin() );
	message.length = t_outLength;
	t_outLength = 0;

	// Full: refuse this whole event and count it. Never keep a fragment and never
	// write around a refused event, because either would corrupt the byte stream.
	if( m_output != nullptr && !m_output->queue.push( message ) )
	{
		m_output->dropped.fetch_add( 1, std::memory_order_relaxed );
	}
}


std::uint64_t MidiSndio::outputQueueOverflowCount() const noexcept
{
	return m_output == nullptr ? 0
		: m_output->dropped.load( std::memory_order_relaxed )
			+ m_output->undelivered.load( std::memory_order_relaxed );
}


//! The only thread that writes to the device, so a blocking mio_write() here cannot hold up the
//! caller of processOutEvent(). It takes no pointer to MidiSndio, so it can outlive the object.
void MidiSndio::runWriter(std::shared_ptr<OutputWriter> out)
{
	bool streamBroken = false;
	while( !streamBroken )
	{
		OutputWriter::Message message;
		if( out->queue.pop( &message ) )
		{
			for( std::size_t done = 0; done < message.length; )
			{
				errno = 0;
				const std::size_t written = mio_write( out->hdl,
					message.bytes.data() + done, message.length - done );
				if( written == 0 )
				{
					if( errno == EINTR )
					{
						continue;
					}
					out->undelivered.fetch_add( 1, std::memory_order_relaxed );
					// A partial message cannot be followed by another one on a
					// device byte stream: stop rather than corrupt what follows.
					streamBroken = done > 0;
					break;
				}
				done += written;
			}
			continue;
		}

		if( out->closing.load( std::memory_order_acquire ) )
		{
			break;
		}
		std::this_thread::sleep_for( writerIdlePace );
	}

	// Whatever remains could never be delivered after the device failed. Count it
	// rather than dropping it silently; the queue is bounded, so this is finite.
	OutputWriter::Message remaining;
	while( out->queue.pop( &remaining ) )
	{
		out->undelivered.fetch_add( 1, std::memory_order_relaxed );
	}

	mio_close( out->hdl );
	out->finished.set_value();
}


void MidiSndio::run()
{
	struct pollfd pfd;
	char buf[0x100];
	while( !m_quit.load() && m_hdl )
	{
		nfds_t nfds = mio_pollfd(m_hdl, &pfd, POLLIN);
		int ret = poll(&pfd, nfds, 100);
		if ( ret < 0 )
			break;
		if ( !ret || !( mio_revents( m_hdl, &pfd ) & POLLIN ) )
			continue;
		size_t n = mio_read(m_hdl, buf, sizeof(buf));
		if ( !n )
		{
			break;
		}
		for (char* p = buf; n > 0; n--, p++)
		{
			parseData( *p );
		}
	}
}


} // namespace lmms

#endif	// LMMS_HAVE_SNDIO
