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

#include <array>
#include <atomic>
#include <cstdint>

#include "MidiClient.h"
#include "MidiJackOutSchedule.h"
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

	/*! 0.5.0 W4: @a event, produced @a offset frames into the engine period, is written to the
	 *  JACK output port at that frame of the cycle instead of at frame 0. The MIDI path reaches it
	 *  through processOutEvent() (offset 0) until MidiPort hands the offset on (see the request in
	 *  the W4 report: MidiClient::processOutEventAt, which this then overrides). The frame is
	 *  exact when the engine period and the JACK cycle coincide (the usual setup, AudioJack); with
	 *  a different period the message keeps its place in the order of its period. */
	void processOutEventAt( const MidiEvent& event, const TimePos& time, const MidiPort* port, f_cnt_t offset ) override;


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

	//! Senders (the audio thread, the GUI) -> the JACK process callback. Bounded, lock-free
	//! (0.5.0 engine audit E25); each message carries its frame (W4, MidiJackOutSchedule.h).
	MpscQueue<midijack::OutMessage, 1024> m_outQueue;
	std::atomic<std::uint64_t> m_outDropped{0};
	//! The JACK process callback's own list of this cycle's messages, ordered by frame.
	std::array<midijack::OutMessage, midijack::MaxOutPerCycle> m_outCycle{};

	void JackMidiOutEvent(uint8_t *buf, uint8_t len);
	void lock();
	void unlock();

	void getPortInfo( const QString& sPortName, int& nClient, int& nPort );

	volatile bool m_quit;

};


} // namespace lmms

#endif // LMMS_HAVE_JACK

#endif // LMMS_MIDI_JACK_H
