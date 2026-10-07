/*
 * LinkAudioFollower.h - the audio-thread half of Ableton Link session sync
 *                       (docs/LINK-SYNC.md section 7).
 *
 * Song::processNextBuffer() calls followLinkSession() once per period. With sync
 * off that is one relaxed atomic load. With a session engaged it is, in this
 * order and nothing else:
 *   1. the period's host time: Link's HostTimeFilter over the running sample
 *      count, plus the output latency (one engine period plus the device's own);
 *   2. ONE capture -> change -> commit of the session through
 *      AbletonLinkSession::audioPeriod() - Link's captureAudioSessionState /
 *      commitAudioSessionState, used exactly as Link.hpp prescribes: in a local
 *      scope, on the audio thread only. The change carries a tempo the UI thread
 *      posted (a lock-free mailbox) and, with start/stop sync on, this
 *      transport's own start or stop edge;
 *   3. while the song plays: the frame/tick rate for the session tempo
 *      (Engine::updateFramesPerTickForTempo of its whole part - the engine runs a
 *      hair slow, never fast, and the phase lock makes up the difference
 *      forward), and the phase lock (include/LinkPhaseLock.h) applied to the
 *      play head: a bounded nudge of the frame offset, a count-in Hold, or a
 *      re-lock Jump.
 * No allocation, no lock, no log, no Qt call that allocates: the reposition
 * uses Timeline::setTicks, which the loop wrap (Song::enforceLoop) already calls
 * on this thread. LinkPhaseLockTest measures the allocation count.
 *
 * Every control-side setter is a relaxed or release atomic store: the UI thread
 * never waits on the audio thread, except in attach(nullptr), which waits for
 * the period in flight to let go of the session before it may be destroyed.
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

#ifndef LMMS_LINK_AUDIO_FOLLOWER_H
#define LMMS_LINK_AUDIO_FOLLOWER_H

#include <atomic>
#include <cstdint>

#include "AbletonLinkSession.h"
#include "LinkPhaseLock.h"
#include "lmms_export.h"

namespace lmms
{

class Song;
class Timeline;

//! What Song::processNextBuffer() does with this period.
enum class LinkPeriod
{
	Off,        //!< no session engaged: the engine's own block
	Following,  //!< engaged: render the block as usual (rate and play head already set)
	Hold,       //!< the count-in to the session's phase: render nothing, advance nothing
	Released    //!< the follower stopped setting the rate: restore the engine's own
};

//! A snapshot of the follower for link.get_state and the tests.
struct LinkFollowerReading
{
	bool engaged = false;
	bool locked = false;
	std::uint64_t periods = 0;
	std::uint64_t holds = 0;
	std::uint64_t jumps = 0;
	double lastErrorBeats = 0.0;
	std::int64_t lastHostTimeUs = 0;
};

class LMMS_EXPORT LinkAudioFollower
{
public:
	static LinkAudioFollower& instance();

	// ---- control side (UI thread) ------------------------------------------
	//! Publish the session the audio thread may use; nullptr withdraws it and
	//! returns only once no period is using the old one.
	void attach(AbletonLinkSession* session);
	void setEngaged(bool on) { m_engaged.store(on, std::memory_order_release); }
	bool engaged() const { return m_engaged.load(std::memory_order_acquire); }
	void setQuantum(double beats) { m_quantum.store(beats, std::memory_order_relaxed); }
	void setStartStopSync(bool on) { m_startStopSync.store(on, std::memory_order_relaxed); }
	//! Ask the next period to commit \a bpm to the session (Link's audio-thread commit).
	void requestTempo(double bpm) { m_requestedTempo.store(bpm, std::memory_order_release); }
	//! A tempo requested and not yet committed by a period, or 0.
	double pendingTempo() const { return m_requestedTempo.load(std::memory_order_acquire); }
	/*! The transport started, paused or stopped (Song::playbackStateChanged).
	 *  The audio thread also sees the transport's state each period, but a stop
	 *  and a start inside ONE period look like no change at all there; this
	 *  count makes every edge visible. */
	void noteTransportEdge() { m_transportEdges.fetch_add(1, std::memory_order_release); }
	//! True when a period ran within the last 200 ms of \a nowUs (Link clock):
	//! the audio thread is running, so session changes must go through it.
	bool audioRunning(std::int64_t nowUs) const;
	LinkFollowerReading reading() const;

	// ---- audio thread ------------------------------------------------------
	LinkPeriod process(Song& song, bool rolling) noexcept;

private:
	struct PeriodClock
	{
		std::int64_t hostUs = 0;
		double frames = 0.0;
		double rate = 0.0;
	};

	LinkPeriod disengaged() noexcept;
	LinkPeriod engagedPeriod(AbletonLinkSession& session, Song& song, bool rolling) noexcept;
	PeriodClock clockFor(AbletonLinkSession& session) noexcept;
	std::int64_t filteredHostTime(AbletonLinkSession& session, std::int64_t now, const PeriodClock& clock) noexcept;
	LinkAudioChange takeChange(Song& song, bool rolling, bool edged) noexcept;
	LinkPeriod lockPeriod(Song& song, const LinkTimelinePoint& point, const PeriodClock& clock,
		bool started) noexcept;
	void apply(Timeline& timeline, const link::PhaseLockOutput& out, double framesPerTick) noexcept;
	LinkPeriod releaseRate() noexcept;

	std::atomic<AbletonLinkSession*> m_session{nullptr};
	std::atomic<bool> m_busy{false};
	std::atomic<bool> m_engaged{false};
	std::atomic<bool> m_startStopSync{false};
	std::atomic<double> m_quantum{4.0};
	std::atomic<double> m_requestedTempo{0.0};
	std::atomic<std::uint32_t> m_transportEdges{0};

	// diagnostics, written by the audio thread
	std::atomic<std::int64_t> m_lastPeriodUs{0};
	std::atomic<std::int64_t> m_lastHostUs{0};
	std::atomic<double> m_lastError{0.0};
	std::atomic<bool> m_locked{false};
	std::atomic<std::uint64_t> m_periods{0};
	std::atomic<std::uint64_t> m_holds{0};
	std::atomic<std::uint64_t> m_jumps{0};

	// audio-thread state
	link::PhaseLock m_lock;
	double m_sampleTime = 0.0;
	bool m_wasEngaged = false;
	bool m_wasRolling = false;
	std::uint32_t m_seenEdges = 0;
	bool m_overriding = false;
};

//! The one call Song::processNextBuffer() makes per period (see the header).
LMMS_EXPORT LinkPeriod followLinkSession(Song& song, bool rolling) noexcept;

} // namespace lmms

#endif // LMMS_LINK_AUDIO_FOLLOWER_H
