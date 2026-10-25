/*
 * AudioResampler.cpp
 *
 * Copyright (c) 2025 saker <sakertooth@gmail.com>
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

#include "AudioResampler.h"

#include <samplerate.h>
#include <array>
#include <atomic>
#include <new>
#include <stdexcept>
#include <utility>

#include "ExportRenderSettings.h"
#include "PlayHandle.h"
#include "RealtimeScope.h"

namespace lmms {

namespace {

#ifdef LMMS_TESTING
std::atomic<int> s_allocationsBeforeFailure{0};
std::atomic<int> s_failuresToInject{0};
std::atomic<std::uint64_t> s_realtimeConverterAllocations{0};

bool converterAllocationMustFailForTest() noexcept
{
	if (s_allocationsBeforeFailure.load(std::memory_order_relaxed) > 0)
	{
		s_allocationsBeforeFailure.fetch_sub(1, std::memory_order_relaxed);
		return false;
	}
	if (s_failuresToInject.load(std::memory_order_relaxed) > 0)
	{
		s_failuresToInject.fetch_sub(1, std::memory_order_relaxed);
		return true;
	}
	return false;
}
#else
bool converterAllocationMustFailForTest() noexcept { return false; }
#endif

constexpr auto converterType(AudioResampler::Mode mode) -> int
{
	switch (mode)
	{
	case AudioResampler::Mode::ZOH:
		return SRC_ZERO_ORDER_HOLD;
	case AudioResampler::Mode::Linear:
		return SRC_LINEAR;
	case AudioResampler::Mode::SincFastest:
		return SRC_SINC_FASTEST;
	case AudioResampler::Mode::SincMedium:
		return SRC_SINC_MEDIUM_QUALITY;
	case AudioResampler::Mode::SincBest:
		return SRC_SINC_BEST_QUALITY;
	default:
		throw std::invalid_argument{"Invalid interpolation mode"};
	}
}

auto createConverterState(AudioResampler::Mode mode, ch_cnt_t channels, int* error) -> SRC_STATE*
{
#ifdef LMMS_TESTING
	if (RealtimeScope::isActive())
	{
		s_realtimeConverterAllocations.fetch_add(1, std::memory_order_relaxed);
	}
#endif
	return src_new(converterType(mode), channels, error);
}

class RealtimeVoiceConverterPool
{
public:
	static constexpr std::size_t ChunkCapacity = PlayHandle::MaxNumber;

	RealtimeVoiceConverterPool() = default;
	~RealtimeVoiceConverterPool()
	{
		destroyChain(m_head.load(std::memory_order_relaxed));
	}

	auto initialize(AudioResampler::Mode mode, std::size_t capacity) noexcept -> bool
	{
		if (m_initialized.load(std::memory_order_acquire))
		{
			if (!ensureCapacity(capacity)) { return false; }
			if (m_mode == mode) { return true; }
			if (!prepareAll(mode)) { return false; }
			adoptAll();
			return true;
		}

		if (!appendChunks(capacity, mode)) { return false; }
		m_mode = mode;
		m_initialized.store(true, std::memory_order_release);
		return true;
	}

	auto ensureCapacity(std::size_t capacity) noexcept -> bool
	{
		if (!m_initialized.load(std::memory_order_acquire)) { return true; }
		return appendChunks(capacity, m_mode);
	}

	auto prepareAll(AudioResampler::Mode mode) noexcept -> bool
	{
		if (!m_initialized.load(std::memory_order_acquire)) { return false; }
		for (Chunk* chunk = m_head.load(std::memory_order_acquire); chunk != nullptr; chunk = chunk->next)
		{
			for (Slot& slot : chunk->items)
			{
				if (slot.inUse.load(std::memory_order_acquire)) { continue; }
				if (!prepareSlot(&slot, mode))
				{
					discardAll();
					return false;
				}
			}
		}
		return true;
	}

	void adoptAll() noexcept
	{
		for (Chunk* chunk = m_head.load(std::memory_order_acquire); chunk != nullptr; chunk = chunk->next)
		{
			for (Slot& slot : chunk->items) { adoptSlot(&slot); }
		}
		if (m_head.load(std::memory_order_relaxed) != nullptr)
		{
			m_mode = m_head.load(std::memory_order_relaxed)->items[0].mode;
		}
	}

	void discardAll() noexcept
	{
		for (Chunk* chunk = m_head.load(std::memory_order_acquire); chunk != nullptr; chunk = chunk->next)
		{
			for (Slot& slot : chunk->items) { discard(slot); }
		}
	}

	auto acquire() noexcept -> void*
	{
		if (!m_initialized.load(std::memory_order_acquire)) { return nullptr; }
		for (Chunk* chunk = m_head.load(std::memory_order_acquire); chunk != nullptr; chunk = chunk->next)
		{
			for (Slot& slot : chunk->items)
			{
				bool expected = false;
				if (!slot.inUse.compare_exchange_strong(expected, true,
					std::memory_order_acquire, std::memory_order_relaxed)) { continue; }
				if (slot.state != nullptr)
				{
					src_reset(slot.state);
					return &slot;
				}
				slot.inUse.store(false, std::memory_order_release);
				return nullptr;
			}
		}
		return nullptr;
	}

	void release(void* token) noexcept
	{
		if (Slot* const slot = static_cast<Slot*>(token)) { slot->inUse.store(false, std::memory_order_release); }
	}

	auto mode(void* token) const noexcept -> AudioResampler::Mode
	{
		const Slot* const slot = static_cast<const Slot*>(token);
		return slot != nullptr ? slot->mode : AudioResampler::Mode::Linear;
	}

	auto state(void* token) const noexcept -> void*
	{
		const Slot* const slot = static_cast<const Slot*>(token);
		return slot != nullptr ? slot->state : nullptr;
	}

	auto prepareSlot(void* token, AudioResampler::Mode mode) noexcept -> bool
	{
		Slot* const slotPointer = static_cast<Slot*>(token);
		if (!m_initialized.load(std::memory_order_acquire) || slotPointer == nullptr) { return false; }
		Slot& slot = *slotPointer;
		if (slot.hasPrepared && slot.preparedMode == mode) { return true; }
		discard(slot);
		if (slot.mode == mode)
		{
			slot.preparedMode = mode;
			slot.hasPrepared = true;
			return true;
		}
#ifdef LMMS_TESTING
		if (converterAllocationMustFailForTest()) { return false; }
#endif
		int error = 0;
		SRC_STATE* const prepared = createConverterState(mode, 2, &error);
		if (prepared == nullptr) { return false; }
		slot.prepared = prepared;
		slot.preparedMode = mode;
		slot.hasPrepared = true;
		return true;
	}

	auto hasPrepared(void* token) const noexcept -> bool
	{
		const Slot* const slot = static_cast<const Slot*>(token);
		return slot != nullptr && slot->hasPrepared;
	}

	void adoptSlot(void* token) noexcept
	{
		Slot* const slotPointer = static_cast<Slot*>(token);
		if (slotPointer == nullptr) { return; }
		Slot& slot = *slotPointer;
		if (!slot.hasPrepared) { return; }
		if (slot.prepared != nullptr)
		{
			src_delete(slot.state);
			slot.state = slot.prepared;
			slot.prepared = nullptr;
		}
		slot.mode = slot.preparedMode;
		slot.hasPrepared = false;
	}

	void discardSlot(void* token) noexcept
	{
		if (Slot* const slot = static_cast<Slot*>(token)) { discard(*slot); }
	}

	auto trySetSlot(void* token, AudioResampler::Mode mode) noexcept -> bool
	{
		if (token == nullptr) { return false; }
		if (this->mode(token) == mode) { return true; }
		if (!prepareSlot(token, mode)) { return false; }
		adoptSlot(token);
		return true;
	}

private:
	struct Slot
	{
		SRC_STATE* state = nullptr;
		SRC_STATE* prepared = nullptr;
		AudioResampler::Mode mode = AudioResampler::Mode::Linear;
		AudioResampler::Mode preparedMode = AudioResampler::Mode::Linear;
		bool hasPrepared = false;
		std::atomic<bool> inUse{false};
	};
	struct Chunk
	{
		std::array<Slot, ChunkCapacity> items{};
		Chunk* next = nullptr;
	};

	static auto createChunk(AudioResampler::Mode mode) noexcept -> Chunk*
	{
		Chunk* const chunk = new (std::nothrow) Chunk;
		if (chunk == nullptr) { return nullptr; }
		std::size_t initialized = 0;
		for (; initialized < chunk->items.size(); ++initialized)
		{
			int error = 0;
			Slot& slot = chunk->items[initialized];
			slot.state = createConverterState(mode, 2, &error);
			if (slot.state == nullptr) { break; }
			slot.mode = mode;
		}
		if (initialized == chunk->items.size()) { return chunk; }
		for (std::size_t i = 0; i < initialized; ++i) { src_delete(chunk->items[i].state); }
		delete chunk;
		return nullptr;
	}

	static void discard(Slot& slot) noexcept
	{
		if (slot.prepared != nullptr) { src_delete(slot.prepared); }
		slot.prepared = nullptr;
		slot.hasPrepared = false;
	}

	static void destroyChain(Chunk* chunk) noexcept
	{
		while (chunk != nullptr)
		{
			Chunk* const next = chunk->next;
			for (Slot& slot : chunk->items)
			{
				if (slot.prepared != nullptr) { src_delete(slot.prepared); }
				if (slot.state != nullptr) { src_delete(slot.state); }
			}
			delete chunk;
			chunk = next;
		}
	}

	auto appendChunks(std::size_t targetCapacity, AudioResampler::Mode mode) noexcept -> bool
	{
		if (targetCapacity <= m_capacity) { return true; }
		Chunk* pending = nullptr;
		Chunk* tail = nullptr;
		std::size_t added = 0;
		while (m_capacity + added * ChunkCapacity < targetCapacity)
		{
			Chunk* const chunk = createChunk(mode);
			if (chunk == nullptr)
			{
				destroyChain(pending);
				return false;
			}
			if (pending == nullptr) { pending = chunk; }
			else { tail->next = chunk; }
			tail = chunk;
			++added;
		}
		tail->next = m_head.load(std::memory_order_relaxed);
		m_head.store(pending, std::memory_order_release);
		m_capacity += added * ChunkCapacity;
		return true;
	}

	std::atomic<Chunk*> m_head{nullptr};
	std::size_t m_capacity = 0;
	AudioResampler::Mode m_mode = AudioResampler::Mode::Linear;
	std::atomic<bool> m_initialized{false};
};

auto realtimeVoiceConverterPool() -> RealtimeVoiceConverterPool&
{
	static RealtimeVoiceConverterPool pool;
	return pool;
}
} // namespace

AudioResampler::AudioResampler(Mode mode, ch_cnt_t channels)
	: m_state{createConverterState(mode, channels, &m_error)}
	, m_mode{mode}
	, m_channels{channels}
{
	if (channels <= 0) { throw std::logic_error{"Invalid channel count"}; }
	if (!m_state) { throw std::runtime_error{src_strerror(m_error)}; }
}

AudioResampler::AudioResampler(RealtimeVoiceTag, ch_cnt_t channels)
	: m_poolSlot{nullptr}
	, m_mode{Mode::Linear}
	, m_channels{channels}
{
	if (channels <= 0) { throw std::logic_error{"Invalid channel count"}; }
	if (channels != 2) { throw std::logic_error{"Realtime voice converter must be stereo"}; }
	m_poolSlot = realtimeVoiceConverterPool().acquire();
	if (m_poolSlot == nullptr) { throw std::runtime_error{"No prepared realtime voice converter is available"}; }
	m_mode = realtimeVoiceConverterPool().mode(m_poolSlot);
}

AudioResampler::~AudioResampler()
{
	if (m_poolSlot != nullptr) { realtimeVoiceConverterPool().release(m_poolSlot); }
}

AudioResampler::AudioResampler(AudioResampler&& other) noexcept
	: m_state{std::move(other.m_state)}
	, m_preparedState{std::move(other.m_preparedState)}
	, m_poolSlot{std::exchange(other.m_poolSlot, nullptr)}
	, m_mode{other.m_mode}
	, m_preparedMode{other.m_preparedMode}
	, m_hasPrepared{std::exchange(other.m_hasPrepared, false)}
	, m_channels{other.m_channels}
	, m_ratio{other.m_ratio}
	, m_error{other.m_error}
{
}

auto AudioResampler::operator=(AudioResampler&& other) noexcept -> AudioResampler&
{
	if (this == &other) { return *this; }
	if (m_poolSlot != nullptr) { realtimeVoiceConverterPool().release(m_poolSlot); }
	m_state = std::move(other.m_state);
	m_preparedState = std::move(other.m_preparedState);
	m_poolSlot = std::exchange(other.m_poolSlot, nullptr);
	m_mode = other.m_mode;
	m_preparedMode = other.m_preparedMode;
	m_hasPrepared = std::exchange(other.m_hasPrepared, false);
	m_channels = other.m_channels;
	m_ratio = other.m_ratio;
	m_error = other.m_error;
	return *this;
}

auto AudioResampler::currentState() const noexcept -> void*
{
	return m_poolSlot != nullptr ? realtimeVoiceConverterPool().state(m_poolSlot) : m_state.get();
}

auto AudioResampler::mode() const -> Mode
{
	return m_poolSlot != nullptr ? realtimeVoiceConverterPool().mode(m_poolSlot) : m_mode;
}

auto AudioResampler::hasPreparedMode() const noexcept -> bool
{
	return m_poolSlot != nullptr ? realtimeVoiceConverterPool().hasPrepared(m_poolSlot) : m_hasPrepared;
}

auto AudioResampler::process(InterleavedBufferView<const float> input, InterleavedBufferView<float> output) -> Result
{
	if (input.channels() != m_channels || output.channels() != m_channels)
	{
		throw std::invalid_argument{"Invalid channel count"};
	}

	auto data = SRC_DATA{};

	data.data_in = input.data();
	data.input_frames = input.frames();

	data.data_out = output.data();
	data.output_frames = output.frames();

	data.src_ratio = m_ratio;

	/*! THE CONVENTION, at the one place it can be got wrong.
	 *
	 *  `m_ratio` is output/input (`setRatio()`'s documented contract) and
	 *  libsamplerate's `src_ratio` is the SAME convention - measured, not
	 *  assumed: `SRC_DATA` with `src_ratio = 2.0` consumes 4096 input frames and
	 *  generates 8192 output frames (tests/src/core/AudioResamplerRatioTest.cpp).
	 *
	 *  DO NOT INVERT THIS LINE. Three prose sites in this tree asserted the
	 *  opposite and `docs/WARP.md` §3.1 called the mismatch-rate path a live
	 *  defect because of it; inverting here makes every 48 kHz source in a
	 *  44.1 kHz project play ~8.8 % fast and sharp. The test named above fails
	 *  if this line is inverted, which is the whole point of it.
	 */
	data.end_of_input = 0;

	if ((m_error = src_process(static_cast<SRC_STATE*>(currentState()), &data)))
	{
		throw std::runtime_error{src_strerror(m_error)};
	}

	return {static_cast<f_cnt_t>(data.input_frames_used), static_cast<f_cnt_t>(data.output_frames_gen)};
}

void AudioResampler::reset()
{
	if ((m_error = src_reset(static_cast<SRC_STATE*>(currentState()))))
	{
		throw std::runtime_error{src_strerror(m_error)};
	}
}

void AudioResampler::setMode(Mode mode)
{
	if (mode == this->mode()) { return; }
	if (m_poolSlot != nullptr)
	{
		if (!trySetMode(mode)) { throw std::runtime_error{"Unable to switch realtime voice converter"}; }
		return;
	}

	// A fresh converter. The requested quality is what a render's SRC setting
	// selects; the state has to be re-created because libsamplerate fixes the
	// converter at src_new() time.
	auto* state = createConverterState(mode, m_channels, &m_error);
	if (state == nullptr) { throw std::runtime_error{src_strerror(m_error)}; }

	m_state.reset(state);
	m_mode = mode;
}

auto AudioResampler::trySetMode(Mode mode) noexcept -> bool
{
	if (mode == this->mode()) { return true; }
	if (m_poolSlot != nullptr)
	{
		if (RealtimeScope::isActive()) { return false; }
		return realtimeVoiceConverterPool().trySetSlot(m_poolSlot, mode);
	}
	if (converterAllocationMustFailForTest()) { return false; }
	auto* const state = createConverterState(mode, m_channels, &m_error);
	if (state == nullptr) { return false; }
	m_state.reset(state);
	m_mode = mode;
	return true;
}

auto AudioResampler::prepareMode(Mode mode) noexcept -> bool
{
	if (m_poolSlot != nullptr)
	{
		return mode == this->mode() || realtimeVoiceConverterPool().prepareSlot(m_poolSlot, mode);
	}
	if (mode == m_mode)
	{
		m_preparedState.reset();
		m_preparedMode = mode;
		m_hasPrepared = true;
		return true;
	}
	if (converterAllocationMustFailForTest()) { return false; }
	auto* const state = createConverterState(mode, m_channels, &m_error);
	if (state == nullptr) { return false; }
	m_preparedState.reset(state);
	m_preparedMode = mode;
	m_hasPrepared = true;
	return true;
}

void AudioResampler::adoptPreparedMode() noexcept
{
	if (m_poolSlot != nullptr)
	{
		realtimeVoiceConverterPool().adoptSlot(m_poolSlot);
		return;
	}
	if (!m_hasPrepared) { return; }
	if (m_preparedState) { m_state.reset(m_preparedState.release()); }
	m_mode = m_preparedMode;
	m_hasPrepared = false;
}

void AudioResampler::discardPreparedMode() noexcept
{
	if (m_poolSlot != nullptr)
	{
		realtimeVoiceConverterPool().discardSlot(m_poolSlot);
		return;
	}
	m_preparedState.reset();
	m_hasPrepared = false;
}

auto AudioResampler::initializeRealtimeVoiceConverterPool(Mode mode, std::size_t capacity) noexcept -> bool
{
	return realtimeVoiceConverterPool().initialize(mode, capacity);
}

auto AudioResampler::ensureRealtimeVoiceConverterPoolCapacity(std::size_t capacity) noexcept -> bool
{
	return realtimeVoiceConverterPool().ensureCapacity(capacity);
}

auto AudioResampler::prepareRealtimeVoiceConverterPool(Mode mode) noexcept -> bool
{
	return realtimeVoiceConverterPool().prepareAll(mode);
}

void AudioResampler::adoptRealtimeVoiceConverterPool() noexcept
{
	realtimeVoiceConverterPool().adoptAll();
}

void AudioResampler::discardRealtimeVoiceConverterPool() noexcept
{
	realtimeVoiceConverterPool().discardAll();
}

#ifdef LMMS_TESTING
void AudioResampler::forceConverterAllocationFailuresForTest(int failuresToInject,
	int allocationsToSkip) noexcept
{
	s_allocationsBeforeFailure.store(failuresToInject > 0 ? allocationsToSkip : 0,
		std::memory_order_relaxed);
	s_failuresToInject.store(failuresToInject > 0 ? failuresToInject : 0,
		std::memory_order_relaxed);
}

auto AudioResampler::realtimeConverterAllocationsForTest() noexcept -> std::uint64_t
{
	return s_realtimeConverterAllocations.load(std::memory_order_relaxed);
}

void AudioResampler::resetRealtimeConverterAllocationsForTest() noexcept
{
	s_realtimeConverterAllocations.store(0, std::memory_order_relaxed);
}
#endif

auto AudioResampler::modeForSrcQuality(SrcQuality quality) noexcept -> Mode
{
	// The mapping is total and the default is the historical converter: a
	// caller that asks for nothing gets `Linear`, which is what every render
	// used before this setting existed.
	switch (quality)
	{
	case SrcQuality::SincFastest: return Mode::SincFastest;
	case SrcQuality::SincMedium:  return Mode::SincMedium;
	case SrcQuality::SincBest:    return Mode::SincBest;
	case SrcQuality::Linear:
	default:                      return Mode::Linear;
	}
}

auto AudioResampler::modeForCurrentSrcQuality() noexcept -> Mode
{
	return modeForSrcQuality(ExportRenderSettings::srcQuality());
}

void AudioResampler::StateDeleter::operator()(void* state)
{
	src_delete(static_cast<SRC_STATE*>(state));
}

} // namespace lmms
