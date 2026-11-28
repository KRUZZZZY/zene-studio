/*
 * MidiWinMM.cpp - WinMM MIDI client
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

#include "MidiWinMM.h"

#ifdef LMMS_HAVE_WINMM

#include <cstdint>
#include <utility>
#include <vector>

#include "MidiWinMMOutput.h"
#include "MidiWinMMShortMessage.h"


namespace lmms
{


MidiWinMM::MidiWinMM() :
	MidiClient(),
	m_outputSink(),
	m_output( m_outputSink, true ),
	m_inputDevices(),
	m_outputDevices(),
	m_inputSubs()
{
	openDevices();
}




MidiWinMM::~MidiWinMM()
{
	closeDevices();
}




void MidiWinMM::processOutEvent( const MidiEvent& event, const TimePos& time, const MidiPort* port )
{
	const DWORD shortMsg = static_cast<DWORD>( winmmShortMessage( event ) );

	// Cluster new-p9-276: one atomic load, a scan of the published POD routing and a bounded
	// push. The driver call - which Microsoft documents can wait for the device - happens on
	// MidiWinMMOutput's worker, never here on the audio thread.
	m_output.processOutEvent( port, static_cast<std::uint32_t>( shortMsg ) );
	(void) time;
}




void MidiWinMM::applyPortMode( MidiPort* port )
{
	// make sure no subscriptions exist which are not possible with
	// current port-mode
	if( !port->isInputEnabled() )
	{
		for( SubMap::Iterator it = m_inputSubs.begin(); it != m_inputSubs.end(); ++it )
		{
			it.value().removeAll( port );
		}
	}

	if( !port->isOutputEnabled() )
	{
		m_output.applyPortMode( port, false );
	}
}




void MidiWinMM::removePort( MidiPort* port )
{
	for( SubMap::Iterator it = m_inputSubs.begin(); it != m_inputSubs.end(); ++it )
	{
		it.value().removeAll( port );
	}

	m_output.removePort( port );

	MidiClient::removePort( port );
}




QString MidiWinMM::sourcePortName( const MidiEvent& event ) const
{
	if( event.sourcePort() )
	{
		return m_inputDevices.value( *static_cast<const HMIDIIN *>( event.sourcePort() ) );
	}

	return MidiClient::sourcePortName( event );
}




void MidiWinMM::subscribeReadablePort( MidiPort* port, const QString& dest, bool subscribe )
{
	if( subscribe && port->isInputEnabled() == false )
	{
		qWarning( "port %s can't be (un)subscribed!\n", port->displayName().toLatin1().constData() );
		return;
	}

	m_inputSubs[dest].removeAll( port );
	if( subscribe )
	{
		m_inputSubs[dest].push_back( port );
	}
}




void MidiWinMM::subscribeWritablePort( MidiPort* port, const QString& dest, bool subscribe )
{
	if( subscribe && port->isOutputEnabled() == false )
	{
		qWarning( "port %s can't be (un)subscribed!\n", port->displayName().toLatin1().constData() );
		return;
	}

	m_output.subscribeWritablePort( port, dest, subscribe );
}




void WINAPI CALLBACK MidiWinMM::inputCallback( HMIDIIN hm, UINT msg, DWORD_PTR inst, DWORD_PTR param1, DWORD_PTR param2 )
{
	if( msg == MIM_DATA )
	{
		auto client = (MidiWinMM *) inst;
		const MidiInputGate::Scope dispatch( client->m_inputGate );
		if( dispatch ) { client->handleInputEvent( hm, param1 ); }
	}
}




void MidiWinMM::handleInputEvent( HMIDIIN hm, DWORD ev )
{
	const int cmd = ev & 0xff;
	if( cmd == MidiActiveSensing )
	{
		return;
	}
	const int par1 = ( ev >> 8 ) & 0xff;
	const int par2 = ev >> 16;
	const MidiEventTypes cmdtype = static_cast<MidiEventTypes>( cmd & 0xf0 );
	const int chan = cmd & 0x0f;

	const QString d = m_inputDevices.value( hm );
	if( d.isEmpty() || !m_inputSubs.contains( d ) )
	{
		return;
	}

	const MidiPortList & l = m_inputSubs[d];
	for (MidiPortList::ConstIterator it = l.begin(); it != l.end(); ++it)
	{
		switch (cmdtype)
		{
			case MidiNoteOn:
			case MidiNoteOff:
			case MidiKeyPressure:
			case MidiControlChange:
			case MidiProgramChange:
			case MidiChannelPressure:
				(*it)->processInEvent(MidiEvent(cmdtype, chan, par1, par2 & 0xff, &hm));
				break;

			case MidiPitchBend:
				(*it)->processInEvent(MidiEvent(cmdtype, chan, par1 + par2 * 128, 0, &hm));
				break;

			default:
				qWarning("MidiWinMM: unhandled input event %d\n", cmdtype);
				break;
		}
	}
}




void MidiWinMM::updateDeviceList()
{
	closeDevices();
	openDevices();

	emit readablePortsChanged();
	emit writablePortsChanged();
}



void MidiWinMM::closeDevices()
{
	m_inputSubs.clear();

	QMapIterator<HMIDIIN, QString> i( m_inputDevices );

	HMIDIIN hInDev;
	while( i.hasNext() )
	{
		hInDev = i.next().key();
		midiInReset( hInDev );
		midiInClose( hInDev );
	}

	// Refuse new output, discard queued commands and wait out an in-flight driver call before
	// any handle is closed (cluster new-p9-276).
	m_output.clearDevices();

	QMapIterator<HMIDIOUT, QString> o( m_outputDevices );
	while( o.hasNext() )
	{
		midiOutClose( o.next().key() );
	}

	m_inputDevices.clear();
	m_outputDevices.clear();
}




void MidiWinMM::openDevices()
{
	m_inputDevices.clear();
	for( unsigned int i = 0; i < midiInGetNumDevs(); ++i )
	{
		MIDIINCAPSW c;
		midiInGetDevCapsW(i, &c, sizeof(c));
		HMIDIIN hm = 0;
		MMRESULT res = midiInOpen( &hm, i, (DWORD_PTR) &inputCallback,
						(DWORD_PTR) this,
							CALLBACK_FUNCTION );
		if( res == MMSYSERR_NOERROR )
		{
			m_inputDevices[hm] = QString::fromWCharArray(c.szPname);
			midiInStart( hm );
		}
	}

	m_outputDevices.clear();
	for( unsigned int i = 0; i < midiOutGetNumDevs(); ++i )
	{
		MIDIOUTCAPSW c;
		midiOutGetDevCapsW(i, &c, sizeof(c));
		HMIDIOUT hm = 0;
		MMRESULT res = midiOutOpen( &hm, i, 0, 0, CALLBACK_NULL );
		if( res == MMSYSERR_NOERROR )
		{
			m_outputDevices[hm] = QString::fromWCharArray(c.szPname);
		}
	}

	// QMap order is the order the old name search sent in, so keep it.
	std::vector<std::pair<QString, std::uintptr_t>> outputs;
	outputs.reserve( static_cast<std::size_t>( m_outputDevices.size() ) );
	for( QMap<HMIDIOUT, QString>::ConstIterator it = m_outputDevices.begin(); it != m_outputDevices.end(); ++it )
	{
		outputs.emplace_back( it.value(), reinterpret_cast<std::uintptr_t>( it.key() ) );
	}
	m_output.setDevices( std::move( outputs ) );
}


} // namespace lmms

#endif // LMMS_HAVE_WINMM
