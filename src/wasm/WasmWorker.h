/*
 * WasmWorker.h - the off-audio-thread worker that owns a WasmSandbox
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

#ifndef LMMS_WASM_WORKER_H
#define LMMS_WASM_WORKER_H

#include "WasmAbi.h"
#include "WasmSandbox.h"
#include "WasmSpscRingBuffer.h"

#include "SampleFrame.h"
#include "lmms_export.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace lmms::wasm
{

//! Runs one WasmSandbox on a dedicated worker thread.
//!
//! Threading contract (specs/SPEC-wasm-sandbox.md section 4): modules NEVER run
//! on the audio thread. The audio thread calls submit()/collect(), which only
//! touch pre-allocated slot memory and lock-free SPSC queues - no allocation,
//! no locks, no syscalls. The worker thread owns the sandbox and does all
//! module loading, memory copying and instantiation work.
// Exported from the host: the WasmEffect plugin (a separate .so) resolves
// these symbols at load time, exactly like Effect/PluginFactory.
class LMMS_EXPORT WasmWorker
{
public:
	//! Maximum block size the pre-allocated slots can carry.
	static constexpr std::uint32_t maxBlockFrames = 8192;
	//! Pre-allocated slots (one in flight + one collecting + headroom).
	static constexpr std::size_t slotCount = 4;
	static constexpr std::size_t commandQueueCapacity = 8;
	static constexpr std::size_t resultQueueCapacity = 8;

	enum class State
	{
		Idle,       //!< not started
		Loading,    //!< worker thread is compiling/instantiating
		Ready,      //!< module loaded, accepting blocks
		Failed,     //!< module could not be loaded
		Corrupted   //!< the module trapped; it is no longer called
	};

	WasmWorker() = default;
	~WasmWorker();

	WasmWorker(const WasmWorker&) = delete;
	WasmWorker& operator=(const WasmWorker&) = delete;

	//! Control thread: launch the worker and load \p modulePath.
	bool start(const std::string& modulePath, std::string& error, int timeoutMs = 10000);
	//! Control thread: stop and join the worker.
	void stop();

	State state() const { return m_state.load(std::memory_order_acquire); }
	bool isReady() const { return state() == State::Ready; }
	bool isCorrupted() const { return state() == State::Corrupted; }
	//! Control thread only; safe once the worker has stopped changing state.
	std::string lastError() const { return m_lastError; }

	//! Channels the loaded module declares (1 when nothing is loaded).
	int declaredChannels() const { return m_declaredChannels.load(std::memory_order_relaxed); }
	//! Frames of latency the loaded module declares (0 when it declares none).
	//! Survives a quarantine so the host can keep compensating the dry path.
	int declaredLatency() const { return m_declaredLatency.load(std::memory_order_relaxed); }
	//! Times the module was re-instantiated because the sample rate changed.
	std::uint64_t reinstantiatedModules() const
	{
		return m_reinstantiated.load(std::memory_order_relaxed);
	}

	// ---- audio thread API (no allocation, no locks, no syscalls) ----
	//! Copy one interleaved stereo block into a free slot and queue it.
	//! Returns false (and counts a drop) when no slot or queue entry is free.
	bool submit(const SampleFrame* interleaved, std::uint32_t frames, float sampleRate);
	//! Copy the oldest processed block into \p interleaved. Returns false when
	//! no processed block is waiting (caller should pass audio through dry).
	bool collect(SampleFrame* interleaved, std::uint32_t frames);

	// ---- control thread API ----
	void setParam(std::uint32_t index, float value);
	float param(std::uint32_t index) const;
	void setTransportState(std::int32_t transportState);
	std::string takeLog();

	std::uint64_t submittedBlocks() const { return m_submitted.load(std::memory_order_relaxed); }
	std::uint64_t processedBlocks() const { return m_processed.load(std::memory_order_relaxed); }
	std::uint64_t droppedBlocks() const { return m_dropped.load(std::memory_order_relaxed); }
	std::uint64_t trappedBlocks() const { return m_trapped.load(std::memory_order_relaxed); }
	std::uint64_t collectedBlocks() const { return m_collected.load(std::memory_order_relaxed); }

	//! Control/test helper: block until every submitted block has been processed.
	bool waitForIdle(int timeoutMs);

private:
	enum class SlotState : std::uint32_t
	{
		Free,
		Filled,      //!< audio thread wrote input, command queued
		Processing,  //!< worker is running the module on this slot
		Done         //!< worker wrote output, result queued
	};

	struct Slot
	{
		std::atomic<SlotState> state{SlotState::Free};
		std::uint32_t frames = 0;
		std::array<float, 2 * maxBlockFrames> in{};
		std::array<float, 2 * maxBlockFrames> out{};
	};

	void run();
	void processSlot(Slot& slot, float sampleRate);
	void passthrough(Slot& slot);
	//! Worker thread: reload/re-instantiate the module for a new sample rate.
	bool reinstantiate(float sampleRate);

	std::string m_modulePath;
	std::thread m_thread;
	std::atomic<bool> m_stop{false};
	std::atomic<State> m_state{State::Idle};
	std::string m_lastError;
	//! Rate of the most recent block, written by the audio thread in submit().
	std::atomic<float> m_sampleRate{44100.0f};
	//! Rate the current module instance was created for (worker thread only).
	float m_loadedSampleRate = 0.0f;
	std::atomic<int> m_declaredChannels{1};
	std::atomic<int> m_declaredLatency{0};
	std::atomic<std::uint64_t> m_reinstantiated{0};

	std::unique_ptr<WasmSandbox> m_sandbox;
	std::array<Slot, slotCount> m_slots{};
	SpscRingBuffer<std::uint32_t, commandQueueCapacity> m_commands;
	SpscRingBuffer<std::uint32_t, resultQueueCapacity> m_results;

	std::array<std::atomic<float>, abi::maxParams> m_params{};
	std::atomic<std::int32_t> m_transportState{abi::transportStopped};

	std::atomic<std::uint64_t> m_submitted{0};
	std::atomic<std::uint64_t> m_processed{0};
	std::atomic<std::uint64_t> m_dropped{0};
	std::atomic<std::uint64_t> m_trapped{0};
	std::atomic<std::uint64_t> m_collected{0};
};

} // namespace lmms::wasm

#endif // LMMS_WASM_WORKER_H
