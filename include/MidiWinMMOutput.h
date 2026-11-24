/*
 * MidiWinMMOutput.h - bounded, off-audio-thread delivery for WinMM MIDI output
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio - an LMMS-derived DAW
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

#ifndef LMMS_MIDI_WINMM_OUTPUT_H
#define LMMS_MIDI_WINMM_OUTPUT_H

/*
 * The fixed tree carries the bounded implementation this job added. The macro lets the
 * regression tell "the fix is present" from "the fix was reversed" without reading product
 * source text: the test includes this header when it can and fails with a message when it
 * cannot.
 */
#define ZENE_HAVE_WINMM_BOUNDED_OUTPUT 1

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <semaphore>
#include <thread>
#include <utility>
#include <vector>

#include <QList>
#include <QMap>
#include <QString>

#include "MidiHandoffQueue.h"
#include "MidiWinMMOutRouting.h"

namespace lmms
{

class MidiPort;

/*! The device side of the bounded output: one packed short message for one WinMM handle.
 *  \c send() runs on the output worker (or the test pump), never on the submitting thread,
 *  so it is allowed to wait in the driver. */
class MidiWinMMSink
{
public:
	virtual ~MidiWinMMSink() = default;
	virtual void send( std::uintptr_t device, std::uint32_t message ) = 0;
};

/*! Cluster new-p9-276: what MidiWinMM::processOutEvent() actually does now.
 *
 *  The audio path resolves the published routing for the event's port and pushes one fixed
 *  POD command per destination into a bounded multi-producer/single-consumer queue. It never
 *  calls the driver, allocates, frees, locks, waits or logs. A worker thread drains the queue
 *  and calls \c MidiWinMMSink::send(), so a WinMM call that waits for the device waits on that
 *  thread, not in an audio period.
 *
 *  Queue-full behaviour: a command that cannot be queued is dropped and counted in
 *  \c overflowCount(); submission never blocks and the remaining destinations of the same
 *  event are still attempted. Commands leave in push order, so per-device order is preserved.
 *
 *  Teardown: \c clearDevices() first refuses new submissions, waits for an in-flight submit to
 *  finish enqueueing, then advances a generation. Queued commands from the old generation are
 *  discarded without touching a device, and an in-flight sink call is waited out. Only then may
 *  the owner close the WinMM handles. The worker itself is joined by the destructor.
 *
 *  Device and subscription state lives here, not in the Windows translation unit, so the
 *  regression drives the same publication, subscription and playback code production runs;
 *  MidiWinMM is the thin WinMM handle adapter around this class.
 */
class MidiWinMMOutput
{
public:
	static constexpr std::size_t QueueCapacity = 1024;

	struct Command
	{
		std::uintptr_t device = 0;
		std::uint32_t message = 0;
		std::uint64_t generation = 0;
	};

	using Queue = MidiHandoffQueue<QueueCapacity, Command>;
	using PortList = QList<const MidiPort*>;
	using SubscriptionMap = QMap<QString, PortList>;
	using DeviceList = std::vector<std::pair<QString, std::uintptr_t>>;

	/*! @param threaded false is the deterministic test pump: no worker runs and the test calls
	 *  \c drainForTesting(). Production always uses the default, threaded mode. */
	explicit MidiWinMMOutput( MidiWinMMSink& sink, bool threaded = true ) :
		m_sink( sink ),
		m_threaded( threaded )
	{
		if( m_threaded )
		{
			m_worker = std::thread( &MidiWinMMOutput::run, this );
		}
	}

	~MidiWinMMOutput()
	{
		stop();
	}

	MidiWinMMOutput( const MidiWinMMOutput& ) = delete;
	MidiWinMMOutput& operator=( const MidiWinMMOutput& ) = delete;

	//! Publisher: replaces the open-device list and republishes the routing (GUI thread).
	void setDevices( DeviceList devices )
	{
		m_devices = std::move( devices );
		publishRoutes();
		m_accepting.store( true, std::memory_order_seq_cst );
	}

	//! Publisher: coordinates queued and in-flight sends, then empties all output state.
	//! Returns only when no sink call from before the call can still be running.
	void clearDevices()
	{
		m_accepting.store( false, std::memory_order_seq_cst );
		waitForSubmitToFinish();
		m_generation.fetch_add( 1, std::memory_order_seq_cst );
		waitForWorkerToFinish();
		m_devices.clear();
		m_subscriptions.clear();
		publishRoutes();
	}

	//! Publisher: the client's subscribeWritablePort() removal-then-push semantics.
	void subscribeWritablePort( const MidiPort* port, const QString& dest, bool subscribe )
	{
		m_subscriptions[dest].removeAll( port );
		if( subscribe )
		{
			m_subscriptions[dest].push_back( port );
		}
		publishRoutes();
	}

	//! Publisher: drop @a port from every output name, as MidiWinMM::removePort() did.
	void removePort( const MidiPort* port )
	{
		for( SubscriptionMap::Iterator it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it )
		{
			it.value().removeAll( port );
		}
		publishRoutes();
	}

	//! Publisher: a port that is no longer output-enabled loses every output subscription.
	void applyPortMode( const MidiPort* port, bool outputEnabled )
	{
		if( !outputEnabled )
		{
			removePort( port );
		}
	}

	//! Audio thread: resolve @a port against the published routing and queue every destination.
	void processOutEvent( const MidiPort* port, std::uint32_t message ) noexcept
	{
		m_submitInFlight.fetch_add( 1, std::memory_order_seq_cst );
		if( !m_accepting.load( std::memory_order_seq_cst ) )
		{
			m_submitInFlight.fetch_sub( 1, std::memory_order_seq_cst );
			return;
		}

		const std::uint64_t generation = m_generation.load( std::memory_order_seq_cst );
		const MidiWinMMOutRouting::Snapshot* published = m_routing.snapshot();
		bool queued = false;
		if( published != nullptr )
		{
			for( std::size_t i = 0; i < published->count; ++i )
			{
				const MidiWinMMOutRoute& route = published->routes[i];
				if( route.port != port )
				{
					continue;
				}
				if( m_queue.push( Command{ route.device, message, generation } ) )
				{
					queued = true;
				}
				else
				{
					m_overflow.fetch_add( 1, std::memory_order_relaxed );
				}
			}
		}

		m_submitInFlight.fetch_sub( 1, std::memory_order_seq_cst );
		if( queued && m_threaded )
		{
			requestWake();
		}
	}

	std::uint64_t overflowCount() const noexcept
	{
		return m_overflow.load( std::memory_order_relaxed );
	}

#ifdef LMMS_TESTING
	//! Test pump: runs the worker's single-consumer drain on the calling thread. Only valid
	//! for a non-threaded output; a threaded one has a real consumer.
	std::size_t drainForTesting( std::size_t limit = QueueCapacity )
	{
		std::size_t taken = 0;
		while( taken < limit && takeAndSend() )
		{
			++taken;
		}
		return taken;
	}

	//! Whether clearDevices() observed a submit or sink call in flight and waited for it.
	bool teardownWaitedForInFlightForTesting() const noexcept
	{
		return m_teardownObservedInFlight.load( std::memory_order_acquire );
	}
#endif

private:
	void publishRoutes()
	{
		m_routing.publish( MidiWinMMOutRouting::buildRoutes( m_devices, m_subscriptions ) );
	}

	void requestWake() noexcept
	{
		if( !m_wakePending.exchange( true, std::memory_order_seq_cst ) )
		{
			m_wake.release();
		}
	}

	void waitForSubmitToFinish()
	{
		while( m_submitInFlight.load( std::memory_order_seq_cst ) != 0 )
		{
			markTeardownWait();
			std::this_thread::yield();
		}
	}

	void waitForWorkerToFinish()
	{
		while( m_workerActive.load( std::memory_order_seq_cst ) != 0 )
		{
			markTeardownWait();
			std::this_thread::yield();
		}
	}

	void markTeardownWait() noexcept
	{
#ifdef LMMS_TESTING
		m_teardownObservedInFlight.store( true, std::memory_order_release );
#endif
	}

	//! Single consumer: pop one command and, if its generation is still current, hand it to the
	//! sink. The active counter is raised before the generation is read, which is the half of
	//! clearDevices()' handshake that makes an in-flight sink call observable.
	bool takeAndSend()
	{
		Command command;
		if( !m_queue.pop( command ) )
		{
			return false;
		}
		m_workerActive.fetch_add( 1, std::memory_order_seq_cst );
		if( command.generation == m_generation.load( std::memory_order_seq_cst ) )
		{
			m_sink.send( command.device, command.message );
		}
		m_workerActive.fetch_sub( 1, std::memory_order_seq_cst );
		return true;
	}

	void run()
	{
		while( !m_stop.load( std::memory_order_acquire ) )
		{
			m_wakePending.store( false, std::memory_order_seq_cst );
			while( takeAndSend() )
			{
			}
			m_wake.acquire();
		}
	}

	void stop()
	{
		m_accepting.store( false, std::memory_order_seq_cst );
		waitForSubmitToFinish();
		if( m_worker.joinable() )
		{
			m_stop.store( true, std::memory_order_seq_cst );
			// Unconditional: the pending flag may describe a token this loop has already
			// consumed, and that must not leave the worker asleep past the stop.
			m_wake.release();
			m_worker.join();
		}
	}

	MidiWinMMSink& m_sink;
	const bool m_threaded;
	std::thread m_worker;

	DeviceList m_devices;
	SubscriptionMap m_subscriptions;
	MidiWinMMOutRouting m_routing;
	Queue m_queue;

	std::atomic<bool> m_accepting{false};
	std::atomic<std::uint64_t> m_generation{0};
	std::atomic<std::uint64_t> m_submitInFlight{0};
	std::atomic<std::uint64_t> m_workerActive{0};
	std::atomic<std::uint64_t> m_overflow{0};

	std::atomic<bool> m_stop{false};
	std::atomic<bool> m_wakePending{false};
	//! At most one normal wake token plus the unconditional stop token.
	std::counting_semaphore<2> m_wake{0};

#ifdef LMMS_TESTING
	std::atomic<bool> m_teardownObservedInFlight{false};
#endif
};

} // namespace lmms

#endif // LMMS_MIDI_WINMM_OUTPUT_H
