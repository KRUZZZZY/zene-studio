/*
 * LinkAudioCodec.cpp - the datagram codec, the single-producer single-consumer
 *                      ring and the IPv4 literal parser of the Link audio stream
 *                      (include/LinkAudioStream.h, docs/LINK-AUDIO.md).
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
#include <cmath>
#include <cstring>

namespace lmms::link
{

namespace
{

constexpr std::uint8_t Magic[4] = {'Z', 'L', 'A', '1'};

void put32(std::uint8_t* p, std::uint32_t v)
{
	p[0] = static_cast<std::uint8_t>(v);
	p[1] = static_cast<std::uint8_t>(v >> 8);
	p[2] = static_cast<std::uint8_t>(v >> 16);
	p[3] = static_cast<std::uint8_t>(v >> 24);
}

std::uint32_t get32(const std::uint8_t* p)
{
	return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
		| (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::size_t roundUpPowerOfTwo(std::size_t v)
{
	std::size_t p = 1;
	while (p < v) { p <<= 1; }
	return p;
}

} // namespace

std::size_t encodeAudioPacket(const AudioPacketHeader& header, const float* interleaved,
	std::uint8_t* out, std::size_t capacity)
{
	const std::size_t floats = static_cast<std::size_t>(header.frames) * AudioChannels;
	const std::size_t bytes = AudioHeaderBytes + floats * 4;
	if (header.frames == 0 || header.frames > AudioMaxPacketFrames || bytes > capacity) { return 0; }
	std::memcpy(out, Magic, 4);
	put32(out + 4, header.sequence);
	put32(out + 8, header.sampleRate);
	out[12] = static_cast<std::uint8_t>(header.frames);
	out[13] = static_cast<std::uint8_t>(header.frames >> 8);
	out[14] = static_cast<std::uint8_t>(AudioChannels);
	out[15] = 0;
	for (std::size_t i = 0; i < floats; ++i)
	{
		std::uint32_t bits;
		std::memcpy(&bits, &interleaved[i], 4);
		put32(out + AudioHeaderBytes + i * 4, bits);
	}
	return bytes;
}

bool decodeAudioPacket(const std::uint8_t* data, std::size_t size, AudioPacketHeader& header,
	float* interleavedOut, std::size_t outCapacityFloats)
{
	if (size < AudioHeaderBytes || std::memcmp(data, Magic, 4) != 0 || data[14] != AudioChannels) { return false; }
	const std::size_t frames = static_cast<std::size_t>(data[12]) | (static_cast<std::size_t>(data[13]) << 8);
	const std::size_t floats = frames * AudioChannels;
	if (frames == 0 || frames > AudioMaxPacketFrames || size != AudioHeaderBytes + floats * 4
		|| floats > outCapacityFloats)
	{
		return false;
	}
	header.sequence = get32(data + 4);
	header.sampleRate = get32(data + 8);
	header.frames = static_cast<std::uint16_t>(frames);
	for (std::size_t i = 0; i < floats; ++i)
	{
		const std::uint32_t bits = get32(data + AudioHeaderBytes + i * 4);
		float sample;
		std::memcpy(&sample, &bits, 4);
		// The peer is untrusted and the samples are summed into the master mix: NaN or an infinity
		// would poison it, so a non-finite sample becomes silence and its neighbours are kept.
		interleavedOut[i] = std::isfinite(sample) ? sample : 0.f;
	}
	return true;
}

namespace
{

//! One decimal octet (1-3 digits, 0..255) at \a pos; advances \a pos.
bool parseOctet(const std::string& text, std::size_t& pos, std::uint32_t& octet)
{
	octet = 0;
	std::size_t digits = 0;
	while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' && digits < 4)
	{
		octet = octet * 10 + static_cast<std::uint32_t>(text[pos++] - '0');
		++digits;
	}
	return digits >= 1 && digits <= 3 && octet <= 255;
}

} // namespace

bool parseIpv4(const std::string& text, std::uint32_t& address)
{
	std::uint32_t value = 0;
	std::size_t pos = 0;
	for (int octets = 0; octets < 4; ++octets)
	{
		std::uint32_t octet = 0;
		if (!parseOctet(text, pos, octet)) { return false; }
		value = (value << 8) | octet;
		if (octets < 3 && (pos >= text.size() || text[pos++] != '.')) { return false; }
	}
	if (pos != text.size()) { return false; }
	address = value;
	return true;
}

// ---- AudioRing ----------------------------------------------------------

AudioRing::AudioRing(std::size_t capacityFrames)
	: m_capacity(roundUpPowerOfTwo(std::max<std::size_t>(capacityFrames, 2)))
	, m_data(m_capacity * AudioChannels, 0.0f)
{
}

std::size_t AudioRing::fill() const
{
	// Load the tail first: head only grows, so head - tail can never read negative.
	const std::size_t tail = m_tail.load(std::memory_order_acquire);
	const std::size_t head = m_head.load(std::memory_order_acquire);
	return head - tail;
}

std::size_t AudioRing::write(const float* interleaved, std::size_t frames)
{
	const std::size_t head = m_head.load(std::memory_order_relaxed);
	const std::size_t tail = m_tail.load(std::memory_order_acquire);
	const std::size_t n = std::min(frames, m_capacity - (head - tail));
	for (std::size_t i = 0; i < n; ++i)
	{
		const std::size_t slot = ((head + i) & (m_capacity - 1)) * AudioChannels;
		m_data[slot] = interleaved[i * 2];
		m_data[slot + 1] = interleaved[i * 2 + 1];
	}
	m_head.store(head + n, std::memory_order_release);
	return n;
}

std::size_t AudioRing::writeSilence(std::size_t frames)
{
	const std::size_t head = m_head.load(std::memory_order_relaxed);
	const std::size_t tail = m_tail.load(std::memory_order_acquire);
	const std::size_t n = std::min(frames, m_capacity - (head - tail));
	for (std::size_t i = 0; i < n; ++i)
	{
		const std::size_t slot = ((head + i) & (m_capacity - 1)) * AudioChannels;
		m_data[slot] = 0.0f;
		m_data[slot + 1] = 0.0f;
	}
	m_head.store(head + n, std::memory_order_release);
	return n;
}

std::size_t AudioRing::read(float* interleaved, std::size_t frames)
{
	const std::size_t tail = m_tail.load(std::memory_order_relaxed);
	const std::size_t head = m_head.load(std::memory_order_acquire);
	const std::size_t n = std::min(frames, head - tail);
	for (std::size_t i = 0; i < n; ++i)
	{
		const std::size_t slot = ((tail + i) & (m_capacity - 1)) * AudioChannels;
		interleaved[i * 2] = m_data[slot];
		interleaved[i * 2 + 1] = m_data[slot + 1];
	}
	m_tail.store(tail + n, std::memory_order_release);
	return n;
}

std::size_t AudioRing::skip(std::size_t frames)
{
	const std::size_t tail = m_tail.load(std::memory_order_relaxed);
	const std::size_t head = m_head.load(std::memory_order_acquire);
	const std::size_t n = std::min(frames, head - tail);
	m_tail.store(tail + n, std::memory_order_release);
	return n;
}

} // namespace lmms::link
