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

#include <chrono>

#include <ableton/Link.hpp>
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

//! Apply one requested change to a captured state; true when it changed.
bool applyChange(ableton::Link::SessionState& state, Micros at, double quantum,
	const LinkAudioChange& change)
{
	bool changed = false;
	if (change.tempo > 0.0)
	{
		state.setTempo(change.tempo, at);
		changed = true;
	}
	if (change.playing >= 0)
	{
		// Link's own recipe for a start: the requested beat lands on `at` when
		// this peer is alone, or on the next time with the same phase when it
		// is not ("quantized launch", Link.hpp requestBeatAtTime).
		state.setIsPlayingAndRequestBeatAtTime(change.playing == 1, at, change.requestBeat, quantum);
		changed = true;
	}
	return changed;
}

} // namespace

struct AbletonLinkSession::Impl
{
	explicit Impl(double bpm) : link(bpm) {}
	ableton::Link link;
	ableton::link::HostTimeFilter<ableton::link::platform::Clock> filter;
};

bool AbletonLinkSession::compiledIn() { return true; }
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
	if (applyChange(state, at, quantum, change))
	{
		m_impl->link.commitAudioSessionState(state);
	}
	return pointOf(state, at, quantum);
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
