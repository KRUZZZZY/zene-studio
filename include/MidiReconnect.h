/*
 * MidiReconnect.h - remembering a MIDI controller ASSIGNMENT by identity, so a
 *                   device that is unplugged and replugged is re-attached
 *                   without user action (0.3.0 feature-list row 18, OWNER-31
 *                   item 7)
 *
 * Copyright (c) 2026 Zene Studio contributors
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
 */

#ifndef LMMS_MIDI_RECONNECT_H
#define LMMS_MIDI_RECONNECT_H

#include <QString>
#include <QStringList>

#include <vector>

namespace lmms
{

class MidiClient;
class MidiPort;


// The problem this file solves, in one paragraph.
//
// A port name is not stable. The ALSA sequencer client behind a controller is
// destroyed when the device is unplugged (or when the controlling process
// exits) and recreated with a NEW client number when it comes back, and the
// name LMMS stores and shows is "<client>:<port> <client name>:<port name>"
// (src/core/midi/MidiAlsaSeq.cpp, portName()). So the record a project carries
// in <midiport inports="..."> - the port a user's keyboard was bound to - names
// an address that no longer exists, and MidiPort::updateReadablePorts() (which
// keeps only the selections still present in the client's current list) drops
// the binding for good. Nothing brings it back when the same controller is
// plugged in again, which is the whole feature of row 18.
//
// The IDENTITY - the client name and the port name, with no address in front of
// them - is what survives. This class remembers an assignment by identity,
// watches the client's port list, and re-establishes the subscription when the
// identity reappears at a new address.

//! The identity of \a portName: everything after the volatile numeric address,
//! i.e. "<client name>:<port name>". A name with no address in front of it is
//! its own identity (never the empty string, so nothing matches everything).
QString midiPortIdentity( const QString& portName );

//! The address in front of \a portName ("<client>:<port>"), or the empty string
//! when the name carries none. This is the VOLATILE half - the number a client
//! gets when it opens the sequencer - and it is never part of an identity.
QString midiPortAddress( const QString& portName );

//! \a portName's identity with \a address written in front of it: the name the
//! engine subscribes to now, carrying the identity the assignment remembers.
QString midiPortWithAddress( const QString& portName, const QString& address );


//! One remembered controller assignment.
struct MidiReconnectAssignment
{
	MidiPort* port = nullptr;   //!< the engine port the controller drives
	bool readable = true;       //!< input direction (false: the port's output)
	QString identity;           //!< "<client name>:<port name>", no address
	QString name;               //!< the live full name last matched
	bool live = false;          //!< the identity was in the last port list
	bool lost = false;          //!< was live, then went away, not seen since
	int reconnects = 0;         //!< times re-established after a loss
	int matches = 0;            //!< live ports sharing the identity this pass
};


//! The re-connection memory of ONE MidiClient.
/*!
 * It lives on the client because that is what owns the port list: the client is
 * the object that knows when the outside world changed (MidiAlsaSeq re-reads
 * the sequencer's client and port inventory once a second and emits
 * readablePortsChanged()/writablePortsChanged() when it differs), and every
 * MidiPort registers with the same client (MidiPort::m_midiClient).
 *
 * Threading: every entry point here runs on the thread that owns the port list
 * - the client's own port-list update, which for MidiAlsaSeq is the GUI/control
 * thread's one-second QTimer, and the control command handlers. Nothing here is
 * called from the MIDI receive thread (MidiAlsaSeq::run()).
 */
class MidiReconnect
{
public:
	explicit MidiReconnect( MidiClient* client = nullptr ):
		m_client( client )
	{
	}

	//! Remember that \a port is subscribed to \a portName.
	/*!
	 * Called from MidiPort::subscribeReadablePort/subscribeWritablePort, so the
	 * memory is built by the same call that builds the subscription - whether it
	 * came from a project load, the GUI's port menu, or this class' own
	 * re-attachment. Re-remembering an assignment that is already known updates
	 * the name it last matched and leaves its counters alone, which is what makes
	 * the re-attachment idempotent.
	 */
	void remember( MidiPort* port, bool readable, const QString& portName );

	//! Forget one direction of \a port (an explicit unsubscribe).
	void forget( MidiPort* port, bool readable, const QString& portName );

	//! Forget every assignment of \a port (the port is being destroyed).
	void forgetPort( MidiPort* port );

	//! Reconcile the memory against a freshly published port list.
	/*!
	 * Every assignment whose identity is in \a readablePorts / \a writablePorts is
	 * marked live; one that is no longer there is marked lost ONCE (the
	 * transition, not every pass). When re-connection is enabled, an assignment
	 * that has been lost and is now live again at a DIFFERENT name has its
	 * subscription re-established at the new name. An assignment that is already
	 * live under the name it holds is left untouched.
	 *
	 * \return how many subscriptions this pass re-established.
	 */
	int reconcile( const QStringList& readablePorts, const QStringList& writablePorts );

	void setEnabled( bool enabled )
	{
		m_enabled = enabled;
		m_modeKnown = true;
	}

	//! The mode, resolved from the config file's `midi/reconnect` key on FIRST
	//! read rather than in the constructor.
	/*!
	 * The constructor of a MidiClient runs inside Engine::init(), and the same
	 * shape of read from a MIDI object's constructor has already cost this
	 * codebase a null dereference (RetroMidiCaptureSettings.cpp: the capture is
	 * constructed before main()). The first reader here is
	 * MidiAlsaSeq::updatePortList() - one second after start-up, on the thread
	 * that owns the port list - or a control command, and both are well past
	 * main().
	 */
	bool isEnabled() const;

	const std::vector<MidiReconnectAssignment>& assignments() const
	{
		return m_assignments;
	}

	//! Total re-attachments since this client opened.
	int reconnected() const { return m_reconnected; }
	//! Total losses (a live assignment going away) since this client opened.
	int lost() const { return m_lostCount; }
	//! Assignments currently live / lost.
	int liveCount() const;
	int lostCount() const;

	//! The assignment of \a port for one direction, or nullptr.
	const MidiReconnectAssignment* assignmentOf( const MidiPort* port, bool readable ) const;

	//! Whether \a client's class publishes a port-list change at all.
	/*!
	 * The honest form of "does this backend expose hotplug notice": it is the
	 * client class's own answer, so a backend this build does not consume changes
	 * from cannot be reported as one that does. Only MidiAlsaSeq answers true in
	 * this build (its one-second port inventory poll); the base returns false and
	 * no other client class overrides it.
	 */
	static bool clientNoticesPortChanges( const MidiClient* client );

private:
	MidiClient* m_client = nullptr;
	std::vector<MidiReconnectAssignment> m_assignments;
	mutable bool m_enabled = true;
	mutable bool m_modeKnown = false;
	int m_reconnected = 0;
	int m_lostCount = 0;
};


// The persisted mode (the config file's `midi/reconnect` key, the same
// "<class>/<attribute>" form settings.get/set use). ON unless it was turned
// off: a bound controller coming back is the expected behaviour, and the switch
// exists for the player whose controller is being hot-swapped mid-take and who
// does not want the engine re-attaching it. It is engine/mode state, never
// project state, so it is not journalled - the midi/retrocapture precedent
// (src/core/RetroMidiCaptureSettings.cpp).
bool midiReconnectPersistedEnabled();
void setMidiReconnectPersistedEnabled( bool enabled );

} // namespace lmms

#endif // LMMS_MIDI_RECONNECT_H
