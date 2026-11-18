/*
 * MidiTakeCapture.cpp - the realtime half of arrangement MIDI take recording
 *
 * Copyright (c) 2026 LMMS developers
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

#include "MidiTakeCapture.h"

#include "Midi.h"
#include "MidiEvent.h"

namespace lmms
{

namespace
{

//! Written by the audio thread once per rendered period: (loop wraps << 32) | tick.
std::atomic<std::uint64_t> g_position{0};

constexpr std::size_t Mask = MidiTakeCapture::CapacityEvents - 1;
static_assert((MidiTakeCapture::CapacityEvents & Mask) == 0, "the capacity must be a power of two");

} // namespace


MidiTakeCapture::~MidiTakeCapture() = default;


void MidiTakeCapture::arm(bool armed)
{
	if (armed && !m_ready.load(std::memory_order_acquire))
	{
		m_cells = std::make_unique<Cell[]>(CapacityEvents);
		for (std::size_t i = 0; i < CapacityEvents; ++i) { m_cells[i].sequence.store(i, std::memory_order_relaxed); }
		m_ready.store(true, std::memory_order_release);
	}
	m_armed.store(armed, std::memory_order_relaxed);
	if (!armed) { stopCapture(); }
}


bool MidiTakeCapture::startCapture()
{
	if (!isArmed() || !m_ready.load(std::memory_order_acquire)) { return false; }
	// Whatever a producer queued after the last session closed belongs to no session.
	MidiTakeEvent stale;
	while (pop(&stale)) {}
	m_capturing.store(true, std::memory_order_release);
	return true;
}


void MidiTakeCapture::capture(const MidiEvent& event) noexcept
{
	if (!m_capturing.load(std::memory_order_acquire)) { return; }
	const auto type = event.type();
	if (type != MidiNoteOn && type != MidiNoteOff) { return; }
	const std::uint64_t position = g_position.load(std::memory_order_relaxed);
	MidiTakeEvent captured;
	captured.tick = static_cast<tick_t>(static_cast<std::int32_t>(position & 0xffffffffu));
	captured.wraps = static_cast<std::uint32_t>(position >> 32);
	captured.on = (type == MidiNoteOn && event.velocity() > 0) ? 1 : 0;
	captured.key = static_cast<std::uint8_t>(event.key() & 0x7f);
	captured.velocity = static_cast<std::uint8_t>(event.velocity() & 0x7f);
	captured.channel = static_cast<std::uint8_t>(event.channel() & 0x0f);
	if (!push(captured)) { m_dropped.fetch_add(1, std::memory_order_relaxed); }
}


bool MidiTakeCapture::push(const MidiTakeEvent& event) noexcept
{
	// Vyukov's bounded queue: a cell is free for position p when its sequence is p. A producer only retries
	// when another producer claimed the same position first, and at most MaxClaimAttempts times: under that
	// much contention the event is dropped (and counted), so an audio-thread caller never spins unbounded.
	std::uint64_t pos = m_enqueue.load(std::memory_order_relaxed);
	bool claimed = false;
	for (int attempt = 0; attempt < MaxClaimAttempts && !claimed; ++attempt)
	{
		Cell& cell = m_cells[pos & Mask];
		const std::uint64_t sequence = cell.sequence.load(std::memory_order_acquire);
		const auto diff = static_cast<std::int64_t>(sequence) - static_cast<std::int64_t>(pos);
		if (diff < 0) { return false; }  // full: the consumer has not freed this cell yet
		if (diff == 0)
		{
			// The test seam loses this attempt as a rival producer would (pos unchanged, as a spurious CAS failure).
			const bool contended = m_claimGate != nullptr && !m_claimGate(m_claimGateContext);
			claimed = !contended && m_enqueue.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed);
		}
		else { pos = m_enqueue.load(std::memory_order_relaxed); }
	}
	if (!claimed) { return false; }
	Cell& cell = m_cells[pos & Mask];
	cell.event = event;
	cell.sequence.store(pos + 1, std::memory_order_release);
	return true;
}


bool MidiTakeCapture::pop(MidiTakeEvent* event) noexcept
{
	if (!m_ready.load(std::memory_order_acquire)) { return false; }
	Cell& cell = m_cells[m_dequeue & Mask];
	if (cell.sequence.load(std::memory_order_acquire) != m_dequeue + 1) { return false; }
	*event = cell.event;
	cell.sequence.store(m_dequeue + CapacityEvents, std::memory_order_release);
	++m_dequeue;
	return true;
}


std::size_t MidiTakeCapture::drain(std::vector<MidiTakeEvent>* out)
{
	std::size_t count = 0;
	MidiTakeEvent event;
	while (pop(&event))
	{
		out->push_back(event);
		++count;
	}
	return count;
}


void MidiTakeCapture::publishPosition(tick_t tick, std::uint64_t loopWraps) noexcept
{
	const auto low = static_cast<std::uint64_t>(static_cast<std::uint32_t>(tick));
	g_position.store((loopWraps << 32) | low, std::memory_order_relaxed);
}


tick_t MidiTakeCapture::publishedTick() noexcept
{
	return static_cast<tick_t>(static_cast<std::int32_t>(g_position.load(std::memory_order_relaxed) & 0xffffffffu));
}


std::uint32_t MidiTakeCapture::publishedWraps() noexcept
{
	return static_cast<std::uint32_t>(g_position.load(std::memory_order_relaxed) >> 32);
}

} // namespace lmms
