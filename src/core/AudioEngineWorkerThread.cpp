/*
 * AudioEngineWorkerThread.cpp - implementation of AudioEngineWorkerThread
 *
 * Copyright (c) 2009-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AudioEngineWorkerThread.h"

#include <QDebug>
#include <QMutex>
#include <QWaitCondition>

#include <atomic>

#include "AudioEngine.h"
#include "Hardware.h"
#include "ThreadableJob.h"


namespace lmms
{

AudioEngineWorkerThread::JobQueue AudioEngineWorkerThread::globalJobQueue;
QWaitCondition * AudioEngineWorkerThread::queueReadyWaitCond = nullptr;
QList<AudioEngineWorkerThread *> AudioEngineWorkerThread::workerThreads;

//! Off when the engine starts: live playback keeps the pool (see the header).
static std::atomic<bool> s_deterministicProcessing{false};

void AudioEngineWorkerThread::setDeterministicProcessing(bool deterministic)
{
	s_deterministicProcessing.store(deterministic, std::memory_order_release);
}

bool AudioEngineWorkerThread::deterministicProcessing()
{
	return s_deterministicProcessing.load(std::memory_order_acquire);
}

// implementation of internal JobQueue
void AudioEngineWorkerThread::JobQueue::reset( OperationMode _opMode )
{
	m_writeIndex = 0;
	m_itemsDone = 0;
	m_opMode = _opMode;
}




void AudioEngineWorkerThread::JobQueue::addJob( ThreadableJob * _job )
{
	if( _job->requiresProcessing() )
	{
		// update job state
		_job->queue();
		// actually queue the job via atomic operations
		auto index = m_writeIndex++;
		if (index < JOB_QUEUE_SIZE) {
			m_items[index] = _job;
		} else {
			qWarning() << "Job queue is full!";
			++m_itemsDone;
		}
	}
}



void AudioEngineWorkerThread::JobQueue::run()
{
	bool processedJob = true;
	while (processedJob && m_itemsDone < m_writeIndex)
	{
		processedJob = false;
		for (auto i = std::size_t{0}; i < m_writeIndex && i < JOB_QUEUE_SIZE; ++i)
		{
			ThreadableJob * job = m_items[i].exchange(nullptr);
			if( job )
			{
				job->process();
				processedJob = true;
				++m_itemsDone;
			}
		}
		// always exit loop if we're not in dynamic mode
		processedJob = processedJob && ( m_opMode == OperationMode::Dynamic );
	}
}




void AudioEngineWorkerThread::JobQueue::wait()
{
	while (m_itemsDone < m_writeIndex) { busyWaitHint(); }
}





// implementation of worker threads

AudioEngineWorkerThread::AudioEngineWorkerThread( AudioEngine* audioEngine ) :
	QThread( audioEngine ),
	m_quit( false )
{
	// initialize global static data
	if( queueReadyWaitCond == nullptr )
	{
		queueReadyWaitCond = new QWaitCondition;
	}

	// keep track of all instantiated worker threads - this is used for
	// processing the last worker thread "inline", see comments in
	// AudioEngineWorkerThread::startAndWaitForJobs() for details
	workerThreads << this;

	resetJobQueue();
}




AudioEngineWorkerThread::~AudioEngineWorkerThread()
{
	workerThreads.removeAll( this );
}




void AudioEngineWorkerThread::quit()
{
	m_quit = true;
	resetJobQueue();
}




void AudioEngineWorkerThread::startAndWaitForJobs()
{
	if (deterministicProcessing())
	{
		// Deterministic offline rendering (ProjectRenderer): the calling thread takes
		// every job, so nothing about the result depends on which worker was awake.
		// run() drains in Dynamic mode, so a job that queues another job (the mixer's
		// dependency-driven channels do) is picked up in the same call.
		globalJobQueue.run();
		globalJobQueue.wait();
		return;
	}

	queueReadyWaitCond->wakeAll();
	// The last worker-thread is never started. Instead it's processed "inline"
	// i.e. within the global AudioEngine thread. This way we can reduce latencies
	// that otherwise would be caused by synchronizing with another thread.
	globalJobQueue.run();
	globalJobQueue.wait();
}




//! How long an idle worker sleeps before re-checking m_quit. See the comment in
//! run() - this bound is what stops a lost wake-up from stranding a worker.
static constexpr unsigned long kQuitRecheckMs = 100;

void AudioEngineWorkerThread::run()
{
	disableDenormals();

	QMutex m;
	while( m_quit == false )
	{
		m.lock();
		// BOUNDED wait, and this bound is load-bearing. `quit()` sets m_quit and
		// `startAndWaitForJobs()` wakes this condition, but the two are not atomic
		// with respect to a worker arriving here: the flag is read at the top of the
		// loop, outside the mutex, so a worker that is preempted between reading
		// m_quit and reaching wait() misses the wake that was meant to release it and
		// then sleeps for good. On a loaded machine that window is wide (measured:
		// 13 of 64 PdcMixerTest runs, load average ~27, exactly one worker of 19
		// stranded in wait()). A stranded worker then outlives the QThread object
		// that owns it, and Qt answers that with
		//   qFatal("QThread: Destroyed while thread is still running")
		// i.e. SIGABRT during Engine::destroy(), blamed on whatever test was running.
		// Re-checking every 100 ms bounds a lost wake-up instead of hanging on it.
		// Cost: while the engine is idle each worker wakes, drains an empty queue and
		// sleeps again 10 times a second; while it renders, wake-ups arrive every
		// period and this timer never fires.
		queueReadyWaitCond->wait( &m, kQuitRecheckMs );
		globalJobQueue.run();
		m.unlock();
	}
}

} // namespace lmms
