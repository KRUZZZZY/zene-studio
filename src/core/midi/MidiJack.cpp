/*
 * MidiJack.cpp - MIDI client for Jack
 *
 * Copyright (c) 2015 Shane Ambler <develop/at/shaneware.biz>
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

#include "MidiJack.h"

#include <algorithm>

#ifdef LMMS_HAVE_JACK

#include <QMessageBox>

#include "AudioEngine.h"
#include "AudioJack.h"
#include "ConfigManager.h"
#include "GuiApplication.h"
#include "Engine.h"
#include "MainWindow.h"

namespace lmms
{

/* callback functions for jack */
static int JackMidiProcessCallback(jack_nframes_t nframes, void *arg)
{
	auto jmd = (MidiJack*)arg;

	if (nframes <= 0)
		return (0);

	jmd->JackMidiRead(nframes);
	jmd->JackMidiWrite(nframes);

	return (0);
}

static void JackMidiShutdown(void *arg)
{
        //: When JACK(JACK Audio Connection Kit) disconnects, it will show the following message (title)
	QString msg_short = MidiJack::tr("JACK server down");
        //: When JACK(JACK Audio Connection Kit) disconnects, it will show the following message (dialog message)
	QString msg_long = MidiJack::tr("The JACK server seems to be shut down.");
	QMessageBox::information(gui::getGUI()->mainWindow(), msg_short, msg_long);
}

MidiJack::MidiJack() :
	MidiClientRaw(),
	m_jackClient( nullptr ),
	m_input_port( nullptr ),
	m_output_port( nullptr ),
	m_quit( false )
{
	// if jack is currently used for audio then we share the connection
	// AudioJack creates and maintains the jack connection
	// and also handles the callback, we pass it our address
	// so that we can also process during the callback

	m_jackAudio = dynamic_cast<AudioJack*>(Engine::audioEngine()->audioDev());
	if( m_jackAudio )
	{
		// if a jack connection has been created for audio we use that
		m_jackAudio->addMidiClient(this);
	}else{
		m_jackAudio = nullptr;
		m_jackClient = jack_client_open(probeDevice().toLatin1().data(),
										JackNoStartServer, nullptr);

		if(m_jackClient)
		{
			jack_set_process_callback(m_jackClient,
							JackMidiProcessCallback, this);
			jack_on_shutdown(m_jackClient,
							JackMidiShutdown, 0);
		}
	}

	if(jackClient())
	{
		// 0.5.0 (engine audit E25): MIDI out - the messages processOutEvent() queues are
		// written to this port by JackMidiWrite() in the JACK process callback.
		m_output_port = jack_port_register(
				jackClient(), "MIDI out", JACK_DEFAULT_MIDI_TYPE,
				JackPortIsOutput, 0);

		m_input_port = jack_port_register(
				jackClient(), "MIDI in", JACK_DEFAULT_MIDI_TYPE,
				JackPortIsInput, 0);

		if(jack_activate(jackClient()) == 0 )
		{
			// only start thread, if we have an active jack client.
			start( QThread::LowPriority );
		}
	}
}

MidiJack::~MidiJack()
{
	if(jackClient())
	{
		if (m_jackAudio) {
			// remove ourselves first (atomically), so we will not get called again
			m_jackAudio->removeMidiClient();
		}

		if( jack_port_unregister( jackClient(), m_input_port) != 0){
			printf("Failed to unregister jack midi input\n");
		}

		if( m_output_port != nullptr && jack_port_unregister( jackClient(), m_output_port) != 0){
			printf("Failed to unregister jack midi output\n");
		}

		if(m_jackClient)
		{
			// an m_jackClient means we are handling the jack connection
			if( jack_deactivate(m_jackClient) != 0){
				printf("Failed to deactivate jack midi client\n");
			}

			if( jack_client_close(m_jackClient) != 0){
				printf("Failed close jack midi client\n");
			}
		}
	}
	if( isRunning() )
	{
		m_quit = true;
		wait( 1000 );
		terminate();
	}
}

jack_client_t* MidiJack::jackClient()
{
	if( m_jackAudio == nullptr && m_jackClient == nullptr)
		return nullptr;

	if( m_jackAudio == nullptr && m_jackClient )
		return m_jackClient;

	return m_jackAudio->jackClient();
}

QString MidiJack::probeDevice()
{
	// The client name lives under the "zene" attribute; the pre-rename "lmms"
	// attribute is still read so an adopted config keeps the user's device.
	QString jid = ConfigManager::inst()->value( "midijack", "zene" );
	if( jid.isEmpty() )
	{
		jid = ConfigManager::inst()->value( "midijack", "lmms" );
	}
	if( jid.isEmpty() )
	{
		return "Zene Studio";
	}
	return jid;
}

// we read data from jack
void MidiJack::JackMidiRead(jack_nframes_t nframes)
{
	void* port_buf = jack_port_get_buffer(m_input_port, nframes);
	jack_midi_event_t in_event;
	jack_nframes_t event_index = 0;
	jack_nframes_t event_count = jack_midi_get_event_count(port_buf);

	int rval = jack_midi_event_get(&in_event, port_buf, 0);
	if (rval == 0 /* 0 = success */)
	{
		for (unsigned int i = 0; i < nframes; i++)
		{
			while((in_event.time == i) && (event_index < event_count))
			{
				// lmms is setup to parse bytes coming from a device
				// parse it byte by byte as it expects
				for (unsigned int b = 0; b < in_event.size; b++)
					parseData( *(in_event.buffer + b) );

				event_index++;
				if(event_index < event_count)
					jack_midi_event_get(&in_event, port_buf, event_index);
			}
		}
	}
}

namespace
{

//! The message processOutEvent() is assembling on this thread (MidiClientRaw writes it byte by
//! byte through sendByte()); per thread, so concurrent senders never interleave.
thread_local uint8_t t_outBytes[3];
thread_local uint8_t t_outLength = 0;

} // namespace

void MidiJack::processOutEvent( const MidiEvent& event, const TimePos& time, const MidiPort* port )
{
	t_outLength = 0;
	MidiClientRaw::processOutEvent( event, time, port );
	if( t_outLength == 0 ) { return; }
	OutMessage message{};
	std::copy( t_outBytes, t_outBytes + t_outLength, message.bytes );
	message.length = t_outLength;
	t_outLength = 0;
	if( !m_outQueue.push( message ) ) { m_outDropped.fetch_add( 1, std::memory_order_relaxed ); }
}

void MidiJack::sendByte( const unsigned char c )
{
	// Collected into this thread's message; longer messages (SysEx) are not carried by this port.
	if( t_outLength < sizeof( t_outBytes ) ) { t_outBytes[t_outLength++] = c; }
}

// we write data to jack
void MidiJack::JackMidiWrite(jack_nframes_t nframes)
{
	// The JACK process callback (realtime): every queued message goes out at the start of this
	// cycle. jack_midi_event_write copies into the port buffer; nothing allocates or locks.
	if( m_output_port == nullptr ) { return; }
	void* buffer = jack_port_get_buffer( m_output_port, nframes );
	jack_midi_clear_buffer( buffer );
	OutMessage message;
	while( m_outQueue.pop( &message ) )
	{
		if( jack_midi_event_write( buffer, 0, message.bytes, message.length ) != 0 )
		{
			m_outDropped.fetch_add( 1, std::memory_order_relaxed );
		}
	}
}

void MidiJack::run()
{
	while( m_quit == false )
	{
		// we sleep the thread to keep it alive
		// midi processing is handled by jack server callbacks
		sleep(1);
	}
}

} // namespace lmms

#endif // LMMS_HAVE_JACK
