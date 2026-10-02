/*
 * MidiJack.h - MIDI client for Jack
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

#ifndef LMMS_MIDI_JACK_H
#define LMMS_MIDI_JACK_H

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_JACK
#ifndef LMMS_HAVE_WEAKJACK
#include <jack/jack.h>
#include <jack/midiport.h>
#else
#include <weak_libjack.h>
#endif

#include <QThread>

#include <atomic>
#include <cstdint>

#include "MidiClient.h"
#include "MpscQueue.h"


namespace lmms
{

constexpr size_t JACK_MIDI_BUFFER_MAX = 64; /* events */

class AudioJack;

class MidiJack : public QThread, public MidiClientRaw
{
        Q_OBJECT
public:
	MidiJack();
	~MidiJack() override;

	jack_client_t* jackClient();

	static QString probeDevice();

	inline static QString name()
	{
		return( QT_TRANSLATE_NOOP( "MidiSetupWidget",
			"Jack-MIDI" ) );
	}

	void JackMidiWrite(jack_nframes_t nframes);
	void JackMidiRead(jack_nframes_t nframes);


	inline static QString configSection()
	{
		return "MidiJack";
	}


protected:
	void sendByte( const unsigned char c ) override;
	//! Encodes the event with MidiClientRaw's own encoder into this thread's buffer, then queues
	//! the whole message, so two senders never interleave bytes.
	void processOutEvent( const MidiEvent& event, const TimePos& time, const MidiPort* port ) override;
	void run() override;


private:
	AudioJack *m_jackAudio;
	jack_client_t *m_jackClient;
	jack_port_t *m_input_port;
	jack_port_t *m_output_port;
	uint8_t m_jack_buffer[JACK_MIDI_BUFFER_MAX * 4];

	//! 0.5.0 (engine audit E25): one short MIDI message on its way to the JACK output port.
	struct OutMessage
	{
		uint8_t bytes[3];
		uint8_t length;
	};
	//! Senders (the audio thread, the GUI) -> the JACK process callback. Bounded, lock-free.
	MpscQueue<OutMessage, 1024> m_outQueue;
	std::atomic<std::uint64_t> m_outDropped{0};

	void JackMidiOutEvent(uint8_t *buf, uint8_t len);
	void lock();
	void unlock();

	void getPortInfo( const QString& sPortName, int& nClient, int& nPort );

	volatile bool m_quit;

};


} // namespace lmms

#endif // LMMS_HAVE_JACK

#endif // LMMS_MIDI_JACK_H
