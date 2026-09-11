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

#include <span>

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
	//! @return true if the remote client processed this period
	bool process();

	//! (Re)allocates the shared audio buffer used by the ports infrastructure
	//! and tells the remote client about the new shared memory key (Part C).
	//! The returned block holds `channelsIn + channelsOut` channel-major planar
	//! buffers of `frames` floats each: first `channelsIn` input planes, then
	//! `channelsOut` output planes. A repeat call with unchanged arguments
	//! returns the existing block without reallocating.
	//! @return pointer to the shared buffer, or nullptr on failure
	auto updateAudioBuffer(ch_cnt_t channelsIn, ch_cnt_t channelsOut, f_cnt_t frames) -> float*;

	void processMidiEvent( const MidiEvent&, const f_cnt_t _offset );

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

	QString m_exec;
	QStringList m_args;

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
