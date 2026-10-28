/*
 * RemotePluginBase.h - base class providing RPC like mechanisms
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

#ifndef LMMS_REMOTE_PLUGIN_BASE_H
#define LMMS_REMOTE_PLUGIN_BASE_H

#include <atomic>  // IWYU pragma: keep
#include <vector>
#include <cerrno>
#include <clocale> // IWYU pragma: keep
#include <cstdio>
#include <cstdlib>
#include <charconv>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string_view>
#include <thread>
#include <vector>
#include <cstring>
#include <string>

#include "lmmsconfig.h"

#if !(defined(LMMS_HAVE_SYS_IPC_H) && defined(LMMS_HAVE_SEMAPHORE_H))
#ifndef SYNC_WITH_SHM_FIFO // a build may already select it (tests/CMakeLists.txt)
#define SYNC_WITH_SHM_FIFO
#endif // SYNC_WITH_SHM_FIFO

#ifdef LMMS_HAVE_PROCESS_H
#include <process.h>
#endif
// getpid() and usleep() are used by the shm-FIFO sync path on POSIX hosts
// that lack SysV semaphores (e.g. macOS), where the unistd include below is skipped.
#ifdef LMMS_HAVE_UNISTD_H
#include <unistd.h>
#endif
#else // !(LMMS_HAVE_SYS_IPC_H && LMMS_HAVE_SEMAPHORE_H)
#ifdef LMMS_HAVE_UNISTD_H
#include <unistd.h>
#endif
#endif // !(LMMS_HAVE_SYS_IPC_H && LMMS_HAVE_SEMAPHORE_H)

#ifdef LMMS_HAVE_PTHREAD_H
#include <pthread.h>
#endif


#ifdef BUILD_REMOTE_PLUGIN_CLIENT
#undef LMMS_EXPORT
#define LMMS_EXPORT

#ifndef SYNC_WITH_SHM_FIFO
#include <sys/socket.h>
#include <sys/un.h>
#endif // SYNC_WITH_SHM_FIFO

#else // BUILD_REMOTE_PLUGIN_CLIENT
#include "lmms_export.h"
#include <QString>

#ifndef SYNC_WITH_SHM_FIFO
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>  // IWYU pragma: keep
#endif // SYNC_WITH_SHM_FIFO

#endif // BUILD_REMOTE_PLUGIN_CLIENT

#ifdef SYNC_WITH_SHM_FIFO
#include "SharedMemory.h"
#include "SystemSemaphore.h"
#endif

#ifdef LMMS_TESTING
class RemotePluginAudioPortsTest;
class RemotePluginShmFifoTest;
#endif

namespace lmms
{


#ifdef SYNC_WITH_SHM_FIFO


// sometimes we need to exchange bigger messages (e.g. for VST parameter dumps)
// so set a usable value here
const int SHM_FIFO_SIZE = 512*1024;


// implements a FIFO inside a shared memory segment
class shmFifo
{
#ifdef LMMS_TESTING
	friend class ::RemotePluginShmFifoTest;
#endif
	friend class RemotePluginBase;

	// need this union to handle different sizes of sem_t on 32 bit
	// and 64 bit platforms
	union sem32_t
	{
		int semKey;
		char fill[32];
	} ;
	struct shmData
	{
		sem32_t dataSem;	// semaphore for locking this
					// FIFO management data
		sem32_t messageSem;	// semaphore for incoming messages
		int32_t startPtr; // current start of FIFO in memory
		int32_t endPtr;   // current end of FIFO in memory
		char data[SHM_FIFO_SIZE];  // actual data
	} ;

public:
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
	// constructor for master-side
	shmFifo() :
		m_invalid( false ),
		m_master( true )
	{
		m_data.create();
		m_data->startPtr = m_data->endPtr = 0;
		static int k = 0;
		m_data->dataSem.semKey = ( getpid()<<10 ) + ++k;
		m_data->messageSem.semKey = ( getpid()<<10 ) + ++k;
		m_dataSem = SystemSemaphore{std::to_string(m_data->dataSem.semKey), 1u};
		m_messageSem = SystemSemaphore{std::to_string(m_data->messageSem.semKey), 0u};
	}
#endif

	// constructor for remote-/client-side - use _shm_key for making up
	// the connection to master
	shmFifo(const std::string& shmKey) :
		m_invalid( false ),
		m_master( false )
	{
		m_data.attach(shmKey);
		m_dataSem = SystemSemaphore{std::to_string(m_data->dataSem.semKey)};
		m_messageSem = SystemSemaphore{std::to_string(m_data->messageSem.semKey)};
	}

	inline bool isInvalid() const
	{
		return m_invalid.load( std::memory_order_relaxed );
	}

	void invalidate()
	{
		m_invalid.store( true, std::memory_order_relaxed );
	}

	// do we act as master (i.e. not as remote-process?)
	inline bool isMaster() const
	{
		return m_master;
	}

	//! Identifies the calling thread. The data lock is owned by one thread at a time: only the owner may enter it
	//! again without waiting, so a thread that does not hold the lock can never ride on another thread's hold.
	static const void * callerKey() noexcept
	{
		// Every live thread has a distinct instance of this thread-local object. Its address is a collision-free
		// ownership token for as long as that thread can own this FIFO lock.
		thread_local const char callerToken = 0;
		return &callerToken;
	}

#ifdef LMMS_TESTING
	//! Lets the regression force a collision in the legacy thread-id hash path.
	using CallerIdHashForTest = std::uint64_t (*)( std::thread::id );
	static void setCallerIdHashForTest( CallerIdHashForTest _hash ) noexcept
	{
		s_callerIdHashForTest.store( _hash, std::memory_order_relaxed );
	}
	static std::uint64_t callerIdHashForTest( std::thread::id _id ) noexcept
	{
		if( const auto hashForTest = s_callerIdHashForTest.load( std::memory_order_relaxed ) )
		{
			return hashForTest( _id );
		}
		return std::hash<std::thread::id>{}( _id );
	}

	//! Lets the regression stop a writer when the legacy send path waits for FIFO space.
	using SpaceWaitHookForTest = void (*)( shmFifo * ) noexcept;
	static void setSpaceWaitHookForTest( SpaceWaitHookForTest _hook ) noexcept
	{
		s_spaceWaitHookForTest.store( _hook, std::memory_order_relaxed );
	}
#endif

	// recursive lock, owned per thread
	inline void lock()
	{
		if( isInvalid() ) { return; }
		const auto me = callerKey();
		if( m_owner.load( std::memory_order_acquire ) == me )
		{
			++m_depth;
			return;
		}
		m_dataSem.acquire();
		m_owner.store( me, std::memory_order_release );
		m_depth = 1;
	}

	// recursive unlock; a thread that does not own the lock does nothing
	inline void unlock()
	{
		if( m_owner.load( std::memory_order_acquire ) != callerKey() ) { return; }
		if( --m_depth == 0 )
		{
			m_owner.store( nullptr, std::memory_order_release );
			m_dataSem.release();
		}
	}

	// wait until message-semaphore is available
	inline void waitForMessage()
	{
		if( !isInvalid() )
		{
			m_messageSem.acquire();
		}
	}

	// increase message-semaphore
	inline void messageSent()
	{
		m_messageSem.release();
	}


	inline int32_t readInt()
	{
		int32_t i;
		read( &i, sizeof( i ) );
		return i;
	}

	inline void writeInt( const int32_t & _i )
	{
		write( &_i, sizeof( _i ) );
	}

	std::string readString()
	{
		std::string ret;
		const int len = readInt();
		if (len < 0)
		{
			invalidate();
			return ret;
		}
		if (len > 0)
		{
			// Never trust the wire length before allocating: the FIFO can only hold what is left of the
			// message it has already signalled.
			lock();
			const bool fits = !isInvalid() && len <= m_data->endPtr - m_data->startPtr;
			unlock();
			if (!fits)
			{
				invalidate();
				return ret;
			}
			ret.resize(static_cast<std::size_t>(len));
			read(ret.data(), len);
		}
		return ret;
	}


	//! Reads raw bytes of the current message. Does not allocate; the caller holds the data lock.
	inline void readRaw( void * _buf, int _len )
	{
		read( _buf, _len );
	}


	inline void writeString( const std::string & _s )
	{
		const int len = _s.size();
		writeInt( len );
		write( _s.c_str(), len );
	}

	//! Writes raw bytes, for a frame built by RemotePluginBase::sendFrameNonBlocking(). Does not allocate.
	inline void writeRaw( const void * _buf, int _len )
	{
		write( _buf, _len );
	}

	//! Timed forms for the audio thread: each waits at most _timeoutMs and returns false when it could not.
	//! tryLockFor() takes the data lock (recursively, like lock()); waitMessageFor() takes one message token.
	inline bool tryLockFor( int _timeoutMs )
	{
		if( isInvalid() ) { return false; }
		const auto me = callerKey();
		if( m_owner.load( std::memory_order_acquire ) == me )
		{
			++m_depth;
			return true;
		}
		if( !m_dataSem.tryAcquire( _timeoutMs ) ) { return false; }
		m_owner.store( me, std::memory_order_release );
		m_depth = 1;
		return true;
	}

	inline bool waitMessageFor( int _timeoutMs )
	{
		return !isInvalid() && m_messageSem.tryAcquire( _timeoutMs );
	}

	//! Bytes the FIFO can take now. The caller holds the data lock.
	inline std::size_t freeSpace()
	{
		return static_cast<std::size_t>( SHM_FIFO_SIZE - ( m_data->endPtr - m_data->startPtr ) );
	}


	inline bool messagesLeft()
	{
		if( isInvalid() )
		{
			return false;
		}
		lock();
		const bool empty = ( m_data->startPtr == m_data->endPtr );
		unlock();
		return !empty;
	}


	const std::string& shmKey() const
	{
		return m_data.key();
	}


private:
	//! Serializes writers separately from readers while a blocking message yields the data semaphore for space.
	inline void lockWriter()
	{
		while( m_writerBusy.test_and_set( std::memory_order_acquire ) ) { std::this_thread::yield(); }
	}

	//! Audio-thread form: it never waits when a blocking message writer owns the write direction.
	inline bool tryLockWriter()
	{
		return !m_writerBusy.test_and_set( std::memory_order_acquire );
	}

	inline void unlockWriter()
	{
		m_writerBusy.clear( std::memory_order_release );
	}

	//! Drops every recursive data-lock level so a reader can consume queued bytes during a space wait.
	inline int releaseLockForSpaceWait()
	{
		const int depth = m_depth;
		while( m_depth > 0 ) { unlock(); }
		return depth;
	}

	//! Restores the caller's recursive-lock depth after the writer reacquires the data semaphore.
	inline void reacquireLockAfterSpaceWait( int _depth )
	{
		if( _depth == 0 ) { return; }
		lock();
		for( int depth = 1; depth < _depth; ++depth ) { lock(); }
	}

	void read( void * _buf, int _len )
	{
		if( isInvalid() )
		{
			memset( _buf, 0, _len );
			return;
		}
		lock();
		// A reader only reads a message the writer has signalled, and the writer writes it whole under the lock, so
		// the data is there. If it is not, the peer broke that rule: the FIFO is given up at once. Waiting here would
		// hold the data lock that the writer needs in order to write, so it could never succeed.
		if( isInvalid() || _len > m_data->endPtr - m_data->startPtr )
		{
			invalidate();
			memset( _buf, 0, _len );
			unlock();
			return;
		}
		std::memcpy(_buf, m_data->data + m_data->startPtr, _len);
		m_data->startPtr += _len;
		// nothing left?
		if( m_data->startPtr == m_data->endPtr )
		{
			// then reset to 0
			m_data->startPtr = m_data->endPtr = 0;
		}
		unlock();
	}

	void write( const void * _buf, int _len )
	{
		if( isInvalid() || _len > SHM_FIFO_SIZE )
		{
			return;
		}
		lock();
		while( !isInvalid() && _len > SHM_FIFO_SIZE - m_data->endPtr )
		{
			// if no space is left, try to move data to front
			if( m_data->startPtr > 0 )
			{
				memmove( m_data->data,
					m_data->data + m_data->startPtr,
					m_data->endPtr - m_data->startPtr );
				m_data->endPtr = m_data->endPtr -
							m_data->startPtr;
				m_data->startPtr = 0;
			}
			const int lockDepth = releaseLockForSpaceWait();
#ifdef LMMS_TESTING
			if( const auto hook = s_spaceWaitHookForTest.load( std::memory_order_relaxed ) ) { hook( this ); }
#endif
			if( isInvalid() ) { return; }
#ifndef LMMS_BUILD_WIN32
			usleep( 5 );
#endif
			if( isInvalid() ) { return; }
			reacquireLockAfterSpaceWait( lockDepth );
		}
		if( isInvalid() )
		{
			unlock();
			return;
		}
		std::memcpy(m_data->data + m_data->endPtr, _buf, _len);
		m_data->endPtr += _len;
		unlock();
	}

	std::atomic<bool> m_invalid;
	bool m_master;
	SharedMemory<shmData> m_data;
	SystemSemaphore m_dataSem;
	SystemSemaphore m_messageSem;
	//! Thread that holds the data lock (0: none) and its recursion depth; the depth is touched by the owner only.
	std::atomic<const void *> m_owner{nullptr};
	int m_depth = 0;
	std::atomic_flag m_writerBusy = ATOMIC_FLAG_INIT;
#ifdef LMMS_TESTING
	inline static std::atomic<CallerIdHashForTest> s_callerIdHashForTest{nullptr};
	inline static std::atomic<SpaceWaitHookForTest> s_spaceWaitHookForTest{nullptr};
#endif
};
#endif // SYNC_WITH_SHM_FIFO



enum RemoteMessageIDs
{
	IdUndefined,
	IdHostInfoGotten,
	IdInitDone,
	IdQuit,
	IdSyncKey,
	IdSampleRateInformation,
	IdBufferSizeInformation,
	IdInformationUpdated,
	IdMidiEvent,
	IdStartProcessing,
	IdProcessingDone,
	IdChangeSharedMemoryKey,
	//! Retired by the planar audio-ports migration (#589): a client must report
	//! both channel counts at once (IdChangeInputOutputCount). These two ids are
	//! deliberately *not* deleted from the enumeration - the ids after them keep
	//! their numeric value, so a client built against the old interleaved
	//! protocol is still recognised and can be refused loudly (see
	//! RemotePlugin::processMessage()) instead of having its messages
	//! misinterpreted as unrelated ids.
	IdChangeInputCount,
	IdChangeOutputCount,
	IdChangeInputOutputCount,
	IdShowUI,
	IdHideUI,
	IdToggleUI,
	IdIsUIVisible,
	IdSaveSettingsToString,
	IdSaveSettingsToFile,
	IdLoadSettingsFromString,
	IdLoadSettingsFromFile,
	IdSavePresetFile,
	IdLoadPresetFile,
	IdDebugMessage,
	IdIdle,
	IdUserBase = 64
} ;



class LMMS_EXPORT RemotePluginBase
{
#ifdef LMMS_TESTING
	friend class ::RemotePluginAudioPortsTest;
#endif

public:
	struct message
	{
		message() :
			id( IdUndefined ),
			data()
		{
		}

		message( const message & _m ) = default;

		message( int _id ) :
			id( _id ),
			data()
		{
		}

		template<class... Args>
		message& addString(Args&&... args)
		{
			data.emplace_back(std::forward<Args>(args)...);
			return *this;
		}

		message & addInt( int _i )
		{
			char buf[32];
			std::snprintf(buf, 32, "%d", _i);
			data.emplace_back( buf );
			return *this;
		}

		message & addFloat( float _f )
		{
			char buf[32];
			std::snprintf(buf, 32, "%f", _f);
			data.emplace_back( buf );
			return *this;
		}

		//! Argument \a _p by reference. getString() returns a copy, which allocates on the audio thread.
		inline const std::string & stringArgument( int _p = 0 ) const
		{
			return data[_p];
		}

		//! Argument \a _p as a view. getString() returns a copy, which allocates on the audio thread.
		//! In bounded mode (a message received by the audio thread, see receiveFrameBefore()) the view
		//! points into the receiver's frame buffer and is valid only until that receiver receives again.
		inline std::string_view argumentView( int _p = 0 ) const
		{
			if( !m_bounded ) { return data[_p]; }
			std::size_t length = 0;
			const char * arg = boundedArgument( _p, length );
			return arg ? std::string_view{ arg, length } : std::string_view{};
		}

		inline std::string getString( int _p = 0 ) const
		{
			return std::string{ argumentView( _p ) };
		}

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
		inline QString getQString( int _p = 0 ) const
		{
			return QString::fromStdString( getString( _p ) );
		}
#endif

		inline int getInt( int _p = 0 ) const
		{
			if( !m_bounded ) { return atoi( data[_p].c_str() ); }
			const auto arg = argumentView( _p );
			if( arg.empty() ) { return 0; }
			int value = 0;
			std::from_chars( arg.data(), arg.data() + arg.size(), value );
			return value;
		}

		inline float getFloat( int _p ) const
		{
			if( !m_bounded ) { return (float) atof( data[_p].c_str() ); }
			const auto arg = argumentView( _p );
			if( arg.empty() ) { return 0.0f; }
			char buf[64];
			const auto length = std::min<std::size_t>( arg.size(), sizeof( buf ) - 1 );
			std::memcpy( buf, arg.data(), length );
			buf[length] = '\0';
			return (float) atof( buf );
		}

		//! Number of arguments this message carries. The wire format writes the count with
		//! the message, so a structurally valid (complete) message may still carry fewer
		//! arguments than a handler reads: dispatch must check this before indexing, since
		//! getString()/getInt() do not bounds-check.
		inline std::size_t size() const
		{
			return m_bounded ? m_boundedCount : data.size();
		}

		inline bool operator==( const message & _m ) const
		{
			return( id == _m.id );
		}

		int id;

	private:
		std::vector<std::string> data;

		//! Bounded receive (audio thread): the arguments are the wire bytes in the receiver's frame buffer,
		//! length-prefixed exactly as they arrived. No string or vector is built for them.
		bool m_bounded = false;
		const char * m_boundedFrame = nullptr;
		std::size_t m_boundedSize = 0;
		std::size_t m_boundedCount = 0;

		//! Start of argument \a _p in bounded mode, with its length; nullptr when the index or frame is bad.
		const char * boundedArgument( int _p, std::size_t & _length ) const
		{
			_length = 0;
			if( _p < 0 || static_cast<std::size_t>( _p ) >= m_boundedCount ) { return nullptr; }
			const char * cursor = m_boundedFrame + 2 * sizeof( std::int32_t );
			const char * const end = m_boundedFrame + m_boundedSize;
			for( int i = 0; i <= _p; ++i )
			{
				std::int32_t length = 0;
				if( static_cast<std::size_t>( end - cursor ) < sizeof length ) { return nullptr; }
				std::memcpy( &length, cursor, sizeof length );
				cursor += sizeof length;
				if( length < 0 || static_cast<std::size_t>( length ) > static_cast<std::size_t>( end - cursor ) )
				{
					return nullptr;
				}
				if( i == _p )
				{
					_length = static_cast<std::size_t>( length );
					return cursor;
				}
				cursor += length;
			}
			return nullptr;
		}

		friend class RemotePluginBase;

	} ;

#ifdef SYNC_WITH_SHM_FIFO
	RemotePluginBase( shmFifo * _in, shmFifo * _out );
#else
	RemotePluginBase();
#endif
	virtual ~RemotePluginBase();

#ifdef SYNC_WITH_SHM_FIFO
	void reset( shmFifo *in, shmFifo *out )
	{
		delete m_in;
		delete m_out;
		m_in = in;
		m_out = out;
	}
#endif

	int sendMessage( const message & _m );
	message receiveMessage();
#ifdef SYNC_WITH_SHM_FIFO
	//! The message of a token already taken on the shared-memory FIFO, with its data lock held by the caller.
	message readFrameLocked();
#endif

	//! Sends a message whose arguments are all integers, in the wire format sendMessage() writes. Each
	//! argument is formatted into a stack buffer, so unlike building a message with addInt() this does not
	//! allocate; the audio thread uses it. @return the number of bytes written, as sendMessage() does
	//! Outcome of a send made on the audio thread.
	enum class SendResult
	{
		Sent,   //!< the whole frame was written
		NoRoom, //!< nothing was written: the send buffer had no room, or the send lock was busy
		Broken  //!< the connection failed, or a partial frame was written; the stream is no longer usable
	};

	//! Writes one complete frame without blocking. Used on the audio thread: it never waits for the send
	//! lock or the socket. The socket path checks the send buffer has room for the frame before writing,
	//! so a short write only happens if that check was wrong, and then the stream is marked broken.
	SendResult sendFrameNonBlocking( const char * _frame, std::size_t _size );

	//! Tests only: a non-blocking send leaves at most \a bytes queued in the socket (as SIOCOUTQ counts them);
	//! zero, the default, keeps the socket's own limit. Lets a test fill the send buffer on demand. Linux only:
	//! elsewhere there is no queued-bytes count to compare against.
	static void setSendRoomForTesting( std::size_t bytes );

	//! Outcome of a receive on the audio thread.
	enum class ReceiveResult
	{
		Got,     //!< a complete message was read into the argument
		Timeout, //!< no complete message arrived before the deadline; nothing was consumed
		Lost     //!< the connection failed, or the peer closed it
	};

	//! Receives the next message, waiting no longer than \a _deadline. Reads only a complete message (socket:
	//! the framing is checked first; shared memory: a token is taken, and each message is written whole), so a
	//! client that stops mid-message cannot hold the caller. Used on the audio thread.
	//! With \a _bounded the arguments are views into a preallocated frame buffer rather than owned strings,
	//! so decoding a client message there allocates nothing; the views are valid until the next receive.
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
	ReceiveResult receiveFrameBefore( std::chrono::steady_clock::time_point _deadline, message & _out,
		bool _bounded = false );

	//! The main thread's bounded receive. Like receiveFrameBefore(), but a frame that has started on the socket is
	//! finished rather than abandoned: a frame larger than the socket buffer cannot be complete until it is read.
	//! Each new piece of it must arrive within _progress, or the connection is taken as lost.
	ReceiveResult receiveWithin( std::chrono::steady_clock::time_point _deadline,
		std::chrono::milliseconds _progress, message & _out );

#ifndef SYNC_WITH_SHM_FIFO
	//! Reads exactly _len bytes, waiting at most _progress for each new piece and never past _total.
	//! False if the bytes did not all arrive in time.
	bool readExactlyWithin( void * _buf, int _len, std::chrono::milliseconds _progress,
		std::chrono::steady_clock::time_point _total );
	//! Finishes a frame whose first bytes are on the socket (see receiveWithin()), within StartedFrameTimeout but
	//! never past \a _deadline: a short query must not be stretched to the started-frame bound.
	ReceiveResult receiveStartedFrame( std::chrono::steady_clock::time_point _deadline,
		std::chrono::milliseconds _progress, message & _out );
#endif

	//! The whole of a started frame must arrive within this (on top of the per-piece bound of receiveWithin()).
	static constexpr std::chrono::milliseconds StartedFrameTimeout{2000};
#endif // BUILD_REMOTE_PLUGIN_CLIENT

	//! Writes the tail of an earlier partial non-blocking send, if there is one, without blocking.
	SendResult flushPendingSend();

	//! Whether part of a frame is still waiting to be written (socket transport; always false on shared memory).
	bool sendTailPending() const
	{
#ifdef SYNC_WITH_SHM_FIFO
		return false;
#else
		return m_txTailSize != 0;
#endif
	}

#ifndef SYNC_WITH_SHM_FIFO
	//! Writes the tail with the send mutex held, without blocking. @return Sent when nothing is left
	SendResult writeTailLocked();
#endif

	//! Encodes a message of integer arguments in the wire format into \a _out, without allocating.
	//! @return the frame size, or 0 if it does not fit in \a _capacity bytes
	static std::size_t frameFromInts( char * _out, std::size_t _capacity, int _id,
						const std::int32_t * _values, std::size_t _count );

	//! Encodes \a _m in the wire format into \a _out, without allocating.
	//! @return the frame size, or 0 if it does not fit in \a _capacity bytes
	static std::size_t frameFromMessage( char * _out, std::size_t _capacity, const message & _m );

	inline bool isInvalid() const
	{
#ifdef SYNC_WITH_SHM_FIFO
		return m_in->isInvalid() || m_out->isInvalid();
#else
		return m_invalid.load( std::memory_order_relaxed );
#endif
	}

	message waitForMessage( const message & _m,
						bool _busy_waiting = false );

	inline message fetchAndProcessNextMessage()
	{
		message m = receiveMessage();
		processMessage( m );
		return m;
	}

#ifndef SYNC_WITH_SHM_FIFO
	inline int32_t readInt()
	{
		int32_t i;
		read( &i, sizeof( i ) );
		return i;
	}

	inline void writeInt( const int32_t & _i )
	{
		write( &_i, sizeof( _i ) );
	}

	std::string readString()
	{
		std::string ret;
		const int len = readInt();
		if (len < 0)
		{
			invalidate();
			return ret;
		}
		if (len > 0)
		{
			// This owning path is the main thread's wait for a whole message (settings, presets, chunks),
			// which may legitimately be larger than the audio thread's peek window; it is not bounded here.
			ret.resize(static_cast<std::size_t>(len));
			read(ret.data(), len);
		}
		return ret;
	}


	inline void writeString( const std::string & _s )
	{
		const int len = _s.size();
		writeInt( len );
		write( _s.c_str(), len );
	}
#endif // SYNC_WITH_SHM_FIFO

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
	inline bool messagesLeft()
	{
#ifdef SYNC_WITH_SHM_FIFO
		return m_in->messagesLeft();
#else
		struct pollfd pollin;
		pollin.fd = m_socket;
		pollin.events = POLLIN;

		if ( poll( &pollin, 1, 0 ) == -1 )
		{
			qWarning( "Unexpected poll error." );
		}
		return pollin.revents & POLLIN;
#endif
	}

	//! Waits at most \a timeoutMs milliseconds (zero polls) for the next message to start arriving.
	//! @return false when nothing arrived in time. Only the socket transport can time out: the
	//! shared-memory FIFO has no timed wait, so it reports ready and the caller blocks in
	//! receiveMessage() as it always did. Virtual so a test can observe the bound a receive asks for
	//! without waiting for it.
	virtual bool waitForMessageReadable(int timeoutMs)
	{
#ifdef SYNC_WITH_SHM_FIFO
		(void)timeoutMs;
		return true;
#else
		struct pollfd pollin;
		pollin.fd = m_socket;
		pollin.events = POLLIN;
		int ready = 0;
		do
		{
			ready = poll(&pollin, 1, timeoutMs);
		} while (ready < 0 && errno == EINTR);
		return ready > 0;
#endif
	}

	//! What a look at the socket finds at the start of the next message, without consuming anything.
	enum class Framing
	{
		Complete,   //!< the whole message is already in the socket, so receiveMessage() will not block
		Incomplete, //!< more bytes must still arrive
		Unusable    //!< the peer closed the connection, or the message is larger than the peek window
	};

	//! Looks at the next message without consuming it. The audio thread calls receiveMessage() only after
	//! Complete, so a client that stops mid-message cannot hold it. The shared-memory FIFO has no such look
	//! and reports Complete, as it always blocks in receiveMessage().
	inline Framing peekNextMessage()
	{
#ifdef SYNC_WITH_SHM_FIFO
		return Framing::Complete;
#else
		m_peekedFrameBytes = 0;
		const std::size_t windowBytes = m_peekWindow.size();
		char * const window = m_peekWindow.data();
		const ssize_t available = ::recv(m_socket, window, windowBytes, MSG_PEEK | MSG_DONTWAIT);
		if (available == 0) { return Framing::Unusable; }
		if (available < 0)
		{
			const bool retry = errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
			return retry ? Framing::Incomplete : Framing::Unusable;
		}
		const auto size = static_cast<std::size_t>(available);

		// Wire layout: id, argument count, then each argument as a length followed by its bytes.
		auto int32At = [&window](std::size_t at) {
			std::int32_t value = 0;
			std::memcpy(&value, window + at, sizeof value);
			return value;
		};
		if (size < 8) { return Framing::Incomplete; }
		const std::int32_t count = int32At(4);
		if (count < 0) { return Framing::Unusable; }

		std::size_t offset = 8;
		for (std::int32_t i = 0; i < count; ++i)
		{
			if (offset + sizeof(std::int32_t) > windowBytes) { return Framing::Unusable; }
			if (offset + sizeof(std::int32_t) > size) { return Framing::Incomplete; }
			const std::int32_t length = int32At(offset);
			if (length < 0) { return Framing::Unusable; }
			offset += sizeof(std::int32_t) + static_cast<std::size_t>(length);
			if (offset > windowBytes) { return Framing::Unusable; }
			if (offset > size) { return Framing::Incomplete; }
		}
		m_peekedFrameBytes = offset;
		return Framing::Complete;
#endif
	}

	//! Whether a complete message is already waiting, so that receiveFrameBefore() with a zero deadline takes it
	//! without waiting. Never reads anything.
	inline bool completeMessageWaiting()
	{
		return peekNextMessage() == Framing::Complete;
	}

	inline void fetchAndProcessAllMessages()
	{
		while( messagesLeft() )
		{
			fetchAndProcessNextMessage();
		}
	}

	static bool isMainThreadWaiting()
	{
		return waitDepthCounter() > 0;
	}
#endif // BUILD_REMOTE_PLUGIN_CLIENT

	virtual bool processMessage( const message & _m ) = 0;


protected:
#ifdef SYNC_WITH_SHM_FIFO
	inline const shmFifo * in() const
	{
		return m_in;
	}

	inline const shmFifo * out() const
	{
		return m_out;
	}
#endif

	inline void invalidate()
	{
#ifdef SYNC_WITH_SHM_FIFO
		m_in->invalidate();
		m_out->invalidate();
		m_in->messageSent();
#else
		m_invalid.store( true, std::memory_order_relaxed );
#endif
	}


#ifndef SYNC_WITH_SHM_FIFO
	int m_socket;

	//! What the audio thread peeks into. Allocated once in the constructor, never on the audio thread; a
	//! complete message longer than this cannot be read without blocking, and is refused (see peekNextMessage()).
	std::vector<char> m_peekWindow;
	static constexpr std::size_t PeekWindowBytes = std::size_t{1} << 20;

	//! The unsent tail of a frame that a non-blocking send only partly wrote. It goes out before anything else.
	std::array<char, 512> m_txTail{};
	std::size_t m_txTailOffset = 0;
	std::size_t m_txTailSize = 0;

	//! Size of the frame peekNextMessage() found complete, for the bounded receive that consumes it.
	std::size_t m_peekedFrameBytes = 0;

#endif


private:
#ifndef BUILD_REMOTE_PLUGIN_CLIENT
	//! One whole wire frame read on the audio thread. A bounded receive points the message's arguments
	//! into it, so decoding a client message there allocates nothing; it is overwritten by the next receive.
	std::vector<char> m_frameScratch;

	//! Validates a complete wire frame and makes \a _out view it. Never allocates.
	bool parseBoundedFrame( const char * _frame, std::size_t _size, message & _out );

#ifdef SYNC_WITH_SHM_FIFO
	//! Reads the current whole FIFO message into m_frameScratch and makes \a _out view it. The caller holds the lock.
	bool readFrameLockedBounded( message & _out );
#else
	//! Reads the complete socket frame peekNextMessage() sized into m_frameScratch; no argument decoding.
	ReceiveResult receiveBoundedSocketFrame( message & _out );
#endif // SYNC_WITH_SHM_FIFO
#endif // BUILD_REMOTE_PLUGIN_CLIENT

#ifndef BUILD_REMOTE_PLUGIN_CLIENT
	static int & waitDepthCounter()
	{
		static int waitDepth = 0;
		return waitDepth;
	}
#endif

#ifdef SYNC_WITH_SHM_FIFO
	shmFifo * m_in;
	shmFifo * m_out;
#else
	void read( void * _buf, int _len )
	{
		if( isInvalid() )
		{
			memset( _buf, 0, _len );
			return;
		}
		char * buf = (char *) _buf;
		int remaining = _len;
		while ( remaining )
		{
			ssize_t nread = ::read( m_socket, buf, remaining );
			switch ( nread )
			{
				case -1:
					fprintf( stderr,
						"Error while reading.\n" );
				case 0:
					invalidate();
					memset( _buf, 0, _len );
					return;
			}
			buf += nread;
			remaining -= nread;
		}
	}

	void write( const void * _buf, int _len )
	{
		if( isInvalid() )
		{
			return;
		}
		const char * buf = (const char *) _buf;
		int remaining = _len;
		while ( remaining )
		{
			ssize_t nwritten = ::write( m_socket, buf, remaining );
			switch ( nwritten )
			{
				case -1:
					fprintf( stderr,
						"Error while writing.\n" );
				case 0:
					invalidate();
					return;
			}
			buf += nwritten;
			remaining -= nwritten;
		}
	}


	std::atomic<bool> m_invalid;

	pthread_mutex_t m_receiveMutex;
	pthread_mutex_t m_sendMutex;
#endif // SYNC_WITH_SHM_FIFO

} ;

} // namespace lmms

#endif // LMMS_REMOTE_PLUGIN_BASE_H
