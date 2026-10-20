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

namespace
{

// A wire message has an 8-byte header and each argument needs at least its
// 4-byte string length. Keep the count within the 512 KiB shared FIFO frame
// size on every transport.
constexpr int MaxRemoteMessageArguments = (512 * 1024 - 8) / 4;

} // namespace

 
#ifdef SYNC_WITH_SHM_FIFO
RemotePluginBase::RemotePluginBase(shmFifo * _in, shmFifo * _out) :
	m_in(_in),
	m_out(_out)
#else
RemotePluginBase::RemotePluginBase() :
	m_socket(-1),
	m_peekWindow(PeekWindowBytes),
	m_invalid(false)
#endif
{
	// One whole frame, allocated once here (never on the audio thread), for the bounded receive that
	// hands the audio thread argument views instead of building strings.
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
#ifdef SYNC_WITH_SHM_FIFO
	m_frameScratch.resize(SHM_FIFO_SIZE);
#else
	m_frameScratch.resize(PeekWindowBytes);
#endif
#endif
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
	// Exact room check: the frame is written only when the FIFO has space for all of it, so the write never waits.
	// A data lock that is busy (the client is writing) means there is no room now.
	if( !m_out->tryLockFor( 0 ) ) { return SendResult::NoRoom; }
	if( m_out->freeSpace() < _size )
	{
		m_out->unlock();
		return SendResult::NoRoom;
	}
	m_out->writeRaw( _frame, static_cast<int>( _size ) );
	m_out->unlock();
	m_out->messageSent();
	return SendResult::Sent;
#else
	if( isInvalid() ) { return SendResult::Broken; }
	if( pthread_mutex_trylock( &m_sendMutex ) != 0 ) { return SendResult::NoRoom; }

	// A tail left by an earlier partial send goes first, so the stream keeps its order.
	SendResult result = writeTailLocked();
	if( result == SendResult::Sent )
	{
		result = SendResult::NoRoom;
		bool room = true;
#ifdef SIOCOUTQ
		// Bytes still queued in the socket's send buffer, and the buffer's size. Linux reports SO_SNDBUF doubled,
		// so half of it is the conservative figure. A frame is sent only when it fits in what is left of that,
		// with a margin. Elsewhere there is no such count; the partial-write path below covers it.
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
			else if( written >= 0 )
			{
				// Part of the frame went out. The rest is kept, and goes out before anything else; the
				// frame is in the stream, so it counts as sent. The connection is not failed for a short write.
				const std::size_t rest = _size - static_cast<std::size_t>( written );
				if( rest > m_txTail.size() )
				{
					invalidate();
					result = SendResult::Broken;
				}
				else
				{
					std::memcpy( m_txTail.data(), _frame + written, rest );
					m_txTailOffset = 0;
					m_txTailSize = rest;
					result = SendResult::Sent;
				}
			}
			else if( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR )
			{
				result = SendResult::NoRoom; // nothing was written
			}
			else
			{
				invalidate();
				result = SendResult::Broken;
			}
		}
	}
	pthread_mutex_unlock( &m_sendMutex );
	return result;
#endif
}

#ifndef SYNC_WITH_SHM_FIFO
RemotePluginBase::SendResult RemotePluginBase::writeTailLocked()
{
	if( m_txTailSize == 0 ) { return SendResult::Sent; }
	if( isInvalid() ) { return SendResult::Broken; }

	const std::size_t remaining = m_txTailSize - m_txTailOffset;
	int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
	flags |= MSG_NOSIGNAL;
#endif
	const ssize_t written = ::send( m_socket, m_txTail.data() + m_txTailOffset, remaining, flags );
	if( written == static_cast<ssize_t>( remaining ) )
	{
		m_txTailOffset = 0;
		m_txTailSize = 0;
		return SendResult::Sent;
	}
	if( written > 0 )
	{
		m_txTailOffset += static_cast<std::size_t>( written );
		return SendResult::NoRoom;
	}
	if( written == 0 || errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR )
	{
		return SendResult::NoRoom;
	}
	invalidate();
	return SendResult::Broken;
}

RemotePluginBase::SendResult RemotePluginBase::flushPendingSend()
{
	if( m_txTailSize == 0 ) { return SendResult::Sent; }
	if( isInvalid() ) { return SendResult::Broken; }
	if( pthread_mutex_trylock( &m_sendMutex ) != 0 ) { return SendResult::NoRoom; }
	const SendResult result = writeTailLocked();
	pthread_mutex_unlock( &m_sendMutex );
	return result;
}
#else
RemotePluginBase::SendResult RemotePluginBase::flushPendingSend()
{
	return SendResult::Sent;
}
#endif

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
	if (m_txTailSize != 0)
	{
		// A frame the audio thread left partly sent goes out first, so the stream keeps its order.
		write(m_txTail.data() + m_txTailOffset, static_cast<int>(m_txTailSize - m_txTailOffset));
		m_txTailOffset = 0;
		m_txTailSize = 0;
	}
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
	message m = readFrameLocked();
	m_in->unlock();
#else
	pthread_mutex_lock(&m_receiveMutex);
	message m;
	m.id = readInt();
	const int s = readInt();
	if (s < 0 || s > MaxRemoteMessageArguments)
	{
		invalidate();
		pthread_mutex_unlock(&m_receiveMutex);
		return message();
	}
	for (int i = 0; i < s; ++i)
	{
		m.data.push_back(readString());
	}
	pthread_mutex_unlock(&m_receiveMutex);
#endif
	return m;
}




#ifndef BUILD_REMOTE_PLUGIN_CLIENT
bool RemotePluginBase::parseBoundedFrame(const char * _frame, std::size_t _size, message & _out)
{
	// The frame was already checked whole by the peek/payload reader; validate it again while walking the
	// length prefixes, so nothing is read past the buffer even if a length changed.
	constexpr std::size_t headerBytes = 2 * sizeof(std::int32_t);
	if (_size < headerBytes) { return false; }
	std::int32_t id = 0;
	std::int32_t count = 0;
	std::memcpy(&id, _frame, sizeof id);
	std::memcpy(&count, _frame + sizeof id, sizeof count);
	if (count < 0) { return false; }
	std::size_t offset = headerBytes;
	for (std::int32_t i = 0; i < count; ++i)
	{
		std::int32_t length = 0;
		if (_size - offset < sizeof length) { return false; }
		std::memcpy(&length, _frame + offset, sizeof length);
		offset += sizeof length;
		if (length < 0 || static_cast<std::size_t>(length) > _size - offset) { return false; }
		offset += static_cast<std::size_t>(length);
	}
	if (offset != _size) { return false; }
	_out.id = id;
	_out.m_bounded = true;
	_out.m_boundedFrame = _frame;
	_out.m_boundedSize = _size;
	_out.m_boundedCount = static_cast<std::size_t>(count);
	return true;
}

#ifndef SYNC_WITH_SHM_FIFO
bool RemotePluginBase::readExactlyWithin(void * _buf, int _len, std::chrono::milliseconds _progress,
	std::chrono::steady_clock::time_point _total)
{
	char * out = static_cast<char *>(_buf);
	int remaining = _len;
	while (remaining > 0)
	{
		const auto left = std::chrono::ceil<std::chrono::milliseconds>(_total - std::chrono::steady_clock::now());
		if (left.count() <= 0) { return false; }
		const auto wait = std::min(_progress, left);
		if (!waitForMessageReadable(static_cast<int>(wait.count()))) { return false; }
		const ssize_t n = ::read(m_socket, out, static_cast<std::size_t>(remaining));
		if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) { continue; }
		if (n <= 0)
		{
			invalidate();
			return false;
		}
		out += n;
		remaining -= static_cast<int>(n);
	}
	return true;
}

RemotePluginBase::ReceiveResult RemotePluginBase::receiveStartedFrame(std::chrono::steady_clock::time_point _deadline,
	std::chrono::milliseconds _progress, message & _out)
{
	// A started frame may take longer than a short query allows. Stop at whichever comes first: the frame's own
	// bound or the query's deadline.
	const auto total = std::min(std::chrono::steady_clock::now() + StartedFrameTimeout, _deadline);
	std::int32_t id = 0;
	std::int32_t count = 0;
	if (!readExactlyWithin(&id, sizeof id, _progress, total) || !readExactlyWithin(&count, sizeof count, _progress, total)
		|| count < 0 || count > MaxRemoteMessageArguments)
	{
		invalidate();
		return ReceiveResult::Lost;
	}
	_out.id = id;
	std::size_t consumed = 2 * sizeof(std::int32_t);
	constexpr std::size_t lengthBytes = sizeof(std::int32_t);
	for (std::int32_t i = 0; i < count; ++i)
	{
		std::int32_t length = 0;
		if (!readExactlyWithin(&length, sizeof length, _progress, total) || length < 0)
		{
			invalidate();
			return ReceiveResult::Lost;
		}
		// The whole frame must fit in the peek window (see peekNextMessage()), so an argument that cannot fit
		// in what is left is malformed. Refuse it before its length is turned into an allocation.
		const auto argSize = static_cast<std::size_t>(length);
		if (consumed > PeekWindowBytes || lengthBytes > PeekWindowBytes - consumed
			|| argSize > PeekWindowBytes - consumed - lengthBytes)
		{
			invalidate();
			return ReceiveResult::Lost;
		}
		consumed += lengthBytes + argSize;
		std::string arg(argSize, '\0');
		if (length > 0 && !readExactlyWithin(arg.data(), length, _progress, total))
		{
			invalidate();
			return ReceiveResult::Lost;
		}
		_out.data.push_back(std::move(arg));
	}
	return ReceiveResult::Got;
}

RemotePluginBase::ReceiveResult RemotePluginBase::receiveBoundedSocketFrame(message & _out)
{
	constexpr std::size_t headerBytes = 2 * sizeof(std::int32_t);
	const std::size_t frameBytes = m_peekedFrameBytes;
	if (frameBytes < headerBytes || frameBytes > m_frameScratch.size())
	{
		invalidate();
		return ReceiveResult::Lost;
	}

	pthread_mutex_lock(&m_receiveMutex);
	std::size_t got = 0;
	while (got < frameBytes)
	{
		const ssize_t n = ::read(m_socket, m_frameScratch.data() + got, frameBytes - got);
		if (n > 0)
		{
			got += static_cast<std::size_t>(n);
			continue;
		}
		if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) { continue; }
		invalidate();
		pthread_mutex_unlock(&m_receiveMutex);
		return ReceiveResult::Lost;
	}
	const bool parsed = parseBoundedFrame(m_frameScratch.data(), frameBytes, _out);
	pthread_mutex_unlock(&m_receiveMutex);
	if (!parsed)
	{
		invalidate();
		return ReceiveResult::Lost;
	}
	return _out.id == IdUndefined ? ReceiveResult::Lost : ReceiveResult::Got;
}

RemotePluginBase::ReceiveResult RemotePluginBase::receiveWithin(std::chrono::steady_clock::time_point _deadline,
	std::chrono::milliseconds _progress, message & _out)
{
	for (;;)
	{
		if (isInvalid()) { return ReceiveResult::Lost; }
		switch (peekNextMessage())
		{
		case Framing::Unusable:
			invalidate();
			return ReceiveResult::Lost;
		case Framing::Complete:
			_out = receiveMessage();
			return _out.id == IdUndefined ? ReceiveResult::Lost : ReceiveResult::Got;
		case Framing::Incomplete:
			if (waitForMessageReadable(0))
			{
				// The frame has started: finish it, as far as it goes.
				return receiveStartedFrame(_deadline, _progress, _out);
			}
			if (std::chrono::steady_clock::now() >= _deadline) { return ReceiveResult::Timeout; }
			{
				const auto left = std::chrono::ceil<std::chrono::milliseconds>(
					_deadline - std::chrono::steady_clock::now());
				if (!waitForMessageReadable(static_cast<int>(left.count())))
				{
					return isInvalid() ? ReceiveResult::Lost : ReceiveResult::Timeout;
				}
			}
			continue;
		}
	}
}
#else
RemotePluginBase::ReceiveResult RemotePluginBase::receiveWithin(std::chrono::steady_clock::time_point _deadline,
	std::chrono::milliseconds, message & _out)
{
	return receiveFrameBefore(_deadline, _out);
}
#endif
#endif // BUILD_REMOTE_PLUGIN_CLIENT

#ifdef SYNC_WITH_SHM_FIFO
RemotePluginBase::message RemotePluginBase::readFrameLocked()
{
	message m;
	m.id = m_in->readInt();
	const int s = m_in->readInt();
	if (s < 0 || s > MaxRemoteMessageArguments)
	{
		invalidate();
		return message();
	}
	for (int i = 0; i < s; ++i)
	{
		m.data.push_back(m_in->readString());
	}
	return m;
}

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
bool RemotePluginBase::readFrameLockedBounded(message & _out)
{
	constexpr std::size_t headerBytes = 2 * sizeof(std::int32_t);
	constexpr std::size_t lengthBytes = sizeof(std::int32_t);
	if (m_frameScratch.size() < headerBytes) { return false; }
	m_in->readRaw(m_frameScratch.data(), static_cast<int>(headerBytes));
	if (m_in->isInvalid()) { return false; }
	std::int32_t count = 0;
	std::memcpy(&count, m_frameScratch.data() + lengthBytes, sizeof count);
	if (count < 0 || count > MaxRemoteMessageArguments) { return false; }
	std::size_t offset = headerBytes;
	for (std::int32_t i = 0; i < count; ++i)
	{
		if (m_frameScratch.size() - offset < lengthBytes) { return false; }
		m_in->readRaw(m_frameScratch.data() + offset, static_cast<int>(lengthBytes));
		if (m_in->isInvalid()) { return false; }
		std::int32_t length = 0;
		std::memcpy(&length, m_frameScratch.data() + offset, sizeof length);
		offset += lengthBytes;
		if (length < 0) { return false; }
		const auto argSize = static_cast<std::size_t>(length);
		if (argSize > m_frameScratch.size() - offset) { return false; }
		if (argSize > 0)
		{
			m_in->readRaw(m_frameScratch.data() + offset, length);
			if (m_in->isInvalid()) { return false; }
		}
		offset += argSize;
	}
	return parseBoundedFrame(m_frameScratch.data(), offset, _out);
}
#endif // BUILD_REMOTE_PLUGIN_CLIENT
#endif

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
RemotePluginBase::ReceiveResult RemotePluginBase::receiveFrameBefore(
	std::chrono::steady_clock::time_point _deadline, message & _out, bool _bounded )
{
	const auto remainingMs = [_deadline]() -> int
	{
		const auto left = std::chrono::ceil<std::chrono::milliseconds>(_deadline - std::chrono::steady_clock::now());
		return left.count() > 0 ? static_cast<int>(left.count()) : 0;
	};

#ifdef SYNC_WITH_SHM_FIFO
	// Each message is written whole and signalled once, so a token means a complete message. The token and the
	// data lock are both taken within the budget; if the lock is not free the token goes back.
	if (isInvalid()) { return ReceiveResult::Lost; }
	if (!m_in->waitMessageFor(remainingMs())) { return isInvalid() ? ReceiveResult::Lost : ReceiveResult::Timeout; }
	if (!m_in->tryLockFor(remainingMs()))
	{
		m_in->messageSent();
		return isInvalid() ? ReceiveResult::Lost : ReceiveResult::Timeout;
	}
	if (_bounded)
	{
		if (!readFrameLockedBounded(_out))
		{
			m_in->unlock();
			invalidate();
			return ReceiveResult::Lost;
		}
	}
	else
	{
		_out = readFrameLocked();
	}
	m_in->unlock();
	return _out.id == IdUndefined ? ReceiveResult::Lost : ReceiveResult::Got;
#else
	for (;;)
	{
		if (isInvalid()) { return ReceiveResult::Lost; }
		switch (peekNextMessage())
		{
		case Framing::Unusable:
			invalidate();
			return ReceiveResult::Lost;
		case Framing::Incomplete:
			if (std::chrono::steady_clock::now() >= _deadline) { return ReceiveResult::Timeout; }
			if (waitForMessageReadable(0))
			{
				// Unread bytes make the socket readable at once, so a plain poll would spin here while the rest of the
				// message is on its way. Step in short sleeps instead; the deadline is checked on every turn.
				std::this_thread::sleep_for(std::chrono::microseconds{50});
			}
			else if (!waitForMessageReadable(remainingMs()))
			{
				return isInvalid() ? ReceiveResult::Lost : ReceiveResult::Timeout;
			}
			continue;
		case Framing::Complete:
			// Every byte is already in the socket, so this read does not wait.
			if (_bounded) { return receiveBoundedSocketFrame(_out); }
			_out = receiveMessage();
			return _out.id == IdUndefined ? ReceiveResult::Lost : ReceiveResult::Got;
		}
	}
#endif
}
#endif // BUILD_REMOTE_PLUGIN_CLIENT

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
