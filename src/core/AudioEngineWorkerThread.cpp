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

#include <atomic>
#include <cstdint>

#include "AudioEngine.h"
#include "Hardware.h"
#include "RealtimeScope.h"
#include "ThreadableJob.h"


namespace lmms
{

AudioEngineWorkerThread::JobQueue AudioEngineWorkerThread::globalJobQueue;
std::atomic<std::uint32_t> AudioEngineWorkerThread::s_wakeGeneration{0};
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

// Counted queue-full refusals since the process started (SPEC-MULTICORE-SCHEDULING
// §2.6 / §3.2.3: "a counted, lock-free refusal"). addJob()'s overflow branch runs on
// the audio thread - Mixer.cpp:255 and :1729 seed this queue from masterMix - so it
// increments this counter and formats nothing: qWarning() there would build a QString
// on the path tests/rt-safety-scope.txt declares. External linkage so
// tests/src/core/RoutingGraphScheduleTest.cpp can assert the refusal fired and was
// counted, without a header/ABI change (the member accessor lands when a later slice
// touches AudioEngineWorkerThread.h).
std::atomic<std::uint64_t> queueFullRefusals{0};

// implementation of internal JobQueue
void AudioEngineWorkerThread::JobQueue::reset( OperationMode _opMode )
{
	m_writeIndex = 0;
	m_itemsDone = 0;
	m_opMode.store( _opMode, std::memory_order_relaxed );
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
			// Counted refusal instead of a bare qWarning (multicore Slice 0,
			// SPEC-MULTICORE-SCHEDULING §2.6 / §3.2.3). The refusal itself is
			// unchanged - the job is dropped and m_itemsDone is still advanced, so
			// wait() terminates - but the event is now a lock-free increment on the
			// audio thread rather than formatted log text, and it is readable by the
			// registered test that proves the branch fires.
			queueFullRefusals.fetch_add(1, std::memory_order_relaxed);
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
				// R7.2: a job is audio-thread work on whichever thread takes it.
				const RealtimeScope realtime;
				job->process();
				processedJob = true;
				++m_itemsDone;
			}
		}
		// always exit loop if we're not in dynamic mode
		processedJob = processedJob && ( m_opMode.load( std::memory_order_relaxed ) == OperationMode::Dynamic );
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
	m_quit.store( true, std::memory_order_release );
	resetJobQueue();
	// Release a worker parked in run(): the generation moves, so its wait returns.
	s_wakeGeneration.fetch_add(1, std::memory_order_release);
	s_wakeGeneration.notify_all();
}




void AudioEngineWorkerThread::startAndWaitForJobs()
{
	if (deterministicProcessing())
	{
		// Deterministic offline rendering (ProjectRenderer): the calling thread takes
		// every job, so nothing about the result depends on which worker was awake.
		// run() drains in Dynamic mode, so a job that queues another job (the mixer's
		// dependency-driven channels do) is picked up in the same call.
		//
		// Note that this branch is not on its own enough to make that true, and it
		// never was: a worker that is already awake (or that wakes on its own bounded
		// re-check - see run()) drains globalJobQueue without passing through here.
		// The queue itself is what both threads share, so the switch is honoured on
		// both sides: here, and in run().
		globalJobQueue.run();
		globalJobQueue.wait();
		return;
	}

	// A futex / wait-on-address post, not a mutex and a condition variable (BUGS_FOUND 11.22 a).
	s_wakeGeneration.fetch_add(1, std::memory_order_release);
	s_wakeGeneration.notify_all();
	// The last worker-thread is never started. Instead it's processed "inline"
	// i.e. within the global AudioEngine thread. This way we can reduce latencies
	// that otherwise would be caused by synchronizing with another thread.
	globalJobQueue.run();
	globalJobQueue.wait();
}




void AudioEngineWorkerThread::run()
{
	disableDenormals();

	std::uint32_t seen = s_wakeGeneration.load(std::memory_order_acquire);
	while( !m_quit.load( std::memory_order_acquire ) )
	{
		// Sleep until the generation moves. This used to be a QWaitCondition with a 100 ms
		// bound, and the bound was load-bearing: a worker preempted between reading m_quit and
		// reaching wait() missed the wake meant for it (13 of 64 PdcMixerTest runs under load,
		// one worker of 19 stranded) and outlived its QThread - qFatal at Engine::destroy().
		// A wait on a value cannot miss a wake: if the generation moved after `seen` was read,
		// wait() returns at once. quit() moves it too.
		s_wakeGeneration.wait(seen, std::memory_order_acquire);
		seen = s_wakeGeneration.load(std::memory_order_acquire);
		if( m_quit.load( std::memory_order_acquire ) ) { break; }

		// A wake must not overtake deterministic rendering: with the switch on the CALLING
		// thread takes every job (see startAndWaitForJobs()), and a worker that drained the
		// queue here would run an offline render's jobs on this thread instead - the result
		// would then depend on which worker happened to wake inside the render window, which is
		// exactly the variability the switch exists to remove (docs/RENDER-DETERMINISM.md;
		// measured: RenderJobQueueTest::inlineModeNeverLetsThePoolTakeAJob took a job on a pool
		// thread on both macOS jobs without this check).
		if( !deterministicProcessing() )
		{
			globalJobQueue.run();
		}
	}
}

} // namespace lmms
