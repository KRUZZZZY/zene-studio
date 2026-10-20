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
#include <cstdlib>
#include <sndio.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "ConfigManager.h"


namespace lmms
{


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
	m_writer.reset( QThread::create( [this] { writeQueuedBytes(); } ) );
	m_writer->start();
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

	if( m_writer )
	{
		m_quit.store(true);
		// The byte wakes the writer out of its read; it then sees m_quit and stops.
		[[maybe_unused]] const ssize_t woke = ::write( m_outPipe[1], "", 1 );
		if( !m_writer->wait( 1000 ) )
		{
			m_writer->terminate();
			m_writer->wait();
		}
	}

	if( m_hdl != nullptr )
	{
		mio_close( m_hdl );
	}

	if( m_outPipe[0] >= 0 )
	{
		::close( m_outPipe[0] );
		::close( m_outPipe[1] );
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


//! Runs on m_writer, the only thread that writes to the device, so a blocking mio_write() here
//! cannot hold up the caller of sendByte().
void MidiSndio::writeQueuedBytes()
{
	char buf[0x100];
	while( true )
	{
		const ssize_t n = ::read( m_outPipe[0], buf, sizeof( buf ) );
		if( n < 0 && errno == EINTR )
		{
			continue;
		}
		// m_quit is checked after every read, so bytes still queued at shutdown are discarded
		// rather than written to a device that may be going away.
		if( n <= 0 || m_quit.load() )
		{
			break;
		}
		for( size_t done = 0; done < static_cast<size_t>( n ); )
		{
			const size_t written = mio_write( m_hdl, buf + done, static_cast<size_t>( n ) - done );
			if( written == 0 )
			{
				break;
			}
			done += written;
		}
	}
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
