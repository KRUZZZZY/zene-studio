/*
 * MidiReconnect.cpp - the re-connection memory of a MIDI client (0.3.0
 *                     feature-list row 18, OWNER-31 item 7)
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

#include "MidiReconnect.h"

#include <QRegularExpression>

#include "ConfigManager.h"
#include "MidiClient.h"
#include "MidiPort.h"

namespace lmms
{

namespace
{

//! The config file's own key form (settings.get/set split the same way).
const QString ReconnectClass = QStringLiteral( "midi" );
const QString ReconnectAttribute = QStringLiteral( "reconnect" );
const QString ReconnectOff = QStringLiteral( "0" );
const QString ReconnectOn = QStringLiteral( "1" );

//! The address an ALSA-sequencer port name starts with: "<client>:<port>".
const QRegularExpression& addressPattern()
{
	static const QRegularExpression pattern( QStringLiteral( "^\\d+:\\d+$" ) );
	return pattern;
}

//! The boundary between a port name's address and its identity: the first
//! space. Everything before it is the address, everything after it is
//! "<client name>:<port name>" (src/core/midi/MidiAlsaSeq.cpp, portName()).
int identityStart( const QString& portName )
{
	return portName.indexOf( QLatin1Char( ' ' ) );
}

//! The live name for \a identity, preferring the name the assignment last
//! matched, and preferring the LOWEST address when several clients share a name
//! - which they do: two instances of the same controller, or a device that
//! enumerates twice, produce two clients with one name.
QString bestMatch( const QStringList& livePorts, const QString& identity,
	const QString& lastName, int* matchCount )
{
	QString first;
	QString exact;
	int matches = 0;
	for ( const QString& candidate : livePorts )
	{
		if ( midiPortIdentity( candidate ) != identity ) { continue; }
		++matches;
		if ( first.isEmpty() ) { first = candidate; }
		if ( candidate == lastName ) { exact = candidate; }
	}
	if ( matchCount != nullptr ) { *matchCount = matches; }
	return !exact.isEmpty() ? exact : first;
}

} // namespace


QString midiPortIdentity( const QString& portName )
{
	const int space = identityStart( portName );
	if ( space < 0 ) { return portName; }
	// Only a REAL address ("<digits>:<digits>") is stripped. A name that merely
	// contains a space - a raw client's "Some Raw Port" - is its own identity;
	// cutting it at the first space would make two such names, and every future
	// one, a single identity.
	if ( !addressPattern().match( portName.left( space ) ).hasMatch() ) { return portName; }
	const QString tail = portName.mid( space + 1 );
	return tail.isEmpty() ? portName : tail;
}


QString midiPortAddress( const QString& portName )
{
	const int space = identityStart( portName );
	if ( space < 0 ) { return QString(); }
	const QString head = portName.left( space );
	return addressPattern().match( head ).hasMatch() ? head : QString();
}


QString midiPortWithAddress( const QString& portName, const QString& address )
{
	if ( address.isEmpty() ) { return portName; }
	return address + QLatin1Char( ' ' ) + midiPortIdentity( portName );
}


void MidiReconnect::remember( MidiPort* port, bool readable, const QString& portName )
{
	if ( port == nullptr || portName.isEmpty() ) { return; }
	const QString identity = midiPortIdentity( portName );
	for ( MidiReconnectAssignment& assignment : m_assignments )
	{
		if ( assignment.port != port || assignment.readable != readable
			|| assignment.identity != identity )
		{
			continue;
		}
		// Already known: the name it now holds is the one just subscribed. The
		// counters stay where they are, so a re-attachment (which calls this
		// through MidiPort::subscribeReadablePort) cannot reset its own record.
		assignment.name = portName;
		return;
	}
	MidiReconnectAssignment assignment;
	assignment.port = port;
	assignment.readable = readable;
	assignment.identity = identity;
	assignment.name = portName;
	// The caller is subscribing to a name it has just resolved, so the identity
	// is live as of this call; the next reconcile is what can contradict it.
	assignment.live = true;
	assignment.matches = 1;
	m_assignments.push_back( assignment );
}


void MidiReconnect::forget( MidiPort* port, bool readable, const QString& portName )
{
	const QString identity = portName.isEmpty() ? QString() : midiPortIdentity( portName );
	for ( auto it = m_assignments.begin(); it != m_assignments.end(); )
	{
		const bool samePort = it->port == port && it->readable == readable;
		if ( samePort && (identity.isEmpty() || it->identity == identity) )
		{
			it = m_assignments.erase( it );
			continue;
		}
		++it;
	}
}


void MidiReconnect::forgetPort( MidiPort* port )
{
	for ( auto it = m_assignments.begin(); it != m_assignments.end(); )
	{
		if ( it->port == port ) { it = m_assignments.erase( it ); }
		else { ++it; }
	}
}


bool MidiReconnect::isEnabled() const
{
	if ( !m_modeKnown )
	{
		m_enabled = midiReconnectPersistedEnabled();
		m_modeKnown = true;
	}
	return m_enabled;
}


int MidiReconnect::reconcile( const QStringList& readablePorts, const QStringList& writablePorts )
{
	const bool enabled = isEnabled();
	int applied = 0;
	for ( MidiReconnectAssignment& assignment : m_assignments )
	{
		const QStringList& livePorts = assignment.readable ? readablePorts : writablePorts;
		int matches = 0;
		const QString target = bestMatch( livePorts, assignment.identity, assignment.name,
			&matches );
		assignment.matches = matches;

		if ( target.isEmpty() )
		{
			// The transition, not every pass: a controller left unplugged must
			// count as lost once, and the memory stays so it can come back.
			if ( assignment.live )
			{
				assignment.live = false;
				assignment.lost = true;
				++m_lostCount;
			}
			continue;
		}

		// Nothing to do ONLY when it is live under the name it already holds.
		// A LOST assignment is re-established even when the address came back
		// the same - which the sequencer does whenever the freed number is still
		// free - because the loss is what dropped the subscription: the engine
		// port drops a selection whose port left its list
		// (MidiPort::updateReadablePorts), so "the name is the same again" is
		// not the same as "the subscription is there again".
		const bool wasLost = assignment.lost;
		if ( !wasLost && target == assignment.name )
		{
			assignment.live = true;
			continue;
		}

		if ( !enabled )
		{
			// The loss stands, and nothing is re-attached: that is the whole
			// content of the switch.
			assignment.live = false;
			continue;
		}

		assignment.name = target;
		assignment.live = true;
		assignment.lost = false;
		if ( wasLost )
		{
			++assignment.reconnects;
			++m_reconnected;
		}
		if ( assignment.port != nullptr )
		{
			if ( assignment.readable )
			{
				assignment.port->subscribeReadablePort( target, true );
			}
			else
			{
				assignment.port->subscribeWritablePort( target, true );
			}
		}
		++applied;
	}
	return applied;
}


int MidiReconnect::liveCount() const
{
	int live = 0;
	for ( const MidiReconnectAssignment& assignment : m_assignments )
	{
		if ( assignment.live ) { ++live; }
	}
	return live;
}


int MidiReconnect::lostCount() const
{
	int lost = 0;
	for ( const MidiReconnectAssignment& assignment : m_assignments )
	{
		if ( assignment.lost ) { ++lost; }
	}
	return lost;
}


const MidiReconnectAssignment* MidiReconnect::assignmentOf( const MidiPort* port,
	bool readable ) const
{
	for ( const MidiReconnectAssignment& assignment : m_assignments )
	{
		if ( assignment.port == port && assignment.readable == readable )
		{
			return &assignment;
		}
	}
	return nullptr;
}


bool MidiReconnect::clientNoticesPortChanges( const MidiClient* client )
{
	return client != nullptr && client->noticesPortChanges();
}


bool midiReconnectPersistedEnabled()
{
	return ConfigManager::inst()->value( ReconnectClass, ReconnectAttribute,
		ReconnectOn ) != ReconnectOff;
}


void setMidiReconnectPersistedEnabled( bool enabled )
{
	ConfigManager::inst()->setValue( ReconnectClass, ReconnectAttribute,
		enabled ? ReconnectOn : ReconnectOff );
	ConfigManager::inst()->saveConfigFile();
}

} // namespace lmms
