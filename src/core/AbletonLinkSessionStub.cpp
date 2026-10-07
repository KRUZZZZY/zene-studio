/*
 * AbletonLinkSessionStub.cpp - include/AbletonLinkSession.h for a build without
 *                              the vendored Ableton Link library
 *                              (WANT_ABLETON_LINK=OFF, or a MinGW cross build).
 *
 * compiledIn() is false, so LinkSyncEngine never selects the Ableton Link
 * backend and nothing below is reached by the product; every call is an inert,
 * well-defined answer so a test or a caller that asks anyway gets "no session"
 * rather than undefined behaviour.
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

namespace lmms
{

struct AbletonLinkSession::Impl
{
	double tempo = 120.0;
};

bool AbletonLinkSession::compiledIn() { return false; }
const char* AbletonLinkSession::libraryVersion() { return ""; }
const char* AbletonLinkSession::libraryCommit() { return ""; }

AbletonLinkSession::AbletonLinkSession(double bpm) : m_impl(std::make_unique<Impl>())
{
	m_impl->tempo = bpm;
}

AbletonLinkSession::~AbletonLinkSession() = default;

void AbletonLinkSession::enable(bool) {}
bool AbletonLinkSession::isEnabled() const { return false; }
void AbletonLinkSession::enableStartStopSync(bool) {}
bool AbletonLinkSession::isStartStopSyncEnabled() const { return false; }
std::size_t AbletonLinkSession::numPeers() const { return 0; }

std::int64_t AbletonLinkSession::clockMicros() const
{
	return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

LinkTimelinePoint AbletonLinkSession::appCapture(double) const
{
	LinkTimelinePoint point;
	point.tempo = m_impl->tempo;
	return point;
}

void AbletonLinkSession::appSetTempo(double bpm) { m_impl->tempo = bpm; }

LinkTimelinePoint AbletonLinkSession::audioPeriod(std::int64_t, double, const LinkAudioChange&) noexcept
{
	LinkTimelinePoint point;
	point.tempo = m_impl->tempo;
	return point;
}

std::int64_t AbletonLinkSession::hostTimeAtSample(double) noexcept { return 0; }
void AbletonLinkSession::resetHostTimeFilter() noexcept {}

} // namespace lmms
