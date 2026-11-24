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

	//! Joins the input worker (stopWorker()); the device stays open until destruction, and output
	//! keeps flowing: the output writer is a thread of its own.
	void stopInput() override
	{
		stopWorker();
	}

	/*! The realtime output messages that were not delivered: refused because the bounded hand-off
	 *  was full, or still unwritten when the device was closed after the shutdown retries ran out
	 *  (BUG 12, cluster new-p12-118). Both are counted, never lost silently. */
	std::uint64_t outputQueueOverflowCount() const noexcept override
	{
		return m_outDropped.load( std::memory_order_relaxed )
			+ m_outUndelivered.load( std::memory_order_relaxed );
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

	/*! The audio path's output, and nothing else: the event is encoded on this thread and the whole
	 *  message is pushed into a bounded lock-free queue. That push is one compare-exchange - no
	 *  allocation, no lock, no wait and no system call of any kind, so neither a full device buffer
	 *  nor a stalled one can hold a render period (BUG 12, cluster new-p12-118). */
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

	//! Messages that may wait for the output writer. A full queue refuses (and counts) further events
	//! instead of growing, so a stalled device cannot consume memory from the audio path.
	static constexpr std::size_t OutputQueueCapacity = 1024;

	/*! The device's only writer, on a thread of its own. It is deliberately NOT the input worker: an
	 *  input poll or read error ends that worker, and output must keep being delivered after it (the
	 *  device's output handle is still open). Needs no wake from the senders - they must not perform
	 *  I/O - so it paces itself: a short sleep while idle, a shorter one while the device is behind. */
	class OutputWriter : public QThread
	{
	public:
		explicit OutputWriter( MidiAlsaRaw* client ) : m_client( client ) {}
		void run() override;

	private:
		MidiAlsaRaw* m_client;
	};

	void closeDevice();
	void closeWakePipe();
	//! The hand-off's single consumer: writes what the senders queued. True when nothing is left
	//! waiting for the device; false when it refused and the rest must wait for the next round.
	bool drainOutQueue();
	//! Writes m_writing's remaining bytes. False: the device buffer is full, so the rest waits for
	//! the next round instead of being dropped.
	bool writePending();
	//! Stops the output writer and joins it. Idempotent; releases nothing else.
	void stopOutput();
	//! The bounded shutdown flush: retries a message the device refused until the retry deadline,
	//! then counts what is still undelivered instead of dropping it silently. Called by the writer as
	//! it leaves (and again by the destructor after the join, which is what covers a client whose
	//! writer never started).
	void finishOutput();

	snd_rawmidi_t * m_input, * * m_inputp;
	snd_rawmidi_t * m_output, * * m_outputp;
	//! Number of ALSA descriptors in m_pfds; m_pfds holds one more, the wake pipe.
	int m_npfds;
	pollfd * m_pfds;
	//! Self-pipe: stopWorker() writes a byte so the worker's poll returns at once.
	int m_wakePipe[2];

	//! Senders (the audio thread, the GUI) -> the output writer. Bounded and lock-free (MpscQueue.h).
	MpscQueue<OutMessage, OutputQueueCapacity> m_outQueue;
	std::atomic<std::uint64_t> m_outDropped{0};
	//! Messages that were still unwritten when the device was closed (see finishOutput()).
	std::atomic<std::uint64_t> m_outUndelivered{0};
	//! The output writer's own stop flag; the senders never touch it.
	std::atomic<bool> m_outputQuit{false};
	OutputWriter m_outputWriter{ this };
	//! The message the writer is writing, and how much of it the device has taken. The writer is their
	//! only writer (and, after the join, the destructor), so they need no synchronisation.
	OutMessage m_writing;
	std::uint8_t m_written = 0;

	std::atomic_bool m_quit;

} ;


} // namespace lmms

#endif // LMMS_HAVE_ALSA

#endif // LMMS_MIDI_ALSA_RAW_H
