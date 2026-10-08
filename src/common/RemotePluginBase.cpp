/*
 * RemotePluginBase.cpp - base class providing RPC like mechanisms
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "RemotePluginBase.h"

#include <charconv>

#ifndef SYNC_WITH_SHM_FIFO
#include <cerrno>
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
#include <QCoreApplication>
#include <QThread>
#endif


namespace lmms
{


#ifdef SYNC_WITH_SHM_FIFO
RemotePluginBase::RemotePluginBase(shmFifo * _in, shmFifo * _out) :
	m_in(_in),
	m_out(_out)
#else
RemotePluginBase::RemotePluginBase() :
	m_socket(-1),
	m_invalid(false)
#endif
{
	// make sure, we're using common ways to print/scan
	// floats to/from strings (',' vs. '.' for decimal point etc.)
	setlocale(LC_NUMERIC, "C");
#ifndef SYNC_WITH_SHM_FIFO
	pthread_mutex_init(&m_receiveMutex, nullptr);
	pthread_mutex_init(&m_sendMutex, nullptr);
#endif
}




RemotePluginBase::~RemotePluginBase()
{
#ifdef SYNC_WITH_SHM_FIFO
	delete m_in;
	delete m_out;
#else
	pthread_mutex_destroy(&m_receiveMutex);
	pthread_mutex_destroy(&m_sendMutex);
#endif
}




std::size_t RemotePluginBase::frameFromInts( char * _out, std::size_t _capacity, int _id,
					const std::int32_t * _values, std::size_t _count )
{
	// Same bytes sendMessage() writes for a message built with addInt(): each argument is its decimal
	// string, length-prefixed. The digits go into a stack buffer, so nothing is allocated.
	char * cursor = _out;
	char * const end = _out + _capacity;
	const auto put = [&cursor, end]( const void * _data, std::size_t _size ) -> bool
	{
		if( static_cast<std::size_t>( end - cursor ) < _size ) { return false; }
		std::memcpy( cursor, _data, _size );
		cursor += _size;
		return true;
	};

	const std::int32_t header[2] = { _id, static_cast<std::int32_t>( _count ) };
	if( !put( header, sizeof header ) ) { return 0; }
	for( std::size_t i = 0; i < _count; ++i )
	{
		char digits[16];
		const auto digitsEnd = std::to_chars( digits, digits + sizeof digits, _values[i] ).ptr;
		const auto length = static_cast<std::int32_t>( digitsEnd - digits );
		if( !put( &length, sizeof length ) || !put( digits, static_cast<std::size_t>( length ) ) ) { return 0; }
	}
	return static_cast<std::size_t>( cursor - _out );
}

std::size_t RemotePluginBase::frameFromMessage( char * _out, std::size_t _capacity, const message & _m )
{
	char * cursor = _out;
	char * const end = _out + _capacity;
	const auto put = [&cursor, end]( const void * _data, std::size_t _size ) -> bool
	{
		if( static_cast<std::size_t>( end - cursor ) < _size ) { return false; }
		std::memcpy( cursor, _data, _size );
		cursor += _size;
		return true;
	};

	const std::int32_t header[2] = { _m.id, static_cast<std::int32_t>( _m.data.size() ) };
	if( !put( header, sizeof header ) ) { return 0; }
	for( const auto & arg : _m.data )
	{
		const auto length = static_cast<std::int32_t>( arg.size() );
		if( !put( &length, sizeof length ) || !put( arg.data(), arg.size() ) ) { return 0; }
	}
	return static_cast<std::size_t>( cursor - _out );
}

RemotePluginBase::SendResult RemotePluginBase::sendFrameNonBlocking( const char * _frame, std::size_t _size )
{
#ifdef SYNC_WITH_SHM_FIFO
	// The FIFO's writer waits on its semaphores and has no non-blocking form. This transport is unproven
	// against the audio-thread bound; it writes the frame as the blocking path always did.
	m_out->lock();
	m_out->writeRaw( _frame, static_cast<int>( _size ) );
	m_out->unlock();
	m_out->messageSent();
	return SendResult::Sent;
#else
	if( isInvalid() ) { return SendResult::Broken; }
	if( pthread_mutex_trylock( &m_sendMutex ) != 0 ) { return SendResult::NoRoom; }

	SendResult result = SendResult::NoRoom;
	bool room = true;
#ifdef SIOCOUTQ
	// Bytes still queued in the socket's send buffer, and the buffer's size. Linux reports SO_SNDBUF doubled,
	// so half of it is the conservative figure. A frame is sent only when it fits in what is left of that,
	// with a margin.
	int queued = 0;
	int sndbuf = 0;
	socklen_t sndbufLength = sizeof sndbuf;
	room = ::ioctl( m_socket, SIOCOUTQ, &queued ) == 0
		&& ::getsockopt( m_socket, SOL_SOCKET, SO_SNDBUF, &sndbuf, &sndbufLength ) == 0
		&& queued >= 0
		&& static_cast<std::size_t>( queued ) + _size + 1024 <= static_cast<std::size_t>( sndbuf ) / 2;
#endif
	if( room )
	{
		int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
		flags |= MSG_NOSIGNAL;
#endif
		const ssize_t written = ::send( m_socket, _frame, _size, flags );
		if( written == static_cast<ssize_t>( _size ) )
		{
			result = SendResult::Sent;
		}
		else if( written < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK ) )
		{
			result = SendResult::NoRoom; // nothing was written
		}
		else
		{
			// A part of the frame went out, or the connection failed: the stream cannot be resynchronised.
			invalidate();
			result = SendResult::Broken;
		}
	}
	pthread_mutex_unlock( &m_sendMutex );
	return result;
#endif
}

int RemotePluginBase::sendMessage(const message & _m)
{
#ifdef SYNC_WITH_SHM_FIFO
	m_out->lock();
	m_out->writeInt(_m.id);
	m_out->writeInt(_m.data.size());
	int j = 8;
	for (unsigned int i = 0; i < _m.data.size(); ++i)
	{
		m_out->writeString(_m.data[i]);
		j += 4 + _m.data[i].size();
	}
	m_out->unlock();
	m_out->messageSent();
#else
	pthread_mutex_lock(&m_sendMutex);
	writeInt(_m.id);
	writeInt(_m.data.size());
	int j = 8;
	for (const auto& str : _m.data)
	{
		writeString(str);
		j += 4 + str.size();
	}
	pthread_mutex_unlock(&m_sendMutex);
#endif

	return j;
}




RemotePluginBase::message RemotePluginBase::receiveMessage()
{
#ifdef SYNC_WITH_SHM_FIFO
	m_in->waitForMessage();
	m_in->lock();
	message m;
	m.id = m_in->readInt();
	const int s = m_in->readInt();
	for (int i = 0; i < s; ++i)
	{
		m.data.push_back(m_in->readString());
	}
	m_in->unlock();
#else
	pthread_mutex_lock(&m_receiveMutex);
	message m;
	m.id = readInt();
	const int s = readInt();
	for (int i = 0; i < s; ++i)
	{
		m.data.push_back(readString());
	}
	pthread_mutex_unlock(&m_receiveMutex);
#endif
	return m;
}




RemotePluginBase::message RemotePluginBase::waitForMessage(
							const message & _wm,
							bool _busy_waiting)
{
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
	if (_busy_waiting)
	{
		// No point processing events outside of the main thread
		_busy_waiting = QThread::currentThread() ==
					QCoreApplication::instance()->thread();
	}

	struct WaitDepthCounter
	{
		WaitDepthCounter(int & depth, bool busy) :
			m_depth(depth),
			m_busy(busy)
		{
			if (m_busy) { ++m_depth; }
		}

		~WaitDepthCounter()
		{
			if (m_busy) { --m_depth; }
		}

		int & m_depth;
		bool m_busy;
	};

	WaitDepthCounter wdc(waitDepthCounter(), _busy_waiting);
#endif
	while (!isInvalid())
	{
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
		if (_busy_waiting && !messagesLeft())
		{
			// FIXME: Can hang sometimes (due to too many messages?)
			QCoreApplication::processEvents(
				QEventLoop::ExcludeUserInputEvents, 50);
			continue;
		}
#endif
		message m = receiveMessage();
		processMessage(m);
		if (m.id == _wm.id)
		{
			return m;
		}
		else if (m.id == IdUndefined)
		{
			return m;
		}
	}

	return message();
}

} // namespace lmms
