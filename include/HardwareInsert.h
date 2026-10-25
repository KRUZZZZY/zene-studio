/*
 * HardwareInsert.h - an outboard device in a channel: send to an output pair, return from an input pair
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
 *
 */

#ifndef LMMS_HARDWARE_INSERT_H
#define LMMS_HARDWARE_INSERT_H

#include <atomic>
#include <bit>
#include <cstdint>
#include <vector>

#include "AudioIoSetup.h"
#include "LatencyCompensation.h"
#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "lmms_export.h"

class QDomDocument;
class QDomElement;

#ifdef LMMS_TESTING
//! The regression test for the ping's state machine (tests/src/core/HardwareIoTest.cpp).
class HardwareIoTest;
#endif

namespace lmms
{

class AudioEngine;

/*! 0.5.0 F1 (MX-061): a hardware insert - the "external effect" of Ableton, ReaInsert, Pipeline. It sits in
 *  a channel after the channel's devices and before its fader: the channel's signal goes out of a device
 *  output pair (the send), through the outboard gear and back in on an input pair (the return).
 *
 *  - Latency: a ping (one click on the send, found on the return) measures the round trip in frames. The
 *    measured figure - or a typed override - is the latency the insert reports to delay compensation
 *    (Mixer::resolveLatency adds it to the channel's), and the dry signal is delayed by the same amount so
 *    dry/wet mixes two aligned signals.
 *  - Dry/wet: 'mix' 0 is the dry signal only, 1 the return only.
 *  - The return is read from the engine's capture input (the N-channel block when the backend delivers
 *    one, else the stereo bus, which carries inputs 1/2) through a ring, so a capture block longer than an
 *    engine period is spread over the periods that follow it.
 *  - A backend without capture returns silence; 'mix' then decides how much of the dry signal remains,
 *    and mixer.hardware_get says the return is unavailable. An export runs no device at all, so the
 *    insert is bypassed there (hardwareio::processInsert): the dry signal is rendered.
 *
 *  Threads. The settings are written on the interface thread under the engine's change lock (so no
 *  render is running) and read by the audio thread. process() runs on the channel's worker; the mixer's
 *  render thread takes the send block after every worker has finished (hardwareio::mixOutputs). The ping
 *  is requested and read through atomics. Buffers are allocated by prepare(), off the audio thread; an
 *  insert that was never prepared does nothing. */
class LMMS_EXPORT HardwareInsert
{
public:
	enum class PingState : int { Idle, Requested, Waiting, Done, Failed };

	struct Settings
	{
		bool enabled = false;
		audioio::Destination send{0, 2};    //!< device outputs
		audioio::Destination ret{0, 2};     //!< device inputs
		float mix = 1.f;                     //!< 0 dry .. 1 wet
		int latencyOverride = -1;            //!< frames; negative: the measured figure
		int measured = -1;                   //!< the last ping's result; negative: never measured
		sample_rate_t measuredRate = 0;      //!< sample rate at which the measured frame count was found
		audioio::Destination measuredSend{0, 2};
		audioio::Destination measuredReturn{0, 2};
		bool operator==(const Settings&) const = default;
	};

	//! The click a ping sends, and how loud the return must be to count as it.
	static constexpr float PingLevel = 0.5f;
	static constexpr float PingThreshold = 0.1f;
	//! A ping that hears nothing for this many seconds fails.
	static constexpr int PingTimeoutSeconds = 2;

	//! Interface thread (under the change lock): buffers for periods of @a frames, once.
	void prepare(f_cnt_t frames);
	bool enabled() const { return m_settings.enabled; }
	//! The settings, with the measured latency as the audio thread last published it.
	Settings settings() const;
	//! Interface thread, under the change lock: apply @a settings (prepares the buffers when enabled).
	void setSettings(const Settings& settings, f_cnt_t frames);

	//! The latency delay compensation counts: 0 while disabled, else the override or the measured figure.
	int latencyFrames() const;

	//! Interface thread: ask for a ping. The next processed period sends the click.
	void requestPing();

	/*! Interface thread: call off a ping - a measurement nobody is waiting for any more.
	 *  The click may already be on its way, so this only means the answer is dropped:
	 *  a ping the user undid must not land a measured latency afterwards. The state machine is
	 *  a compare-and-swap, so a period that had already read the request cannot put the ping
	 *  back behind this cancel. */
	void cancelPing();

	PingState pingState() const { return pingStateOf(m_ping.load(std::memory_order_acquire)); }

	/*! The channel's worker: send @a buffer's period, take the return, and write the insert's output
	 *  (the delayed dry signal and the return, by 'mix') back into @a buffer. Nothing while disabled or
	 *  unprepared. No allocation, no lock. */
	void process(SampleFrame* buffer, f_cnt_t frames, AudioEngine& engine) noexcept;

	//! The render thread, after every worker: the period to send, or nullptr when none was produced
	//! this period (disabled, or the channel was muted). Taking it clears the flag.
	const SampleFrame* takeSend() noexcept;

	//! <hardwareinsert> under @a channelElement, only while it differs from the defaults; read back (absent:
	//! the defaults, disabled).
	void save(QDomDocument& doc, QDomElement& channelElement) const;
	void load(const QDomElement& channelElement, f_cnt_t frames);

private:
	void pullReturn(AudioEngine& engine, f_cnt_t frames) noexcept;
	void pushCaptured(AudioEngine& engine) noexcept;
	void listenForPing(f_cnt_t frames, sample_rate_t rate) noexcept;
	void writeOutput(SampleFrame* buffer, f_cnt_t frames, bool measuring) noexcept;
	void invalidateMeasurementForRate(sample_rate_t rate) noexcept;

	/*! The ping's state machine, in one atomic word: the state in the low bits, the frames the
	 *  last ping measured above them. One word, so a state change and the figure it carries are
	 *  published together - a cancel cannot be overwritten by a move that was already on its
	 *  way, and a ping nobody waits for any more cannot publish its figure. */
	using PingWord = std::int64_t;
	static constexpr int PingStateBits = 3;
	static constexpr PingWord PingStateMask = (PingWord{1} << PingStateBits) - 1;
	static_assert(sizeof(int) == sizeof(std::uint32_t), "hardware insert measurements use 32-bit ints");

	static PingWord packPing(int measured, PingState state) noexcept
	{
		const auto measuredBits = std::bit_cast<std::uint32_t>(measured);
		return static_cast<PingWord>((static_cast<std::uint64_t>(measuredBits) << PingStateBits)
			| static_cast<std::uint64_t>(state));
	}
	static PingState pingStateOf(PingWord word) noexcept
	{
		return static_cast<PingState>(word & PingStateMask);
	}
	static int measuredOf(PingWord word) noexcept
	{
		const auto measuredBits = static_cast<std::uint32_t>(static_cast<std::uint64_t>(word) >> PingStateBits);
		return std::bit_cast<int>(measuredBits);
	}

	/*! The audio thread's moves on the ping state machine. Each of them takes the word the
	 *  period read (@a expected), so it is the move that was on its way when the interface
	 *  thread changed the word in between. */
	//! The period claims the request: the click goes out and the ping waits for its answer.
	bool claimPing(PingWord expected) noexcept;
	bool claimPing(PingWord expected, sample_rate_t rate) noexcept;
	//! The period hears the click: the ping is done and @a frames is what it measured.
	bool publishPingResult(PingWord expected, int frames) noexcept;
	//! The period heard nothing in time: the ping failed.
	void failPing(PingWord expected) noexcept;
	//! The interface thread: move the ping to @a state, keeping the figure.
	void movePingTo(PingState state) noexcept;
	//! The interface thread: the figure the insert reports (@a measured; negative: never measured).
	void setMeasured(int measured) noexcept;
	//! The frames the last ping measured (negative: never measured).
	int measuredFrames() const { return measuredOf(m_ping.load(std::memory_order_acquire)); }
	//! The ping's word a period reads.
	PingWord pingWord() const noexcept { return m_ping.load(std::memory_order_acquire); }

#ifdef LMMS_TESTING
	friend class ::HardwareIoTest;
#endif

	Settings m_settings;
	std::vector<SampleFrame> m_send;
	std::vector<SampleFrame> m_return;
	std::vector<SampleFrame> m_ring;  //!< returned frames waiting for their period
	std::size_t m_ringRead = 0;
	std::size_t m_ringFill = 0;
	LatencyCompensation m_dry;
	bool m_sent = false;
	//! The ping's state and the figure it measured, in one word.
	std::atomic<PingWord> m_ping{packPing(-1, PingState::Idle)};
	std::atomic<sample_rate_t> m_measurementRate{0};
	sample_rate_t m_pingSampleRate = 0;
	int m_pingFrames = 0;
};

} // namespace lmms

#endif // LMMS_HARDWARE_INSERT_H
