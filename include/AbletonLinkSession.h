/*
 * AbletonLinkSession.h - this process's participant in an Ableton Link session:
 *                        a narrow wrapper around the vendored ableton::Link
 *                        (src/3rdparty/ableton-link, docs/LINK-SYNC.md).
 *
 * WHY A WRAPPER. Link is header-only and drags standalone asio into every
 * translation unit that includes it. Exactly one TU includes it
 * (src/core/AbletonLinkSession.cpp, its own static library), so the rest of the
 * core neither compiles asio nor sees its macros, and a build without Link
 * (WANT_ABLETON_LINK=OFF) links src/core/AbletonLinkSessionStub.cpp against this
 * same header instead - no #ifdef anywhere else.
 *
 * THREADING, as Link's own documentation prescribes it (Link.hpp):
 *  - enable/startStopSync/numPeers/clockMicros: thread-safe (Link's own words);
 *  - appCapture/appSetTempo: application threads only (captureAppSessionState /
 *    commitAppSessionState). appSetTempo is for when the audio thread is NOT
 *    running - Link advises against modifying the session from an application
 *    thread while audio runs;
 *  - audioPeriod/hostTimeAtSample/resetHostTimeFilter: the audio thread only.
 *    audioPeriod captures the session with captureAudioSessionState, applies the
 *    requested change in that local scope and commits it with
 *    commitAudioSessionState - realtime-safe by Link's contract, and this
 *    wrapper adds no allocation, lock or log (AbletonLinkSessionTest measures it).
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

#ifndef LMMS_ABLETON_LINK_SESSION_H
#define LMMS_ABLETON_LINK_SESSION_H

#include <cstddef>
#include <cstdint>
#include <memory>

#include "lmms_export.h"

namespace lmms
{

//! One reading of the session timeline at one host time, for one quantum.
struct LinkTimelinePoint
{
	double tempo = 0.0;          //!< session tempo, BPM
	double beat = 0.0;           //!< beatAtTime(hostTime, quantum): local magnitude, shared phase
	double phase = 0.0;          //!< phaseAtTime(hostTime, quantum), in [0, quantum)
	bool playing = false;        //!< the session's start/stop state
	double startBeat = 0.0;      //!< beatAtTime(timeForIsPlaying(), quantum)
	std::int64_t startTimeUs = 0; //!< timeForIsPlaying(), Link clock microseconds
	//! audioPeriod() only: this period committed a start or stop of its own.
	bool committedPlaying = false;
};

//! What the audio thread asks the session to change in one period.
struct LinkAudioChange
{
	double tempo = 0.0;          //!< > 0: setTempo(tempo, hostTime)
	int playing = -1;            //!< 0 or 1, committed only when it changes the session
	double requestBeat = 0.0;    //!< alone in the session: the beat a start maps to hostTime
};

class LMMS_EXPORT AbletonLinkSession
{
public:
	//! True when this build carries the vendored library (WANT_ABLETON_LINK).
	static bool compiledIn();
	//! The pinned release ("4.1"), or "" without the library.
	static const char* libraryVersion();
	//! The pinned upstream commit, or "" without the library.
	static const char* libraryCommit();

	explicit AbletonLinkSession(double bpm);
	~AbletonLinkSession();
	AbletonLinkSession(const AbletonLinkSession&) = delete;
	AbletonLinkSession& operator=(const AbletonLinkSession&) = delete;

	void enable(bool on);
	bool isEnabled() const;
	void enableStartStopSync(bool on);
	bool isStartStopSyncEnabled() const;
	std::size_t numPeers() const;
	//! Link's clock, microseconds - the clock every host time here is on.
	std::int64_t clockMicros() const;

	//! Application thread: the session at `now`.
	LinkTimelinePoint appCapture(double quantum) const;
	//! Application thread, audio thread NOT running: declare a tempo now.
	void appSetTempo(double bpm);

	/*! Audio thread: capture the session, apply `change` at `hostTimeUs` and
	 *  commit it if anything changed, then return the timeline at `hostTimeUs`.
	 *  Never allocates, locks or logs. */
	LinkTimelinePoint audioPeriod(std::int64_t hostTimeUs, double quantum,
		const LinkAudioChange& change) noexcept;
	/*! Audio thread: the host time of a running sample count, through Link's
	 *  HostTimeFilter (a linear regression of the sample clock against Link's
	 *  clock over the last 512 periods, preallocated). */
	std::int64_t hostTimeAtSample(double sampleTime) noexcept;
	void resetHostTimeFilter() noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace lmms

#endif // LMMS_ABLETON_LINK_SESSION_H
