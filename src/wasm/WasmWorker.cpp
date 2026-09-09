/*
 * WasmWorker.cpp - the off-audio-thread worker that owns a WasmSandbox
 *
 * Copyright (c) 2026 LMMS WASM DSP sandbox contributors
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

#include <algorithm>
#include <chrono>
#include <cstring>

namespace lmms::wasm
{

WasmWorker::~WasmWorker()
{
	stop();
}

bool WasmWorker::start(const std::string& modulePath, std::string& error,
	int timeoutMs)
{
	stop();
	m_modulePath = modulePath;
	m_stop.store(false, std::memory_order_release);
	m_state.store(State::Loading, std::memory_order_release);
	m_thread = std::thread(&WasmWorker::run, this);

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
	if (m_thread.joinable())
	{
		m_thread.join();
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
	m_sandbox.reset();
	m_declaredChannels.store(1, std::memory_order_relaxed);
	m_declaredLatency.store(0, std::memory_order_relaxed);
	m_loadedSampleRate = 0.0f;
}

void WasmWorker::run()
{
	m_sandbox = std::make_unique<WasmSandbox>();
	std::string error;
	if (!m_sandbox->loadModuleFile(m_modulePath, error))
	{
		m_lastError = error;
		m_state.store(State::Failed, std::memory_order_release);
		return;
	}
	if (!m_sandbox->hasProcess())
	{
		m_lastError = "module does not export " + std::string(abi::processExport)
			+ "()";
		m_state.store(State::Failed, std::memory_order_release);
		return;
	}
	m_declaredChannels.store(m_sandbox->declaredChannels(), std::memory_order_relaxed);
	m_declaredLatency.store(m_sandbox->declaredLatency(), std::memory_order_relaxed);
	m_loadedSampleRate = m_sampleRate.load(std::memory_order_acquire);
	m_state.store(State::Ready, std::memory_order_release);

	while (!m_stop.load(std::memory_order_acquire))
	{
		std::uint32_t index = 0;
		bool didWork = false;
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
				// with a stale rate or stale state. This happens on the worker
				// thread only; while it runs the audio thread sees
				// State::Loading and passes audio through dry.
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
			while (!m_results.push(index) &&
				!m_stop.load(std::memory_order_acquire))
			{
				std::this_thread::yield();
			}
			m_processed.fetch_add(1, std::memory_order_release);
		}
		if (!didWork)
		{
			// Idle: sleep briefly. The audio thread never blocks on this; it
			// only ever observes empty queues and passes audio through dry.
			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}
	}
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
		result = m_sandbox->callProcess(
			inBase + static_cast<std::uint32_t>(channel) * planeBytes,
			outBase + static_cast<std::uint32_t>(channel) * planeBytes,
			frames, sampleRate);
		if (!result.ok())
		{
			break;
		}
	}

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

bool WasmWorker::reinstantiate(float sampleRate)
{
	m_state.store(State::Loading, std::memory_order_release);
	std::string error;
	if (!m_sandbox->loadModuleFile(m_modulePath, error) ||
		!m_sandbox->hasProcess())
	{
		m_lastError = error.empty() ? "module reload failed" : error;
		m_state.store(State::Failed, std::memory_order_release);
		return false;
	}
	m_loadedSampleRate = sampleRate;
	m_declaredChannels.store(m_sandbox->declaredChannels(), std::memory_order_relaxed);
	m_declaredLatency.store(m_sandbox->declaredLatency(), std::memory_order_relaxed);
	m_reinstantiated.fetch_add(1, std::memory_order_relaxed);
	m_state.store(State::Ready, std::memory_order_release);
	return true;
}

bool WasmWorker::submit(const SampleFrame* interleaved, std::uint32_t frames,
	float sampleRate)
{
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
	m_submitted.fetch_add(1, std::memory_order_release);
	return true;
}

bool WasmWorker::collect(SampleFrame* interleaved, std::uint32_t frames)
{
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
	if (!m_sandbox)
	{
		return {};
	}
	return m_sandbox->takeLog();
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
