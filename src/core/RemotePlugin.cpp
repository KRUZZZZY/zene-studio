/*
 * RemotePlugin.cpp - base class providing RPC like mechanisms
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

#include "RemotePlugin.h"

//#define DEBUG_REMOTE_PLUGIN
#ifdef DEBUG_REMOTE_PLUGIN
#include <QDebug>
#endif

#include <algorithm>
#include <iterator>

#ifdef LMMS_BUILD_WIN32
#include <windows.h>
#endif

#include "AudioEngine.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "OutOfProcessHosting.h"
#include "RemotePluginAudioPorts.h"
#include "RenderPeriodClock.h"
#include "Song.h"

#include <iostream>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QUuid>

#ifndef SYNC_WITH_SHM_FIFO
#include <sys/socket.h>
#include <sys/un.h>
#endif

#ifdef LMMS_BUILD_WIN32

namespace {

HANDLE getRemotePluginJob()
{
	static const auto job = []
	{
		const auto job = CreateJobObject(nullptr, nullptr);

		auto limitInfo = JOBOBJECT_EXTENDED_LIMIT_INFORMATION{};
		limitInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limitInfo, sizeof(limitInfo));

		return job;
	}();

	return job;
}

} // namespace

#endif // LMMS_BUILD_WIN32

namespace lmms
{

// simple helper thread monitoring our RemotePlugin - if process terminates
// unexpectedly invalidate plugin so LMMS doesn't lock up
ProcessWatcher::ProcessWatcher( RemotePlugin * _p ) :
	QThread(),
	m_plugin( _p ),
	m_quit( false )
{
}


void ProcessWatcher::run()
{
	auto& process = m_plugin->m_process;
	process.start(m_plugin->m_exec, m_plugin->m_args);

#ifdef LMMS_BUILD_WIN32
	// Add the process to our job so it is killed if we crash
	if (process.waitForStarted(-1))
	{
		if (const auto processHandle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, false, process.processId()))
		{
			// Ensure the process is still running, otherwise the handle we
			// obtained may be for a different process that happened to reuse
			// the same process id.
			// QProcess::state() alone is insufficient as it only returns a
			// cached state variable that is updated asynchronously. To query
			// the process itself, we can use QProcess::waitForFinished() with a
			// zero timeout, but that too is insufficient as it fails if the
			// process has already finished. Therefore, we check both.
			if (!process.waitForFinished(0) && process.state() == QProcess::Running)
			{
				AssignProcessToJobObject(getRemotePluginJob(), processHandle);
			}
			CloseHandle(processHandle);
		}
	}
#endif // LMMS_BUILD_WIN32

	exec();
	process.moveToThread(m_plugin->thread());
	while (!m_quit && m_plugin->messagesLeft())
	{
		msleep(200);
	}
	if (!m_quit)
	{
		fprintf(stderr, "remote plugin died! invalidating now.\n");
		m_plugin->invalidate();
	}
}





RemotePlugin::RemotePlugin(RemotePluginAudioPortsController& audioPorts) :
	QObject(),
#ifdef SYNC_WITH_SHM_FIFO
	RemotePluginBase( new shmFifo(), new shmFifo() ),
#else
	RemotePluginBase(),
#endif
	m_failed( true ),
	m_watcher( this ),
	m_audioPorts( &audioPorts )
{
#ifndef SYNC_WITH_SHM_FIFO
	struct sockaddr_un sa;
	sa.sun_family = AF_LOCAL;

	m_socketFile = QDir::tempPath() + QDir::separator() +
						QUuid::createUuid().toString();
	auto path = m_socketFile.toUtf8();
	size_t length = path.length();
	if ( length >= sizeof sa.sun_path )
	{
		length = sizeof sa.sun_path - 1;
		qWarning( "Socket path too long." );
	}
	memcpy(sa.sun_path, path.constData(), length );
	sa.sun_path[length] = '\0';

	m_server = socket( PF_LOCAL, SOCK_STREAM, 0 );
	if ( m_server == -1 )
	{
		qWarning( "Unable to start the server." );
	}
	remove(path.constData());
	int ret = bind( m_server, (struct sockaddr *) &sa, sizeof sa );
	if ( ret == -1 || listen( m_server, 1 ) == -1 )
	{
		qWarning( "Unable to start the server." );
	}
#endif

	m_audioPorts->connectBuffers(this);

	connect( &m_process, SIGNAL(finished(int,QProcess::ExitStatus)),
		this, SLOT(processFinished(int,QProcess::ExitStatus)),
		Qt::DirectConnection );
	connect( &m_process, SIGNAL(errorOccurred(QProcess::ProcessError)),
			 this, SLOT(processErrored(QProcess::ProcessError)),
		Qt::DirectConnection );
	connect( &m_process, SIGNAL(finished(int,QProcess::ExitStatus)),
		&m_watcher, SLOT(quit()), Qt::DirectConnection );

	// Main-thread pump for the requests process() had to defer. The timer only takes m_commMutex when
	// m_deferredWork is set, so an idle plugin never contends with the audio thread.
	m_deferredTimer.setInterval(std::chrono::milliseconds{10});
	connect(&m_deferredTimer, &QTimer::timeout, this, &RemotePlugin::applyDeferredClientRequests);
	m_deferredTimer.start();
}




RemotePlugin::~RemotePlugin()
{
	m_deferredTimer.stop();
	reportRefusedChannelCounts();
	reportDeferredClientRequests();

	// Detach from the audio ports controller before the shared buffer is
	// destroyed, so the controller never holds a freed buffer view.
	m_audioPorts->disconnectBuffers();

	m_watcher.stop();
	m_watcher.wait();

	// Everything below is a DELIBERATE client shutdown: this flag is what makes
	// processFinished() report it as one (feature row 80, board card #670).
	m_hostShutdownRequested = true;

	if( m_failed == false )
	{
		if( isRunning() )
		{
			lock();
			sendMessage( IdQuit );

			m_process.waitForFinished( 1000 );
			if( m_process.state() != QProcess::NotRunning )
			{
				m_process.terminate();
				m_process.kill();
			}
			unlock();
		}
	}

#ifndef SYNC_WITH_SHM_FIFO
	if ( close( m_server ) == -1)
	{
		qWarning( "Error freeing resources." );
	}
	remove( m_socketFile.toUtf8().constData() );
#endif
}




bool RemotePlugin::init(const QString &pluginExecutable,
							bool waitForInitDoneMsg , QStringList extraArgs)
{
	lock();
	if( m_failed )
	{
#ifdef SYNC_WITH_SHM_FIFO
		reset( new shmFifo(), new shmFifo() );
#endif
		m_failed = false;
		m_initDoneSeen = false;
		// A reused object starts a new connection: the state of the old one (abandoned queries, owed replies, the
		// cached visibility, MIDI waiting for the old client) must not carry over.
		m_staleQueries.clear();
		m_owedProcessingReplies = 0;
		m_uiVisible = -1;
		m_pendingMidiSize = 0;
		m_connectionLost = false;
	}
	QString exec = QFileInfo(QDir("plugins:"), pluginExecutable).absoluteFilePath();

	// We may have received a directory via a environment variable
	if (const char* env_path = std::getenv("LMMS_PLUGIN_DIR"))
			exec = QFileInfo(QDir(env_path), pluginExecutable).absoluteFilePath();

#ifdef LMMS_BUILD_APPLE
	// search current directory first
	QString curDir = QCoreApplication::applicationDirPath() + "/" + pluginExecutable;
	if( QFile( curDir ).exists() )
	{
		exec = curDir;
	}
#endif
#ifdef LMMS_BUILD_WIN32
	if( ! exec.endsWith( ".exe", Qt::CaseInsensitive ) )
	{
		exec += ".exe";
	}
#endif

	if( ! QFile( exec ).exists() )
	{
		qWarning( "Remote plugin '%s' not found",
						exec.toUtf8().constData() );
		m_failed = true;
		invalidate();
		unlock();
		return failed();
	}

	// ensure the watcher is ready in case we're running again
	// (e.g. 32-bit VST plugins on Windows)
	m_watcher.wait();
	m_watcher.reset();

	QStringList args;
#ifdef SYNC_WITH_SHM_FIFO
	// swap in and out for bidirectional communication
	args << QString::fromStdString(out()->shmKey());
	args << QString::fromStdString(in()->shmKey());
#else
	args << m_socketFile;
#endif
	args << extraArgs;
#ifndef DEBUG_REMOTE_PLUGIN
	m_process.setProcessChannelMode( QProcess::ForwardedChannels );
	m_process.setWorkingDirectory( QCoreApplication::applicationDirPath() );
	m_exec = exec;
	m_args = args;
	// we start the process on the watcher thread to work around QTBUG-8819
	m_process.moveToThread( &m_watcher );
	m_watcher.start( QThread::LowestPriority );
#else
	qDebug() << exec << args;
#endif

	// With the shm-fifo transport there is no socket to accept, so the client is
	// as connected as it will ever be; the socket transport sets this from the
	// accept() below.
	bool connected = true;

#ifndef SYNC_WITH_SHM_FIFO
	connected = false;
	struct pollfd pollin;
	pollin.fd = m_server;
	pollin.events = POLLIN;

	switch ( poll( &pollin, 1, 30000 ) )
	{
		case -1:
			qWarning( "Unexpected poll error." );
			break;

		case 0:
			qWarning( "Remote plugin did not connect." );
			break;

		default:
			m_socket = accept( m_server, nullptr, nullptr );
			if ( m_socket == -1 )
			{
				qWarning( "Unexpected socket error." );
			}
			else
			{
				connected = true;
			}
	}
#endif

	// Feature row 80 (out-of-process hosting, board card #670): the client is
		// connected, so its pid is the QProcess's own from here on - record the
		// start under the client executable's name. The record is what `oop.*`
		// reports and what the crash-loop REFUSAL is measured against; a start that
	// never got this far leaves no record, which is correct (there is nothing to
	// say about a client that never ran - a client that launched and never
	// connected is exactly that, and its exit may even have been recorded
	// before this start).
	if (connected)
	{
		oop::HostTracker& tracker = oop::HostTracker::instance();
		tracker.noteStarted(QFileInfo{m_exec}.fileName(), m_process.processId(),
			QString::fromLatin1(metaObject()->className()));
	}

	sendMessage(message(IdSyncKey).addString(Engine::getSong()->syncKey()));

	// NOTE: the shared audio buffer is allocated by the audio ports, which are
	// activated once the remote side reported IdInitDone (see waitForInitDone()).
	// There is no buffer to size before the client exists (#589).

	if( waitForInitDoneMsg )
	{
		waitForInitDone();
	}
	unlock();

	return failed();
}




void RemotePlugin::waitForInitDone(bool busyWaiting)
{
	// waitForMessage() processes every pending message before it returns, and one of
	// those messages may already have failed this plugin: a client built against the
	// pre-#589 interleaved protocol announces its channel counts with the retired
	// IdChangeInputCount id, which the handler refuses and marks failed. That verdict
	// has to be sticky - assigning the wait's own result directly erases it, and the
	// failure path then degrades into the silent wrong audio it exists to prevent
	// (measured with a pre-#589 client: the refusal was logged, the plugin stayed
	// unfailed, and the client's interleaved writes were consumed as planar planes).
	const bool initDone = m_initDoneSeen || waitForMessage(IdInitDone, busyWaiting).id == IdInitDone;
	m_failed = m_failed || !initDone;

	if (!m_failed)
	{
		// The remote side exists now, so its buffers do too: activate the audio
		// ports, which allocates the shared audio block for this plugin's
		// channel counts (RemotePluginAudioPorts::activate() -> updateBuffers()
		// -> updateAudioBuffer()).
		m_audioPorts->activate(Engine::audioEngine()->framesPerPeriod());
	}
}




RemotePluginBufferChangeGuard::RemotePluginBufferChangeGuard()
{
	if (Engine::audioEngine()) { Engine::audioEngine()->requestChangeInModel(); }
}

RemotePluginBufferChangeGuard::~RemotePluginBufferChangeGuard()
{
	if (Engine::audioEngine()) { Engine::audioEngine()->doneChangeInModel(); }
}

namespace
{

//! MIDI events sent per period at most; the rest wait in the queue (see sendQueuedMidiEvents()).
constexpr std::size_t MidiDrainPerPeriod = 128;

//! Controller number of "all notes off" (channel mode message).
constexpr std::int32_t MidiAllNotesOffController = 123;

//! The time one processing period lasts.
auto periodBudget() -> std::chrono::steady_clock::duration
{
	const auto frames = Engine::audioEngine()->framesPerPeriod();
	const auto rate = Engine::audioEngine()->outputSampleRate();
	if (rate == 0) { return std::chrono::milliseconds{20}; }
	return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
		std::chrono::duration<double>{static_cast<double>(frames) / static_cast<double>(rate)});
}

//! The instant by which the client must have answered this period: the end of the render period that is
//! running, so the wait gets only what the callback has left. A render start more than two periods old
//! means no render period is running (tests, offline callers), and the budget is one period from now.
auto periodDeadline(std::chrono::steady_clock::time_point now) -> std::chrono::steady_clock::time_point
{
	const auto budget = periodBudget();
	// The wait ends an eighth of a period early: the rest of the graph still has time after a late client.
	const auto usable = budget - budget / 8;
	// Only the thread that renders a period is inside one; anywhere else the budget is one period from now.
	if (!RenderPeriodClock::renderingOnThisThread()) { return now + usable; }
	const auto renderStart = RenderPeriodClock::periodStart();
	if (now < renderStart || now - renderStart > 2 * budget) { return now + usable; }
	return renderStart + usable;
}

} // namespace




bool RemotePlugin::process()
{
	// Every silent period zeroes the host-private outputs the instrument reads, never the client's block.
	if (m_failed || !isRunning())
	{
		std::ranges::fill(m_hostOutputs, 0.f);
		return false;
	}

	// Realtime: the main thread holds m_commMutex across its own request/reply exchanges, so this
	// thread never waits for it. A busy mutex skips this period, rendered as silence.
	if (!m_commMutex.tryLock())
	{
		std::ranges::fill(m_hostOutputs, 0.f);
		return false;
	}

	m_inProcessPeriod = true;
	const bool processed = processPeriod();
	m_inProcessPeriod = false;
	m_commMutex.unlock();

	// A lost connection is not silence to be left unexplained: the main thread reports it and fails the plugin.
	if (!processed && isInvalid())
	{
		m_connectionLost = true;
		m_deferredWork = true;
	}

	return processed;
}




bool RemotePlugin::processPeriod()
{
	// The instrument reads and writes only the host-private block. Until a reply has completed for this
	// period, that block is silent, so a late or lost client cannot be heard, and it cannot write into it.
	std::ranges::fill(m_hostOutputs, 0.f);

	if (!m_audioBuffer)
	{
		// No shared audio block yet: the activation handshake has not completed, or the block could not be made.
		// Nothing is requested, and the audio thread does not read the connection here: a partly written message
		// or a lock held by the client would hold this period. The main thread's waits read those messages.
		return false;
	}

	// The test comes *before* the request on purpose: returning without sending keeps the protocol in step
	// (an unconsumed reply would be read as the next period's, i.e. one period of skew), and a plugin with
	// zero output channels would otherwise collect one reply per period until a socket buffer fills.
	// A pending channel-count change is the same kind of stop: the client already lays its planes out with
	// the new counts, which this host has not applied yet.
	if (m_failed || m_hostOutputs.empty() || m_countChangePending)
	{
		return false;
	}

	const auto start = std::chrono::steady_clock::now();

	// A request that was partly written in an earlier period is finished first. If that cannot happen now, the
	// period is skipped and nothing new is sent.
	if (flushPendingSend() != SendResult::Sent)
	{
		++m_processingOverruns;
		return false;
	}

	// A reply still owed by an earlier, abandoned period comes first. It is drained without waiting: while
	// one is outstanding the protocol is out of step, so no new request is sent this period.
	switch (collectProcessingReplies(start))
	{
	case ReplyWait::Done:
		break;
	case ReplyWait::Late:
		++m_processingOverruns;
		return false;
	case ReplyWait::Lost:
		return false;
	}

	// MIDI for this period goes out ahead of the request, so the client has it when it processes. Nothing
	// here waits: a send that finds no room skips the period, and the events stay queued for it.
	switch (sendQueuedMidiEvents())
	{
	case SendResult::Sent:
		break;
	case SendResult::NoRoom:
		++m_processingOverruns;
		return false;
	case SendResult::Broken:
		return false;
	}

	// The inputs for this period go into the shared block only now: no reply is owed, so the client is not
	// reading it.
	std::ranges::copy(m_hostInputs, m_sharedInputs.begin());

	char startFrame[8];
	const auto startSize = frameFromInts(startFrame, sizeof startFrame, IdStartProcessing, nullptr, 0);
	switch (sendFrameNonBlocking(startFrame, startSize))
	{
	case SendResult::Sent:
		break;
	case SendResult::NoRoom:
		++m_processingOverruns;
		return false;
	case SendResult::Broken:
		return false;
	}

	// The request is counted as owed once sent, so a late reply is always drained later.
	++m_owedProcessingReplies;

	switch (collectProcessingReplies(periodDeadline(start)))
	{
	case ReplyWait::Done:
		break;
	case ReplyWait::Late:
		// The client did not answer within the period. The reply stays owed: the next period drains it,
		// and the client's planes for this period are never copied out.
		++m_processingOverruns;
		return false;
	case ReplyWait::Lost:
		// The client died (or abandoned the period) before it replied.
		return false;
	}

	// A channel-count change that arrived during this period means the planes were laid out with counts
	// this host has not applied: they are not usable.
	if (m_countChangePending)
	{
		return false;
	}

	// The reply is complete, so the client has finished writing its planes: copy them out.
	std::ranges::copy(m_audioOutputs, m_hostOutputs.begin());
	return true;
}




RemotePlugin::ReplyWait RemotePlugin::collectProcessingReplies(std::chrono::steady_clock::time_point deadline)
{
	while (m_owedProcessingReplies > 0)
	{
		// A request that was only partly written must be finished before the client can answer it. While such a
		// tail waits, the wait is sliced so the tail is retried often; otherwise the wait runs to the deadline.
		// A reply already complete in the socket is taken even when the deadline has passed; the receive itself
		// gives up at the deadline when nothing is complete.
		flushPendingSend();
		const auto now = std::chrono::steady_clock::now();
		auto slice = deadline;
		if (sendTailPending())
		{
			const auto retry = now + std::chrono::milliseconds{1};
			if (retry < slice) { slice = retry; }
		}

		message m;
		switch (receiveFrameBefore(slice, m))
		{
		case ReceiveResult::Got:
			break;
		case ReceiveResult::Timeout:
			if (std::chrono::steady_clock::now() >= deadline) { return ReplyWait::Late; }
			continue;
		case ReceiveResult::Lost:
			invalidate(); // a connection that cannot be read is lost; the main thread is told (see process())
			return ReplyWait::Lost;
		}

		if (m.id == IdProcessingDone) { --m_owedProcessingReplies; }
		else { processMessage(m); }
	}
	return ReplyWait::Done;
}




void RemotePlugin::applyDeferredClientRequests()
{
	if (!m_audioBuffer && !m_failed && isRunning()) { pumpBeforeBuffer(); }

	if (!m_deferredWork.load()) { return; }

	// Re-sizing the ports reallocates the shared buffer the audio thread's router reads, so it is done with
	// rendering held off, as every other main-thread model change is. The request comes first: the audio
	// thread only ever try-locks m_commMutex, so it cannot be waiting on this thread here.
	Engine::audioEngine()->requestChangeInModel();
	lock();
	m_deferredWork = false;
	reportDeferredClientRequests();
	if (m_connectionLost.exchange(false))
	{
		m_failed = true;
		qCritical() << "RemotePlugin: the connection to the remote plugin client was lost; the plugin is silenced";
	}

	if (m_countChangePending)
	{
		m_countChangePending = false;
		if (!m_failed)
		{
			m_audioPorts->audioPortsModel().setChannelCounts(
				static_cast<ch_cnt_t>(m_pendingInCount), static_cast<ch_cnt_t>(m_pendingOutCount));
		}
	}
	unlock();
	Engine::audioEngine()->doneChangeInModel();
}




void RemotePlugin::reportDeferredClientRequests()
{
	const auto drops = m_midiOverflow.recorded();
	if( drops != m_reportedDrops )
	{
		qWarning() << "RemotePlugin:" << ( drops - m_reportedDrops )
			<< "MIDI event(s) refused by a full queue; their channels were sent all-notes-off (" << drops
			<< "refused in total)";
		m_reportedDrops = drops;
	}
	if (m_pendingReplyLost)
	{
		qCritical() << "RemotePlugin: a reply to the remote client could not be sent without blocking; the plugin is failed";
		m_pendingReplyLost = false;
	}
	if (m_pendingRetiredId != 0)
	{
		qCritical() << "RemotePlugin: remote client sent removed message id" << m_pendingRetiredId
			<< "- host and remote plugin client are from different builds"
			<< "(pre-#589 interleaved protocol); refusing the request and marking the plugin as failed";
		m_pendingRetiredId = 0;
	}
	if (m_pendingDebug)
	{
		fprintf( stderr, "RemotePlugin::DebugMessage: %.*s",
			static_cast<int>(m_pendingDebugLength), m_pendingDebugText.data() );
		m_pendingDebug = false;
		m_pendingDebugLength = 0;
	}
}




void RemotePlugin::processMidiEvent( const MidiEvent & _e,
							const f_cnt_t _offset )
{
	// Realtime: no lock and no allocation here, since the instrument hosts call this from the audio thread
	// while the main thread may hold m_commMutex for a whole request/reply exchange. The event is sent by
	// the next processing period, from sendQueuedMidiEvents().
	HandoffMidiEvent event;
	event.type = static_cast<std::uint8_t>( _e.type() );
	event.channel = _e.channel();
	event.param1 = _e.param( 0 );
	event.param2 = _e.param( 1 );
	event.offset = static_cast<std::uint32_t>( _offset );
	if( !pushOrRecord( m_midiHandoff, m_midiOverflow, event ) )
	{
		m_deferredWork = true; // the main thread reports the refusal
	}
}

RemotePluginBase::SendResult RemotePlugin::sendQueuedMidiEvents()
{
	if (m_pendingMidiSize != 0)
	{
		const auto result = sendFrameNonBlocking(m_pendingMidi.data(), m_pendingMidiSize);
		if (result != SendResult::Sent) { return result; }
		m_pendingMidiSize = 0;
	}

	// The drain is ordered and capped (see drainMidi()). A frame that cannot be sent is kept in m_pendingMidi and
	// goes first next period, so nothing is taken from the queue without a place to go.
	SendResult failure = SendResult::Sent;
	const auto emitFrame = [this, &failure](const std::array<std::int32_t, 5>& values) -> bool
	{
		m_pendingMidiSize = frameFromInts(m_pendingMidi.data(), m_pendingMidi.size(), IdMidiEvent,
			values.data(), values.size());
		const auto result = sendFrameNonBlocking(m_pendingMidi.data(), m_pendingMidiSize);
		if (result == SendResult::Sent)
		{
			m_pendingMidiSize = 0;
			return true;
		}
		failure = result;
		return false;
	};

	drainMidi(m_midiHandoff, m_midiOverflow, MidiDrainPerPeriod,
		[&](const HandoffMidiEvent& event) -> bool
		{
			return emitFrame({event.type, event.channel, event.param1, event.param2,
				static_cast<std::int32_t>(event.offset)});
		},
		[&](int channel) -> bool
		{
			return emitFrame({MidiControlChange, channel, MidiAllNotesOffController, 0, 0});
		});
	return failure;
}

void RemotePlugin::showUI()
{
	lock();
	sendMessage( IdShowUI );
	m_uiVisible = 1;
	unlock();
}

void RemotePlugin::hideUI()
{
	lock();
	sendMessage( IdHideUI );
	m_uiVisible = 0;
	unlock();
}

RemotePluginBase::message RemotePlugin::waitForReplyWithin( const RemotePluginBase::message & _wm,
	std::chrono::milliseconds _timeout )
{
	const auto deadline = std::chrono::steady_clock::now() + _timeout;
	for (;;)
	{
		RemotePluginBase::message m;
		if (receiveWithin(deadline, _timeout, m) != ReceiveResult::Got)
		{
			// Abandoned: its reply may still arrive, and must not be taken as the answer to a later query.
			abandonQuery(_wm.id);
			return RemotePluginBase::message();
		}
		// The reply to this query is the first one for its id that no abandoned query is waiting for. An abandoned
		// query's reply is consumed here, once, and the wait goes on; any other message goes through processMessage().
		if (m.id != _wm.id) { processMessage(m); }
		else if (!consumeStaleQuery(m.id)) { return m; }
		if (std::chrono::steady_clock::now() >= deadline)
		{
			abandonQuery(_wm.id);
			return RemotePluginBase::message();
		}
	}
}

void RemotePlugin::abandonQuery( int _id )
{
	auto& stale = m_staleQueries[_id];
	if( stale.count == 0 ) { stale.since = std::chrono::steady_clock::now(); }
	++stale.count;
}

bool RemotePlugin::consumeStaleQuery( int _id )
{
	const auto it = m_staleQueries.find( _id );
	if( it == m_staleQueries.end() || it->second.count <= 0 ) { return false; }
	// Expired: the reply was never coming. The next reply for this id is the answer to the next query.
	if( std::chrono::steady_clock::now() - it->second.since > StaleQueryExpiry )
	{
		m_staleQueries.erase( it );
		return false;
	}
	--it->second.count;
	return true;
}

void RemotePlugin::pumpBeforeBuffer()
{
	// A client's handshake messages are read here, on the main thread, while no shared block exists: the audio thread
	// reads nothing then, and without this nothing would complete the handshake until a main-thread wait ran.
	lock();
	const auto now = std::chrono::steady_clock::now();
	// A client that floods messages is read a bounded number per tick, so the main thread never stays in here.
	constexpr int MaxMessagesPerTick = 64;
	for (int taken = 0; taken < MaxMessagesPerTick; ++taken)
	{
		// Only a complete message is taken: a frame still arriving is left for the next tick, not given up on.
		if (!completeMessageWaiting()) { break; }
		RemotePluginBase::message m;
		if (receiveFrameBefore(now, m) != ReceiveResult::Got) { break; }
		processMessage(m);
		if (m.id == IdInitDone && !m_failed)
		{
			m_audioPorts->activate(Engine::audioEngine()->framesPerPeriod());
		}
		if (m_audioBuffer) { break; }
	}
	unlock();
}




auto RemotePlugin::updateAudioBuffer(ch_cnt_t channelsIn, ch_cnt_t channelsOut, f_cnt_t frames) -> float*
{
	const RemotePluginBufferChangeGuard guard;
	reportRefusedChannelCounts();
	if (channelsIn == 0 && channelsOut == 0)
	{
		std::cerr << "Invalid channel count for shared audio buffer\n";
		return nullptr;
	}

	if (channelsIn == m_channelsIn && channelsOut == m_channelsOut && frames == m_frames)
	{
		return m_hostBlock.data();
	}

	try
	{
		m_hostInputs = {};
		m_hostOutputs = {};
		m_sharedInputs = {};
		m_audioOutputs = {};
		m_audioBuffer.create((channelsIn + channelsOut) * frames);
		m_hostBlock.assign((channelsIn + channelsOut) * frames, 0.f);
	}
	catch (const std::exception& error)
	{
		std::cerr << "Failed to allocate shared audio buffer: " << error.what() << '\n';
		m_audioBuffer.detach();
		m_hostBlock.clear();
		m_hostBlock.shrink_to_fit();
		return nullptr;
	}

	m_channelsIn = channelsIn;
	m_channelsOut = channelsOut;
	m_frames = frames;

	const auto inputs = static_cast<std::size_t>(channelsIn * frames);
	const auto outputs = static_cast<std::size_t>(channelsOut * frames);
	m_sharedInputs = std::span{m_audioBuffer.get(), inputs};
	m_audioOutputs = std::span{m_audioBuffer.get() + inputs, outputs};
	m_hostInputs = std::span{m_hostBlock.data(), inputs};
	m_hostOutputs = std::span{m_hostBlock.data() + inputs, outputs};

	sendMessage(message(IdChangeSharedMemoryKey).addString(m_audioBuffer.key()));

	return m_hostBlock.data();
}





void RemotePlugin::processFinished( int exitCode,
						QProcess::ExitStatus exitStatus )
{
	reportRefusedChannelCounts();
	if ( exitStatus == QProcess::CrashExit )
	{
		qCritical() << "Remote plugin crashed";
	}
	else if ( exitCode )
	{
		qCritical() << "Remote plugin exit code: " << exitCode;
	}

	// Feature row 80 (out-of-process hosting, board card #670): the client's
	// exit is recorded under the CLIENT EXECUTABLE's name - which is what the
	// crash-loop refusal and the `oop.*` surface read. A deliberate shutdown
	// (our own destructor taking the client down, m_hostShutdownRequested) is
	// counted as an exit but never as a crash: that is the difference between
	// "this client executable is broken" and "we stopped using it".
	//
	// The record is written BEFORE invalidate(), so a plugin that is being
	// failed reports the exit that failed it.
	oop::HostTracker& tracker = oop::HostTracker::instance();
	const QString client = QFileInfo{m_exec}.fileName();
	if (m_hostShutdownRequested)
	{
		tracker.noteShutdown(client, exitCode);
	}
	else
	{
		tracker.noteExited(client, exitCode, exitStatus == QProcess::CrashExit,
			QString::fromLatin1(metaObject()->className()));
	}
#ifndef SYNC_WITH_SHM_FIFO
	invalidate();
#endif
}

void RemotePlugin::processErrored( QProcess::ProcessError err )
{
	qCritical() << "Process error: " << err;
}




bool RemotePlugin::reportRefusedChannelCounts()
{
	if (!m_refusalPending.exchange(false)) { return false; }
	qWarning() << "RemotePlugin: remote client reported an out-of-range channel count"
		<< m_refusedInCount.load() << "in /" << m_refusedOutCount.load() << "out - refusing the request";
	return true;
}

bool RemotePlugin::processMessage( const message & _m )
{
	lock();
	message reply_message( _m.id );
	bool reply = false;
	switch( _m.id )
	{
		case IdUndefined:
			unlock();
			return false;

		case IdInitDone:
			m_initDoneSeen = true;
			reply = true;
			break;

		case IdSampleRateInformation:
			reply = true;
			reply_message.addInt( Engine::audioEngine()->outputSampleRate() );
			break;

		case IdBufferSizeInformation:
			reply = true;
			reply_message.addInt( Engine::audioEngine()->framesPerPeriod() );
			break;

		case IdChangeInputCount:
		case IdChangeOutputCount:
			// Retired by the planar audio-ports migration (#589): channel counts
			// are owned by the audio ports model and a client must report both
			// counts at once (IdChangeInputOutputCount), which is the only
			// channel-count message the client interface still sends.
			//
			// A client that sends one of these ids was built against the old
			// interleaved protocol, whose buffer layout this host no longer
			// speaks: its per-channel offsets would read planar planes as
			// interleaved frames and silently render wrong audio. There is no
			// protocol version in this class to negotiate with, so refuse
			// loudly instead of rendering garbage: fail the plugin, which makes
			// process() zero-fill the output planes until the host is restarted
			// with a matching client.
			// On the audio thread the log is deferred: it allocates and locks.
			if (m_inProcessPeriod)
			{
				m_pendingRetiredId = _m.id;
				m_deferredWork = true;
			}
			else
			{
				qCritical() << "RemotePlugin: remote client sent removed message id" << _m.id
					<< "- host and remote plugin client are from different builds"
					<< "(pre-#589 interleaved protocol); refusing the request and marking the plugin as failed";
			}
			m_failed = true;
			break;

		case IdChangeInputOutputCount:
		{
			// The client reports both counts at once; the ports model owns the
			// shared buffer sizing from here on (updateAudioBuffer()).
			//
			// The wire values are signed ints, `ch_cnt_t` is unsigned: casting a
			// negative count wrapped it (-1 -> 65535) and no upper bound stopped
			// an oversized one, and the shared block is sized from these counts.
			// Refuse anything outside the range a buffer can hold instead.
			const auto inCount = _m.getInt(0);
			const auto outCount = _m.getInt(1);
			const auto outOfRange = [](int count)
			{ return count < 0 || count > MaxChannelsPerAudioBuffer; };
			if (outOfRange(inCount) || outOfRange(outCount))
			{
				// Realtime rule: no log here, since this can run on the audio thread (process() pumps
				// messages while it waits for the client). The refusal is recorded and reported later.
				m_refusedInCount.store(inCount);
				m_refusedOutCount.store(outCount);
				m_refusalPending.store(true);
				break;
			}
			if (m_inProcessPeriod)
			{
				// Realtime rule: the ports model re-sizes the shared buffer, which allocates and may
				// create shared memory. Apply the change on the main thread (applyDeferredClientRequests());
				// process() renders silence until it has been applied.
				m_pendingInCount = inCount;
				m_pendingOutCount = outCount;
				m_countChangePending = true;
				m_deferredWork = true;
				break;
			}
			m_audioPorts->audioPortsModel().setChannelCounts(
				static_cast<ch_cnt_t>(inCount), static_cast<ch_cnt_t>(outCount));
			break;
		}

		case IdDebugMessage:
			if (m_inProcessPeriod)
			{
				// Realtime rule: the text is copied into a fixed slot and printed on the main thread. A
				// second message while one is pending is dropped.
				if (!m_pendingDebug)
				{
					const auto& text = _m.stringArgument(0);
					m_pendingDebugLength = std::min(text.size(), m_pendingDebugText.size() - 1);
					std::copy_n(text.data(), m_pendingDebugLength, m_pendingDebugText.data());
					m_pendingDebug = true;
					m_deferredWork = true;
				}
				break;
			}
			fprintf( stderr, "RemotePlugin::DebugMessage: %s",
						_m.getString( 0 ).c_str() );
			break;

		case IdProcessingDone:
			// Only a main-thread wait gets here (collectProcessingReplies() takes the replies it is owed). The
			// reply belongs to a period the audio thread gave up on, so that period is no longer owed.
			if (m_owedProcessingReplies > 0) { --m_owedProcessingReplies; }
			break;
		case IdIsUIVisible:
			if (!consumeStaleQuery(_m.id)) { m_uiVisible = _m.getInt() ? 1 : 0; }
			break;
		case IdInformationUpdated:
			consumeStaleQuery(_m.id);
			break;
		case IdQuit:
		default:
			break;
	}
	if( reply )
	{
		if( m_inProcessPeriod )
		{
			// On the audio thread the reply is written without blocking. If it cannot be, the client
			// is left waiting for it, which is the same failure as a lost client; it is reported later.
			std::array<char, 512> frame{};
			const auto size = frameFromMessage( frame.data(), frame.size(), reply_message );
			if( size == 0 || sendFrameNonBlocking( frame.data(), size ) != SendResult::Sent )
			{
				m_pendingReplyLost = true;
				m_deferredWork = true;
				m_failed = true;
			}
		}
		else
		{
			sendMessage( reply_message );
		}
	}
	unlock();

	return true;
}


} // namespace lmms
