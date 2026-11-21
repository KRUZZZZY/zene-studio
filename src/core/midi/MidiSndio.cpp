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

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <sndio.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "ConfigManager.h"


namespace lmms
{


//! How long teardown waits for the writer to drain before it abandons a stalled write.
static constexpr auto writerTeardownGrace = std::chrono::milliseconds( 1000 );


//! The writer's share of the output path: the handle and the read end of the pipe, which the
//! writer closes when it finishes. Whoever holds the shared_ptr keeps them valid.
struct MidiSndio::OutputWriter
{
	mio_hdl* hdl = nullptr;
	int readFd = -1;
	std::promise<void> finished;
};


//! Creates the output pipe. Its write end is non-blocking, so sendByte() never waits on it.
static bool openOutputPipe( int fds[2] )
{
	if( ::pipe( fds ) != 0 )
	{
		return false;
	}
	if( ::fcntl( fds[1], F_SETFL, ::fcntl( fds[1], F_GETFL ) | O_NONBLOCK ) < 0 )
	{
		::close( fds[0] );
		::close( fds[1] );
		fds[0] = -1;
		fds[1] = -1;
		return false;
	}
	return true;
}


MidiSndio::MidiSndio() :
	MidiClientRaw(),
	m_quit( false ),
	m_outPipe{ -1, -1 }
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

	if( !openOutputPipe( m_outPipe ) )
	{
		mio_close( m_hdl );
		m_hdl = nullptr;
		printf( "sndio: failed creating the MIDI output pipe\n" );
		return;
	}

	start( QThread::LowPriority );

	m_output = std::make_shared<OutputWriter>();
	m_output->hdl = m_hdl;
	m_output->readFd = m_outPipe[0];
	m_writerDone = m_output->finished.get_future();
	m_writer = std::thread( &MidiSndio::runWriter, m_output );
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
		// Closing the write end is the end of the queue: the writer writes what is still
		// queued, then closes the handle. The reader has already stopped, so nothing else uses it.
		::close( m_outPipe[1] );
		m_outPipe[1] = -1;

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
	if ( m_hdl == nullptr )
	{
		return;
	}

	// Never waits on the device: the byte goes into the pipe for m_writer. When the pipe is full
	// (the device has stalled for a long time) the byte is dropped instead of holding up the caller.
	[[maybe_unused]] const ssize_t queued = ::write( m_outPipe[1], &c, 1 );
}


//! The only thread that writes to the device, so a blocking mio_write() here cannot hold up the
//! caller of sendByte(). It takes no pointer to MidiSndio, so it can outlive the object.
void MidiSndio::runWriter(std::shared_ptr<OutputWriter> out)
{
	char buf[0x100];
	while( true )
	{
		const ssize_t n = ::read( out->readFd, buf, sizeof( buf ) );
		if( n < 0 && errno == EINTR )
		{
			continue;
		}
		// 0 means the write end is closed and every queued byte has been read.
		if( n <= 0 )
		{
			break;
		}
		for( size_t done = 0; done < static_cast<size_t>( n ); )
		{
			const size_t written = mio_write( out->hdl, buf + done, static_cast<size_t>( n ) - done );
			if( written == 0 )
			{
				break;
			}
			done += written;
		}
	}
	mio_close( out->hdl );
	::close( out->readFd );
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
