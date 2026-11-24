/*
 * MidiWinMMOutRouting.h - the precomputed routing the WinMM output path sends through
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

#ifndef LMMS_MIDI_WINMM_OUT_ROUTING_H
#define LMMS_MIDI_WINMM_OUT_ROUTING_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <QList>
#include <QMap>
#include <QString>

namespace lmms
{

class MidiPort;

/*! Cluster new-p9-276: one stop of the WinMM output path - events produced by \a port are
 *  written to \a device. \a port is an IDENTITY: the playback path compares it and never
 *  calls through it, so a port that was destroyed since the snapshot was published can only
 *  stop matching, it cannot be dereferenced. */
struct MidiWinMMOutRoute
{
	const MidiPort* port = nullptr;
	std::uintptr_t device = 0; //!< the WinMM output handle (HMIDIOUT)
};

/*! How \c MidiWinMM::processOutEvent() decides where an event goes, without building anything
 *  on the playback path.
 *
 *  \c processOutEvent() can run on the audio thread, where neither an allocation nor a free
 *  nor a lock is allowed. It used to collect the destination NAMES subscribed to the event's
 *  port into a fresh \c QStringList for EVERY event and then search that list once per open
 *  device: a growing container per event, on the playback path.
 *
 *  Now the routing is computed when it changes. \c buildRoutes() turns the open-device list and
 *  the name-keyed subscriptions into a flat stop list ONCE (the GUI thread, which is the only
 *  thread that edits either map), \c publish() swaps it in, and the playback path only reads
 *  the published snapshot: one atomic load and a scan over a POD array.
 *
 *  Order and coverage are the ones the name search produced. Stops are emitted device by
 *  device in device-map order (the order the search sent in), and a port that is subscribed to
 *  a device name the search stopped at - a port listed twice for one name - yields ONE stop
 *  per device, exactly as the search's \c break did. Two open devices that share a name both
 *  receive, because each name is looked up for every device.
 *
 *  Threading. \c buildRoutes() and \c publish() are the publisher's (GUI thread). \c snapshot()
 *  and \c forEachDevice() are the reader's and are lock-free and allocation-free, so the audio
 *  thread may call them. Publishing an identical stop list is a no-op: the reader's pointer
 *  does not move and nothing is allocated.
 *
 *  Reclamation. A published snapshot is immutable and stays alive until this object is
 *  destroyed, because a reader that loaded it can never be asked to let go (the audio thread
 *  cannot wait, free or lock). The memory is therefore proportional to the routing CHANGES
 *  made in a session, not to the events played, and the no-op publish above keeps an unchanged
 *  routing from costing even that. The object is destroyed with the client, after the engine
 *  has stopped calling it.
 */
class MidiWinMMOutRouting
{
public:
	//! The immutable stop list the playback path reads.
	struct Snapshot
	{
		const MidiWinMMOutRoute* routes = nullptr;
		std::size_t count = 0;
	};

	MidiWinMMOutRouting() = default;
	~MidiWinMMOutRouting() = default;
	MidiWinMMOutRouting( const MidiWinMMOutRouting& ) = delete;
	MidiWinMMOutRouting& operator=( const MidiWinMMOutRouting& ) = delete;

	/*! The stop list for @a devices - (name, handle) in device-map order - and @a subscriptions,
	 *  the client's name -> ports map. Allocates: publisher thread only. */
	static std::vector<MidiWinMMOutRoute> buildRoutes(
		const std::vector<std::pair<QString, std::uintptr_t>>& devices,
		const QMap<QString, QList<MidiPort*>>& subscriptions );

	//! Makes @a routes the routing the playback path reads. Allocates only when it changed.
	void publish( std::vector<MidiWinMMOutRoute> routes );

	//! The snapshot the playback path must read now, or nullptr before the first publish.
	const Snapshot* snapshot() const noexcept
	{
		return m_published.load( std::memory_order_acquire );
	}

	//! Audio thread: hands every stop of @a port to @a send, in published order. No allocation,
	//! no free, no lock, no logging.
	template <typename Send>
	void forEachDevice( const MidiPort* port, Send&& send ) const noexcept
	{
		const Snapshot* published = snapshot();
		if( published == nullptr )
		{
			return;
		}
		const MidiWinMMOutRoute* routes = published->routes;
		for( std::size_t i = 0; i < published->count; ++i )
		{
			if( routes[i].port == port )
			{
				send( routes[i] );
			}
		}
	}

private:
	struct Owned
	{
		Snapshot snapshot;
		std::vector<MidiWinMMOutRoute> storage;
	};

	static bool sameRoutes( const Snapshot& snapshot, const std::vector<MidiWinMMOutRoute>& routes ) noexcept;

	//! Every snapshot this object has published; freed with it (see the class comment).
	std::vector<std::unique_ptr<Owned>> m_owned;
	std::atomic<const Snapshot*> m_published{nullptr};
};


inline std::vector<MidiWinMMOutRoute> MidiWinMMOutRouting::buildRoutes(
	const std::vector<std::pair<QString, std::uintptr_t>>& devices,
	const QMap<QString, QList<MidiPort*>>& subscriptions )
{
	std::vector<MidiWinMMOutRoute> routes;
	std::size_t firstOfDevice = 0;
	for( const std::pair<QString, std::uintptr_t>& device : devices )
	{
		firstOfDevice = routes.size();
		const auto subscribed = subscriptions.constFind( device.first );
		if( subscribed == subscriptions.constEnd() )
		{
			continue;
		}
		for( const MidiPort* port : *subscribed )
		{
			if( port == nullptr )
			{
				continue;
			}
			// The name search stopped at the first match, so a port listed twice for one name
			// still gets one send per device.
			bool alreadyRouted = false;
			for( std::size_t i = firstOfDevice; i < routes.size(); ++i )
			{
				if( routes[i].port == port )
				{
					alreadyRouted = true;
					break;
				}
			}
			if( !alreadyRouted )
			{
				routes.push_back( MidiWinMMOutRoute{ port, device.second } );
			}
		}
	}
	return routes;
}


inline bool MidiWinMMOutRouting::sameRoutes( const Snapshot& snapshot,
	const std::vector<MidiWinMMOutRoute>& routes ) noexcept
{
	if( snapshot.count != routes.size() )
	{
		return false;
	}
	for( std::size_t i = 0; i < routes.size(); ++i )
	{
		if( snapshot.routes[i].port != routes[i].port || snapshot.routes[i].device != routes[i].device )
		{
			return false;
		}
	}
	return true;
}


inline void MidiWinMMOutRouting::publish( std::vector<MidiWinMMOutRoute> routes )
{
	const Snapshot* published = m_published.load( std::memory_order_acquire );
	if( published != nullptr && sameRoutes( *published, routes ) )
	{
		return; // unchanged routing: no copy, no allocation, and the reader's pointer stays put
	}

	m_owned.push_back( std::make_unique<Owned>() );
	Owned& owned = *m_owned.back();
	owned.storage = std::move( routes );
	owned.snapshot.routes = owned.storage.data();
	owned.snapshot.count = owned.storage.size();
	m_published.store( &owned.snapshot, std::memory_order_release );
}

} // namespace lmms

#endif // LMMS_MIDI_WINMM_OUT_ROUTING_H
