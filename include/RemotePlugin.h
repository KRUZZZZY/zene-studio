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
#include <cstdint>
#include <span>

#include "MidiHandoffQueue.h"
#include "RemotePluginBase.h"
#include "SharedMemory.h"
#include "LmmsTypes.h"

namespace lmms
{

class MidiEvent;
class RemotePlugin;
class RemotePluginAudioPortsController;
class SampleFrame;

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

	RemotePlugin * m_plugin;
	volatile bool m_quit;

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
	//! the event is sent by the next processing period (see process()). A full queue drops the event, counted
	//! by midiEventsDropped().
	void processMidiEvent( const MidiEvent&, const f_cnt_t _offset );

	//! MIDI events refused because the hand-off queue was full, since construction.
	std::uint64_t midiEventsDropped() const { return m_midiHandoff.dropped(); }

	void updateSampleRate( sample_rate_t _sr )
	{
		lock();
		sendMessage( message( IdSampleRateInformation ).addInt( _sr ) );
		waitForMessage( IdInformationUpdated, true );
		unlock();
	}


	virtual void toggleUI()
	{
		lock();
		sendMessage( IdToggleUI );
		unlock();
	}

	int isUIVisible()
	{
		lock();
		sendMessage( IdIsUIVisible );
		unlock();
		message m = waitForMessage( IdIsUIVisible );
		return m.id != IdIsUIVisible ? -1 : m.getInt() ? 1 : 0;
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
	bool m_failed;

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

	//! Sends the MIDI events queued by processMidiEvent(), in order. Called with m_commMutex held.
	void sendQueuedMidiEvents();

	//! MIDI events waiting for the next processing period; see processMidiEvent().
	MidiHandoffQueue<256> m_midiHandoff;

	//! Consumes client messages until every owed IdProcessingDone has arrived, or \a deadline passes.
	//! Non-reply messages go through processMessage(), which defers their main-thread work.
	ReplyWait collectProcessingReplies(std::chrono::steady_clock::time_point deadline);

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

	SharedMemory<float[]> m_audioBuffer;

	//! Channel counts and frame count `m_audioBuffer` was allocated for; a
	//! repeat call of updateAudioBuffer() with these values is a no-op.
	ch_cnt_t m_channelsIn = 0;
	ch_cnt_t m_channelsOut = 0;
	f_cnt_t m_frames = 0;

	//! View into the output planes of `m_audioBuffer` (see updateAudioBuffer())
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
