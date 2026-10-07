/*
 * LinkAudioFollower.cpp - include/LinkAudioFollower.h: the audio-thread half of
 *                         Ableton Link session sync. Read the header first; it
 *                         lists everything a period does and why each step is
 *                         realtime-safe.
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

#include "LinkAudioFollower.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Song.h"
#include "Timeline.h"

namespace lmms
{

namespace
{

//! Zene's beat is a quarter note, DefaultTicksPerBar / 4 ticks - a Link beat.
constexpr double TicksPerBeat = DefaultTicksPerBar / 4.0;
//! A period within this of "now" means the audio thread is running.
constexpr std::int64_t AudioAliveUs = 200000;

//! Marks the session in use for the length of one period, so attach(nullptr)
//! can wait for it to be let go of.
class BusyScope
{
public:
	explicit BusyScope(std::atomic<bool>& busy) : m_busy(busy) { m_busy.store(true); }
	~BusyScope() { m_busy.store(false, std::memory_order_release); }
	BusyScope(const BusyScope&) = delete;
	BusyScope& operator=(const BusyScope&) = delete;

private:
	std::atomic<bool>& m_busy;
};

double playHeadBeats(const Timeline& timeline, double framesPerTick)
{
	return (static_cast<double>(timeline.ticks())
		+ static_cast<double>(timeline.frameOffset()) / framesPerTick) / TicksPerBeat;
}

/*! Move the frame offset by \a frames without changing which tick is processed:
 *  backward never below one frame into the tick (frame 0 is where the tick's
 *  events fire, Song::processNextBuffer), forward never more than half a frame
 *  into the next tick (that tick is then processed, as it would have been). */
void nudgeFrameOffset(Timeline& timeline, double frames, double framesPerTick)
{
	const double offset = timeline.frameOffset();
	const double moved = frames >= 0.0 ? std::min(offset + frames, framesPerTick + 0.5)
		: std::max(offset + frames, std::min(offset, 1.0));
	timeline.setFrameOffset(static_cast<float>(moved));
}

void jumpTo(Timeline& timeline, double beats, double framesPerTick)
{
	const double ticks = std::max(0.0, beats) * TicksPerBeat;
	const double whole = std::floor(ticks);
	timeline.setTicks(static_cast<tick_t>(whole));
	timeline.setFrameOffset(static_cast<float>((ticks - whole) * framesPerTick));
}

} // namespace

LinkAudioFollower& LinkAudioFollower::instance()
{
	// Never destroyed: the audio thread may reach it during static destruction.
	static auto* s_follower = new LinkAudioFollower();
	return *s_follower;
}

void LinkAudioFollower::attach(AbletonLinkSession* session)
{
	m_session.store(session);
	if (session != nullptr) { return; }
	// A period that loaded the old pointer before the store above is still using
	// it; it lets go at the end of that one period.
	while (m_busy.load()) { std::this_thread::yield(); }
}

bool LinkAudioFollower::audioRunning(std::int64_t nowUs) const
{
	const std::int64_t last = m_lastPeriodUs.load(std::memory_order_relaxed);
	return last != 0 && nowUs - last < AudioAliveUs;
}

LinkFollowerReading LinkAudioFollower::reading() const
{
	LinkFollowerReading reading;
	reading.engaged = engaged();
	reading.locked = m_locked.load(std::memory_order_relaxed);
	reading.periods = m_periods.load(std::memory_order_relaxed);
	reading.holds = m_holds.load(std::memory_order_relaxed);
	reading.jumps = m_jumps.load(std::memory_order_relaxed);
	reading.lastErrorBeats = m_lastError.load(std::memory_order_relaxed);
	reading.lastHostTimeUs = m_lastHostUs.load(std::memory_order_relaxed);
	return reading;
}

LinkPeriod LinkAudioFollower::process(Song& song, bool rolling) noexcept
{
	if (!m_engaged.load(std::memory_order_acquire)) { return disengaged(); }
	const BusyScope busy(m_busy);
	AbletonLinkSession* session = m_session.load();
	if (session == nullptr) { return disengaged(); }
	return engagedPeriod(*session, song, rolling);
}

LinkPeriod LinkAudioFollower::disengaged() noexcept
{
	// Sync off and nothing to undo: the period pays this test and nothing else.
	if (!m_wasEngaged && !m_overriding) { return LinkPeriod::Off; }
	m_wasEngaged = false;
	m_wasRolling = false;
	m_lock.reset();
	m_locked.store(false, std::memory_order_relaxed);
	return m_overriding ? releaseRate() : LinkPeriod::Off;
}

LinkPeriod LinkAudioFollower::releaseRate() noexcept
{
	if (!m_overriding) { return LinkPeriod::Following; }
	m_overriding = false;
	return LinkPeriod::Released;
}

LinkPeriod LinkAudioFollower::engagedPeriod(AbletonLinkSession& session, Song& song, bool rolling) noexcept
{
	if (!m_wasEngaged)
	{
		session.resetHostTimeFilter();
		m_sampleTime = 0.0;
		m_wasEngaged = true;
		// Joining is not a start or a stop: the transport's state when sync is
		// switched on is not announced (Link.hpp: the local start/stop state
		// persists when joining), and a song already playing re-locks in place.
		m_seenEdges = m_transportEdges.load(std::memory_order_acquire);
		m_wasRolling = rolling;
	}
	const PeriodClock clock = clockFor(session);
	const std::uint32_t edges = m_transportEdges.load(std::memory_order_acquire);
	const bool edged = edges != m_seenEdges || rolling != m_wasRolling;
	m_seenEdges = edges;
	const LinkAudioChange change = takeChange(song, rolling, edged);
	const double quantum = m_quantum.load(std::memory_order_relaxed);
	const LinkTimelinePoint point = session.audioPeriod(clock.hostUs, quantum, change);
	m_periods.fetch_add(1, std::memory_order_relaxed);
	m_lastHostUs.store(clock.hostUs, std::memory_order_relaxed);
	const bool started = rolling && edged;
	m_wasRolling = rolling;
	if (!rolling)
	{
		m_lock.reset();
		m_locked.store(false, std::memory_order_relaxed);
		return releaseRate();
	}
	return lockPeriod(song, point, clock, started);
}

LinkAudioFollower::PeriodClock LinkAudioFollower::clockFor(AbletonLinkSession& session) noexcept
{
	AudioEngine* engine = Engine::audioEngine();
	PeriodClock clock;
	clock.frames = static_cast<double>(engine->framesPerPeriod());
	clock.rate = static_cast<double>(engine->outputSampleRate());
	const AudioDevice* device = engine->audioDev();
	const double deviceLatency = device != nullptr ? static_cast<double>(device->outputLatencyFrames()) : 0.0;
	// The first sample of this period reaches the output one engine period plus
	// the device's own latency from now (Link README, "Host time").
	const double latencyUs = (clock.frames + deviceLatency) * 1e6 / std::max(clock.rate, 1.0);
	const std::int64_t now = session.clockMicros();
	clock.hostUs = filteredHostTime(session, now, clock) + static_cast<std::int64_t>(std::llround(latencyUs));
	m_sampleTime += clock.frames;
	m_lastPeriodUs.store(now, std::memory_order_relaxed);
	return clock;
}

std::int64_t LinkAudioFollower::filteredHostTime(AbletonLinkSession& session, std::int64_t now,
	const PeriodClock& clock) noexcept
{
	// The filter assumes the sample count and the clock advance together. A gap
	// in rendering (an xrun, a stalled engine, a device restart) breaks that, and
	// a filter that kept regressing over it would lag the clock - which would
	// stamp a start or stop EARLIER than a peer's, and Link keeps the later one.
	// So a reading further from `now` than two periods (plus 5 ms of scheduling
	// slack) restarts the filter, whose first estimate is `now` itself.
	const double gapUs = 2.0 * clock.frames * 1e6 / std::max(clock.rate, 1.0) + 5000.0;
	const std::int64_t filtered = session.hostTimeAtSample(m_sampleTime);
	if (std::fabs(static_cast<double>(filtered - now)) <= gapUs) { return filtered; }
	session.resetHostTimeFilter();
	m_sampleTime = 0.0;
	return session.hostTimeAtSample(m_sampleTime);
}

LinkAudioChange LinkAudioFollower::takeChange(Song& song, bool rolling, bool edged) noexcept
{
	LinkAudioChange change;
	change.tempo = m_requestedTempo.exchange(0.0, std::memory_order_acq_rel);
	if (m_startStopSync.load(std::memory_order_relaxed) && edged)
	{
		// This transport's own edge. AbletonLinkSession commits it only when the
		// session does not already say the same, so a start this engine made
		// BECAUSE the session started is not echoed back as a new start.
		change.playing = rolling ? 1 : 0;
		change.requestBeat = playHeadBeats(song.getTimeline(), Engine::framesPerTick());
	}
	return change;
}

LinkPeriod LinkAudioFollower::lockPeriod(Song& song, const LinkTimelinePoint& point,
	const PeriodClock& clock, bool started) noexcept
{
	// The whole part of the session tempo: the engine never runs FAST, so the
	// phase lock only ever has to make up ground forward (see the header).
	Engine::updateFramesPerTickForTempo(std::max(1, static_cast<int>(std::floor(point.tempo))));
	m_overriding = true;
	const double framesPerTick = Engine::framesPerTick();
	Timeline& timeline = song.getTimeline();

	link::PhaseLockInput in;
	in.linkBeat = point.beat;
	in.quantum = m_quantum.load(std::memory_order_relaxed);
	in.zeneBeat = playHeadBeats(timeline, framesPerTick);
	in.periodBeats = clock.frames * point.tempo / (60.0 * std::max(clock.rate, 1.0));
	in.started = started;
	// Join a session someone ELSE started; a start this period committed itself
	// counts in to the session's phase like a local start (immediate when alone,
	// where the commit mapped the play head's beat to now).
	in.joinRunning = started && point.playing && !point.committedPlaying
		&& m_startStopSync.load(std::memory_order_relaxed);
	in.sessionStartBeat = point.startBeat;
	const link::PhaseLockOutput out = m_lock.step(in);
	m_lastError.store(out.errorBeats, std::memory_order_relaxed);
	m_locked.store(m_lock.locked(), std::memory_order_relaxed);
	if (out.action == link::PhaseAction::Hold)
	{
		m_holds.fetch_add(1, std::memory_order_relaxed);
		return LinkPeriod::Hold;
	}
	apply(timeline, out, framesPerTick);
	return LinkPeriod::Following;
}

void LinkAudioFollower::apply(Timeline& timeline, const link::PhaseLockOutput& out, double framesPerTick) noexcept
{
	if (out.action == link::PhaseAction::Jump)
	{
		m_jumps.fetch_add(1, std::memory_order_relaxed);
		jumpTo(timeline, out.targetBeat, framesPerTick);
		return;
	}
	nudgeFrameOffset(timeline, out.moveBeats * TicksPerBeat * framesPerTick, framesPerTick);
}

LinkPeriod followLinkSession(Song& song, bool rolling) noexcept
{
	return LinkAudioFollower::instance().process(song, rolling);
}

} // namespace lmms
