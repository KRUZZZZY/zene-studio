/*
 * MidiAlsaSeq.cpp - ALSA sequencer client
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "MidiAlsaSeq.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "Song.h"
#include "MidiClock.h"
#include "MidiLearn.h"
#include "MidiPort.h"


#ifdef LMMS_HAVE_ALSA


namespace lmms
{

const int EventPollTimeOut = 250;


// static helper functions
static QString portName( snd_seq_client_info_t * _cinfo,
								snd_seq_port_info_t * _pinfo )
{
	return QString( "%1:%2 %3:%4" ).
					arg( snd_seq_port_info_get_client( _pinfo ) ).
					arg( snd_seq_port_info_get_port( _pinfo ) ).
					arg( snd_seq_client_info_get_name( _cinfo ) ).
					arg( snd_seq_port_info_get_name( _pinfo ) );
}

static QString portName( snd_seq_t * _seq, const snd_seq_addr_t * _addr )
{
	snd_seq_client_info_t * cinfo;
	snd_seq_port_info_t * pinfo;

	snd_seq_client_info_malloc( &cinfo );
	snd_seq_port_info_malloc( &pinfo );

	snd_seq_get_any_port_info( _seq, _addr->client, _addr->port, pinfo );
	snd_seq_get_any_client_info( _seq, _addr->client, cinfo );

	const QString name = portName( cinfo, pinfo );

	snd_seq_client_info_free( cinfo );
	snd_seq_port_info_free( pinfo );

	return name;
}

//! The MidiEvent the retrospective MIDI capture records for one received
//! sequencer event (owner item 14, docs/MIDI-RETRO-CAPTURE.md).
/*!
 * It exists because the capture hook sits above the destination-port lookup and
 * above the event-type switch of run(), so an event addressed to a port this
 * client does not know - the "I played it and nothing was armed" case - is still
 * recorded. The switch below builds its own MidiEvent for the port, unchanged
 * and with the per-type TimePos the capture does not need; only the event types
 * the capture stores are mapped here, and anything else (clock, sensing, ...)
 * arrives as MidiActiveSensing and is not recorded.
 */
static MidiEvent retroCaptureEvent( const snd_seq_event_t * _ev, const snd_seq_addr_t * _source )
{
	switch( _ev->type )
	{
		case SND_SEQ_EVENT_NOTEON:
			return MidiEvent( MidiNoteOn, _ev->data.note.channel,
					_ev->data.note.note, _ev->data.note.velocity, _source );
		case SND_SEQ_EVENT_NOTEOFF:
			return MidiEvent( MidiNoteOff, _ev->data.note.channel,
					_ev->data.note.note, _ev->data.note.velocity, _source );
		case SND_SEQ_EVENT_KEYPRESS:
			return MidiEvent( MidiKeyPressure, _ev->data.note.channel,
					_ev->data.note.note, _ev->data.note.velocity, _source );
		case SND_SEQ_EVENT_CONTROLLER:
			return MidiEvent( MidiControlChange, _ev->data.control.channel,
					_ev->data.control.param, _ev->data.control.value, _source );
		case SND_SEQ_EVENT_PGMCHANGE:
			return MidiEvent( MidiProgramChange, _ev->data.control.channel,
					_ev->data.control.value, 0, _source );
		case SND_SEQ_EVENT_CHANPRESS:
			return MidiEvent( MidiChannelPressure, _ev->data.control.channel,
					_ev->data.control.param, _ev->data.control.value, _source );
		case SND_SEQ_EVENT_PITCHBEND:
			return MidiEvent( MidiPitchBend, _ev->data.control.channel,
					_ev->data.control.value + 8192, 0, _source );
		case SND_SEQ_EVENT_SYSEX:
			return MidiEvent( MidiSysEx, "", 0 );
		default:
			return MidiEvent( MidiActiveSensing );
	}
}



MidiAlsaSeq::MidiAlsaSeq() :
	MidiClient(),
	m_seqMutex(),
	m_seqHandle( nullptr ),
	m_queueID( -1 ),
	m_quit( false ),
	m_portListUpdateTimer( this )
{
	if (int err = snd_seq_open(&m_seqHandle, probeDevice().toLatin1().constData(), SND_SEQ_OPEN_DUPLEX, 0); err < 0)
	{
		fprintf( stderr, "cannot open sequencer: %s\n",
							snd_strerror( err ) );
		return;
	}
	snd_seq_set_client_name( m_seqHandle, "Zene Studio" );


	m_queueID = snd_seq_alloc_queue( m_seqHandle );
	snd_seq_queue_tempo_t * tempo;
	snd_seq_queue_tempo_malloc( &tempo );
	snd_seq_queue_tempo_set_tempo( tempo, 6000000 /
					Engine::getSong()->getTempo() );
	snd_seq_queue_tempo_set_ppq( tempo, 16 );
	snd_seq_set_queue_tempo( m_seqHandle, m_queueID, tempo );
	snd_seq_queue_tempo_free( tempo );

	snd_seq_start_queue( m_seqHandle, m_queueID, nullptr );
	changeQueueTempo( Engine::getSong()->getTempo() );
	connect( Engine::getSong(), SIGNAL(tempoChanged(lmms::bpm_t)),
			this, SLOT(changeQueueTempo(lmms::bpm_t)), Qt::DirectConnection );

	// initial list-update
	updatePortList();

	connect( &m_portListUpdateTimer, SIGNAL(timeout()),
					this, SLOT(updatePortList()));
	// we check for port-changes every second
	m_portListUpdateTimer.start( 1000 );

	// use a pipe to detect shutdown
	if( pipe( m_pipe ) == -1 )
	{
		perror( "MidiAlsaSeq: pipe" );
	}

	start( QThread::IdlePriority );
}




MidiAlsaSeq::~MidiAlsaSeq()
{
	if( isRunning() )
	{
		m_quit = true;
		// Join without a deadline. The budget here was EventPollTimeOut*2 (500 ms)
		// and it is the same defect the engine's own workers had: when the budget
		// expired the thread was still running, and this object IS a QThread - so
		// ~QThread destroyed it while its thread ran and Qt answered with
		//   qFatal("QThread: Destroyed while thread is still running")
		// i.e. an unconditional SIGABRT during shutdown, blamed on whatever was
		// closing. Measured: 1 abort in a 3-run suite sweep from
		// tests/control-shutdown.py under load, with the QFATAL stack naming
		// Engine::destroy -> ~AudioEngine -> delete m_midiClient -> ~MidiAlsaSeq.
		// run() leaves its loop within one poll interval of m_quit being set, so
		// wait() terminates; the trade is the engine workers' trade - a thread
		// that never returns now blocks the shutdown instead of aborting it.
		wait();

		m_seqMutex.lock();
		snd_seq_stop_queue( m_seqHandle, m_queueID, nullptr );
		snd_seq_free_queue( m_seqHandle, m_queueID );
		snd_seq_close( m_seqHandle );
		m_seqMutex.unlock();
	}
}




QString MidiAlsaSeq::probeDevice()
{
	QString dev = ConfigManager::inst()->value( "Midialsaseq", "device" );
	if( dev.isEmpty() )
	{
		if( getenv( "MIDIDEV" ) != nullptr )
		{
			return getenv( "MIDIDEV" );
		}
		return "default";
	}
	return dev;
}




void MidiAlsaSeq::processOutEvent( const MidiEvent& event, const TimePos& time, const MidiPort* port )
{
	// HACK!!! - need a better solution which isn't that easy since we
	// cannot store const-ptrs in our map because we need to call non-const
	// methods of MIDI-port - it's a mess...
	auto p = const_cast<MidiPort*>(port);

	snd_seq_event_t ev;
	snd_seq_ev_clear( &ev );
	snd_seq_ev_set_source( &ev, ( m_portIDs[p][1] != -1 ) ?
					m_portIDs[p][1] : m_portIDs[p][0] );
	snd_seq_ev_set_subs( &ev );
	/* The MIDI-clock family is delivered DIRECT and not on the tick queue
	 * (0.3.0, the `clock.*` group's master half): a clock that waited behind
	 * the note events this client has already scheduled would arrive late by
	 * however far ahead the queue runs, which is the one thing a clock cannot
	 * do, and a system real-time message has no tick position to be scheduled
	 * AT. Everything else keeps the scheduling it has always had. */
	const MidiClockMessage clockMessage =
		midiClockMessageOfByte( static_cast<unsigned char>( event.type() ) );
	if( clockMessage != MidiClockMessage::None )
	{
		snd_seq_ev_set_direct( &ev );
	}
	else
	{
		snd_seq_ev_schedule_tick( &ev, m_queueID, 1, static_cast<int>( time ) );
		ev.queue =  m_queueID;
	}
	switch( event.type() )
	{
		case MidiNoteOn:
			snd_seq_ev_set_noteon( &ev,
						event.channel(),
						event.key(),
						event.velocity() );
			break;

		case MidiNoteOff:
			snd_seq_ev_set_noteoff( &ev,
						event.channel(),
						event.key(),
						event.velocity() );
			break;

		case MidiKeyPressure:
			snd_seq_ev_set_keypress( &ev,
						event.channel(),
						event.key(),
						event.velocity() );
			break;

		case MidiControlChange:
			snd_seq_ev_set_controller( &ev,
						event.channel(),
						event.controllerNumber(),
						event.controllerValue() );
			break;

		case MidiProgramChange:
			snd_seq_ev_set_pgmchange( &ev,
						event.channel(),
						event.program() );
			break;

		case MidiChannelPressure:
			snd_seq_ev_set_chanpress( &ev,
						event.channel(),
						event.channelPressure() );
			break;

		case MidiPitchBend:
			snd_seq_ev_set_pitchbend( &ev,
						event.channel(),
						event.param( 0 ) - 8192 );
			break;

		/* The MIDI-clock family (0.3.0, the `clock.*` group's master half).
		 * These were "unhandled output event" warnings until this change. The
		 * sequencer's own event types carry them; none of these has a channel,
		 * and the two with a payload are the assembled 14-bit position and the
		 * quarter-frame nibble. */
		case MidiSync:
			ev.type = SND_SEQ_EVENT_CLOCK;
			break;

		case MidiStart:
			ev.type = SND_SEQ_EVENT_START;
			break;

		case MidiContinue:
			ev.type = SND_SEQ_EVENT_CONTINUE;
			break;

		case MidiStop:
			ev.type = SND_SEQ_EVENT_STOP;
			break;

		case MidiTimeCode:
			ev.type = SND_SEQ_EVENT_QFRAME;
			ev.data.control.value = event.param( 0 ) & 0x7F;
			break;

		case MidiSongPosition:
			ev.type = SND_SEQ_EVENT_SONGPOS;
			ev.data.control.value = ( event.param( 0 ) & 0x7F )
				| ( ( event.param( 1 ) & 0x7F ) << 7 );
			break;

		default:
			qWarning( "MidiAlsaSeq: unhandled output event %d\n", (int) event.type() );
			return;
	}

	m_seqMutex.lock();
	snd_seq_event_output( m_seqHandle, &ev );
	snd_seq_drain_output( m_seqHandle );
	m_seqMutex.unlock();

}




void MidiAlsaSeq::applyPortMode( MidiPort * _port )
{
	m_seqMutex.lock();

	// determine port-capabilities
	auto caps = std::array<unsigned int, 2>{};

	switch( _port->mode() )
	{
		case MidiPort::Mode::Duplex:
			caps[1] |= SND_SEQ_PORT_CAP_READ |
						SND_SEQ_PORT_CAP_SUBS_READ;

		case MidiPort::Mode::Input:
			caps[0] |= SND_SEQ_PORT_CAP_WRITE |
						SND_SEQ_PORT_CAP_SUBS_WRITE;
			break;

		case MidiPort::Mode::Output:
			caps[1] |= SND_SEQ_PORT_CAP_READ |
						SND_SEQ_PORT_CAP_SUBS_READ;
			break;

		default:
			break;
	}

	for( int i = 0; i < 2; ++i )
	{
		if( caps[i] != 0 )
		{
			// no port there yet?
			if( m_portIDs[_port][i] == -1 )
			{
				// then create one;
				m_portIDs[_port][i] =
						snd_seq_create_simple_port(
							m_seqHandle,
				_port->displayName().toUtf8().constData(),
							caps[i],
						SND_SEQ_PORT_TYPE_MIDI_GENERIC |
						SND_SEQ_PORT_TYPE_APPLICATION );
				continue;
			}
			snd_seq_port_info_t * port_info;
			snd_seq_port_info_malloc( &port_info );
			snd_seq_get_port_info( m_seqHandle, m_portIDs[_port][i],
							port_info );
			snd_seq_port_info_set_capability( port_info, caps[i] );
			snd_seq_set_port_info( m_seqHandle, m_portIDs[_port][i],
							port_info );
			snd_seq_port_info_free( port_info );
		}
		// still a port there although no caps? ( = dummy port)
		else if( m_portIDs[_port][i] != -1 )
		{
			// then remove this port
			snd_seq_delete_simple_port( m_seqHandle,
							m_portIDs[_port][i] );
			m_portIDs[_port][i] = -1;
		}
	}

	m_seqMutex.unlock();
}




void MidiAlsaSeq::applyPortName( MidiPort * _port )
{
	m_seqMutex.lock();

	for( int i = 0; i < 2; ++i )
	{
		if( m_portIDs[_port][i] == -1 )
		{
			continue;
		}
		snd_seq_port_info_t * port_info;
		snd_seq_port_info_malloc( &port_info );
		snd_seq_get_port_info( m_seqHandle, m_portIDs[_port][i],
							port_info );
		snd_seq_port_info_set_name( port_info,
				_port->displayName().toUtf8().constData() );
		snd_seq_set_port_info( m_seqHandle, m_portIDs[_port][i],
							port_info );
		snd_seq_port_info_free( port_info );
	}

	m_seqMutex.unlock();
}




void MidiAlsaSeq::removePort( MidiPort * _port )
{
	if( m_portIDs.contains( _port ) )
	{
		m_seqMutex.lock();
		snd_seq_delete_simple_port( m_seqHandle, m_portIDs[_port][0] );
		snd_seq_delete_simple_port( m_seqHandle, m_portIDs[_port][1] );
		m_seqMutex.unlock();

		m_portIDs.remove( _port );
	}
	MidiClient::removePort( _port );
}




QString MidiAlsaSeq::sourcePortName( const MidiEvent & _event ) const
{
	if( _event.sourcePort() )
	{
		const auto addr = static_cast<const snd_seq_addr_t*>(_event.sourcePort());
		return portName( m_seqHandle, addr );
	}
	return MidiClient::sourcePortName( _event );
}




void MidiAlsaSeq::subscribeReadablePort( MidiPort * _port,
						const QString & _dest,
						bool _subscribe )
{
	if( !m_portIDs.contains( _port ) || m_portIDs[_port][0] < 0 )
	{
		return;
	}

	m_seqMutex.lock();

	snd_seq_addr_t sender;
	if( snd_seq_parse_address( m_seqHandle, &sender,
			_dest.section( ' ', 0, 0 ).toLatin1().constData() ) )
	{
		fprintf( stderr, "error parsing sender-address!\n" );

		m_seqMutex.unlock();
		return;
	}
	snd_seq_port_info_t * port_info;
	snd_seq_port_info_malloc( &port_info );
	snd_seq_get_port_info( m_seqHandle, m_portIDs[_port][0], port_info );
	const snd_seq_addr_t * dest = snd_seq_port_info_get_addr( port_info );
	snd_seq_port_subscribe_t * subs;
	snd_seq_port_subscribe_malloc( &subs );
	snd_seq_port_subscribe_set_sender( subs, &sender );
	snd_seq_port_subscribe_set_dest( subs, dest );
	if( _subscribe )
	{
		snd_seq_subscribe_port( m_seqHandle, subs );
	}
	else
	{
		snd_seq_unsubscribe_port( m_seqHandle, subs );
	}
	snd_seq_port_subscribe_free( subs );
	snd_seq_port_info_free( port_info );

	m_seqMutex.unlock();
}




void MidiAlsaSeq::subscribeWritablePort( MidiPort * _port,
						const QString & _dest,
						bool _subscribe )
{
	if( !m_portIDs.contains( _port ) )
	{
		return;
	}
	const int pid = m_portIDs[_port][1] < 0 ? m_portIDs[_port][0] :
							m_portIDs[_port][1];
	if( pid < 0 )
	{
		return;
	}

	m_seqMutex.lock();

	snd_seq_addr_t dest;
	if( snd_seq_parse_address( m_seqHandle, &dest,
			_dest.section( ' ', 0, 0 ).toLatin1().constData() ) )
	{
		fprintf( stderr, "error parsing dest-address!\n" );
		m_seqMutex.unlock();
		return;
	}
	snd_seq_port_info_t * port_info;
	snd_seq_port_info_malloc( &port_info );
	snd_seq_get_port_info( m_seqHandle, pid, port_info );
	const snd_seq_addr_t * sender = snd_seq_port_info_get_addr( port_info );
	snd_seq_port_subscribe_t * subs;
	snd_seq_port_subscribe_malloc( &subs );
	snd_seq_port_subscribe_set_sender( subs, sender );
	snd_seq_port_subscribe_set_dest( subs, &dest );
	if( _subscribe )
	{
		snd_seq_subscribe_port( m_seqHandle, subs );
	}
	else
	{
		snd_seq_unsubscribe_port( m_seqHandle, subs );
	}
	snd_seq_port_subscribe_free( subs );
	snd_seq_port_info_free( port_info );
	m_seqMutex.unlock();
}




/*! The clock-family message an incoming sequencer event carries, or None for
 *  every event outside the family (0.3.0, the `clock.*` group's slave half). */
static MidiClockMessage sequencerClockMessage( const snd_seq_event_t* ev )
{
	switch( ev->type )
	{
		case SND_SEQ_EVENT_CLOCK:    return MidiClockMessage::Clock;
		case SND_SEQ_EVENT_START:    return MidiClockMessage::Start;
		case SND_SEQ_EVENT_CONTINUE: return MidiClockMessage::Continue;
		case SND_SEQ_EVENT_STOP:     return MidiClockMessage::Stop;
		case SND_SEQ_EVENT_SONGPOS:  return MidiClockMessage::SongPosition;
		case SND_SEQ_EVENT_QFRAME:   return MidiClockMessage::TimeCode;
		default:                     return MidiClockMessage::None;
	}
}

/*! The payload the two events that have one carry: a position in MIDI beats, or
 *  a time-code quarter-frame nibble. The one-byte messages have none. */
static quint32 sequencerClockValue( const snd_seq_event_t* ev, MidiClockMessage message )
{
	if( message == MidiClockMessage::SongPosition || message == MidiClockMessage::TimeCode )
	{
		return static_cast<quint32>( ev->data.control.value ) & 0x3FFF;
	}
	return 0;
}


void MidiAlsaSeq::run()
{
	// watch the pipe and sequencer input events
	int pollfd_count = snd_seq_poll_descriptors_count( m_seqHandle,
								POLLIN );
	auto pollfd_set = new struct pollfd[pollfd_count + 1];
	snd_seq_poll_descriptors( m_seqHandle, pollfd_set + 1, pollfd_count,
								POLLIN );
	pollfd_set[0].fd = m_pipe[0];
	pollfd_set[0].events = POLLIN;
	++pollfd_count;

	while( m_quit == false )
	{
		int pollRet = poll( pollfd_set, pollfd_count, EventPollTimeOut );
		if( pollRet == 0 )
		{
			continue;
		}
		else if( pollRet == -1 )
		{
			// gdb may interrupt the poll
			if( errno == EINTR )
			{
				continue;
			}
			qCritical( "error while polling ALSA sequencer handle" );
			break;
		}
		// shutdown?
		if( m_quit )
		{
			break;
		}

		m_seqMutex.lock();

		// while event queue is not empty
		while( snd_seq_event_input_pending( m_seqHandle, true ) > 0 )
		{
			snd_seq_event_t * ev;
			if( snd_seq_event_input( m_seqHandle, &ev ) < 0 )
			{
				m_seqMutex.unlock();

				qCritical( "error while fetching MIDI event from sequencer" );
				break;
			}
			m_seqMutex.unlock();

			/* The clock family is SYSTEM-wide - a pulse names no port of ours -
			 * so it is dispatched before the destination lookup (0.3.0,
			 * `clock.*`). A slave follows a clock from wherever it arrives,
			 * whether or not one of this client's own ports happens to be
			 * subscribed to the sender. Every other event keeps the routing
			 * below, including the `continue` when no destination matched. */
			{
				const MidiClockMessage clockMessage = sequencerClockMessage( ev );
				if( clockMessage != MidiClockMessage::None )
				{
					MidiClock::instance()->handleInputMessage( clockMessage,
						sequencerClockValue( ev, clockMessage ), MidiClock::nowNs() );
					continue;
				}
			}

			snd_seq_addr_t * source = nullptr;
			MidiPort * dest = nullptr;
			for( int i = 0; i < m_portIDs.size(); ++i )
			{
				if( m_portIDs.values()[i][0] == ev->dest.port )
				{
					dest = m_portIDs.keys()[i];
				}
				if( ( m_portIDs.values()[i][1] != -1 &&
						m_portIDs.values()[i][1] == ev->source.port ) ||
							m_portIDs.values()[i][0] == ev->source.port )
				{
					source = &ev->source;
				}
			}

			// Retrospective MIDI capture (owner item 14, docs/MIDI-RETRO-CAPTURE.md):
			// every received event is recorded once, here - above the destination-port
			// lookup's `continue` below and above the event-type switch - so an event
			// addressed to a port this client does not know is captured too. The
			// sequencer's own time stamp is the tick and the resolved source address is
			// the source port. While nothing is armed this costs one relaxed atomic
			// load per event: the guard is outside, so the mapping below is not even
			// evaluated.
			if( m_retroCapture.isArmed() )
			{
				m_retroCapture.capture( retroCaptureEvent( ev, source ),
						ev->time.tick, ev->source.port );
			}

			if( dest == nullptr )
			{
				continue;
			}

			switch( ev->type )
			{
				case SND_SEQ_EVENT_NOTEON:
					dest->processInEvent( MidiEvent( MidiNoteOn,
								ev->data.note.channel,
								ev->data.note.note,
								ev->data.note.velocity,
								source
								),
							TimePos( ev->time.tick ) );
					break;

				case SND_SEQ_EVENT_NOTEOFF:
					dest->processInEvent( MidiEvent( MidiNoteOff,
								ev->data.note.channel,
								ev->data.note.note,
								ev->data.note.velocity,
								source
								),
							TimePos( ev->time.tick) );
					break;

				case SND_SEQ_EVENT_KEYPRESS:
					dest->processInEvent( MidiEvent(
									MidiKeyPressure,
								ev->data.note.channel,
								ev->data.note.note,
								ev->data.note.velocity,
								source
								), TimePos() );
					break;

				case SND_SEQ_EVENT_CONTROLLER:
				{
					// Global MIDI learn gets the control-change before the port
					// mask is applied, so a control can be bound while nothing is
					// listening on this channel yet. No-op when unarmed.
					const MidiEvent ccEvent(
						MidiControlChange,
						ev->data.control.channel,
						ev->data.control.param,
						ev->data.control.value, source );
					MidiLearn::instance()->handleMidiEvent( ccEvent );
					dest->processInEvent( ccEvent, TimePos() );
					break;
				}

				case SND_SEQ_EVENT_PGMCHANGE:
					dest->processInEvent( MidiEvent(
							MidiProgramChange,
							ev->data.control.channel,
							ev->data.control.value,	0,
							source ),
								TimePos() );
					break;

				case SND_SEQ_EVENT_CHANPRESS:
					dest->processInEvent( MidiEvent(
								MidiChannelPressure,
							ev->data.control.channel,
							ev->data.control.param,
							ev->data.control.value, source ),
									TimePos() );
					break;

				case SND_SEQ_EVENT_PITCHBEND:
					dest->processInEvent( MidiEvent( MidiPitchBend,
							ev->data.control.channel,
							ev->data.control.value + 8192, 0, source ),
									TimePos() );
					break;

				case SND_SEQ_EVENT_SENSING:
				case SND_SEQ_EVENT_CLOCK:
					break;

				default:
					fprintf( stderr,
						"ALSA-sequencer: unhandled input "
							"event %d\n", ev->type );
					break;
			}	// end switch

			m_seqMutex.lock();

		}	// end while

		m_seqMutex.unlock();

	}

	delete[] pollfd_set;
}




void MidiAlsaSeq::changeQueueTempo( bpm_t _bpm )
{
	m_seqMutex.lock();

	snd_seq_change_queue_tempo( m_seqHandle, m_queueID,
					60000000 / (int) _bpm, nullptr );
	snd_seq_drain_output( m_seqHandle );

	m_seqMutex.unlock();
}




void MidiAlsaSeq::updatePortList()
{
	QStringList readablePorts;
	QStringList writablePorts;

	// get input- and output-ports
	snd_seq_client_info_t * cinfo;
	snd_seq_port_info_t * pinfo;

	snd_seq_client_info_malloc( &cinfo );
	snd_seq_port_info_malloc( &pinfo );

	snd_seq_client_info_set_client( cinfo, -1 );

	m_seqMutex.lock();

	while( snd_seq_query_next_client( m_seqHandle, cinfo ) >= 0 )
	{
		int client = snd_seq_client_info_get_client( cinfo );

		snd_seq_port_info_set_client( pinfo, client );
		snd_seq_port_info_set_port( pinfo, -1 );
		while( snd_seq_query_next_port( m_seqHandle, pinfo ) >= 0 )
		{
			// we need both READ and SUBS_READ
			if( ( snd_seq_port_info_get_capability( pinfo )
			     & ( SND_SEQ_PORT_CAP_READ |
					SND_SEQ_PORT_CAP_SUBS_READ ) ) ==
					( SND_SEQ_PORT_CAP_READ |
					  	SND_SEQ_PORT_CAP_SUBS_READ ) )
			{
				readablePorts.push_back( portName( cinfo, pinfo ) );
			}
			if( ( snd_seq_port_info_get_capability( pinfo )
			     & ( SND_SEQ_PORT_CAP_WRITE |
					SND_SEQ_PORT_CAP_SUBS_WRITE ) ) ==
					( SND_SEQ_PORT_CAP_WRITE |
					  	SND_SEQ_PORT_CAP_SUBS_WRITE ) )
			{
				writablePorts.push_back( portName( cinfo, pinfo ) );
			}
		}
	}

	m_seqMutex.unlock();


	snd_seq_client_info_free( cinfo );
	snd_seq_port_info_free( pinfo );

	if( m_readablePorts != readablePorts )
	{
		m_readablePorts = readablePorts;
		emit readablePortsChanged();
	}

	if( m_writablePorts != writablePorts )
	{
		m_writablePorts = writablePorts;
		emit writablePortsChanged();
	}
}



} // namespace lmms

#endif // LMMS_HAVE_ALSA
