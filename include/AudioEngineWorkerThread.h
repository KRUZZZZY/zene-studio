/*
 * AudioEngineWorkerThread.h - declaration of class AudioEngineWorkerThread
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

#ifndef LMMS_AUDIO_ENGINE_WORKER_THREAD_H
#define LMMS_AUDIO_ENGINE_WORKER_THREAD_H

#include <QThread>

#include <atomic>
#include <cstdint>


namespace lmms
{

class AudioEngine;
class ThreadableJob;

class AudioEngineWorkerThread : public QThread
{
	Q_OBJECT
public:
	// internal representation of the job queue - all functions are thread-safe
	class JobQueue
	{
	public:
		enum class OperationMode
		{
			Static,	// no jobs added while processing queue
			Dynamic	// jobs can be added while processing queue
		} ;

		static constexpr size_t JOB_QUEUE_SIZE = 8192;

		JobQueue() :
			m_items(),
			m_writeIndex( 0 ),
			m_itemsDone( 0 ),
			m_opMode( OperationMode::Static )
		{
			std::fill(m_items, m_items + JOB_QUEUE_SIZE, nullptr);
		}

		void reset( OperationMode _opMode );

		void addJob( ThreadableJob * _job );

		void run();
		void wait();

	private:
		std::atomic<ThreadableJob*> m_items[JOB_QUEUE_SIZE];
		std::atomic_size_t m_writeIndex;
		std::atomic_size_t m_itemsDone;
		//! Atomic: reset() writes it while running workers read it (BUGS_FOUND 10.9b).
		std::atomic<OperationMode> m_opMode;
	} ;


	AudioEngineWorkerThread( AudioEngine* audioEngine );
	~AudioEngineWorkerThread() override;

	virtual void quit();

	static void resetJobQueue( JobQueue::OperationMode _opMode =
													JobQueue::OperationMode::Static )
	{
		globalJobQueue.reset( _opMode );
	}

	static void addJob( ThreadableJob * _job )
	{
		globalJobQueue.addJob( _job );
	}

	// a convenient helper function allowing to pass a container with pointers
	// to ThreadableJob objects
	template<typename T>
	static void fillJobQueue( const T & _vec,
							JobQueue::OperationMode _opMode = JobQueue::OperationMode::Static )
	{
		resetJobQueue( _opMode );
		for (const auto& job : _vec)
		{
			addJob(job);
		}
	}

	static void startAndWaitForJobs();
	//! Wake every idle pool worker to drain the queue, without running it here (what
	//! startAndWaitForJobs() does before it takes its own share). A worker sleeps until woken -
	//! there is no idle poll - so a caller that queues work for the pool alone uses this.
	static void wakeWorkers();

	/**
	 * @brief Process every job on the calling thread instead of handing it to the pool.
	 *
	 * The worker pool exists for live playback, where deadline misses are worse than a
	 * scheduling decision. In an OFFLINE render that trade is backwards: which thread
	 * runs which job is a property of the host and of the moment, and a render whose
	 * effect chains amplify a one-ULP difference into a different waveform stops being
	 * reproducible run to run (measured in docs/RENDER-DETERMINISM.md). With this set,
	 * ProjectRenderer's render thread does all the work itself, exactly once per job,
	 * in queue order.
	 *
	 * Set it for the duration of an export, never for live playback. The setting is
	 * process-global and read only by the engine thread that calls
	 * startAndWaitForJobs(), so it does not need a lock; it is atomic so the render
	 * thread and a late worker cannot disagree about it.
	 */
	static void setDeterministicProcessing(bool deterministic);
	static bool deterministicProcessing();


private:
	void run() override;
	//! The platform half of the wake (see the .cpp): post, and wait for the generation to move.
	static void notifyWorkers();
	static void waitForWake(std::uint32_t seen);

	static JobQueue globalJobQueue;
	//! The wake (R7.2, BUGS_FOUND 11.22 a): startAndWaitForJobs() and quit() bump it and
	//! notify; a worker waits for it to move. Replaces a QWaitCondition, whose wakeAll() took a
	//! mutex on the render thread every period. A wait on a value that has already moved returns
	//! at once, so no wake can be lost between reading m_quit and sleeping.
	static std::atomic<std::uint32_t> s_wakeGeneration;
	static QList<AudioEngineWorkerThread *> workerThreads;

	//! Atomic, not volatile: quit() writes it while run() reads it on the worker (10.9a).
	std::atomic<bool> m_quit;
} ;

} // namespace lmms

#endif // LMMS_AUDIO_ENGINE_WORKER_THREAD_H
