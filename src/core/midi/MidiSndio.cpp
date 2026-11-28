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
#include <bit>
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
//!
//! Protocol notes. The overflow/pushing/failed operations use the default seq_cst
//! ordering on purpose: the argument in runWriter() needs one single total order of
//! them. They are lock-free read-modify-writes, so they are fine on the audio thread.
struct MidiSndio::OutputWriter
{
	//! One complete event. The queue moves events whole, never individual bytes.
	struct Message
	{
		std::array<unsigned char, 3> bytes{};
		std::uint8_t length = 0;
	};

	static_assert( std::atomic<std::uint32_t>::is_always_lock_free,
		"the audio thread must never take a lock for the producer count" );

	mio_hdl* hdl = nullptr;
	//! Set when MidiSndio is being torn down: the writer drains what is queued,
	//! then closes the handle and exits.
	std::atomic<bool> closing{false};
	//! Set once by the writer when the device failed for good. The writer then keeps
	//! consuming (and counting) until closing, so the handle outlives the reader.
	std::atomic<bool> failed{false};
	//! Set by a producer that refused an event; cleared only by the writer, as the last
	//! step of recovery. While it is set every event is refused, so nothing accepted
	//! after the overflow can be cut by the recovery releases.
	std::atomic<bool> overflow{false};
	//! Producers inside submit().
	std::atomic<std::uint32_t> pushing{0};
	MpscQueue<Message, MidiSndio::OutputQueueCapacity> queue;
	//! Events refused because queue was full, and events the device never took.
	std::atomic<std::uint64_t> dropped{0};
	std::atomic<std::uint64_t> undelivered{0};
	//! Notes the device is sounding, bit = channel * 128 + key (16 channels x 128 keys).
	//! Plain, not atomic: touched ONLY by the writer thread (deliver() and recovery).
	std::array<std::uint64_t, 32> sounding{};
	std::promise<void> finished;

	//! Hands one complete event to the writer, or refuses and counts it. Audio thread:
	//! no allocation, lock, system call or log.
	void submit( const Message& message )
	{
		pushing.fetch_add( 1 );
		const bool accepted = !failed.load() && !overflow.load() && queue.push( message );
		if( !accepted && !failed.load() )
		{
			// Set before leaving the pushing window: a writer that sees no push in
			// flight therefore also sees every refusal's flag.
			overflow.store( true );
		}
		pushing.fetch_sub( 1 );
		if( !accepted )
		{
			dropped.fetch_add( 1, std::memory_order_relaxed );
		}
	}

	//! Records what a completely written event did to the device's sounding notes.
	//! Writer thread only. Note On with velocity > 0 sets the bit; Note Off, or Note On
	//! with velocity 0, clears it.
	void trackSounding( const unsigned char* bytes, std::size_t length ) noexcept
	{
		const unsigned status = bytes[0] & 0xF0u;
		if( length != 3 || ( status != 0x80 && status != 0x90 ) )
		{
			return;
		}
		const unsigned index = ( bytes[0] & 0x0Fu ) * 128u + ( bytes[1] & 0x7Fu );
		const std::uint64_t mask = std::uint64_t{1} << ( index % 64 );
		if( status == 0x90 && bytes[2] != 0 )
		{
			sounding[index / 64] |= mask;
		}
		else
		{
			sounding[index / 64] &= ~mask;
		}
	}

	//! Writes one event to the device, or counts it when the device has failed for good.
	//! Only the writer thread calls this. A short write that mio_eof() confirms is
	//! terminal; a short write interrupted by a signal is retried.
	void deliver( const unsigned char* bytes, std::size_t length )
	{
		if( failed.load() )
		{
			undelivered.fetch_add( 1 );
			return;
		}
		std::size_t done = 0;
		while( done < length )
		{
			errno = 0;
			const std::size_t written = mio_write( hdl, bytes + done, length - done );
			done += written;
			if( done < length && ( mio_eof( hdl ) != 0 || ( written == 0 && errno != EINTR ) ) )
			{
				failed.store( true );
				undelivered.fetch_add( 1 );
				return;
			}
		}
		trackSounding( bytes, length );
	}

	//! Ends an overflow: releases every note the device is sounding, held notes included,
	//! then clears the flag as the LAST step. `overflow` is still set while the releases are
	//! written, so every producer is refused until all of them are out and nothing accepted
	//! after the overflow can be cut by a release. On a failed device nothing can be
	//! written, so the record is just cleared. At most 2048 messages.
	void recoverOverflow()
	{
		for( std::size_t word = 0; word < sounding.size(); ++word )
		{
			// A copy: deliver() clears the live bit through trackSounding().
			std::uint64_t bits = sounding[word];
			while( bits != 0 && !failed.load() )
			{
				const unsigned index = static_cast<unsigned>( word * 64 )
					+ static_cast<unsigned>( std::countr_zero( bits ) );
				bits &= bits - 1;
				Message release;
				release.bytes = { static_cast<unsigned char>( 0x80 | ( index / 128 ) ),
					static_cast<unsigned char>( index % 128 ), 0x00 };
				release.length = 3;
				deliver( release.bytes.data(), release.length );
			}
		}
		if( failed.load() )
		{
			sounding.fill( 0 );
		}
		overflow.store( false );
	}
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

	// Full or failed: the whole event is refused and counted, never a fragment, and
	// never written around a refused event: either would corrupt the byte stream.
	// After a refusal the writer releases every note it has sounded.
	if( m_output != nullptr )
	{
		m_output->submit( message );
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
//!
//! Why the exits are final. `closing` is stored only after the reader stopped and the
//! producers are gone, so closingSeen followed by an empty pop is final. closingSeen,
//! overflowSeen and quiet are all read BEFORE the pop: an event published after the pop
//! cannot be missed by a decision based on state read earlier. overflowSeen + quiet (no
//! push was in flight when observed, and every later push sees overflow and refuses) + an
//! empty pop means every event accepted before the refusal has been delivered, so the
//! recovery releases land after them. `overflow` stays set until every release is written,
//! so every producer is refused meanwhile and nothing accepted after the overflow can be
//! cut by a release. The writer never
//! leaves on a device failure: it keeps consuming (and counting) until `closing`, so
//! mio_close() runs only after the reader thread stopped using the handle.
void MidiSndio::runWriter(std::shared_ptr<OutputWriter> out)
{
	for( ;; )
	{
		const bool closingSeen = out->closing.load( std::memory_order_acquire );
		const bool overflowSeen = out->overflow.load();
		const bool quiet = out->pushing.load() == 0;
		OutputWriter::Message message;
		if( out->queue.pop( &message ) )
		{
			out->deliver( message.bytes.data(), message.length );
			continue;
		}
		if( overflowSeen && quiet )
		{
			out->recoverOverflow();
			continue;
		}
		if( closingSeen && !overflowSeen )
		{
			break;
		}
		std::this_thread::sleep_for( writerIdlePace );
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
