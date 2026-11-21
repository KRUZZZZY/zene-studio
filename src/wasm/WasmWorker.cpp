/*
 * WasmWorker.cpp - the off-audio-thread worker that owns a WasmSandbox
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "WasmWorker.h"

#include "WasmWorkerPool.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace lmms::wasm
{

#ifdef LMMS_TESTING
namespace
{

using AudioEndpointEntryTestHook = void (*)(void*);
std::atomic<AudioEndpointEntryTestHook> audioEndpointEntryTestHook{nullptr};
std::atomic<void*> audioEndpointEntryTestContext{nullptr};

using ModuleLoadTestHook = void (*)(void*);
std::atomic<ModuleLoadTestHook> moduleLoadTestHook{nullptr};
std::atomic<void*> moduleLoadTestContext{nullptr};

} // namespace
#endif

WasmWorker::~WasmWorker()
{
	stop();
}

bool WasmWorker::start(const std::string& modulePath, std::string& error,
	int timeoutMs)
{
	stop();
	m_logs.clear();
	m_modulePath = modulePath;
	m_stop.store(false, std::memory_order_release);
	m_state.store(State::Loading, std::memory_order_release);
	openAudioEndpoints();
	// No thread of our own: a lane of the shared pool loads the module and then
	// runs this worker's blocks. addWorker() asks for a wake-up, so the load
	// starts immediately rather than at the next tick of a timer.
	if (!WasmWorkerPool::instance().addWorker(this))
	{
		error = "the WASM worker pool is full (WasmWorkerPool::maxWorkers)";
		m_state.store(State::Failed, std::memory_order_release);
		return false;
	}
	m_registeredWithPool = true;

	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(timeoutMs);
	while (m_state.load(std::memory_order_acquire) == State::Loading &&
		std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (m_state.load(std::memory_order_acquire) == State::Ready)
	{
		return true;
	}
	error = m_lastError.empty() ? "timed out loading module" : m_lastError;
	stop();
	return false;
}

void WasmWorker::stop()
{
	m_stop.store(true, std::memory_order_release);
	// The audio side only makes a bounded atomic attempt. The control thread
	// closes the gate and waits for already-pinned SPSC access to finish.
	closeAudioEndpointsAndWait();
	if (m_registeredWithPool)
	{
		WasmWorkerPool::instance().removeWorker(this);
		m_registeredWithPool = false;
		// Wait for the lane that may be inside drainOnLane() to let go: the
		// sandbox below is destroyed once nobody can be using it.
		while (m_laneClaimed.load(std::memory_order_acquire))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
	if (m_state.load(std::memory_order_acquire) != State::Failed)
	{
		m_state.store(State::Idle, std::memory_order_release);
	}
	// Drain queues and free every slot so a restart starts clean.
	std::uint32_t index = 0;
	while (m_commands.pop(index))
	{
		m_slots[index].state.store(SlotState::Free, std::memory_order_release);
	}
	while (m_results.pop(index))
	{
		m_slots[index].state.store(SlotState::Free, std::memory_order_release);
	}
	// A lane can stop between marking a slot Done and pushing its result (the
	// push loop gives up when m_stop is set), so that slot is in neither queue.
	// The audio endpoints are closed and no lane holds the claim here: free
	// every slot, not just the queued ones, or a restart inherits them.
	for (Slot& slot : m_slots)
	{
		slot.state.store(SlotState::Free, std::memory_order_release);
		slot.frames = 0;
	}
	m_sandbox.reset();
	m_declaredChannels.store(1, std::memory_order_relaxed);
	m_declaredLatency.store(0, std::memory_order_relaxed);
	m_loadedSampleRate = 0.0f;
}

bool WasmWorker::claimForLane()
{
	bool expected = false;
	return m_laneClaimed.compare_exchange_strong(expected, true, std::memory_order_acquire,
		std::memory_order_relaxed);
}

void WasmWorker::releaseLane()
{
	m_laneClaimed.store(false, std::memory_order_release);
}

void WasmWorker::openAudioEndpoints()
{
	m_audioEndpointState.store(0, std::memory_order_release);
}

void WasmWorker::closeAudioEndpointsAndWait()
{
	m_audioEndpointState.fetch_or(audioEndpointClosedBit, std::memory_order_acq_rel);
	while ((m_audioEndpointState.load(std::memory_order_acquire) & audioEndpointUsersMask) != 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

bool WasmWorker::beginAudioEndpoint()
{
	// Entry and close modify the same atomic word. If this increment wins the
	// modification order before stop's fetch_or, stop sees the pin and waits;
	// if close wins first, this CAS observes the closed bit and cannot enter.
	std::uint32_t state = m_audioEndpointState.load(std::memory_order_acquire);
	for (;;)
	{
		if ((state & audioEndpointClosedBit) != 0
			|| (state & audioEndpointUsersMask) == audioEndpointUsersMask)
		{
			return false;
		}
		if (m_audioEndpointState.compare_exchange_weak(state, state + 1,
			std::memory_order_acq_rel, std::memory_order_acquire))
		{
			return true;
		}
	}
}

void WasmWorker::endAudioEndpoint()
{
	m_audioEndpointState.fetch_sub(1, std::memory_order_release);
}

#ifdef LMMS_TESTING
void WasmWorker::setAudioEndpointEntryTestHook(AudioEndpointEntryTestHook hook, void* context)
{
	audioEndpointEntryTestContext.store(context, std::memory_order_relaxed);
	audioEndpointEntryTestHook.store(hook, std::memory_order_release);
}

void WasmWorker::notifyAudioEndpointEnteredForTest()
{
	if (const auto hook = audioEndpointEntryTestHook.load(std::memory_order_acquire))
	{
		hook(audioEndpointEntryTestContext.load(std::memory_order_relaxed));
	}
}

void WasmWorker::setModuleLoadTestHook(ModuleLoadTestHook hook, void* context)
{
	moduleLoadTestContext.store(context, std::memory_order_relaxed);
	moduleLoadTestHook.store(hook, std::memory_order_release);
}

void WasmWorker::notifyModuleLoadForTest()
{
	if (const auto hook = moduleLoadTestHook.load(std::memory_order_acquire))
	{
		hook(moduleLoadTestContext.load(std::memory_order_relaxed));
	}
}
#endif

bool WasmWorker::hasPendingWork() const
{
	if (m_stop.load(std::memory_order_acquire)) { return false; }
	// A module to load (or reload) is work: the first block cannot be processed
	// before it, and the pool's lane is the thread that does it.
	if (m_state.load(std::memory_order_acquire) == State::Loading) { return true; }
	return !m_commands.empty();
}

bool WasmWorker::drainOnLane()
{
	bool didWork = false;

	// 1. the module, on the first pass after start() (and after every
	//    re-instantiation request).
	if (m_state.load(std::memory_order_acquire) == State::Loading)
	{
		loadModuleOnLane();
		didWork = true;
	}

	// 2. every queued block, in FIFO order. This lane holds the claim, so no
	//    other lane is inside the sandbox - which is what keeps a stateful
	//    module's blocks in the order the audio thread submitted them.
	std::uint32_t index = 0;
	while (m_commands.pop(index))
	{
		didWork = true;
		Slot& slot = m_slots[index];
		slot.state.store(SlotState::Processing, std::memory_order_release);
		const float sampleRate = m_sampleRate.load(std::memory_order_acquire);
		if (m_state.load(std::memory_order_acquire) == State::Ready &&
			sampleRate != m_loadedSampleRate)
		{
			// Sample-rate change: re-instantiate so the module never runs
			// with a stale rate or stale state. While it runs the audio thread
			// sees State::Loading and passes audio through dry.
			reinstantiate(sampleRate);
		}
		if (m_state.load(std::memory_order_acquire) == State::Ready)
		{
			processSlot(slot, sampleRate);
		}
		else
		{
			passthrough(slot);
		}
		slot.state.store(SlotState::Done, std::memory_order_release);
		while (!m_results.push(index) && !m_stop.load(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
		m_processed.fetch_add(1, std::memory_order_release);
	}
	return didWork;
}

bool WasmWorker::loadModuleOnLane()
{
#ifdef LMMS_TESTING
	// The seam fires before the module is compiled, so a test can hold the
	// load and look at the worker while it is still Loading.
	notifyModuleLoadForTest();
#endif
	m_sandbox = std::make_unique<WasmSandbox>();
	std::string error;
	if (!m_sandbox->loadModuleFile(m_modulePath, error))
	{
		transferLogFromLane();
		m_lastError = error;
		m_state.store(State::Failed, std::memory_order_release);
		return false;
	}
	transferLogFromLane();
	if (!m_sandbox->hasProcess())
	{
		m_lastError = "module does not export " + std::string(abi::processExport)
			+ "()";
		m_state.store(State::Failed, std::memory_order_release);
		return false;
	}
	m_declaredChannels.store(m_sandbox->declaredChannels(), std::memory_order_relaxed);
	m_declaredLatency.store(m_sandbox->declaredLatency(), std::memory_order_relaxed);
	m_loadedSampleRate = m_sampleRate.load(std::memory_order_acquire);
	m_state.store(State::Ready, std::memory_order_release);
	return true;
}

void WasmWorker::processSlot(Slot& slot, float sampleRate)
{
	const std::uint32_t frames = slot.frames;
	if (frames == 0)
	{
		return;
	}
	const int channels = std::clamp(m_sandbox->declaredChannels(), 1,
		static_cast<int>(abi::maxChannels));
	const std::uint32_t planeBytes = frames * sizeof(float);
	const std::uint32_t inBase = 0;
	const std::uint32_t outBase = static_cast<std::uint32_t>(channels) * planeBytes;

	std::uint8_t* memory = m_sandbox->memoryData();
	const std::size_t needed =
		static_cast<std::size_t>(outBase) + static_cast<std::size_t>(channels) * planeBytes;
	if (memory == nullptr || m_sandbox->memorySize() < needed)
	{
		passthrough(slot);
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	for (std::size_t i = 0; i < abi::maxParams; ++i)
	{
		m_sandbox->setParam(static_cast<std::uint32_t>(i),
			m_params[i].load(std::memory_order_relaxed));
	}
	m_sandbox->setTransportState(
		m_transportState.load(std::memory_order_relaxed));

	// Start from dry passthrough so channels the module does not process (a
	// mono module in a stereo host) are never zeroed or left stale.
	passthrough(slot);

	// Deinterleave host stereo into the module's planar input planes.
	for (int channel = 0; channel < channels; ++channel)
	{
		float* plane = reinterpret_cast<float*>(
			memory + inBase + static_cast<std::uint32_t>(channel) * planeBytes);
		for (std::uint32_t frame = 0; frame < frames; ++frame)
		{
			plane[frame] = slot.in[frame * 2 + static_cast<std::size_t>(channel)];
		}
	}

	CallResult result;
	for (int channel = 0; channel < channels; ++channel)
	{
		result = detail::callProcessAndRefreshMemory(*m_sandbox,
			inBase + static_cast<std::uint32_t>(channel) * planeBytes,
			outBase + static_cast<std::uint32_t>(channel) * planeBytes,
			frames, sampleRate, memory, needed);
		if (!result.ok())
		{
			break;
		}
	}

	transferLogFromLane();

	if (!result.ok())
	{
		// Crash isolation: a trapping module is quarantined and the host keeps
		// running. The block falls back to dry passthrough.
		m_trapped.fetch_add(1, std::memory_order_relaxed);
		m_lastError = result.message;
		passthrough(slot);
		m_state.store(State::Corrupted, std::memory_order_release);
		return;
	}

	for (int channel = 0; channel < channels; ++channel)
	{
		const float* plane = reinterpret_cast<const float*>(
			memory + outBase + static_cast<std::uint32_t>(channel) * planeBytes);
		for (std::uint32_t frame = 0; frame < frames; ++frame)
		{
			slot.out[frame * 2 + static_cast<std::size_t>(channel)] = plane[frame];
		}
	}
}

void WasmWorker::passthrough(Slot& slot)
{
	std::memcpy(slot.out.data(), slot.in.data(),
		slot.frames * sizeof(SampleFrame));
}

void WasmWorker::transferLogFromLane()
{
	if (!m_sandbox)
	{
		return;
	}
	const std::string log = m_sandbox->takeLog();
	if (!log.empty())
	{
		m_logs.append(reinterpret_cast<const std::uint8_t*>(log.data()), log.size());
	}
}

bool WasmWorker::reinstantiate(float sampleRate)
{
	m_state.store(State::Loading, std::memory_order_release);
	if (!loadModuleOnLane())
	{
		m_lastError = m_lastError.empty() ? "module reload failed" : m_lastError;
		return false;
	}
	m_loadedSampleRate = sampleRate;
	m_reinstantiated.fetch_add(1, std::memory_order_relaxed);
	return true;
}

bool WasmWorker::submit(const SampleFrame* interleaved, std::uint32_t frames,
	float sampleRate)
{
	AudioEndpointPin endpoint(*this);
	if (!endpoint)
	{
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
#ifdef LMMS_TESTING
	notifyAudioEndpointEnteredForTest();
#endif
	if (frames == 0 || frames > maxBlockFrames ||
		m_state.load(std::memory_order_acquire) != State::Ready)
	{
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	Slot* slot = nullptr;
	for (Slot& candidate : m_slots)
	{
		if (candidate.state.load(std::memory_order_acquire) == SlotState::Free)
		{
			slot = &candidate;
			break;
		}
	}
	if (slot == nullptr)
	{
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	std::memcpy(slot->in.data(), interleaved, frames * sizeof(SampleFrame));
	slot->frames = frames;
	m_sampleRate.store(sampleRate, std::memory_order_release);
	slot->state.store(SlotState::Filled, std::memory_order_release);

	const std::uint32_t index = static_cast<std::uint32_t>(slot - m_slots.data());
	if (!m_commands.push(index))
	{
		slot->state.store(SlotState::Free, std::memory_order_release);
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	// Real wake-up, from the audio thread: the pool bumps its work generation
	// and wakes a parked lane. While a lane is already awake this is a relaxed
	// increment and an acquire load - no lock, no allocation, no syscall.
	WasmWorkerPool::instance().notifyWork();
	m_submitted.fetch_add(1, std::memory_order_release);
	return true;
}

bool WasmWorker::collect(SampleFrame* interleaved, std::uint32_t frames)
{
	AudioEndpointPin endpoint(*this);
	if (!endpoint) { return false; }
#ifdef LMMS_TESTING
	notifyAudioEndpointEnteredForTest();
#endif
	std::uint32_t index = 0;
	if (!m_results.pop(index))
	{
		return false;
	}
	Slot& slot = m_slots[index];
	const std::uint32_t count = std::min(frames, slot.frames);
	// SampleFrame is trivially copyable (a plain std::array<float, 2>); the
	// explicit void* cast documents the deliberate raw byte copy and keeps
	// -Wclass-memaccess quiet about the user-provided default constructor.
	std::memcpy(static_cast<void*>(interleaved), slot.out.data(),
		count * sizeof(SampleFrame));
	if (count < frames)
	{
		std::memset(static_cast<void*>(interleaved + count), 0,
			(frames - count) * sizeof(SampleFrame));
	}
	slot.state.store(SlotState::Free, std::memory_order_release);
	m_collected.fetch_add(1, std::memory_order_relaxed);
	return true;
}

void WasmWorker::setParam(std::uint32_t index, float value)
{
	if (index < m_params.size())
	{
		m_params[index].store(value, std::memory_order_relaxed);
	}
}

float WasmWorker::param(std::uint32_t index) const
{
	if (index < m_params.size())
	{
		return m_params[index].load(std::memory_order_relaxed);
	}
	return 0.0f;
}

void WasmWorker::setTransportState(std::int32_t transportState)
{
	m_transportState.store(transportState, std::memory_order_relaxed);
}

std::string WasmWorker::takeLog()
{
	// The lane owns the sandbox and moves its latest message into m_logs; this
	// reader never touches m_sandbox, which stop()/reload may replace.
	return m_logs.take();
}

bool WasmWorker::waitForIdle(int timeoutMs)
{
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(timeoutMs);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (m_processed.load(std::memory_order_acquire) >=
			m_submitted.load(std::memory_order_acquire))
		{
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return m_processed.load(std::memory_order_acquire) >=
		m_submitted.load(std::memory_order_acquire);
}

} // namespace lmms::wasm
