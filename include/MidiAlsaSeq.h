/*
 * MidiAlsaSeq.h - ALSA-sequencer-client
 *
 * Copyright (c) 2005-2013 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_MIDI_ALSA_SEQ_H
#define LMMS_MIDI_ALSA_SEQ_H

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_ALSA
#include <cstdint>
#ifdef LMMS_TESTING
#include <atomic>
#endif

#include <alsa/asoundlib.h>

#include <QMap>
#include <QMutex>
#include <QThread>
#include <QTimer>


#include "MidiClient.h"
#include "MidiOutQueue.h"




namespace lmms
{


class MidiAlsaSeq : public QThread, public MidiClient
{
	Q_OBJECT
public:
	MidiAlsaSeq();
	~MidiAlsaSeq() override;
#ifdef LMMS_TESTING
	struct PipeOnlyForTesting {};
	explicit MidiAlsaSeq(PipeOnlyForTesting);
	int wakeReadFdForTesting() const noexcept { return m_pipe[0]; }
	bool pollWaitActiveForTesting() const noexcept
	{
		return m_pollWaitActiveForTesting.load(std::memory_order_acquire);
	}
#ifdef LMMS_HAVE_ALSA
	//! The sequencer mutex, for the test that proves the audio-thread output path
	//! never takes it. The sequencer worker owns the mutex; a contended mutex on the
	//! audio thread can stall an audio period, which is how the MIDI clock used to
	//! reach the device from the audio path.
	QMutex& seqMutexForTesting() noexcept { return m_seqMutex; }
#endif
	/*! Q050-277: the sequencer thread's drain, for a PipeOnlyForTesting client (whose thread does not run): pops what
	 *  both output queues hold, the audio thread's first, and hands each command to @a sink with whether it came
	 *  through sendFromNonAudioThread(). At most @a limit per queue per call, so a test of a corrupted queue fails
	 *  instead of spinning. Returns how many it popped. */
	template <typename Sink>
	std::size_t drainOutputForTesting(Sink&& sink, std::size_t limit = MidiOutQueue::Capacity)
	{
		std::size_t popped = 0;
		MidiOutQueue::Command command;
		for (std::size_t i = 0; i < limit && m_outQueue.pop(command); ++i) { sink(command, false); ++popped; }
		for (std::size_t i = 0; i < limit && m_nonAudioQueue.pop(command); ++i) { sink(command, true); ++popped; }
		return popped;
	}
#endif // LMMS_TESTING

	static QString probeDevice();

	//! Q050-246: the output queue's timer, in Hz: a scheduled event leaves on this grid (0.25 ms).
	static constexpr unsigned int QueueTimerHz = 4000;


	inline static QString name()
	{
		return QT_TRANSLATE_NOOP( "MidiSetupWidget",
			"ALSA-Sequencer (Advanced Linux Sound "
							"Architecture)" );
	}

	inline static QString configSection()
	{
		return "Midialsaseq";
	}



	void processOutEvent( const MidiEvent & _me,
						const TimePos & _time,
						const MidiPort * _port ) override;
	/*! Q050-246: @a event, produced @a offset frames into the render period, is due at the period's start plus
	 *  that offset in real time; the sequencer thread schedules it there on the ALSA queue (sub-tick, in
	 *  nanoseconds) instead of at the period's whole tick. Called on the audio path: one clock read and a
	 *  bounded, lock-free push, as processOutEvent(). */
	void processOutEventAt( const MidiEvent& event, const TimePos& time, const MidiPort* port,
						f_cnt_t offset ) override;

	void applyPortMode( MidiPort * _port ) override;
	void applyPortName( MidiPort * _port ) override;

	void removePort( MidiPort * _port ) override;

	//! TRUE: this client re-reads its port inventory once a second and publishes
	//! the difference (updatePortList()), which is what lets the engine
	//! re-establish a controller assignment whose device went away and came back
	//! (0.3.0 feature-list row 18, OWNER-31 item 7).
	bool noticesPortChanges() const override
	{
		return true;
	}

	std::uint64_t outputQueueOverflowCount() const noexcept override
	{
		return m_outQueue.overflowCount();
	}
	//! Q050-277: events sendFromNonAudioThread() found its own queue full for (refused, not lost: the caller retries).
	std::uint64_t nonAudioQueueOverflowCount() const noexcept
	{
		return m_nonAudioQueue.overflowCount();
	}

	// list seq-ports from ALSA
	QStringList readablePorts() const override
	{
		return m_readablePorts;
	}

	QStringList writablePorts() const override
	{
		return m_writablePorts;
	}

	// return name of port which specified MIDI event came from
	QString sourcePortName( const MidiEvent & ) const override;

	// (un)subscribe given MidiPort to/from destination-port
	void subscribeReadablePort( MidiPort * _port,
						const QString & _dest,
						bool _subscribe = true ) override;
	void subscribeWritablePort( MidiPort * _port,
						const QString & _dest,
						bool _subscribe = true ) override;
	void connectRPChanged( QObject * _receiver,
							const char * _member ) override
	{
		connect( this, SIGNAL( readablePortsChanged() ),
							_receiver, _member );
	}

	void connectWPChanged( QObject * _receiver,
							const char * _member ) override
	{
		connect( this, SIGNAL( writablePortsChanged() ),
							_receiver, _member );
	}


private slots:
	void changeQueueTempo( lmms::bpm_t _bpm );
	void updatePortList();


protected:
	/*! Q050-277: the off-audio output (MidiClock's capture sender). It does NOT touch m_outQueue - that queue is
	 *  single-producer and its producer is the audio thread - but pushes into m_nonAudioQueue, whose one producer is
	 *  whoever holds MidiClient's off-audio mutex, and wakes the sequencer thread, which drains both queues: one
	 *  consumer merges track MIDI and capture clock. False when that queue is full. */
	bool processNonAudioOutEvent( const MidiEvent& event, const MidiPort* port ) override;

private:
	//! The shared, audio-path half of processOutEvent() / processOutEventAt(): @a dueNs 0 = no frame position.
	void enqueueOutEvent( const MidiEvent& event, const TimePos& time, const MidiPort* port, std::int64_t dueNs );
	void run() override;
	void processQueuedOutEvent(const MidiOutQueue::Command& command);

#ifdef LMMS_HAVE_ALSA
	QMutex m_seqMutex;
	snd_seq_t * m_seqHandle;
	struct Ports
	{
		Ports() { p[0] = -1; p[1] = -1; }
		int & operator[]( const int _i ) { return p[_i]; }
		const int & operator[]( const int _i ) const { return p[_i]; }
		private: int p[2];
	} ;
	QMap<MidiPort *, Ports> m_portIDs;
#endif

	int m_queueID;
	MidiOutQueue m_outQueue;
	//! Q050-277: what processNonAudioOutEvent() hands the sequencer thread (one producer: MidiClient's off-audio mutex).
	MidiOutQueue m_nonAudioQueue;

	volatile bool m_quit;

	QTimer m_portListUpdateTimer;
	QStringList m_readablePorts;
	QStringList m_writablePorts;

	int m_pipe[2] = {-1, -1};
#ifdef LMMS_TESTING
	std::atomic<bool> m_pollWaitActiveForTesting{false};
#endif


signals:
	void readablePortsChanged();
	void writablePortsChanged();

} ;


} // namespace lmms

#endif // LMMS_HAVE_ALSA

#endif // LMMS_MIDI_ALSA_SEQ_H
