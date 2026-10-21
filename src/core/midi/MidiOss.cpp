/*
 * MidiOss.cpp - OSS raw MIDI client
 *
 * Copyright (c) 2005-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#include "MidiOss.h"

#ifdef LMMS_HAVE_OSS

#include <cerrno>
#include <cstddef>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "ConfigManager.h"


extern "C" ssize_t zeneOssDeviceWrite( int, const void*, std::size_t );
extern "C" int zeneOssDeviceClose( int );

namespace lmms
{


//! How long teardown waits for the writer to drain before it abandons a stalled write.
static constexpr auto writerTeardownGrace = std::chrono::milliseconds( 1000 );


//! The writer's share of the output path: the device fd and the read end of the pipe.
struct MidiOss::OutputWriter
{
	int fd = -1;
	int readFd = -1;
	std::promise<void> finished;
};


//! Creates the output pipe. Its write end is non-blocking, so sendByte() never waits on it.
bool MidiOss::openOutputPipe( int fds[2] )
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


MidiOss::MidiOss() :
	MidiClientRaw(),
	m_fd( -1 ),
	m_quit( false ),
	m_outPipe{ -1, -1 }
{
	QString dev = probeDevice();

	m_fd = ::open( dev.toLocal8Bit().constData(), O_RDWR );
	if( m_fd < 0 )
	{
		m_fd = ::open( dev.toLocal8Bit().constData(), O_RDONLY );
	}
	if( m_fd < 0 )
	{
		return;
	}

	if( !openOutputPipe( m_outPipe ) )
	{
		zeneOssDeviceClose( m_fd );
		m_fd = -1;
		return;
	}

	start( QThread::LowPriority );

	m_output = std::make_shared<OutputWriter>();
	m_output->fd = m_fd;
	m_output->readFd = m_outPipe[0];
	m_writerDone = m_output->finished.get_future();
	m_writer = std::thread( &MidiOss::runWriter, m_output );
}




MidiOss::~MidiOss()
{
	if( isRunning() )
	{
		m_quit.store( true );
		if( !wait( 1000 ) )
		{
			terminate();
			wait();
		}
	}

	if( m_output )
	{
		::close( m_outPipe[1] );
		m_outPipe[1] = -1;

		if( m_writerDone.wait_for( writerTeardownGrace ) == std::future_status::ready )
		{
			m_writer.join();
		}
		else
		{
			m_writer.detach();
		}
	}

	if( m_fd >= 0 )
	{
		zeneOssDeviceClose( m_fd );
		m_fd = -1;
	}
}




QString MidiOss::probeDevice()
{
	QString dev = ConfigManager::inst()->value( "midioss", "device" );
	if( dev.isEmpty() )
	{
		if( getenv( "MIDIDEV" ) != nullptr )
		{
			return getenv( "MIDIDEV" );
		}
#ifdef __NetBSD__
		return "/dev/rmidi0";
#else
		return "/dev/midi";
#endif
	}
	return dev;
}




void MidiOss::sendByte( const unsigned char c )
{
	if( m_fd < 0 || m_outPipe[1] < 0 )
	{
		return;
	}

	[[maybe_unused]] const ssize_t queued = ::write( m_outPipe[1], &c, 1 );
}




extern "C" __attribute__((weak)) ssize_t zeneOssDeviceWrite( int fd, const void* buf, size_t size )
{
	return ::write( fd, buf, size );
}

extern "C" __attribute__((weak)) int zeneOssDeviceClose( int fd )
{
	return ::close( fd );
}

//! The only thread that writes to the device, so a blocking write() here cannot hold up the caller.
void MidiOss::runWriter( std::shared_ptr<OutputWriter> out )
{
	char buf[0x100];
	while( true )
	{
		const ssize_t n = ::read( out->readFd, buf, sizeof( buf ) );
		if( n < 0 && errno == EINTR )
		{
			continue;
		}
		if( n <= 0 )
		{
			break;
		}
		for( size_t done = 0; done < static_cast<size_t>( n ); )
		{
			const ssize_t written = zeneOssDeviceWrite( out->fd, buf + done, static_cast<size_t>( n ) - done );
			if( written <= 0 )
			{
				if( errno == EINTR )
				{
					continue;
				}
				break;
			}
			done += static_cast<size_t>( written );
		}
	}
	::close( out->readFd );
	out->finished.set_value();
}




void MidiOss::run()
{
	while( m_quit.load() == false && m_fd >= 0 )
	{
		struct pollfd pfd;
		pfd.fd = m_fd;
		pfd.events = POLLIN;
		int ret = ::poll( &pfd, 1, 100 );
		if( ret < 0 )
		{
			if( errno == EINTR )
			{
				continue;
			}
			break;
		}
		if( ret == 0 )
		{
			continue;
		}
		if( !( pfd.revents & POLLIN ) )
		{
			continue;
		}
		char buf[0x100];
		ssize_t n = ::read( m_fd, buf, sizeof( buf ) );
		if( n < 0 )
		{
			if( errno == EINTR || errno == EAGAIN )
			{
				continue;
			}
			break;
		}
		if( n == 0 )
		{
			break;
		}
		for( ssize_t i = 0; i < n; ++i )
		{
			parseData( static_cast<unsigned char>( buf[i] ) );
		}
	}
}


} // namespace lmms

#endif // LMMS_HAVE_OSS
