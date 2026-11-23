/*
 * Lv2Worker.cpp - Lv2Worker implementation
 *
 * Copyright (c) 2022-2022 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#include "Lv2Worker.h"

#include <cassert>

#include "AudioEngineWorkerThread.h"
#include "CrashReporter.h"

#ifdef LMMS_HAVE_LV2



namespace lmms
{

namespace
{

//! Depth of inline work() calls on THIS thread. A response must go straight to
//! run() only when it comes from work() running inline in the caller (offline
//! render); work() queued to the worker thread must queue its response for
//! emitResponses() on the audio thread instead. Thread-local because the same
//! instance may be inline-worked by its render thread and queued-worked by its
//! worker thread.
thread_local unsigned s_inlineWorkDepth = 0;
//! Depth of queued work() calls on THIS thread (the worker thread). A nested
//! schedule_work() from inside work() must queue, not try to take the work lock
//! this thread already holds.
thread_local unsigned s_queuedWorkDepth = 0;

struct InlineWorkScope
{
	InlineWorkScope() noexcept { ++s_inlineWorkDepth; }
	~InlineWorkScope() noexcept { --s_inlineWorkDepth; }
	InlineWorkScope(const InlineWorkScope&) = delete;
	InlineWorkScope& operator=(const InlineWorkScope&) = delete;
};

struct QueuedWorkScope
{
	QueuedWorkScope() noexcept { ++s_queuedWorkDepth; }
	~QueuedWorkScope() noexcept { --s_queuedWorkDepth; }
	QueuedWorkScope(const QueuedWorkScope&) = delete;
	QueuedWorkScope& operator=(const QueuedWorkScope&) = delete;
};

} // namespace


// static wrappers

static LV2_Worker_Status
staticWorkerRespond(LV2_Worker_Respond_Handle handle,
	uint32_t size, const void* data)
{
	Lv2Worker* worker = static_cast<Lv2Worker*>(handle);
	return worker->respond(size, data);
}




std::size_t Lv2Worker::bufferSize() const
{
	// ardour uses this fixed size for ALSA:
	return 8192 * 4;
	// for jack, they use 4 * jack_port_type_get_buffer_size (..., JACK_DEFAULT_MIDI_TYPE)
	// (possible extension for AudioDevice)
}




Lv2Worker::Lv2Worker(Semaphore* commonWorkLock, bool threaded) :
	m_threaded(threaded),
	m_response(bufferSize()),
	m_requests(bufferSize()),
	m_responses(bufferSize()),
	m_requestsReader(m_requests),
	m_responsesReader(m_responses),
	m_sem(0),
	m_workLock(commonWorkLock)
{
	m_scheduleFeature.handle = static_cast<LV2_Worker_Schedule_Handle>(this);
	m_scheduleFeature.schedule_work = [](LV2_Worker_Schedule_Handle handle,
		uint32_t size, const void* data) -> LV2_Worker_Status
		{
			Lv2Worker* worker = static_cast<Lv2Worker*>(handle);
			return worker->scheduleWork(size, data);
		};

	// The worker thread always exists. A render-only worker (threaded == false)
	// still needs it: its inline path is legal only while a deterministic offline
	// render is running. AudioDummy paces renderNextPeriod() in real time without
	// that flag, and work() must stay off that thread. Starting it lazily on the
	// first such request is not an option - it would allocate on the audio thread.
	m_thread = std::thread([](Lv2Worker* self)
	{
		crashreporter::installAlternateStackForCurrentThread();
		self->workerFunc();
	}, this);


	m_requests.mlock();
	m_responses.mlock();
}




void Lv2Worker::setHandle(LV2_Handle handle)
{
	assert(handle);
	m_handle = handle;
}




void Lv2Worker::setInterface(const LV2_Worker_Interface* newInterface)
{
	assert(newInterface);
	m_interface = newInterface;
}




Lv2Worker::~Lv2Worker()
{
	m_exit = true;
	m_sem.post();
	m_thread.join();
}




// Let the worker send responses to the audio thread
LV2_Worker_Status Lv2Worker::respond(uint32_t size, const void* data)
{
	if(s_inlineWorkDepth == 0)
	{
		if(m_responses.free() < sizeof(size) + size)
		{
			return LV2_WORKER_ERR_NO_SPACE;
		}
		else
		{
			m_responses.write((const char*)&size, sizeof(size));
			if(size && data) { m_responses.write((const char*)data, size); }
		}
	}
	else
	{
		assert(m_handle);
		assert(m_interface);
		m_interface->work_response(m_handle, size, data);
	}
	return LV2_WORKER_SUCCESS;
}




// Let the worker receive work from the audio thread and "work" on it
void Lv2Worker::workerFunc()
{
	std::vector<char> buf;
	uint32_t size;
	while (true) {
		m_sem.wait();
		if (m_exit) { break; }

		const std::size_t readSpace = m_requestsReader.read_space();
		if (readSpace <= sizeof(size)) { continue; } // (should not happen)

		m_requestsReader.read(sizeof(size)).copy((char*)&size, sizeof(size));
		assert(size <= readSpace - sizeof(size));
		if(size > buf.size()) { buf.resize(size); }
		if(size) { m_requestsReader.read(size).copy(buf.data(), size); }

		assert(m_handle);
		assert(m_interface);
		m_workLock->wait();
		QueuedWorkScope queuedScope;
		m_interface->work(m_handle, staticWorkerRespond, this, size, buf.data());
		m_workLock->post();
	}
}




// Let the audio thread schedule work for the worker
LV2_Worker_Status Lv2Worker::scheduleWork(uint32_t size, const void *data)
{
	// Inline execution is allowed only for the render-only worker AND only while
	// an offline (deterministic) render is running, where the LV2 Worker
	// specification permits immediate execution and there is no real-time
	// deadline. AudioDummy's paced renderNextPeriod() runs with that flag off, so
	// its schedule_work() queues to the worker thread below like live playback.
	const bool inlineWork = !m_threaded
		&& s_inlineWorkDepth == 0
		&& s_queuedWorkDepth == 0
		&& AudioEngineWorkerThread::deterministicProcessing();
	if (inlineWork)
	{
		assert(m_handle);
		assert(m_interface);
		// Execute work immediately in this (offline render) thread
		m_workLock->wait();
		InlineWorkScope inlineScope;
		m_interface->work(m_handle, staticWorkerRespond, this, size, data);
		m_workLock->post();
	}
	else
	{
		if(m_requests.free() < sizeof(size) + size)
		{
			return LV2_WORKER_ERR_NO_SPACE;
		}
		else
		{
			// Schedule a request to be executed by the worker thread
			m_requests.write((const char*)&size, sizeof(size));
			if(size && data) { m_requests.write((const char*)data, size); }
			m_sem.post();
		}
	}

	return LV2_WORKER_SUCCESS;
}




// Let the audio thread read incoming worker responses, and process it
void Lv2Worker::emitResponses()
{
	std::size_t read_space = m_responsesReader.read_space();
	uint32_t size;
	while (read_space > sizeof(size))
	{
		assert(m_handle);
		assert(m_interface);
		m_responsesReader.read(sizeof(size)).copy((char*)&size, sizeof(size));
		if(size) { m_responsesReader.read(size).copy(m_response.data(), size); }
		m_interface->work_response(m_handle, size, m_response.data());
		read_space -= sizeof(size) + size;
	}
}


} // namespace lmms

#endif // LMMS_HAVE_LV2
