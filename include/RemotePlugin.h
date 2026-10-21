/*
 * RemotePlugin.h - base class providing RPC like mechanisms
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

#ifndef LMMS_REMOTE_PLUGIN_H
#define LMMS_REMOTE_PLUGIN_H

#include <QThread>
#include <QProcess>
#include <QRecursiveMutex>
#include <QTimer>

#include <array>
#include <atomic>
#include <chrono>
#include <vector>
#include <cstdint>
#include <map>
#include <span>

#include "MidiHandoffQueue.h"
#include "RemotePluginBase.h"
#include "SharedMemory.h"
#include "LmmsTypes.h"

#ifdef LMMS_TESTING
class RemotePluginAudioPortsTest;
#endif

namespace lmms
{

class MidiEvent;
class RemotePlugin;
class RemotePluginAudioPortsController;
class SampleFrame;

//! Holds off rendering while a shared or host-private audio block is re-sized or its view pointers change.
/*!
 * The render thread holds the audio engine's change mutex for every period (renderNextPeriod()), so a
 * resize taken under this guard cannot overlap a period that reads the block. Every resize of a remote
 * plugin's blocks goes through this guard: RemotePlugin::updateAudioBuffer() and the ports' updateBuffers().
 * Nests with itself.
 */
class LMMS_EXPORT RemotePluginBufferChangeGuard
{
public:
	RemotePluginBufferChangeGuard();
	~RemotePluginBufferChangeGuard();
	RemotePluginBufferChangeGuard(const RemotePluginBufferChangeGuard&) = delete;
	RemotePluginBufferChangeGuard& operator=(const RemotePluginBufferChangeGuard&) = delete;
};

class ProcessWatcher : public QThread
{
	Q_OBJECT
public:
	ProcessWatcher( RemotePlugin * );
	~ProcessWatcher() override = default;

	void stop()
	{
		m_quit = true;
		quit();
	}

	void reset()
	{
		m_quit = false;
	}

private:
	void run() override;

#ifdef LMMS_TESTING
	friend class ::RemotePluginAudioPortsTest;
#endif

	RemotePlugin * m_plugin;
	std::atomic<bool> m_quit;

} ;


class LMMS_EXPORT RemotePlugin : public QObject, public RemotePluginBase
{
	Q_OBJECT
public:
	//! Ports-infrastructure constructor: connects this remote plugin's shared
	//! audio buffer to the given audio ports controller. The controller owns the
	//! shared buffer's layout (see updateAudioBuffer()) and decides when the
	//! plugin's buffers become active.
	//! See RemotePluginAudioPorts.h.
	explicit RemotePlugin(RemotePluginAudioPortsController& audioPorts);

	~RemotePlugin() override;

	inline bool isRunning()
	{
#ifdef DEBUG_REMOTE_PLUGIN
		return true;
#else
		return m_process.state() != QProcess::NotRunning;
#endif // DEBUG_REMOTE_PLUGIN
	}

	bool init( const QString &pluginExecutable, bool waitForInitDoneMsg, QStringList extraArgs = {} );

	inline void waitForHostInfoGotten()
	{
		m_failed = waitForMessage( IdHostInfoGotten ).id
							!= IdHostInfoGotten;
	}

	//! Waits for the remote client's IdInitDone. On success the audio ports are
	//! activated for `Engine::audioEngine()->framesPerPeriod()` frames, which is
	//! what allocates the shared audio buffer (see updateAudioBuffer()).
	void waitForInitDone(bool busyWaiting = true);

	bool processMessage( const message & _m ) override;

	//! Runs one processing period: tells the remote client to process the shared
	//! audio buffer and waits for it to finish. The plugin's input and output are
	//! the planar port buffers described in updateAudioBuffer(); there is no
	//! buffered copy in or out. When the plugin is failed, not running or has no
	//! shared buffer yet, the output planes are zero-filled and false is returned.
	//!
	//! Realtime contract: process() never waits for the communication lock (it try-locks and skips the
	//! period when the main thread holds it), and never waits for the client without a bound. A client that
	//! does not answer within one period is abandoned for this period: the period is rendered as silence,
	//! processingOverruns() counts it, and the reply still owed is drained before the next request is sent.
	//! Client messages that need main-thread work (a channel-count change, a retired message, debug text) are
	//! recorded here and applied by applyDeferredClientRequests(); while a channel-count change is pending,
	//! periods are rendered as silence.
	//! @return true if the remote client processed this period
	bool process();

	//! Main-thread half of the deferred client requests (see process()): applies a channel-count change the
	//! client asked for during a period and reports retired messages and debug text. Runs from a timer and
	//! may be called directly. Must not be called from the audio thread.
	void applyDeferredClientRequests();

	//! Number of processing periods abandoned because the client did not answer within one period.
	std::uint64_t processingOverruns() const { return m_processingOverruns.load(); }

	//! Writes the warning for a channel-count request that processMessage refused. processMessage also runs on
	//! the audio thread, where a log would allocate and lock, so the refusal is only recorded there and this
	//! main-thread side reports it. Called from updateAudioBuffer(), processFinished() and the destructor.
	//! @return true when a refusal was waiting and has now been reported
	bool reportRefusedChannelCounts();

	//! (Re)allocates the shared audio buffer used by the ports infrastructure
	//! and tells the remote client about the new shared memory key (Part C).
	//! The returned block holds `channelsIn + channelsOut` channel-major planar
	//! buffers of `frames` floats each: first `channelsIn` input planes, then
	//! `channelsOut` output planes. A repeat call with unchanged arguments
	//! returns the existing block without reallocating.
	//! @return pointer to the shared buffer, or nullptr on failure
	auto updateAudioBuffer(ch_cnt_t channelsIn, ch_cnt_t channelsOut, f_cnt_t frames) -> float*;

	//! Queues a MIDI event for the client. It never waits, so any thread may call it, the audio thread included:
	//! the event is sent by the next processing period (see process()). When the queue is full the event is
	//! refused, and its channel is later sent all-notes-off, so no note can be left sounding (see
	//! sendQueuedMidiEvents()). Refusals are counted by midiEventsDropped() and reported on the main thread.
	void processMidiEvent( const MidiEvent&, const f_cnt_t _offset );

	//! MIDI events refused because the hand-off queue was full, since construction.
	std::uint64_t midiEventsDropped() const { return m_midiOverflow.recorded(); }

	void updateSampleRate( sample_rate_t _sr )
	{
		lock();
		sendMessage( message( IdSampleRateInformation ).addInt( _sr ) );
		waitForReplyWithin( message( IdInformationUpdated ) );
		unlock();
	}


	virtual void toggleUI()
	{
		lock();
		sendMessage( IdToggleUI );
		unlock();
	}

	//! How long a main-thread query waits for its reply. A reply that comes later is not waited for: it stays in the
	//! connection, and the next wait that does not ask for it discards it (processMessage() ignores replies it does
	//! not expect).
	static constexpr std::chrono::milliseconds QueryReplyTimeout{100};

	//! How long an abandoned query's reply is still awaited before the count of abandoned queries expires. A reply
	//! later than this is taken as the answer to the next query of its id (a client that hung for that long).
	static constexpr std::chrono::seconds StaleQueryExpiry{2};

	//! Tests only: a processing period waits up to \a budget for its client's reply instead of one audio period;
	//! zero (the default) restores the real budget. For end-to-end tests about what the client says rather than
	//! about the deadline: a client in another process cannot always answer within one period on a loaded machine.
	static void setPeriodBudgetForTesting(std::chrono::microseconds budget);

	//! Tests only: override a main-thread query's bounded wait; zero (the default) keeps QueryReplyTimeout.
	static void setQueryReplyTimeoutForTesting(std::chrono::milliseconds timeout);

	//! Waits up to \a _timeout for the reply to \a _wm, other messages read meanwhile going through processMessage().
	//! The caller holds the communication lock. @return the reply, or an undefined message on timeout or loss.
	message waitForReplyWithin( const message & _wm, std::chrono::milliseconds _timeout = QueryReplyTimeout );

	//! Whether the client's editor is visible, from a query with a bounded wait. -1 when the client did not answer.
	int isUIVisible()
	{
		lock();
		sendMessage( IdIsUIVisible );
		const message m = waitForReplyWithin( message( IdIsUIVisible ) );
		// The awaited reply is read here, not in processMessage(), so it is checked here too. Refused under the
		// lock, as processMessage() refuses its messages.
		const bool incomplete = m.id == IdIsUIVisible
			&& static_cast<int>( m.size() ) < requiredArgumentCount( IdIsUIVisible );
		if( incomplete ) { refuseIncompleteMessage( m ); }
		unlock();
		if( m.id != IdIsUIVisible || incomplete ) { return -1; }
		m_uiVisible = m.getInt() ? 1 : 0;
		return m_uiVisible;
	}

	//! The last known editor visibility, without asking the client: -1 until it has been asked or told. Saving a
	//! project reads this, so a save never waits for the client.
	int cachedUIVisible() const { return m_uiVisible; }

	//! Main-thread wait for the client's reply to \a _wm. The communication lock is held across the whole wait:
	//! the audio thread then never reads a reply that is not its own, and it skips its period rather than wait
	//! for this one. Hides RemotePluginBase::waitForMessage(), which every caller of this class reaches.
	message waitForMessage( const message & _wm, bool _busyWaiting = false )
	{
		lock();
		const message reply = RemotePluginBase::waitForMessage( _wm, _busyWaiting );
		unlock();
		return reply;
	}

	//! Audio ports controller this plugin is connected to. It is never null: the
	//! constructor requires one (Part C - the legacy single/interleaved-buffer
	//! processing path is gone, so every RemotePlugin has ports).
	auto audioPorts() -> RemotePluginAudioPortsController& { return *m_audioPorts; }

	//! pid of the client process, or 0 when none is running. Feature row 80
	//! (out-of-process hosting, board card #670) reports this through `oop.*`;
	//! it is deliberately a query and not a cached copy, because the pid is the
	//! QProcess's own and a cached one would go stale on exactly the event the
	//! surface exists to report (the client going away).
	qint64 hostProcessId() const { return m_process.processId(); }

	//! The client executable this plugin was started from ("" before init()).
	//! This - and not the plugin's own key - is what the crash accounting is
	//! keyed by: see include/OutOfProcessHosting.h.
	QString hostClientExecutable() const { return m_exec; }

	inline bool failed() const
	{
		return m_failed;
	}

	inline void lock()
	{
		m_commMutex.lock();
	}

	inline void unlock()
	{
		m_commMutex.unlock();
	}

public slots:
	virtual void showUI();
	virtual void hideUI();

protected:
	//! Set by the main thread (applyDeferredClientRequests, init) and read on the audio thread (process), so atomic.
	std::atomic<bool> m_failed;

private:
	QProcess m_process;
	ProcessWatcher m_watcher;

	//! The last channel-count request processMessage refused, and whether one is waiting to be reported.
	//! Atomics, so the audio thread records them without allocating or locking.
	std::atomic<bool> m_refusalPending{false};
	std::atomic<int> m_refusedInCount{0};
	std::atomic<int> m_refusedOutCount{0};

	//! Set by the audio thread when a client message needs main-thread work; read by the main thread, which
	//! clears it under m_commMutex. The fields below are written only under m_commMutex (audio thread inside
	//! process(), main thread in applyDeferredClientRequests()), so they need no atomics of their own.
	std::atomic<bool> m_deferredWork{false};
	//! True while a client-requested channel-count change is waiting for the main thread; process() renders
	//! silence while it is, because the client already lays its planes out with the new counts.
	bool m_countChangePending = false;
	int m_pendingInCount = 0;
	int m_pendingOutCount = 0;
	//! Retired message id a client sent during a period (0 = none); reported and failed on the main thread.
	int m_pendingRetiredId = 0;
	//! Id and argument count of an incomplete message a client sent during a period (id 0 = none); the arity
	//! refusal is reported and failed on the main thread.
	int m_pendingArityId = 0;
	int m_pendingArityCount = 0;
	//! Debug text a client sent during a period, copied at its first 255 bytes; empty when none is pending.
	bool m_pendingDebug = false;
	std::array<char, 256> m_pendingDebugText{};
	std::size_t m_pendingDebugLength = 0;

	//! True while process() is running on the audio thread: processMessage() defers main-thread work then.
	bool m_inProcessPeriod = false;
	//! IdProcessingDone replies owed by the client: sent by process() but not yet received. Non-zero only
	//! after a period timed out; collectProcessingReplies() drains them before the next request.
	int m_owedProcessingReplies = 0;
	std::atomic<std::uint64_t> m_processingOverruns{0};
	QTimer m_deferredTimer;

	//! How a wait for the client's IdProcessingDone replies ended.
	enum class ReplyWait
	{
		Done, //!< every reply owed was received
		Late, //!< the deadline passed first; the reply stays owed
		Lost  //!< the client went away
	};

	//! Body of process(), called with m_commMutex held and m_inProcessPeriod set.
	bool processPeriod();

	//! Sends the MIDI events queued by processMidiEvent(), in order, on the audio thread without blocking, at most
	//! MidiDrainPerPeriod of them. A frame that finds no room is kept in m_pendingMidi and sent first next period;
	//! no event is taken from the queue without a place to go. Called with m_commMutex held.
	SendResult sendQueuedMidiEvents();

	//! Frame of a MIDI event that could not be sent yet (see sendQueuedMidiEvents()).
	std::array<char, 128> m_pendingMidi{};
	std::size_t m_pendingMidiSize = 0;
	//! MIDI frames (events and all-notes-off cuts) sent since the last IdStartProcessing; audio thread only. The cap
	//! is per client period: a period that sent its MIDI but found no room for its request is skipped, and the next
	//! one must not send another MidiDrainPerPeriod before that same request.
	std::size_t m_midiSentSinceRequest = 0;
	//! Channels whose events the full queue refused; each is sent all-notes-off once the queue has drained.
	MidiChannelOverflow m_midiOverflow;
	//! Refusals already reported on the main thread (main thread only).
	std::uint64_t m_reportedDrops = 0;
	//! Editor visibility as last known: from a query, a show or a hide, or a late reply read by any wait. -1: unknown.
	//! Written by the main thread and by processMessage() (which the audio thread can run), so atomic.
	std::atomic<int> m_uiVisible{-1};
	//! Queries abandoned at their timeout, per reply id: their replies still arrive, and are not the answers to later
	//! queries. Main thread only (under m_commMutex), except that processMessage() consumes them. A count expires
	//! StaleQueryExpiry after its first abandonment: a reply that never comes must not poison the id for good.
	struct StaleQueries
	{
		int count = 0;
		std::chrono::steady_clock::time_point since{};
	};
	std::map<int, StaleQueries> m_staleQueries;
	//! Set when the connection was lost on the audio thread; reported and failed on the main thread.
	std::atomic<bool> m_connectionLost{false};
	//! IdInitDone has been received (by any wait), so a later waitForInitDone() must not wait for it again.
	bool m_initDoneSeen = false;

	//! Consumes one abandoned query's reply for \a _id. @return true if the reply was an abandoned query's
	bool consumeStaleQuery( int _id );

	//! Records one abandoned query for \a _id.
	void abandonQuery( int _id );

	//! Main-thread drain while there is no shared block: completes the handshake without a main-thread wait.
	void pumpBeforeBuffer();
	//! A reply the client asked for during a period could not be sent without blocking; reported on the main thread.
	bool m_pendingReplyLost = false;

	//! MIDI events waiting for the next processing period; see processMidiEvent().
	MidiHandoffQueue<256> m_midiHandoff;

	//! Consumes client messages until every owed IdProcessingDone has arrived, or \a deadline passes.
	//! Non-reply messages go through processMessage(), which defers their main-thread work.
	ReplyWait collectProcessingReplies(std::chrono::steady_clock::time_point deadline);

	//! The number of arguments a message with \a _id must carry before its handler may read them. The handlers
	//! index the message without a bounds check, and the wire format carries the count with the message, so a
	//! complete message can still be short. An id that reads no argument needs none.
	static constexpr int requiredArgumentCount( int _id )
	{
		switch( _id )
		{
			case IdChangeInputOutputCount: return 2;
			case IdDebugMessage:
			case IdIsUIVisible: return 1;
			default: return 0;
		}
	}

	//! Refuses a message that arrived with fewer arguments than its id requires: nothing is applied (reading them
	//! would index past the end of the message - undefined behaviour), the plugin is failed like any other protocol
	//! mismatch, and the diagnostic is logged on the main thread or recorded for reportDeferredClientRequests() when
	//! this runs on the audio thread (a log allocates and locks).
	void refuseIncompleteMessage( const message & _m );

	//! Logs the deferred retired-message and debug-text requests. Main thread only, under m_commMutex.
	void reportDeferredClientRequests();

	QString m_exec;
	QStringList m_args;

	//! Set before this plugin deliberately takes its client down (its own
	//! destructor's IdQuit/terminate/kill path). QProcess::finished is
	//! connected DIRECTLY, so that exit arrives in processFinished() while the
	//! object is being destroyed - and it must be reported as a shutdown, not
	//! as a crash: the crash accounting in include/OutOfProcessHosting.h keys
	//! its crash-loop REFUSAL on crashes, and a mode switch that stops a
	//! healthy client is the normal way a client goes away (feature row 80,
	//! board card #670).
	bool m_hostShutdownRequested = false;

	QRecursiveMutex m_commMutex;

	RemotePluginAudioPortsController* const m_audioPorts = nullptr;

	//! The block the client reads and writes. Only process() touches it, and only while a request is
	//! outstanding or after its reply.
	SharedMemory<float[]> m_audioBuffer;

	//! Host-private copy of the planar block the instrument reads and writes (what updateAudioBuffer()
	//! returns). Inputs are copied to the shared block before a request, outputs copied back after a
	//! completed reply. A period without a completed reply leaves the outputs silent, and a late client can
	//! never write into them.
	std::vector<float> m_hostBlock;
	std::span<float> m_hostInputs;
	std::span<float> m_hostOutputs;
	std::span<float> m_sharedInputs;

	//! Channel counts and frame count `m_audioBuffer` was allocated for; a
	//! repeat call of updateAudioBuffer() with these values is a no-op.
	ch_cnt_t m_channelsIn = 0;
	ch_cnt_t m_channelsOut = 0;
	f_cnt_t m_frames = 0;

	//! View into the output planes of `m_audioBuffer`: the client's side, read only after a completed reply.
	std::span<float> m_audioOutputs;

#ifndef SYNC_WITH_SHM_FIFO
	int m_server;
	QString m_socketFile;
#endif // not SYNC_WITH_SHM_FIFO

	friend class ProcessWatcher;


private slots:
	void processFinished( int exitCode, QProcess::ExitStatus exitStatus );
	void processErrored(QProcess::ProcessError err );
} ;

inline std::string QSTR_TO_STDSTR(QString const& qstr)
{
	return qstr.toStdString();
}

} // namespace lmms

#endif // LMMS_REMOTE_PLUGIN_H
