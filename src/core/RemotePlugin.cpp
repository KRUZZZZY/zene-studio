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

#ifdef LMMS_BUILD_WIN32
#include <windows.h>
#endif

#include "AudioEngine.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "RemotePluginAudioPorts.h"
#include "Song.h"

#include <iostream>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
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
}




RemotePlugin::~RemotePlugin()
{
	// Detach from the audio ports controller before the shared buffer is
	// destroyed, so the controller never holds a freed buffer view.
	m_audioPorts->disconnectBuffers();

	m_watcher.stop();
	m_watcher.wait();

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

#ifndef SYNC_WITH_SHM_FIFO
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
	}
#endif

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
	m_failed = waitForMessage(IdInitDone, busyWaiting).id != IdInitDone;

	if (!m_failed)
	{
		// The remote side exists now, so its buffers do too: activate the audio
		// ports, which allocates the shared audio block for this plugin's
		// channel counts (RemotePluginAudioPorts::activate() -> updateBuffers()
		// -> updateAudioBuffer()).
		m_audioPorts->activate(Engine::audioEngine()->framesPerPeriod());
	}
}




bool RemotePlugin::process()
{
	if (m_failed || !isRunning())
	{
		// Zero the output planes so a failed plugin renders silence instead of
		// stale audio; there is no separate output buffer to clear anymore.
		std::ranges::fill(m_audioOutputs, 0.f);
		return false;
	}

	if (!m_audioBuffer)
	{
		// We have no shared audio block yet, which means the activation
		// handshake hasn't completed. Pump messages (and hope we get
		// information like the shm key) until it has.
		lock();
		fetchAndProcessAllMessages();
		unlock();

		std::ranges::fill(m_audioOutputs, 0.f);
		return false;
	}

	lock();
	sendMessage(IdStartProcessing);

	if (m_failed || m_audioOutputs.empty())
	{
		unlock();
		return false;
	}

	waitForMessage(IdProcessingDone);
	unlock();

	return true;
}




void RemotePlugin::processMidiEvent( const MidiEvent & _e,
							const f_cnt_t _offset )
{
	message m( IdMidiEvent );
	m.addInt( _e.type() );
	m.addInt( _e.channel() );
	m.addInt( _e.param( 0 ) );
	m.addInt( _e.param( 1 ) );
	m.addInt( _offset );
	lock();
	sendMessage( m );
	unlock();
}

void RemotePlugin::showUI()
{
	lock();
	sendMessage( IdShowUI );
	unlock();
}

void RemotePlugin::hideUI()
{
	lock();
	sendMessage( IdHideUI );
	unlock();
}




auto RemotePlugin::updateAudioBuffer(ch_cnt_t channelsIn, ch_cnt_t channelsOut, f_cnt_t frames) -> float*
{
	if (channelsIn == 0 && channelsOut == 0)
	{
		std::cerr << "Invalid channel count for shared audio buffer\n";
		return nullptr;
	}

	if (channelsIn == m_channelsIn && channelsOut == m_channelsOut && frames == m_frames)
	{
		return m_audioBuffer.get();
	}

	try
	{
		m_audioOutputs = {};
		m_audioBuffer.create((channelsIn + channelsOut) * frames);
	}
	catch (const std::runtime_error& error)
	{
		std::cerr << "Failed to allocate shared audio buffer: " << error.what() << '\n';
		m_audioBuffer.detach();
		return nullptr;
	}

	m_channelsIn = channelsIn;
	m_channelsOut = channelsOut;
	m_frames = frames;

	m_audioOutputs = std::span{m_audioBuffer.get() + channelsIn * frames, channelsOut * frames};

	sendMessage(message(IdChangeSharedMemoryKey).addString(m_audioBuffer.key()));

	return m_audioBuffer.get();
}




void RemotePlugin::processFinished( int exitCode,
					QProcess::ExitStatus exitStatus )
{
	if ( exitStatus == QProcess::CrashExit )
	{
		qCritical() << "Remote plugin crashed";
	}
	else if ( exitCode )
	{
		qCritical() << "Remote plugin exit code: " << exitCode;
	}
#ifndef SYNC_WITH_SHM_FIFO
	invalidate();
#endif
}

void RemotePlugin::processErrored( QProcess::ProcessError err )
{
	qCritical() << "Process error: " << err;
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
			qCritical() << "RemotePlugin: remote client sent removed message id" << _m.id
				<< "- host and remote plugin client are from different builds"
				<< "(pre-#589 interleaved protocol); refusing the request and marking the plugin as failed";
			m_failed = true;
			break;

		case IdChangeInputOutputCount:
			// The client reports both counts at once; the ports model owns the
			// shared buffer sizing from here on (updateAudioBuffer()).
			m_audioPorts->audioPortsModel().setChannelCounts(
				static_cast<ch_cnt_t>(_m.getInt(0)), static_cast<ch_cnt_t>(_m.getInt(1)));
			break;

		case IdDebugMessage:
			fprintf( stderr, "RemotePlugin::DebugMessage: %s",
						_m.getString( 0 ).c_str() );
			break;

		case IdProcessingDone:
		case IdQuit:
		default:
			break;
	}
	if( reply )
	{
		sendMessage( reply_message );
	}
	unlock();

	return true;
}


} // namespace lmms
