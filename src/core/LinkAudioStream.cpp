/*
 * LinkAudioStream.cpp - the PCM stream between two Zene instances
 *                       (include/LinkAudioStream.h, docs/LINK-AUDIO.md).
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

#include "LinkAudioStream.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

#include "SampleFrame.h"


namespace lmms::link
{

namespace
{

static_assert(std::is_same_v<sample_t, float>, "the wire format is float32");

//! Silence inserted for a sequence gap is capped, so a long outage cannot flood the ring.
constexpr std::uint32_t MaxGapPackets = 8;
//! A sequence jump further than this is a restarted peer, not loss: resynchronise.
constexpr std::int32_t ResyncDistance = 64;
constexpr std::size_t ScratchFrames = 2048;
//! Frames the audio thread converts at a time (its preallocated buffer); the period may be longer.
constexpr std::size_t TxChunkFrames = 1024;

void raiseMax(std::atomic<std::size_t>& slot, std::size_t value)
{
	// Single writer (the audio thread), so a plain load/compare/store is exact.
	if (value > slot.load(std::memory_order_relaxed)) { slot.store(value, std::memory_order_relaxed); }
}

} // namespace

// ---- LinkAudioStreamSeam -------------------------------------------------

std::atomic<bool> LinkAudioStreamSeam::s_sendHold{false};
std::atomic<bool> LinkAudioStreamSeam::s_publishHold{false};
std::atomic<std::uint64_t> LinkAudioStreamSeam::s_sendHolds{0};
std::atomic<std::uint64_t> LinkAudioStreamSeam::s_publishHolds{0};
std::mutex LinkAudioStreamSeam::s_mutex;
std::condition_variable LinkAudioStreamSeam::s_cond;

void LinkAudioStreamSeam::setSendHold(bool on)
{
	{
		const std::lock_guard<std::mutex> lock(s_mutex);
		s_sendHold.store(on, std::memory_order_relaxed);
	}
	s_cond.notify_all();
}

void LinkAudioStreamSeam::setPublishHold(bool on)
{
	{
		const std::lock_guard<std::mutex> lock(s_mutex);
		s_publishHold.store(on, std::memory_order_relaxed);
	}
	s_cond.notify_all();
}

std::uint64_t LinkAudioStreamSeam::sendHolds() { return s_sendHolds.load(std::memory_order_relaxed); }
std::uint64_t LinkAudioStreamSeam::publishHolds() { return s_publishHolds.load(std::memory_order_relaxed); }

//! The I/O thread waits while the hold is on. The counter tells the test that the thread has
//! reached the hold, so the test knows that the I/O thread will not pass it until it lets go.
void LinkAudioStreamSeam::waitAt(std::atomic<bool>& hold, std::atomic<std::uint64_t>& counter)
{
	if (!hold.load(std::memory_order_relaxed)) { return; }   // production: no lock, no wait
	std::unique_lock<std::mutex> lock(s_mutex);
	counter.fetch_add(1, std::memory_order_relaxed);
	s_cond.wait(lock, [&hold] { return !hold.load(std::memory_order_relaxed); });
}

void LinkAudioStreamSeam::waitAtSendHold() { waitAt(s_sendHold, s_sendHolds); }
void LinkAudioStreamSeam::waitAtPublishHold() { waitAt(s_publishHold, s_publishHolds); }

// ---- LinkAudioStream ----------------------------------------------------

std::atomic<LinkAudioStream*> LinkAudioStream::s_active{nullptr};
LinkAudioStream* LinkAudioStream::s_engineTapped = nullptr;

LinkAudioStream::LinkAudioStream()
	: m_tx(AudioRingFrames)
	, m_rx(AudioRingFrames)
	, m_scratch(ScratchFrames * AudioChannels, 0.0f)
	, m_txBuf(TxChunkFrames * AudioChannels, 0.0f)
	, m_packet(AudioMaxPacketBytes + 64, 0)
	, m_samples(AudioMaxPacketFrames * AudioChannels, 0.0f)
{
}

LinkAudioStream::~LinkAudioStream()
{
	stopLocked();
}

LinkAudioStream* LinkAudioStream::instance()
{
	static LinkAudioStream stream;
	s_engineTapped = &stream;
	return &stream;
}

AudioStreamConfig LinkAudioStream::config() const
{
	return m_config;
}

AudioStreamReport LinkAudioStream::report() const
{
	AudioStreamReport r;
	r.running = m_running.load(std::memory_order_relaxed);
	r.error = m_error;
	r.boundAddress = m_boundAddress;
	r.boundPort = m_boundPort;
	r.packetsSent = m_packetsSent.load(std::memory_order_relaxed);
	r.packetsReceived = m_packetsReceived.load(std::memory_order_relaxed);
	r.framesSent = m_framesSent.load(std::memory_order_relaxed);
	r.framesReceived = m_framesReceived.load(std::memory_order_relaxed);
	r.underruns = m_underruns.load(std::memory_order_relaxed);
	r.trims = m_trims.load(std::memory_order_relaxed);
	r.txOverruns = m_txOverruns.load(std::memory_order_relaxed);
	r.rxOverruns = m_rxOverruns.load(std::memory_order_relaxed);
	r.lostPackets = m_lost.load(std::memory_order_relaxed);
	r.reorderedPackets = m_reordered.load(std::memory_order_relaxed);
	r.malformedPackets = m_malformed.load(std::memory_order_relaxed);
	r.rateMismatches = m_rateMismatch.load(std::memory_order_relaxed);
	r.sendErrors = m_sendErrors.load(std::memory_order_relaxed);
	r.periods = m_periods.load(std::memory_order_relaxed);
	r.sampleRate = m_rate.load(std::memory_order_relaxed);
	const double perMs = r.sampleRate > 0 ? r.sampleRate / 1000.0 : 0.0;
	if (perMs > 0.0)
	{
		r.latencyMs = static_cast<double>(m_latencyFrames.load(std::memory_order_relaxed)) / perMs;
		r.latencyMaxMs = static_cast<double>(m_latencyMaxFrames.load(std::memory_order_relaxed)) / perMs;
	}
	r.primed = m_primedReport.load(std::memory_order_relaxed);
	return r;
}

void LinkAudioStream::process(SampleFrame* frames, f_cnt_t count, sample_rate_t sampleRate) noexcept
{
	m_periods.fetch_add(1, std::memory_order_relaxed);
	if (m_rate.load(std::memory_order_relaxed) != sampleRate) { onRateChanged(sampleRate); }

	if (m_sendOn.load(std::memory_order_relaxed)) { sendFrames(frames, count); }
	if (m_receiveOn.load(std::memory_order_relaxed)) { receivePeriod(frames, static_cast<std::size_t>(count), sampleRate); }
}

//! The master's frames go into the TX ring as interleaved floats, read channel by channel:
//! an array of SampleFrame objects is not an array of floats, and reading it as one is
//! undefined behaviour (the size of the frame says nothing about the type of its bytes).
void LinkAudioStream::sendFrames(SampleFrame* frames, f_cnt_t count) noexcept
{
	// The sample rate changed: the I/O thread has not dropped the old-rate frames yet, and new
	// frames must not be mixed with them.
	if (m_txFlushedEpoch.load(std::memory_order_acquire) < m_txRateEpoch) { return; }
	std::size_t written = 0;
	for (f_cnt_t done = 0; done < count;)
	{
		const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(count - done), TxChunkFrames);
		for (std::size_t i = 0; i < n; ++i)
		{
			m_txBuf[i * AudioChannels] = frames[done + i].left();
			m_txBuf[i * AudioChannels + 1] = frames[done + i].right();
		}
		written += m_tx.write(m_txBuf.data(), n);
		done += static_cast<f_cnt_t>(n);
	}
	if (written < static_cast<std::size_t>(count)) { m_txOverruns.fetch_add(1, std::memory_order_relaxed); }
}

void LinkAudioStream::addFromRing(SampleFrame* frames, std::size_t period) noexcept
{
	for (std::size_t done = 0; done < period;)
	{
		const std::size_t n = std::min(period - done, ScratchFrames);
		m_rx.read(m_scratch.data(), n);
		for (std::size_t i = 0; i < n; ++i)
		{
			frames[done + i].left() += m_scratch[i * AudioChannels];
			frames[done + i].right() += m_scratch[i * AudioChannels + 1];
		}
		done += n;
	}
}

void LinkAudioStream::receivePeriod(SampleFrame* frames, std::size_t period, sample_rate_t sampleRate) noexcept
{
	const std::uint64_t epoch = m_epoch.load(std::memory_order_acquire);
	if (epoch != m_seenEpoch)
	{
		m_rx.skip(m_rx.fill());
		m_primed = false;
		m_seenEpoch = epoch;
	}
	const std::size_t wanted = static_cast<std::size_t>(m_latencyMs.load(std::memory_order_relaxed))
		* sampleRate / 1000;
	// A sample rate change: the I/O thread may still publish frames of the old rate (it
	// validated them before the change). Until it has drained the old rate, the audio thread
	// drops what the ring holds and stays silent - the old frames must not be played at the
	// new rate.
	if (m_rxFlushedEpoch.load(std::memory_order_acquire) < m_rxRateEpoch)
	{
		m_rx.skip(m_rx.fill());
		m_primed = false;
		m_latencyFrames.store(0, std::memory_order_relaxed);
		m_primedReport.store(false, std::memory_order_relaxed);
		return;
	}
	// The transition is over: the frames the I/O thread published before it drained the old
	// rate are still in the ring, and they carry the old rate. Drop them once more, then the
	// jitter buffer starts again.
	if (m_rxDrainedEpoch != m_rxRateEpoch)
	{
		m_rx.skip(m_rx.fill());
		m_rxDrainedEpoch = m_rxRateEpoch;
		m_primed = false;
	}
	// At least two periods (so a trim never leaves less than one), at most a quarter of the ring.
	const std::size_t target = std::clamp(wanted, period * 2, m_rx.capacity() / 4);
	std::size_t fill = m_rx.fill();
	m_primed = m_primed || fill >= target;
	if (m_primed && fill > target * 2 + period)
	{
		m_rx.skip(fill - target);
		m_trims.fetch_add(1, std::memory_order_relaxed);
		fill = target;
	}
	if (m_primed && fill < period)
	{
		m_underruns.fetch_add(1, std::memory_order_relaxed);
		m_primed = false;
	}
	if (m_primed) { addFromRing(frames, period); }
	const std::size_t left = m_rx.fill();
	m_latencyFrames.store(left, std::memory_order_relaxed);
	raiseMax(m_latencyMaxFrames, left);
	m_primedReport.store(m_primed, std::memory_order_relaxed);
}

//! The engine's sample rate changed (the audio device was switched). The frames queued in the
//! rings were captured at the old rate: the RX frames must not be played at the new one, and
//! the TX frames must not go out with the new rate in their header - the peer would play
//! old-rate samples at the new rate. Both rings are flushed before anything new is processed.
void LinkAudioStream::onRateChanged(std::uint32_t rate) noexcept
{
	const std::uint32_t old = m_rate.load(std::memory_order_relaxed);
	m_rate.store(rate, std::memory_order_relaxed);
	if (old != 0 && old != rate)
	{
		++m_txRateEpoch;                 // the TX frames belong to the old rate
		++m_rxRateEpoch;                 // the frames in the RX ring belong to the old rate
		// The new epoch travels with the new rate: the I/O thread validates a packet against
		// the pair, so the rate it checks and the epoch it publishes always belong together.
		m_rxStamp.store((m_rxRateEpoch << 32) | rate, std::memory_order_release);
		// The old-rate RX frames are dropped here (the audio thread is that ring's consumer)
		// and the jitter buffer starts again. The I/O thread may still publish a frame of the
		// old rate - it validated it before the change - so the audio thread keeps dropping
		// what the ring holds and stays silent until the I/O thread has drained the old rate.
		m_rx.skip(m_rx.fill());
		m_primed = false;
	}
	else
	{
		m_rxStamp.store((m_rxRateEpoch << 32) | rate, std::memory_order_release);
	}
	m_txStamp.store((m_txRateEpoch << 32) | rate, std::memory_order_release);
}

//! The rate the next frames go out with. The rate is read before the frames are taken from the
//! ring, and the epoch it belongs to is flushed first: a packet never carries a rate that is
//! not the rate of the frames in it.
std::uint32_t LinkAudioStream::txRateForNextFrames() noexcept
{
	const std::uint64_t stamp = m_txStamp.load(std::memory_order_acquire);
	const std::uint64_t epoch = stamp >> 32;
	if (epoch != m_seenTxEpoch)
	{
		m_tx.skip(m_tx.fill());         // the queued frames belong to the old rate
		m_seenTxEpoch = epoch;
		m_txFlushedEpoch.store(epoch, std::memory_order_release);
	}
	return static_cast<std::uint32_t>(stamp & 0xffffffffu);
}

//! The silence that replaces a sequence gap. The frames that do not fit into the RX ring are
//! dropped, and they are an overrun like any other frame the ring could not take.
void LinkAudioStream::insertGapSilence(std::size_t frames) noexcept
{
	const std::size_t silenced = m_rx.writeSilence(frames);
	if (silenced < frames) { m_rxOverruns.fetch_add(frames - silenced, std::memory_order_relaxed); }
}

//! The sequence of the packets: a gap is replaced by silence, a late packet is dropped, and a
//! peer that restarted (a jump further than ResyncDistance) is picked up again.
bool LinkAudioStream::acceptSequence(const AudioPacketHeader& header) noexcept
{
	std::int32_t distance = 0;
	if (m_haveRx)
	{
		distance = static_cast<std::int32_t>(header.sequence - m_expectedRx);
	}
	if (!m_haveRx || distance > ResyncDistance || distance < -ResyncDistance)
	{
		m_haveRx = true;
		m_expectedRx = header.sequence;
		distance = 0;
	}
	if (distance < 0)
	{
		m_reordered.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	if (distance > 0)
	{
		m_lost.fetch_add(static_cast<std::uint64_t>(distance), std::memory_order_relaxed);
		const std::uint32_t gap = std::min<std::uint32_t>(static_cast<std::uint32_t>(distance), MaxGapPackets);
		insertGapSilence(static_cast<std::size_t>(gap) * header.frames);
	}
	m_expectedRx = header.sequence + 1;
	return true;
}

void LinkAudioStream::acceptPacket(const AudioPacketHeader& header, const float* samples)
{
	// The rate and the epoch of the RX ring travel together: the rate this packet is checked
	// against is the rate of the frames that are in the ring.
	const std::uint64_t stamp = m_rxStamp.load(std::memory_order_acquire);
	const std::uint32_t rate = static_cast<std::uint32_t>(stamp & 0xffffffffu);
	if (rate == 0 || header.sampleRate != rate)
	{
		m_rateMismatch.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	// The window between the validation of a packet and its publication: a sample rate
	// change happens here, and the packet then carries the old rate.
	LinkAudioStreamSeam::waitAtPublishHold();
	// The rate changed while this packet was validated: it carries the old rate, and the
	// audio thread has already dropped the old-rate frames, so it must not reach the ring.
	if ((m_rxStamp.load(std::memory_order_acquire) >> 32) != (stamp >> 32))
	{
		m_rateMismatch.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	if (!acceptSequence(header)) { return; }
	const std::size_t written = m_rx.write(samples, header.frames);
	if (written < header.frames)
	{
		m_rxOverruns.fetch_add(header.frames - written, std::memory_order_relaxed);
	}
	m_packetsReceived.fetch_add(1, std::memory_order_relaxed);
	m_framesReceived.fetch_add(written, std::memory_order_relaxed);
}

void LinkAudioStream::acknowledgeRxRate() noexcept
{
	const std::uint64_t stamp = m_rxStamp.load(std::memory_order_acquire);
	const std::uint64_t epoch = stamp >> 32;
	if (epoch == m_seenRxEpoch) { return; }
	m_seenRxEpoch = epoch;
	m_haveRx = false;                 // the sequence stream of the old rate is over
	m_rxFlushedEpoch.store(epoch, std::memory_order_release);
}


namespace
{

std::string rangeRefusal(const AudioStreamConfig& c)
{
	if (c.latencyMs < MinAudioLatencyMs || c.latencyMs > MaxAudioLatencyMs)
	{
		return "latency_ms must be " + std::to_string(MinAudioLatencyMs) + ".." + std::to_string(MaxAudioLatencyMs);
	}
	if (c.listenPort < 0 || c.listenPort > 65535 || c.peerPort < 1 || c.peerPort > 65535)
	{
		return "ports must be 0..65535 (listen_port) and 1..65535 (peer_port)";
	}
	return {};
}

//! Address literals are checked BEFORE anything is stopped, so a typo changes nothing.
std::string addressRefusal(const AudioStreamConfig& c)
{
	std::uint32_t literal = 0;
	if (!parseIpv4(c.bindAddress, literal))
	{
		return "bind_address '" + c.bindAddress + "' is not an IPv4 address (no name lookup is done)";
	}
	if (!c.peerAddress.empty() && !parseIpv4(c.peerAddress, literal))
	{
		return "peer '" + c.peerAddress + "' is not an IPv4 address (no name lookup is done)";
	}
	return {};
}

//! Why \a c cannot be applied, or an empty string. Pure: nothing is stopped for a refusal.
std::string refusalOf(const AudioStreamConfig& c)
{
	std::string why = rangeRefusal(c);
	if (!why.empty() || !c.enabled) { return why; }
	if (!c.send && !c.receive) { return "enable at least one of send and receive"; }
	if (c.send && c.peerAddress.empty()) { return "sending needs a peer address (an IPv4 literal)"; }
	return addressRefusal(c);
}

} // namespace

void LinkAudioStream::resetCounters()
{
	for (auto* counter : {&m_packetsSent, &m_packetsReceived, &m_framesSent, &m_framesReceived, &m_underruns,
			 &m_trims, &m_txOverruns, &m_rxOverruns, &m_lost, &m_reordered, &m_malformed, &m_rateMismatch,
			 &m_sendErrors, &m_periods})
	{
		counter->store(0, std::memory_order_relaxed);
	}
	m_latencyFrames.store(0, std::memory_order_relaxed);
	m_latencyMaxFrames.store(0, std::memory_order_relaxed);
}

void LinkAudioStream::publish(const AudioStreamConfig& cfg)
{
	// Nothing else touches the TX ring now (the I/O thread is joined, the audio thread no
	// longer reaches us), so this thread may act as its consumer; the RX ring is flushed by
	// the audio thread on the epoch.
	m_tx.skip(m_tx.fill());
	resetCounters();
	m_haveRx = false;
	m_nextSequence = 0;
	m_latencyMs.store(cfg.latencyMs, std::memory_order_relaxed);
	m_epoch.fetch_add(1, std::memory_order_release);
	m_stop.store(false, std::memory_order_release);
	m_sendOn.store(cfg.send, std::memory_order_relaxed);
	m_receiveOn.store(cfg.receive, std::memory_order_relaxed);
	m_thread = std::thread([this] { ioLoop(); });
	m_running.store(true, std::memory_order_relaxed);
	// Only the process-wide instance is tapped into the engine's master mix; other instances
	// (the tests') are driven by calling process() themselves.
	if (this == s_engineTapped) { s_active.store(this, std::memory_order_release); }
}

bool LinkAudioStream::configure(const AudioStreamConfig& requested, std::string* error)
{
	const std::string refusal = refusalOf(requested);
	if (!refusal.empty())
	{
		if (error != nullptr) { *error = refusal; }
		return false;
	}
	const AudioStreamConfig previous = m_config;
	stopLocked();
	m_error.clear();
	m_boundAddress.clear();
	m_boundPort = 0;
	m_config = requested;
	if (!requested.enabled) { return true; }

	std::string reason;
	if (!openSocket(requested, &reason))
	{
		// The OS refused (port in use, no such interface): put the previous stream back.
		m_config = previous;
		if (previous.enabled && !m_restoring)
		{
			m_restoring = true;
			std::string ignored;
			configure(previous, &ignored);
			m_restoring = false;
		}
		m_error = reason;
		if (error != nullptr) { *error = reason; }
		return false;
	}
	publish(requested);
	return true;
}

} // namespace lmms::link
