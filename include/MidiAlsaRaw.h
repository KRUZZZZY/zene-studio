/*
 * MidiAlsaRaw.h - MIDI client for RawMIDI via ALSA
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

#ifndef LMMS_MIDI_ALSA_RAW_H
#define LMMS_MIDI_ALSA_RAW_H

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_ALSA

#include <alsa/asoundlib.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <QThread>

#include "MidiClient.h"
#include "MpscQueue.h"


struct pollfd;


namespace lmms
{

class MidiAlsaRaw : public QThread, public MidiClientRaw
{
	Q_OBJECT
public:
	MidiAlsaRaw();
	~MidiAlsaRaw() override;

	static QString probeDevice();

	//! Joins the input worker (stopWorker()); the device stays open until destruction.
	void stopInput() override
	{
		stopWorker();
	}

	//! The realtime output messages the bounded hand-off refused because it was full (BUG 12).
	std::uint64_t outputQueueOverflowCount() const noexcept override
	{
		return m_outDropped.load( std::memory_order_relaxed );
	}


	inline static QString name()
	{
		return QT_TRANSLATE_NOOP( "MidiSetupWidget",
			"ALSA Raw-MIDI (Advanced Linux Sound Architecture)" );
	}

	inline static QString configSection()
	{
		return "MidiAlsaRaw";
	}


protected:
	void sendByte( const unsigned char c ) override;
	void run() override;

	/*! The audio path's output, and never the device: snd_rawmidi_write() blocks for as long as a
	 *  full or stalled device takes, and this runs inside a render period. The event is encoded on
	 *  this thread and handed to the worker whole, which is one compare-exchange plus at most one
	 *  non-blocking wake byte: no allocation, no lock, no wait (BUG 12, cluster new-p12-118). */
	void processOutEvent( const MidiEvent& event, const TimePos& time, const MidiPort* port ) override;

	//! Asks the worker to stop, wakes it out of its poll and joins it. Idempotent;
	//! never terminates the thread. Called by the destructor before any resource
	//! the worker uses is released (Q050-488).
	void stopWorker();


private:
	//! One outgoing message: the bytes MidiClientRaw's encoder produced for one event, handed over
	//! whole so two senders can never interleave their bytes in the device stream.
	struct OutMessage
	{
		std::array<unsigned char, 3> bytes{};
		std::uint8_t length = 0;
	};

	//! Messages that may wait for the worker. A full queue refuses (and counts) further events
	//! instead of growing, so a stalled device or a stopped worker cannot consume memory from the
	//! audio path.
	static constexpr std::size_t OutputQueueCapacity = 1024;

	void closeDevice();
	void closeWakePipe();
	//! The hand-off's single consumer: writes what the senders queued, and never blocks. Only the
	//! worker calls it while it runs, and the destructor calls it after the join.
	void drainOutQueue();
	//! Writes m_writing's remaining bytes. False: the device buffer is full, so the rest waits for
	//! the next round instead of being dropped.
	bool writePending();
	//! One non-blocking wake byte per drain, not per event: a byte still in the pipe is a wake not
	//! served yet, so the racing senders cannot lose one.
	void requestWake();
	//! The worker's half of requestWake(): from here the next sender asks for a wake again.
	void acknowledgeWake();

	snd_rawmidi_t * m_input, * * m_inputp;
	snd_rawmidi_t * m_output, * * m_outputp;
	//! Number of ALSA descriptors in m_pfds; m_pfds holds one more, the wake pipe.
	int m_npfds;
	pollfd * m_pfds;
	//! Self-pipe: a sender that queued output, or stopWorker(), writes a byte so the worker's poll
	//! returns at once.
	int m_wakePipe[2];

	//! Senders (the audio thread, the GUI) -> the worker. Bounded and lock-free (MpscQueue.h).
	MpscQueue<OutMessage, OutputQueueCapacity> m_outQueue;
	std::atomic<std::uint64_t> m_outDropped{0};
	//! Set by the sender that put the wake byte in the pipe; cleared by the worker before a drain.
	std::atomic<bool> m_wakeRequested{false};
	//! The message the worker is writing, and how much of it the device has taken. The worker is
	//! their only writer, so they need no synchronisation.
	OutMessage m_writing;
	std::uint8_t m_written = 0;

	std::atomic_bool m_quit;

} ;


} // namespace lmms

#endif // LMMS_HAVE_ALSA

#endif // LMMS_MIDI_ALSA_RAW_H
