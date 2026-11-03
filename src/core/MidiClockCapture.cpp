/*
 * MidiClockCapture.cpp - the MIDI clock master's CAPTURE SPAN (Q050-277): START and STOP at a launcher capture's own
 *                        boundaries, with the span's pulses between, handed to the MIDI client off the audio thread.
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

/*! THE THREADS. SessionAudioRecorder calls captureStarted()/captureStopped() from the session scheduler's launch events and
 *  processAudioPeriod() calls emitCapturePulses(), all on the AUDIO thread. A MIDI client's output path is not realtime-safe
 *  (a raw client writes a device, Core MIDI is called synchronously and logs), so none of these touch the client: each only
 *  appends a plain record to a fixed single-producer/single-consumer ring (queueOut: two atomic loads, one store, no
 *  allocation, no lock, no I/O). The SENDER thread - made by captureArm() on the control thread - empties the ring in order
 *  into emitMessage(), which counts, records and hands each message to the client. It polls every millisecond while a capture
 *  is armed, open or undelivered, and sleeps on a condition variable otherwise, so a session that never captures runs no
 *  thread at all.
 *
 *  BOUNDED WORK. A period queues at most the pulses its frames cross; a stop queues at most MaxTopUpPulses to catch up the
 *  period it falls in, then STOP. Nothing scales with the length of the capture, so a long or open-ended capture, or a master
 *  turned off mid-capture (the span ends there with a STOP and no catch-up), cannot make one callback queue a backlog. */

#include "MidiClock.h"

#include <algorithm>
#include <chrono>

#include "Engine.h"

namespace lmms
{

bool MidiClock::queueOut(MidiClockMessage message, qint64 tick, bool reserve) noexcept
{
	const quint32 head = m_outHead.load(std::memory_order_relaxed);
	const quint32 tail = m_outTail.load(std::memory_order_acquire);
	if (head - tail >= (reserve ? RingCapacity - ReservedSlots : RingCapacity))
	{
		m_outDropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	m_outRing[head % RingCapacity] = OutRecord{static_cast<quint32>(message), tick};
	m_outHead.store(head + 1, std::memory_order_release);
	return true;
}

void MidiClock::queueCapturePulse() noexcept
{
	const quint32 sent = m_capturePulses.load();
	const qint64 tick = m_captureStart.load() + static_cast<qint64>(sent) * TicksPerMidiClockPulse;
	if (queueOut(MidiClockMessage::Clock, tick, true)) { m_capturePulses.store(sent + 1); }
}

void MidiClock::emitCapturePulses(int frames) noexcept
{
	const float framesPerTick = Engine::framesPerTick();
	if (framesPerTick <= 0.0f || frames <= 0) { return; }
	const double advance = static_cast<double>(frames) / static_cast<double>(framesPerTick);
	double carry = 0.0;
	int pulses = pulsesForPeriod(m_tickRemainder.load(), advance, &carry);
	const qint64 length = m_captureLength.load();
	if (length > 0)
	{
		// A span of known length owes exactly this many pulses, so the last period cannot overshoot it.
		const quint32 owed = pulsesOfTicks(length);
		const quint32 sent = m_capturePulses.load();
		pulses = static_cast<int>(std::min<quint32>(static_cast<quint32>(pulses), owed > sent ? owed - sent : 0u));
	}
	for (int i = 0; i < pulses; ++i) { queueCapturePulse(); }
	m_tickRemainder.store(carry);
}

void MidiClock::captureStarted(qint64 startTick, qint64 lengthTicks) noexcept
{
	if (!m_masterEnabled.load())
	{
		m_captureAwaiting.store(false);
		return;
	}
	m_captureStart.store(startTick);
	m_captureStop.store(-1);
	m_captureLength.store(std::max<qint64>(0, lengthTicks));
	m_capturePulses.store(0);
	m_tickRemainder.store(0.0);
	m_captureStarts.fetch_add(1);
	m_captureActive.store(true);  // before the awaiting flag drops: the sender never sees the capture as idle in between
	m_captureAwaiting.store(false);
	queueOut(MidiClockMessage::Start, startTick, false);
}

void MidiClock::closeCapture(qint64 stopTick) noexcept
{
	queueOut(MidiClockMessage::Stop, stopTick, false);
	m_captureStop.store(stopTick);
	m_captureStops.fetch_add(1);
	m_captureActive.store(false);
	m_tickRemainder.store(0.0);
	// The transport may be running (or not): forget what it was, so the next period reports it afresh.
	m_transportWasRunning.store(false);
	m_positionKnown.store(false);
}

void MidiClock::captureStopped(qint64 stopTick) noexcept
{
	if (!m_captureActive.load()) { return; }
	// What the periods have not queued yet for the span, capped: the period the stop falls in, never the whole span.
	const quint32 owed = pulsesOfTicks(stopTick - m_captureStart.load());
	const quint32 sent = m_capturePulses.load();
	const quint32 extra = owed > sent ? std::min(owed - sent, MaxTopUpPulses) : 0u;
	for (quint32 i = 0; i < extra; ++i) { queueCapturePulse(); }
	closeCapture(stopTick);
}

void MidiClock::captureAbort() noexcept
{
	m_captureAwaiting.store(false);
	if (!m_captureActive.load()) { return; }
	closeCapture(m_captureStart.load() + static_cast<qint64>(m_capturePulses.load()) * TicksPerMidiClockPulse);
}

MidiClock::CaptureReport MidiClock::captureReport() const noexcept
{
	CaptureReport report;
	report.active = m_captureActive.load();
	report.startTick = m_captureStart.load();
	report.stopTick = m_captureStop.load();
	report.lengthTicks = m_captureLength.load();
	report.pulses = m_capturePulses.load();
	report.starts = m_captureStarts.load();
	report.stops = m_captureStops.load();
	return report;
}

// ---------------------------------------------------------------- the sender thread (never the audio thread)

bool MidiClock::senderBusy() const noexcept
{
	return m_captureAwaiting.load() || m_captureActive.load() || captureOutputPending();
}

void MidiClock::captureArm()
{
	{
		std::lock_guard<std::mutex> lock(m_senderMutex);
		m_captureAwaiting.store(true);
		if (!m_sender.joinable()) { m_sender = std::thread([this] { senderMain(); }); }
	}
	m_senderWake.notify_one();
}

void MidiClock::deliverQueued() noexcept
{
	if (m_outHold.load()) { return; }
	quint32 tail = m_outTail.load(std::memory_order_relaxed);
	while (tail != m_outHead.load(std::memory_order_acquire))
	{
		const OutRecord record = m_outRing[tail % RingCapacity];
		emitMessage(static_cast<MidiClockMessage>(record.message), record.tick);
		m_outTail.store(++tail, std::memory_order_release);  // after the client has it: "pending" means undelivered
	}
}

void MidiClock::senderMain()
{
	for (;;)
	{
		deliverQueued();
		if (senderBusy())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}
		std::unique_lock<std::mutex> lock(m_senderMutex);
		m_senderWake.wait(lock, [this] { return senderBusy(); });
	}
}

} // namespace lmms
