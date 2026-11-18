/*
 * AbletonLinkSession.cpp - the ONE translation unit that includes the vendored
 *                          Ableton Link library (include/AbletonLinkSession.h
 *                          says why, and what each call's thread is).
 *
 * Built as its own static library (zene_ableton_link, src/CMakeLists.txt) so the
 * asio configuration Link needs reaches this file and no other.
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

#include "AbletonLinkSession.h"

#include <atomic>
#include <chrono>
#include <cstdlib>

#ifndef _WIN32
#include <net/if.h>
#endif

#include "LinkVendored.h"

namespace lmms::linkseam
{

/*! Test/diagnostic seam (Q050-024): confine Link's interface scan to loopback, so its discovery
 *  sockets exist only on 127.0.0.1 and no packet can leave the host (include/LinkVendored.h). */
std::atomic<bool>& loopbackOnly()
{
	static std::atomic<bool> flag{[] {
		const char* env = std::getenv("ZENE_LINK_LOOPBACK_ONLY");
		return env != nullptr && env[0] == '1';
	}()};
	return flag;
}

#ifndef _WIN32
int scan(struct ifaddrs** out)
{
	const int result = ::getifaddrs(out);
	if (result == 0 && loopbackOnly().load(std::memory_order_relaxed))
	{
		for (struct ifaddrs* entry = *out; entry != nullptr; entry = entry->ifa_next)
		{
			if ((entry->ifa_flags & IFF_LOOPBACK) == 0) { entry->ifa_flags &= ~static_cast<unsigned>(IFF_RUNNING); }
		}
	}
	return result;
}
#endif

} // namespace lmms::linkseam

#include <ableton/link/HostTimeFilter.hpp>

namespace lmms
{

namespace
{

using Micros = std::chrono::microseconds;

//! The pin, recorded once (src/3rdparty/ableton-link/README.lmms carries the same).
constexpr const char* PinnedVersion = "4.1";
constexpr const char* PinnedCommit = "9c9091275e707ab09d09a5a608fcdb84bf0dec85";

//! One reading of a captured state: pure arithmetic on Link's value type, so
//! the audio and the application path share it.
LinkTimelinePoint pointOf(const ableton::Link::SessionState& state, Micros at, double quantum)
{
	LinkTimelinePoint point;
	point.tempo = state.tempo();
	point.beat = state.beatAtTime(at, quantum);
	point.phase = state.phaseAtTime(at, quantum);
	point.playing = state.isPlaying();
	const Micros start = state.timeForIsPlaying();
	point.startTimeUs = static_cast<std::int64_t>(start.count());
	point.startBeat = state.beatAtTime(start, quantum);
	return point;
}

//! Apply a requested start or stop; true when it changed the session.
bool applyPlaying(ableton::Link::SessionState& state, Micros at, double quantum,
	const LinkAudioChange& change, bool alone)
{
	// Committed only when it CHANGES the session: an engine that started because
	// the session started must not re-announce the start (that would move the
	// session's start time for every peer).
	if (change.playing < 0 || state.isPlaying() == (change.playing == 1)) { return false; }
	if (alone)
	{
		// Alone, a peer may re-map the beat grid freely (Link.hpp
		// requestBeatAtTime): the play head's beat lands on `at`, so a start is
		// immediate.
		state.setIsPlayingAndRequestBeatAtTime(change.playing == 1, at, change.requestBeat, quantum);
		return true;
	}
	// With peers the grid is theirs too, so only the start/stop state changes.
	// That is also what keeps the commit from carrying a TIMELINE: within Link's
	// one-second local-modification grace period (link/Controller.hpp) this
	// thread's captured timeline can be older than the session's, and committing
	// it would put a tempo a peer has since replaced back into the session.
	state.setIsPlaying(change.playing == 1, at);
	return true;
}

} // namespace

struct AbletonLinkSession::Impl
{
	explicit Impl(double bpm) : link(bpm) {}
	ableton::Link link;
	ableton::link::HostTimeFilter<ableton::link::platform::Clock> filter;
};

bool AbletonLinkSession::compiledIn() { return true; }
void AbletonLinkSession::setLoopbackOnly(bool on) { linkseam::loopbackOnly().store(on, std::memory_order_relaxed); }
bool AbletonLinkSession::loopbackOnly() { return linkseam::loopbackOnly().load(std::memory_order_relaxed); }
const char* AbletonLinkSession::libraryVersion() { return PinnedVersion; }
const char* AbletonLinkSession::libraryCommit() { return PinnedCommit; }

AbletonLinkSession::AbletonLinkSession(double bpm) : m_impl(std::make_unique<Impl>(bpm)) {}

AbletonLinkSession::~AbletonLinkSession()
{
	m_impl->link.enable(false);
}

void AbletonLinkSession::enable(bool on) { m_impl->link.enable(on); }
bool AbletonLinkSession::isEnabled() const { return m_impl->link.isEnabled(); }
void AbletonLinkSession::enableStartStopSync(bool on) { m_impl->link.enableStartStopSync(on); }
bool AbletonLinkSession::isStartStopSyncEnabled() const { return m_impl->link.isStartStopSyncEnabled(); }
std::size_t AbletonLinkSession::numPeers() const { return m_impl->link.numPeers(); }

std::int64_t AbletonLinkSession::clockMicros() const
{
	return static_cast<std::int64_t>(m_impl->link.clock().micros().count());
}

LinkTimelinePoint AbletonLinkSession::appCapture(double quantum) const
{
	const auto state = m_impl->link.captureAppSessionState();
	return pointOf(state, m_impl->link.clock().micros(), quantum);
}

void AbletonLinkSession::appSetTempo(double bpm)
{
	auto state = m_impl->link.captureAppSessionState();
	state.setTempo(bpm, m_impl->link.clock().micros());
	m_impl->link.commitAppSessionState(state);
}

LinkTimelinePoint AbletonLinkSession::audioPeriod(std::int64_t hostTimeUs, double quantum,
	const LinkAudioChange& change) noexcept
{
	const Micros at{hostTimeUs};
	auto state = m_impl->link.captureAudioSessionState();
	if (change.tempo > 0.0) { state.setTempo(change.tempo, at); }
	const bool playing = applyPlaying(state, at, quantum, change, m_impl->link.numPeers() == 0);
	if (change.tempo > 0.0 || playing) { m_impl->link.commitAudioSessionState(state); }
	LinkTimelinePoint point = pointOf(state, at, quantum);
	point.committedPlaying = playing;
	return point;
}

std::int64_t AbletonLinkSession::hostTimeAtSample(double sampleTime) noexcept
{
	return static_cast<std::int64_t>(m_impl->filter.sampleTimeToHostTime(sampleTime).count());
}

void AbletonLinkSession::resetHostTimeFilter() noexcept
{
	m_impl->filter.reset();
}

} // namespace lmms
