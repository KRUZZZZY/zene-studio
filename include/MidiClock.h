/*
 * MidiClock.h - the DAW as a MIDI clock MASTER and as a MIDI clock SLAVE
 *               (0.3.0, the `clock.*` command group's engine half).
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

#ifndef LMMS_MIDI_CLOCK_H
#define LMMS_MIDI_CLOCK_H

#include <array>
#include <atomic>
#include <cstdint>

#include <QJsonObject>
#include <QObject>
#include <QString>

#include "Midi.h"
#include "TimePos.h" // DefaultTicksPerBar / DefaultStepsPerBar: the tick grid
#include "lmms_export.h"

class QTimer;

namespace lmms
{

class MidiClient;
class MidiEventProcessor;
class MidiPort;

// ---------------------------------------------------------------------------
// The MIDI clock grid, derived from the MIDI 1.0 specification and this
// engine's own tick resolution. These are DEFINITIONS, not tunables:
//
//   * a MIDI clock pulse is 1/24 of a quarter note (24 F8 bytes per quarter);
//   * a MIDI BEAT - the unit of a Song Position Pointer - is six clock pulses,
//     i.e. a sixteenth note;
//   * LMMS resolves a sixteenth as a "step", and calls a step a *beat* in the
//     TimePos vocabulary (DefaultStepsPerBar of them to a bar).
//
// At this tree's 192 ticks/bar and 16 steps/bar a step is 12 ticks, so one
// clock pulse is 2 ticks and one SPP unit is 12 ticks. Every conversion in
// MidiClock.cpp is one of these two constants, so a reader can check the grid
// in one place instead of trusting a magic 24 somewhere on a timing path.
// ---------------------------------------------------------------------------
constexpr int MidiClockPulsesPerQuarter = 24;
constexpr int MidiClockPulsesPerBeat = 6;
constexpr int TicksPerMidiBeat = DefaultTicksPerBar / DefaultStepsPerBar;
constexpr int TicksPerMidiClockPulse = TicksPerMidiBeat / MidiClockPulsesPerBeat;

//! The clock-family messages this engine sends and understands, named once so
//! the command surface, the monitor and the tests agree on the vocabulary.
//! `None` is the empty monitor slot, not a MIDI message.
enum class MidiClockMessage
{
	None,
	Clock,        //!< 0xF8 - 24 per quarter note
	Start,        //!< 0xFA
	Continue,     //!< 0xFB
	Stop,         //!< 0xFC
	SongPosition, //!< 0xF2 - a position in MIDI beats, 14-bit
	TimeCode,     //!< 0xF1 - an MTC quarter-frame nibble
};

//! Wire name of \a message ("clock", "start", ...); empty for None.
LMMS_EXPORT QString midiClockMessageName(MidiClockMessage message);
//! The inverse of midiClockMessageName; None for anything unknown.
LMMS_EXPORT MidiClockMessage midiClockMessageFromName(const QString& name);
//! The MidiEventTypes byte of \a message (MidiSync for Clock, ...).
LMMS_EXPORT MidiEventTypes midiClockEventType(MidiClockMessage message);
/*! The clock-family message a raw MIDI byte names, or None for a system byte
 *  outside the family (0xF9 tick, 0xFE active sensing, 0xFF reset). The parser
 *  reads bytes, the rest of the engine reads messages, and this is the one
 *  conversion between them - so a byte no one consumes cannot be mistaken for
 *  one that is. */
LMMS_EXPORT MidiClockMessage midiClockMessageOfByte(quint8 status);

/*! The timing core of a MIDI clock SLAVE: it turns a stream of timestamped
 *  clock pulses into a measured tempo, a lock state and a drift measurement.
 *
 * Deliberately a plain value type with no Qt objects, no I/O and no allocation,
 * so it is driven deterministically by a test with synthetic timestamps and by
 * the MIDI input thread with real ones (`MidiClock::nowNs()`).
 *
 * The measurement, stated rather than implied: the tempo is measured over a
 * WINDOW of one quarter note (24 pulses) whose ends are two pulse timestamps,
 * so it is an average rate, not an instantaneous one - a slave that reported
 * the last interval would report the jitter of the MIDI client's reader thread
 * as a tempo. The window slides by a whole window, and `driftMs()` reports how
 * far the LAST interval strayed from the window's mean; the lock drops when
 * that stray leaves the drift bound, or when a pulse stops arriving.
 *
 * The BOUND this implies (and that docs/KNOWN-LIMITATIONS.md states): pulse
 * timestamps are taken when the MIDI client's reader thread observes the byte,
 * not when the byte arrived, so a jittering timestamp costs a tempo error of at
 * most tempo * jitter / window. With the tree's ALSA Raw reader - which polls
 * after a 5 ms sleep (src/core/midi/MidiAlsaRaw.cpp) - that is <= ~1.2 BPM at
 * 120 BPM over one quarter-note window. pulseJitterBoundMs() is that 5 ms, and
 * tempoErrorBoundBpm() computes the number instead of leaving it implied.
 */
class LMMS_EXPORT MidiClockTracker
{
public:
	//! Pulses averaged before a tempo is reported: one quarter note.
	static constexpr int WindowPulses = MidiClockPulsesPerQuarter;
	//! Pulse-timestamp jitter of the tree's MIDI input readers, in ms.
	static constexpr double PulseJitterBoundMs = 5.0;
	//! Default drift bound, in ms: how far one interval may stray from the
	//! window's mean and still count as locked.
	static constexpr double DefaultDriftBoundMs = 10.0;
	//! The largest drift bound the surface accepts. Beyond it the bound stops
	//! describing a clock.
	static constexpr double MaxDriftBoundMs = 100.0;
	//! A lock is dropped when no pulse has arrived for this long, in ms.
	static constexpr quint64 LockTimeoutMs = 500;

	void reset() noexcept;

	//! One clock pulse (0xF8) observed at \a timestampNs on a monotonic clock.
	void pulse(quint64 timestampNs) noexcept;
	//! Any other clock-family message, with its payload (SPP beats / an MTC
	//! quarter-frame nibble; ignored for the one-byte messages).
	void received(MidiClockMessage message, quint32 value, quint64 timestampNs) noexcept;
	//! Age the tracker against \a timestampNs; the audio period calls this so a
	//! clock that simply STOPS arriving loses its lock.
	void update(quint64 timestampNs) noexcept;

	bool locked() const noexcept { return m_locked.load(); }
	//! The measured tempo in BPM; 0.0 when not locked.
	double tempoBpm() const noexcept;
	//! Signed deviation of the last interval from the window's mean, in ms.
	double driftMs() const noexcept;
	//! The tempo error the pulse jitter can produce over the live window, in
	//! BPM. 0.0 when fewer than two pulses have been seen.
	double tempoErrorBoundBpm() const noexcept;
	//! The measurement window in ms (the span between the window's end pulses).
	double windowMs() const noexcept;

	double driftBoundMs() const noexcept { return m_driftBoundMs.load(); }
	void setDriftBoundMs(double ms) noexcept;

	int pulseCount() const noexcept { return m_pulseCount.load(); }
	quint64 lastPulseNs() const noexcept { return m_lastPulseNs.load(); }
	quint64 pulseIntervalNs() const noexcept { return m_intervalNs.load(); }
	quint32 messageCount(MidiClockMessage message) const noexcept;
	//! The last Song Position Pointer received, in MIDI beats; -1 when none.
	int lastSongPosition() const noexcept { return m_songPosition.load(); }
	//! The last MTC quarter-frame nibble received; -1 when none.
	int lastTimeCode() const noexcept { return m_timeCode.load(); }

private:
	void unlock() noexcept;
	void accumulate(quint64 timestampNs) noexcept;
	void measure(quint64 timestampNs) noexcept;

	/*! Every mutable field is atomic on purpose. The pulses arrive on the MIDI
	 *  client's reader thread while the control thread reads the state and may
	 *  reset it, and this project has already paid once for a MIDI-path race
	 *  (post-alpha/midi-race). The compound update is still a SEQUENCE of
	 *  atomic stores, so a reader can see the window's end pulse before its
	 *  mean: the reported numbers are each self-consistent, the set is
	 *  eventually consistent, and nothing here is used to make a timing
	 *  decision (the tempo the follower writes comes from the same sequence). */
	std::atomic<quint64> m_windowStartNs{0};
	std::atomic<quint64> m_lastPulseNs{0};
	std::atomic<quint64> m_intervalNs{0};
	std::atomic<double> m_meanIntervalNs{0.0};
	std::atomic<double> m_windowSpanMs{0.0};
	std::atomic<int> m_pulseCount{0};
	std::atomic<int> m_windowPulses{0};
	std::atomic<bool> m_locked{false};
	std::atomic<int> m_songPosition{-1};
	std::atomic<int> m_timeCode{-1};
	std::atomic<double> m_driftBoundMs{DefaultDriftBoundMs};
	std::array<std::atomic<quint32>, 7> m_messageCounts{};
};

/*! The DAW's MIDI clock: ONE object that is both the master generator and the
 *  slave follower, because both answer the same question ("what is this
 *  engine's clock doing") and a caller must be able to read it without asking
 *  two objects that can disagree.
 *
 * Deliberately NOT a QObject: it is reached from three threads (the audio
 * thread through Song::processNextBuffer, the MIDI client's reader thread
 * through the raw parser, and the control thread through `clock.*`), and the
 * first of those can be the first to touch it. A plain object with atomic state
 * has no thread affinity to get wrong; the one piece that DOES need an event
 * loop - the follower's tempo poll - is a QTimer this object creates on the
 * control thread, in startFollowTimer().
 *
 * MASTER (sending). `processAudioPeriod()` is called once per audio period from
 * Song::processNextBuffer() - BEFORE that function's transport gate, because
 * STOP is an edge and a stopped transport is exactly when it has to be sent.
 * It accumulates the ticks the period advanced and emits one 0xF8 per
 * TicksPerMidiClockPulse crossed, and it detects the transport's own edges:
 *   rising edge   -> a Song Position Pointer for the current position (when it
 *                    is not zero) then CONTINUE (or START from tick 0)
 *   a position jump while running -> a Song Position Pointer
 *   falling edge  -> STOP
 * Every message is handed to the engine's own MIDI output path - the client at
 * Engine::audioEngine()->midiClient(), through a MidiPort this object owns and
 * subscribes exactly like an instrument track's - and is recorded in a bounded,
 * lock-free monitor so the emission is observable. There is no second output
 * path in this tree and none is invented here; where the client is the dummy
 * one (a headless build with no MIDI backend) the messages are produced, handed
 * over and recorded, and the dummy client discards them. That is the bound.
 *
 * SLAVE (following). `handleInputMessage()` is called from the MIDI input
 * thread by the raw parser's system-realtime and system-common branches, which
 * until now discarded every one of these bytes. The tracker measures the tempo;
 * `applyMeasuredTempo()` is the single place the measured tempo reaches the
 * SONG, and it runs on the control thread (and, in the test, directly) because
 * a tempo write is project state, not a per-block timing scalar. With no clock
 * arriving the tracker never locks, nothing is written and the transport does
 * not move - the engine half of the `clock.slave_set` contract.
 */
class LMMS_EXPORT MidiClock
{
public:
	//! How many emitted messages the monitor keeps.
	static constexpr int MonitorCapacity = 32;
	//! The tempo dead band of the follower, in BPM: a measured tempo within this
	//! of the song's own is not written. A follower that chased every last digit
	//! would rewrite the project's tempo on every window.
	static constexpr double FollowDeadbandBpm = 0.5;

	static MidiClock* instance();

	//! The monotonic nanosecond clock EVERY timestamp in this class comes from.
	static quint64 nowNs() noexcept;

	/*! The MIDI-beat position (the payload of a Song Position Pointer) for a tick
	 *  position: a MIDI beat is six clock pulses, i.e. a sixteenth note, which is
	 *  TicksPerMidiBeat of this engine's ticks, and the wire form is 14-bit. It is
	 *  a public conversion because it is the one place the grid can be checked
	 *  without a MIDI device on the other end. */
	static quint32 songPositionOf(qint64 ticks) noexcept;
	/*! ONE PERIOD OF THE GENERATOR, as a pure function: given the tick carry the
	 *  previous period left and the ticks this period advances, how many clock
	 *  pulses it emits and what carry it leaves. The audio thread's emitPulses()
	 *  is this function plus the send, so the arithmetic - the one part of the
	 *  emission a test can hold to account without racing the audio device's own
	 *  period - has exactly one implementation. */
	static int pulsesForPeriod(double carryTicks, double advancedTicks,
		double* carryAfter) noexcept;

	// ---------------------------------------------------------------- master
	bool masterEnabled() const noexcept { return m_masterEnabled.load(); }
	/*! Enable or disable the master and name the writable client port it
	 *  subscribes to (empty keeps the current subscription). Answers false and
	 *  fills \a error when the engine has no MIDI client to send through, which
	 *  is a real refusal an agent must see rather than a silent no-op. */
	bool setMasterEnabled(bool enabled, const QString& port, QString* error);
	QString masterPort() const;
	/*! The exact inverse of setMasterEnabled for a recorded undo step: when the
	 *  before-state named no port, unsubscribe the one the call subscribed, so
	 *  control.undo restores the SUBSCRIPTION and not only the flag. */
	void restoreMaster(bool enabled, const QString& port);
	//! True when an output port exists AND it is subscribed to a client port.
	bool masterPortReady() const;
	//! 1000 * framesPerPeriod / sampleRate: the largest delay between the tick
	//! a pulse belongs to and the pulse. The master's ONLY timing granularity,
	//! and the number the bound in KNOWN-LIMITATIONS quotes.
	double periodMs() const;

	// ----------------------------------------------------------------- slave
	bool slaveEnabled() const noexcept { return m_slaveEnabled.load(); }
	void setSlaveEnabled(bool enabled) noexcept;
	bool slaveFollowTempo() const noexcept { return m_slaveFollowTempo.load(); }
	void setSlaveFollowTempo(bool follow) noexcept;
	QString slaveSourcePort() const;
	void setSlaveSourcePort(const QString& port);
	double slaveDriftBoundMs() const noexcept { return m_tracker.driftBoundMs(); }
	void setSlaveDriftBoundMs(double ms) noexcept;

	const MidiClockTracker& tracker() const noexcept { return m_tracker; }

	// ------------------------------------------------------------ the engine
	//! The audio-thread entry point (Song::processNextBuffer). Allocation- and
	//! lock-free in this object; the emission it triggers goes through the MIDI
	//! client's own output path, which the render path already uses for a
	//! track's MIDI output (NotePlayHandle -> MidiPort -> MidiClient) and which
	//! the ALSA-sequencer client serves under its own mutex.
	void processAudioPeriod(int frames, bool transportRunning, qint64 playPosTicks,
		quint64 timestampNs) noexcept;
	//! The MIDI input entry point (MidiClientRaw::parseData).
	void handleInputMessage(MidiClockMessage message, quint32 value,
		quint64 timestampNs) noexcept;

	//! Write the measured tempo to the Song when the follower is enabled, the
	//! slave is locked and the measurement has left the dead band. Returns the
	//! BPM written, 0 when nothing was written. Control thread only.
	int applyMeasuredTempo();

	// ------------------------------------------------------------- observab.
	//! The whole live state of this object as one result - what `clock.get_state`
	//! answers, and what the unit test reads back.
	QJsonObject stateJson() const;
	//! Milliseconds since the last clock pulse arrived; -1 when none ever has.
	qint64 lastPulseAgeMs() const;

	quint32 emittedCount(MidiClockMessage message) const noexcept;
	quint32 emittedTotal() const noexcept { return m_emittedTotal.load(); }
	//! The \a indexFromLatest-th most recently emitted message (0 = the latest);
	//! None once the request runs past the recorded history.
	MidiClockMessage emittedAt(int indexFromLatest) const noexcept;
	//! The play position, in ticks, the \a indexFromLatest-th emission carried.
	qint64 emittedTickAt(int indexFromLatest) const noexcept;

	//! The poll the follower writes the measured tempo from. Created on the
	//! thread that calls it - the control thread - and created once.
	void startFollowTimer();
	void stopFollowTimer();

private:
	explicit MidiClock();
	~MidiClock();

	//! Hand \a message to the MIDI client and record it. Audio thread. Named
	//! emitMessage and NOT emit, which Qt defines as a macro (grep the build's
	//! qglobal.h: `#define emit`) - calling it emit makes the declaration read
	//! `void (MidiClockMessage message, ...)` and the class body fail to parse.
	void emitMessage(MidiClockMessage message, qint64 positionTicks, quint32 payload = 0) noexcept;
	//! Create this object's own MidiPort, or return the existing one. Control
	//! thread: the constructor registers a port on the client, the same call an
	//! instrument track makes from the GUI thread when a track is added.
	MidiPort* outputPort();
	void record(MidiClockMessage message, qint64 positionTicks) noexcept;
	void trackTransport(bool transportRunning, qint64 playPosTicks, qint64 expectedAdvance) noexcept;
	void emitPulses(int frames) noexcept;

	MidiClient* client() const;

	std::atomic<bool> m_masterEnabled{false};
	std::atomic<bool> m_slaveEnabled{false};
	std::atomic<bool> m_slaveFollowTempo{false};
	mutable std::atomic<MidiClient*> m_client{nullptr};
	std::atomic<MidiPort*> m_outputPort{nullptr};
	std::atomic<MidiEventProcessor*> m_processor{nullptr};
	std::atomic<double> m_tickRemainder{0.0};
	std::atomic<qint64> m_lastPlayPosTicks{0};
	std::atomic<bool> m_transportWasRunning{false};
	std::atomic<bool> m_positionKnown{false};
	std::atomic<quint32> m_emittedTotal{0};
	std::atomic<quint32> m_monitorWrite{0};
	std::array<std::atomic<quint32>, 7> m_emittedCounts{};
	//! The bounded emission monitor: a fixed ring of (message, tick) slots,
	//! written by the audio thread and read as a diagnostic. Best-effort by
	//! construction - a reader that races a writer may see one slot mid-update,
	//! which is why the COUNTERS, not the ring, are what a test asserts when it
	//! wants a number and the ring is what it asserts when it wants an ORDER.
	struct MonitorSlot
	{
		std::atomic<quint32> message{0};
		std::atomic<qint64> ticks{0};
	};
	std::array<MonitorSlot, MonitorCapacity> m_monitor;
	MidiClockTracker m_tracker;
	QTimer* m_followTimer = nullptr;
	QString m_masterPort;
	QString m_slavePort;
};

} // namespace lmms

#endif // LMMS_MIDI_CLOCK_H
