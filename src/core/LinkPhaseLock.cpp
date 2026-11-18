/*
 * LinkPhaseLock.cpp - include/LinkPhaseLock.h: the beat-grid arithmetic of the
 *                     Ableton Link follower. Pure, allocation-free, and called
 *                     once per audio period.
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

#include "LinkPhaseLock.h"

#include <algorithm>
#include <cmath>

namespace lmms::link
{

namespace
{

//! A count-in ends when the target is within this of the held position: well
//! below one sample at any tempo, so it only absorbs floating-point noise.
constexpr double HoldEpsilonBeats = 1e-9;

} // namespace

double PhaseLock::signedDistance(double a, double b, double period)
{
	if (!(period > 0.0)) { return a - b; }
	return a - b - period * std::floor((a - b) / period + 0.5);
}

double PhaseLock::nextPhaseMatch(double fromBeat, double beat, double period)
{
	if (!(period > 0.0)) { return fromBeat; }
	const double ahead = beat - fromBeat;
	return fromBeat + (ahead - period * std::floor(ahead / period));
}

void PhaseLock::reset()
{
	m_armed = false;
	m_locked = false;
	m_offset = 0.0;
	m_holdAt = 0.0;
}

void PhaseLock::arm(const PhaseLockInput& in)
{
	m_armed = true;
	m_locked = false;
	m_holdAt = in.zeneBeat;
	// The session beat at which the play head's current position sounds.
	const double startBeat = in.joinRunning ? in.sessionStartBeat
		: nextPhaseMatch(in.linkBeat, in.zeneBeat, in.quantum);
	// A whole number of quanta, so position and session share the phase.
	m_offset = in.quantum * std::round((in.zeneBeat - startBeat) / in.quantum);
}

PhaseLockOutput PhaseLock::step(const PhaseLockInput& in)
{
	if (in.started || !m_armed)
	{
		arm(in);
		// Armed WITHOUT a start (sync switched on while the song plays): no
		// count-in - re-lock in place, at most half a quantum away.
		m_locked = !in.started;
	}
	const double target = in.linkBeat + m_offset;
	if (!m_locked)
	{
		if (target < m_holdAt - HoldEpsilonBeats)
		{
			return PhaseLockOutput{PhaseAction::Hold, target, target - in.zeneBeat, 0.0};
		}
		m_locked = true;
	}
	if (std::fabs(target - in.zeneBeat) > JumpBeats) { return relock(in, target); }
	return nudge(in, target);
}

PhaseLockOutput PhaseLock::relock(const PhaseLockInput& in, double target)
{
	// The phase-aligned position nearest the play head: a seek lands within
	// half a quantum of where it was asked to, on the session's grid.
	m_offset += in.quantum * std::round((in.zeneBeat - target) / in.quantum);
	double aligned = in.linkBeat + m_offset;
	if (aligned < 0.0)
	{
		const double quanta = std::ceil(-aligned / in.quantum);
		m_offset += quanta * in.quantum;
		aligned += quanta * in.quantum;
	}
	return PhaseLockOutput{PhaseAction::Jump, aligned, target - in.zeneBeat, aligned - in.zeneBeat};
}

PhaseLockOutput PhaseLock::nudge(const PhaseLockInput& in, double target) const
{
	const double error = target - in.zeneBeat;
	const double bound = MaxNudgeFraction * std::max(in.periodBeats, 0.0);
	return PhaseLockOutput{PhaseAction::Nudge, target, error, std::clamp(error, -bound, bound)};
}

} // namespace lmms::link
