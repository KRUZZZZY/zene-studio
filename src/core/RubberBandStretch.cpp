/*
 * RubberBandStretch.cpp - the optional Rubber Band time stretch (include/RubberBandStretch.h)
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
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include "RubberBandStretch.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#else
// Without the library the voice is never constructed (the pool holds none), but
// its unique_ptr member still needs a complete type to be destroyed.
namespace RubberBand
{
class RubberBandStretcher {};
} // namespace RubberBand
#endif

namespace lmms
{

namespace
{

//! Set by release() on any thread, taken by the recycler: "some voice is dirty".
std::atomic<bool> s_dirtyPending{false};

} // namespace

/*! The one thread that resets released voices, so the audio thread never pays
 *  for Rubber Band's allocating reset(). It polls a single atomic flag: a
 *  release is one store, never a lock or a wake-up syscall. The pool registry
 *  lock is taken only here and by pool construction/destruction (control
 *  thread), never by a claim or a release. */
class RubberBandRecycler
{
public:
	static RubberBandRecycler& instance()
	{
		static RubberBandRecycler recycler;
		return recycler;
	}

	void add(RubberBandPool* pool)
	{
		std::lock_guard lock(m_mutex);
		m_pools.push_back(pool);
		if (!m_thread.joinable()) { m_thread = std::thread([this] { run(); }); }
	}

	void remove(RubberBandPool* pool)
	{
		std::lock_guard lock(m_mutex);
		m_pools.erase(std::remove(m_pools.begin(), m_pools.end(), pool), m_pools.end());
	}

	void recycleAll()
	{
		s_dirtyPending.exchange(false, std::memory_order_acquire);
		std::lock_guard lock(m_mutex);
		for (RubberBandPool* pool : m_pools) { pool->recycleDirty(); }
	}

private:
	RubberBandRecycler() = default;
	~RubberBandRecycler()
	{
		{
			std::lock_guard lock(m_mutex);
			m_stop = true;
		}
		m_wake.notify_all();
		if (m_thread.joinable()) { m_thread.join(); }
	}

	void run()
	{
		std::unique_lock lock(m_mutex);
		while (!m_stop)
		{
			m_wake.wait_for(lock, std::chrono::milliseconds(4));
			if (m_stop) { break; }
			if (!s_dirtyPending.exchange(false, std::memory_order_acquire)) { continue; }
			for (RubberBandPool* pool : m_pools) { pool->recycleDirty(); }
		}
	}

	std::mutex m_mutex;
	std::condition_variable m_wake;
	std::vector<RubberBandPool*> m_pools;
	std::thread m_thread;
	bool m_stop = false;
};




RubberBandVoice::RubberBandVoice(int sourceRate)
{
#ifdef LMMS_HAVE_RUBBERBAND
	using RB = RubberBand::RubberBandStretcher;
	// R3 ("finer") in real-time mode: the engine whose steady state was measured
	// allocation-free. Channels together keeps the stereo image; the rate given
	// is the source's, which only tunes the engine's frequency bands.
	m_stretcher = std::make_unique<RB>(static_cast<size_t>(std::max(sourceRate, 1)), 2,
		RB::OptionProcessRealTime | RB::OptionEngineFiner | RB::OptionChannelsTogether);
	m_stretcher->setMaxProcessSize(MaxBlock);
#else
	(void) sourceRate;
#endif
}

RubberBandVoice::~RubberBandVoice() = default;

void RubberBandVoice::begin(double pitchScale) noexcept
{
	m_pitchScale = pitchScale > 0.0 ? pitchScale : 1.0;
	m_started = false;
	m_sourcePos = 0.0;
	m_feedPos = 0;
	m_toDiscard = 0;
}

void RubberBandVoice::start(double timeRatio) noexcept
{
#ifdef LMMS_HAVE_RUBBERBAND
	m_stretcher->setPitchScale(m_pitchScale);
	m_stretcher->setTimeRatio(timeRatio);
	m_timeRatio = timeRatio;
	// The documented real-time start: the preferred pad of silence goes in, and
	// the start delay's worth of output is dropped, so output frame 0 is source
	// frame 0 and a Rubber Band clip lands on the same timeline as a WSOLA one.
	std::fill(m_inLeft.begin(), m_inLeft.end(), 0.0f);
	std::fill(m_inRight.begin(), m_inRight.end(), 0.0f);
	const float* in[2] = { m_inLeft.data(), m_inRight.data() };
	auto pad = static_cast<std::int64_t>(m_stretcher->getPreferredStartPad());
	while (pad > 0)
	{
		const auto n = static_cast<size_t>(std::min<std::int64_t>(pad, MaxBlock));
		m_stretcher->process(in, n, false);
		pad -= static_cast<std::int64_t>(n);
	}
	m_toDiscard = static_cast<std::int64_t>(m_stretcher->getStartDelay());
#else
	(void) timeRatio;
#endif
	m_started = true;
}

void RubberBandVoice::feed(const SampleFrame* src, f_cnt_t srcFrames, int frames) noexcept
{
#ifdef LMMS_HAVE_RUBBERBAND
	for (int i = 0; i < frames; ++i)
	{
		const std::int64_t at = m_feedPos + i;
		const bool inside = at >= 0 && at < static_cast<std::int64_t>(srcFrames);
		m_inLeft[static_cast<std::size_t>(i)] = inside ? src[at].left() : 0.0f;
		m_inRight[static_cast<std::size_t>(i)] = inside ? src[at].right() : 0.0f;
	}
	const float* in[2] = { m_inLeft.data(), m_inRight.data() };
	m_stretcher->process(in, static_cast<size_t>(frames), false);
	m_feedPos += frames;
#else
	(void) src; (void) srcFrames; (void) frames;
#endif
}

f_cnt_t RubberBandVoice::render(const SampleFrame* src, f_cnt_t srcFrames, SampleFrame* dst,
	f_cnt_t dstFrames, double speed) noexcept
{
#ifdef LMMS_HAVE_RUBBERBAND
	if (!m_stretcher) { return 0; }
	const double timeRatio = speed > 0.0 ? 1.0 / speed : 1.0;
	if (!m_started) { start(timeRatio); }
	else if (timeRatio != m_timeRatio)
	{
		m_stretcher->setTimeRatio(timeRatio);
		m_timeRatio = timeRatio;
	}

	f_cnt_t produced = 0;
	// A safety net against a stretcher that never yields, not a bound on the
	// render: at the fastest warp a period needs a few dozen feeds.
	int feeds = 0;
	while (produced < dstFrames && feeds < 4096)
	{
		const int available = m_stretcher->available();
		if (available > 0)
		{
			produced += drain(available, dst + produced, dstFrames - produced);
			continue;
		}
		const size_t required = m_stretcher->getSamplesRequired();
		feed(src, srcFrames, static_cast<int>(std::clamp<size_t>(required, 64, MaxBlock)));
		++feeds;
	}
	m_sourcePos += static_cast<double>(produced) * speed;
	return produced;
#else
	(void) src; (void) srcFrames; (void) dst; (void) dstFrames; (void) speed;
	return 0;
#endif
}

f_cnt_t RubberBandVoice::drain(int available, SampleFrame* dst, f_cnt_t room) noexcept
{
#ifdef LMMS_HAVE_RUBBERBAND
	float* out[2] = { m_outLeft.data(), m_outRight.data() };
	// The start delay is output that precedes source frame 0: retrieved and dropped.
	if (m_toDiscard > 0)
	{
		const auto n = static_cast<size_t>(std::min<std::int64_t>(
			std::min<std::int64_t>(available, m_toDiscard), MaxBlock));
		m_stretcher->retrieve(out, n);
		m_toDiscard -= static_cast<std::int64_t>(n);
		return 0;
	}
	const auto n = static_cast<f_cnt_t>(std::min<std::int64_t>(
		std::min<std::int64_t>(available, room), MaxBlock));
	m_stretcher->retrieve(out, n);
	for (f_cnt_t i = 0; i < n; ++i) { dst[i] = SampleFrame(m_outLeft[i], m_outRight[i]); }
	return n;
#else
	(void) available; (void) dst; (void) room;
	return 0;
#endif
}

void RubberBandVoice::release() noexcept
{
	m_state.store(Dirty, std::memory_order_release);
	s_dirtyPending.store(true, std::memory_order_release);
}




bool RubberBandPool::available()
{
#ifdef LMMS_HAVE_RUBBERBAND
	return true;
#else
	return false;
#endif
}

RubberBandPool::RubberBandPool(int sourceRate)
{
	if (!available()) { return; }
	for (auto& voice : m_voices) { voice = std::make_unique<RubberBandVoice>(sourceRate); }
	RubberBandRecycler::instance().add(this);
}

RubberBandPool::~RubberBandPool()
{
	if (available()) { RubberBandRecycler::instance().remove(this); }
}

RubberBandVoice* RubberBandPool::claim() noexcept
{
	for (auto& voice : m_voices)
	{
		int expected = RubberBandVoice::Ready;
		if (voice && voice->m_state.compare_exchange_strong(expected, RubberBandVoice::InUse,
			std::memory_order_acq_rel))
		{
			return voice.get();
		}
	}
	m_misses.fetch_add(1, std::memory_order_relaxed);
	return nullptr;
}

int RubberBandPool::readyVoices() const noexcept
{
	int ready = 0;
	for (const auto& voice : m_voices)
	{
		ready += voice && voice->m_state.load(std::memory_order_acquire) == RubberBandVoice::Ready;
	}
	return ready;
}

void RubberBandPool::recycleNow()
{
	if (available()) { RubberBandRecycler::instance().recycleAll(); }
}

void RubberBandPool::recycleDirty()
{
#ifdef LMMS_HAVE_RUBBERBAND
	for (auto& voice : m_voices)
	{
		if (voice && voice->m_state.load(std::memory_order_acquire) == RubberBandVoice::Dirty)
		{
			voice->m_stretcher->reset();
			voice->m_state.store(RubberBandVoice::Ready, std::memory_order_release);
		}
	}
#endif
}

} // namespace lmms
