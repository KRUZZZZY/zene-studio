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

#ifndef _WIN32
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lmms::link
{

namespace
{

static_assert(sizeof(SampleFrame) == AudioChannels * sizeof(float),
	"the stream reads SampleFrame arrays as interleaved stereo floats");
static_assert(std::is_same_v<sample_t, float>, "the wire format is float32");

//! Silence inserted for a sequence gap is capped, so a long outage cannot flood the ring.
constexpr std::uint32_t MaxGapPackets = 8;
//! A sequence jump further than this is a restarted peer, not loss: resynchronise.
constexpr std::int32_t ResyncDistance = 64;
constexpr std::size_t ScratchFrames = 2048;
constexpr int IoPollMs = 2;

void raiseMax(std::atomic<std::size_t>& slot, std::size_t value)
{
	// Single writer (the audio thread), so a plain load/compare/store is exact.
	if (value > slot.load(std::memory_order_relaxed)) { slot.store(value, std::memory_order_relaxed); }
}

} // namespace

// ---- LinkAudioStream ----------------------------------------------------

std::atomic<LinkAudioStream*> LinkAudioStream::s_active{nullptr};
LinkAudioStream* LinkAudioStream::s_engineTapped = nullptr;

LinkAudioStream::LinkAudioStream()
	: m_tx(AudioRingFrames)
	, m_rx(AudioRingFrames)
	, m_scratch(ScratchFrames * AudioChannels, 0.0f)
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
	if (m_rate.load(std::memory_order_relaxed) != sampleRate) { m_rate.store(sampleRate, std::memory_order_relaxed); }
	float* interleaved = reinterpret_cast<float*>(frames);

	if (m_sendOn.load(std::memory_order_relaxed) && m_tx.write(interleaved, static_cast<std::size_t>(count)) < count)
	{
		m_txOverruns.fetch_add(1, std::memory_order_relaxed);
	}
	if (m_receiveOn.load(std::memory_order_relaxed)) { receivePeriod(interleaved, static_cast<std::size_t>(count), sampleRate); }
}

void LinkAudioStream::addFromRing(float* interleaved, std::size_t period) noexcept
{
	for (std::size_t done = 0; done < period;)
	{
		const std::size_t n = std::min(period - done, ScratchFrames);
		m_rx.read(m_scratch.data(), n);
		float* dst = interleaved + done * AudioChannels;
		for (std::size_t i = 0; i < n * AudioChannels; ++i) { dst[i] += m_scratch[i]; }
		done += n;
	}
}

void LinkAudioStream::receivePeriod(float* interleaved, std::size_t period, sample_rate_t sampleRate) noexcept
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
	if (m_primed) { addFromRing(interleaved, period); }
	const std::size_t left = m_rx.fill();
	m_latencyFrames.store(left, std::memory_order_relaxed);
	raiseMax(m_latencyMaxFrames, left);
	m_primedReport.store(m_primed, std::memory_order_relaxed);
}

void LinkAudioStream::acceptPacket(const AudioPacketHeader& header, const float* samples)
{
	const std::uint32_t rate = m_rate.load(std::memory_order_relaxed);
	if (rate == 0 || header.sampleRate != rate)
	{
		m_rateMismatch.fetch_add(1, std::memory_order_relaxed);
		return;
	}
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
		return;
	}
	if (distance > 0)
	{
		m_lost.fetch_add(static_cast<std::uint64_t>(distance), std::memory_order_relaxed);
		const std::uint32_t gap = std::min<std::uint32_t>(static_cast<std::uint32_t>(distance), MaxGapPackets);
		m_rx.writeSilence(static_cast<std::size_t>(gap) * header.frames);
	}
	m_expectedRx = header.sequence + 1;
	const std::size_t written = m_rx.write(samples, header.frames);
	if (written < header.frames)
	{
		m_rxOverruns.fetch_add(header.frames - written, std::memory_order_relaxed);
	}
	m_packetsReceived.fetch_add(1, std::memory_order_relaxed);
	m_framesReceived.fetch_add(written, std::memory_order_relaxed);
}

#ifdef _WIN32

bool LinkAudioStream::openSocket(const AudioStreamConfig&, std::string* error)
{
	// Same stated gap as the zene-link-style fallback (docs/LINK-SYNC.md section 3.2): the
	// stream's pure parts compile and are tested everywhere, the socket is POSIX for now.
	if (error != nullptr) { *error = "the Link audio stream's socket is not implemented on Windows yet"; }
	return false;
}
void LinkAudioStream::ioLoop() {}
void LinkAudioStream::receiveDatagrams() {}
void LinkAudioStream::sendPending() {}

#else

namespace
{

bool failWith(std::string* error, const std::string& what)
{
	if (error != nullptr) { *error = what + ": " + std::strerror(errno); }
	return false;
}

} // namespace

bool LinkAudioStream::openSocket(const AudioStreamConfig& cfg, std::string* error)
{
	std::uint32_t bindAddress = 0;
	parseIpv4(cfg.bindAddress, bindAddress);   // configure() validated both literals
	if (!cfg.peerAddress.empty()) { parseIpv4(cfg.peerAddress, m_peerAddress); }
	const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) { return failWith(error, "socket()"); }
	const int flags = ::fcntl(fd, F_GETFL, 0);
	sockaddr_in local{};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(bindAddress);
	local.sin_port = htons(static_cast<std::uint16_t>(cfg.receive ? cfg.listenPort : 0));
	if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
		::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0)
	{
		const std::string what = flags < 0 ? "fcntl()" : "bind " + cfg.bindAddress + ":" + std::to_string(cfg.listenPort);
		::close(fd);
		m_peerAddress = 0;
		return failWith(error, what);
	}
	socklen_t length = sizeof(local);
	::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &length);
	m_boundAddress = cfg.bindAddress;
	m_boundPort = ntohs(local.sin_port);
	m_fd = fd;
	m_peerPort = cfg.peerPort;
	return true;
}

void LinkAudioStream::receiveDatagrams()
{
	for (;;)
	{
		sockaddr_in from{};
		socklen_t fromLength = sizeof(from);
		const ssize_t got = ::recvfrom(m_fd, m_packet.data(), m_packet.size(), 0,
			reinterpret_cast<sockaddr*>(&from), &fromLength);
		if (got < 0) { return; }  // EAGAIN: drained (or a transient error: poll again)
		if (m_peerAddress != 0 && ntohl(from.sin_addr.s_addr) != m_peerAddress)
		{
			m_malformed.fetch_add(1, std::memory_order_relaxed);  // from a stranger
			continue;
		}
		AudioPacketHeader header;
		if (!decodeAudioPacket(m_packet.data(), static_cast<std::size_t>(got), header,
				m_samples.data(), m_samples.size()))
		{
			m_malformed.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		acceptPacket(header, m_samples.data());
	}
}

void LinkAudioStream::sendPending()
{
	sockaddr_in to{};
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = htonl(m_peerAddress);
	to.sin_port = htons(static_cast<std::uint16_t>(m_peerPort));
	for (;;)
	{
		const std::size_t available = m_tx.fill();
		if (available == 0) { return; }
		const std::size_t n = m_tx.read(m_samples.data(), std::min(available, AudioMaxPacketFrames));
		const std::uint32_t rate = m_rate.load(std::memory_order_relaxed);
		if (m_peerAddress == 0 || rate == 0 || n == 0) { continue; }  // nowhere to send: drained and dropped
		AudioPacketHeader header;
		header.sequence = m_nextSequence++;
		header.sampleRate = rate;
		header.frames = static_cast<std::uint16_t>(n);
		const std::size_t bytes = encodeAudioPacket(header, m_samples.data(), m_packet.data(), m_packet.size());
		const ssize_t sent = bytes == 0 ? -1
			: ::sendto(m_fd, m_packet.data(), bytes, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
		if (sent < 0)
		{
			m_sendErrors.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		m_packetsSent.fetch_add(1, std::memory_order_relaxed);
		m_framesSent.fetch_add(n, std::memory_order_relaxed);
	}
}

void LinkAudioStream::ioLoop()
{
	while (!m_stop.load(std::memory_order_acquire))
	{
		pollfd wait{m_fd, POLLIN, 0};
		::poll(&wait, 1, IoPollMs);
		receiveDatagrams();
		sendPending();
	}
}

#endif // _WIN32

void LinkAudioStream::stopLocked()
{
	// Order matters: the audio thread stops reaching us first, then the I/O thread ends.
	m_running.store(false, std::memory_order_relaxed);
	if (s_active.load(std::memory_order_relaxed) == this) { s_active.store(nullptr, std::memory_order_release); }
	m_sendOn.store(false, std::memory_order_relaxed);
	m_receiveOn.store(false, std::memory_order_relaxed);
	m_stop.store(true, std::memory_order_release);
	if (m_thread.joinable()) { m_thread.join(); }
#ifndef _WIN32
	if (m_fd >= 0) { ::close(m_fd); }
#endif
	m_fd = -1;
	m_peerAddress = 0;
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
#ifndef _WIN32
	m_thread = std::thread([this] { ioLoop(); });
#endif
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
