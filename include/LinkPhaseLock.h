/*
 * LinkPhaseLock.h - the arithmetic that keeps Zene's play head on an Ableton
 *                   Link session's beat grid (docs/LINK-SYNC.md section 7).
 *
 * Pure: no Link, no engine, no clock. The audio-thread follower
 * (include/LinkAudioFollower.h) reads the session beat at a period's host time
 * and Zene's own play head, hands both to step(), and applies what comes back.
 * Every number here is in BEATS (a quarter note; Zene's DefaultTicksPerBar / 4
 * ticks), which is what a Link beat is.
 *
 * The model:
 *  - Zene's position = session beat + offset, where the offset is a whole number
 *    of quanta. That is "phase-aligned": the position modulo the quantum IS the
 *    session's phase. The magnitude of a Link beat is local to each participant
 *    (Link.hpp beatAtTime), so only the phase is ever compared.
 *  - A transport START arms the lock and picks the offset:
 *      * joining a session that is already playing (start/stop sync, the
 *        session started before this start) - the offset that puts Zene where
 *        it would be had it started with the session, to the nearest quantum;
 *      * otherwise - the next session time whose phase matches Zene's position:
 *        Zene HOLDS (renders nothing, does not advance) until then. That is
 *        Link's "quantized launch" (Link.hpp requestBeatAtTime), the count-in a
 *        Link app performs when started on its own.
 *  - Armed WITHOUT a start (sync switched on while the song already plays):
 *    no count-in; the lock re-locks in place (the Jump below).
 *  - LOCKED, each period nudges the play head toward the target by at most
 *    MaxNudgeFraction of the period - a bounded rate correction that absorbs the
 *    engine running at a whole-number tempo while the session runs at a
 *    fractional one, and the drift between the sound card's clock and Link's.
 *  - An error above JumpBeats (a seek, an xrun, a peer that re-mapped the grid)
 *    re-locks to the phase-aligned position NEAREST the play head (at most half
 *    a quantum away) and jumps there.
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

#ifndef LMMS_LINK_PHASE_LOCK_H
#define LMMS_LINK_PHASE_LOCK_H

#include "lmms_export.h"

namespace lmms::link
{

struct PhaseLockInput
{
	double linkBeat = 0.0;         //!< the session beat at this period's host time
	double quantum = 4.0;          //!< beats per quantum (> 0)
	double zeneBeat = 0.0;         //!< Zene's play head at this period's start
	double periodBeats = 0.0;      //!< how far one period advances at the session tempo
	bool started = false;          //!< Zene's transport started since the last period
	bool joinRunning = false;      //!< start/stop sync: join a session already playing
	double sessionStartBeat = 0.0; //!< the session beat at its start time (joinRunning)
};

enum class PhaseAction
{
	Hold,   //!< count-in: render nothing, do not advance
	Nudge,  //!< move the play head by moveBeats (bounded) and render
	Jump    //!< reposition the play head by moveBeats (re-lock) and render
};

struct PhaseLockOutput
{
	PhaseAction action = PhaseAction::Nudge;
	double targetBeat = 0.0;   //!< where the session says the play head belongs
	double errorBeats = 0.0;   //!< target - play head, before the action
	double moveBeats = 0.0;    //!< what to add to the play head
};

class LMMS_EXPORT PhaseLock
{
public:
	//! Above this the lock re-locks and jumps (1/8 beat: 62.5 ms at 120 BPM).
	static constexpr double JumpBeats = 0.125;
	//! The largest per-period correction, as a fraction of the period (5 %).
	static constexpr double MaxNudgeFraction = 0.05;

	//! Forget the lock (transport stopped, session left).
	void reset();
	bool locked() const { return m_locked; }
	bool armed() const { return m_armed; }
	double offset() const { return m_offset; }

	PhaseLockOutput step(const PhaseLockInput& in);

	//! a - b wrapped into (-period/2, period/2].
	static double signedDistance(double a, double b, double period);
	//! The first beat at or after \a fromBeat whose phase equals \a beat's.
	static double nextPhaseMatch(double fromBeat, double beat, double period);

private:
	void arm(const PhaseLockInput& in);
	PhaseLockOutput relock(const PhaseLockInput& in, double target);
	PhaseLockOutput nudge(const PhaseLockInput& in, double target) const;

	bool m_armed = false;
	bool m_locked = false;
	double m_offset = 0.0;
	double m_holdAt = 0.0;
};

} // namespace lmms::link

#endif // LMMS_LINK_PHASE_LOCK_H
