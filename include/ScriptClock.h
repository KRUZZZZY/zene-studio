/*
 * ScriptClock.h - SCHEDULED Lua evaluation: the idle and transport hooks the
 *                 Lua engine did not have (board card #708), and the object
 *                 the `livecode.*` command group drives.
 *
 * WHAT WAS MISSING. docs/specs/SPEC-lua-api-v0.md (pinned snapshot, line 78)
 * and its OQ-1 record that a v0 script runs ONLY on an explicit trigger - the
 * menu action, `--run-script`, `script.run`. Nothing in the tree asked the
 * engine to evaluate a script when the TRANSPORT crossed a bar or beat, or on
 * a play/stop edge. This is that scheduler.
 *
 * WHERE IT SITS, AND WHAT IT DELIBERATELY DOES NOT TOUCH. Every access to the
 * audio thread goes through none of this: the transport is OBSERVED from the
 * control thread (the same Song accessors transport.get_state already reads),
 * on a QTimer, and each fire runs through ScriptEngine::runString - the exact
 * explicit-trigger path script.run uses, on the worker thread, with the engine
 * apply side pumped on the calling (control) thread. The audio thread's rule
 * (spec section 4: no Lua on the audio thread, no allocation, no locks) is
 * therefore kept by construction: this file adds no call to any audio path and
 * rewrites none. A runaway script is bounded by the per-fire instruction budget
 * the same way script.run bounds it, so the worst it can do is block the
 * control thread for that budget - never the audio thread.
 *
 * THE GRID IS PURE. ScriptClockGrid is arithmetic over (playing, position,
 * meter) observations: no Engine, no Song, no lua_State. tests/src/core/
 * ScriptClockTest.cpp drives it with synthetic positions, which is why the
 * boundary rules (edges, crossings, seeks, the per-poll cap) can be proved
 * without an audio device.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef LMMS_SCRIPT_CLOCK_H
#define LMMS_SCRIPT_CLOCK_H

#include <QJsonObject>
#include <QString>

#include <vector>

class QTimer;

namespace lmms
{

//! What ONE observation of the transport reported: the edges and the number of
//! boundaries crossed between the previous observation and this one. Pure data.
struct ScriptTickEvents
{
	bool playEdge = false;  //!< stopped -> playing (a transport hook fires)
	bool stopEdge = false;  //!< playing -> stopped (a transport hook fires)
	int bars = 0;           //!< bar boundaries crossed, capped at CrossCap
	int beats = 0;          //!< beat boundaries crossed, capped at CrossCap
	int coalesced = 0;      //!< crossings past the cap: dropped by a regrid
	int regrids = 0;        //!< grid rebuilds (first observation, seek, meter)
};

/*! The boundary detector: pure arithmetic over transport observations.
 *
 *  A bar (or beat) "fires" when the closed interval (previous, current]
 *  contains a grid boundary. Three rules bound it:
 *
 *    * a play/stop EDGE is detected from the flag, so a transport hook fires
 *      even when the stop happens with no boundary in the same poll;
 *    * a SEEK backwards, the first observation and a meter change RE-ANCHOR the
 *      grid where playback is and fire nothing - a seek is not a boundary
 *      playback crossed;
 *    * at most CrossCap crossings of one stride are reported per observation;
 *      past that (a seek forwards, or a control thread that was blocked inside
 *      a long script.run) the grid re-anchors and the excess is counted as
 *      `coalesced` rather than firing a thousand stale bars.
 */
class ScriptClockGrid
{
public:
	//! Feed one observation: \a playing and \a pos are the transport flag and
	//! play-head position in ticks, \a ticksPerBar and \a ticksPerBeat the grid
	//! strides to detect against (both must be > 0 or nothing is detected).
	ScriptTickEvents advance(bool playing, qint64 pos, qint64 ticksPerBar, qint64 ticksPerBeat);

	qint64 nextBar() const { return m_nextBar; }
	qint64 nextBeat() const { return m_nextBeat; }
	qint64 position() const { return m_pos; }
	bool playing() const { return m_playing; }

	//! Crossings one observation may report before it regrids instead.
	static constexpr int CrossCap = 4;

private:
	void regrid(qint64 pos, qint64 ticksPerBar, qint64 ticksPerBeat);
	static int consume(qint64 pos, qint64 stride, qint64* next, int* coalesced);

	bool m_playing = false;
	qint64 m_pos = 0;
	qint64 m_ticksPerBar = 0;
	qint64 m_ticksPerBeat = 0;
	qint64 m_nextBar = 0;
	qint64 m_nextBeat = 0;
};

//! One scheduled script - the state `livecode.schedule` writes and
//! `livecode.get_state` reads back.
struct ScriptSchedule
{
	QString id;         //!< "lc-<n>" or the caller's own stable id
	QString source;     //!< the Lua text, captured ONCE when the schedule was made
	QString hook;       //!< "bar" | "beat" | "transport"
	//! Per-fire instruction budget in instructions; 0 = inherit the engine's
	//! budget at fire time (the bound script.run's own `budget` argument
	//! overrides for one explicit run).
	int budget = 0;

	quint64 fires = 0;          //!< dispatches that completed (Ok or an error)
	quint64 errors = 0;         //!< dispatches that failed
	quint64 budgetExceeded = 0; //!< failures whose message is the typed budget bound
	qint64 lastFirePos = -1;    //!< play-head ticks when the slot fired last
	QString lastError;          //!< the last failure, logs folded in
	QString lastErrorKind;      //!< "budget" | "script" | ""
};

/*! The scheduler itself: the registry of scheduled scripts, the poll that
 *  observes the transport, and the bounded fire.
 *
 *  Thread: everything here runs on the control thread - the command handlers
 *  that call setSchedule/removeSchedule/stateJson (the registry guarantees
 *  handlers run on the UI thread) and the QTimer that calls pollOnce. Nothing
 *  is shared with the audio thread, so no lock exists to take; the only other
 *  thread reached is the ScriptEngine's own worker, through the same
 *  runString()/pump path script.run uses.
 */
class ScriptClock
{
public:
	//! How often the transport is observed, ms. The fire therefore lands at
	//! most one poll after the boundary - stated in docs/KNOWN-LIMITATIONS.md.
	static constexpr int PollMs = 25;
	//! A scheduled source is capped so the schedule, its transaction record
	//! (before + inverse both carry it) and its compile check stay inside
	//! control::MaxTransactionBytes with room to spare.
	static constexpr int SourceByteCap = 65536;

	//! Never destroyed (the MidiClock singleton's argument): command handlers,
	//! the timer and tests reach it for the process's life.
	static ScriptClock& instance();

	//! Register \a id, or REPLACE the schedule already held under it (a live
	//! edit: the counters stay, the source/hook/budget move). \a source is
	//! COMPILE-CHECKED here - luaL_loadbuffer parses it without running it - so
	//! a syntax error is a typed refusal at schedule time instead of a silent
	//! per-bar failure. \a error says why when false comes back.
	bool setSchedule(const QString& id, const QString& source, const QString& hook, int budget,
		QString* error);
	//! Remove \a id; false (with \a error) when no schedule carries it.
	bool removeSchedule(const QString& id, QString* error);
	bool hasSchedule(const QString& id) const;
	//! The schedule as livecode.get_state and the transaction records see it,
	//! or an empty object when \a id is not scheduled.
	QJsonObject scheduleState(const QString& id) const;
	//! The next free "lc-<n>" id (the schedule command's default).
	QString newScheduleId();
	int scheduleCount() const { return static_cast<int>(m_schedules.size()); }

	//! One observation: read the transport, advance the grid, fire the hooks
	//! whose boundaries it crossed. The QTimer calls this; tests call it direct.
	void pollOnce();

	//! The whole surface as one JSON object (livecode.get_state's payload).
	QJsonObject stateJson() const;

	//! For the unit test: the grid this clock drives.
	ScriptClockGrid& grid() { return m_grid; }

private:
	ScriptClock();

	//! Create/start/stop the QTimer so it runs iff something is scheduled.
	void syncTimer();
	void fireHook(const QString& hook, int count, qint64 pos);
	void runEntry(ScriptSchedule& entry, int count, qint64 pos);
	//! Parse-only check on a scratch state; no code runs and no engine is touched.
	static bool parses(const QString& source, QString* error);

	ScriptClockGrid m_grid;
	std::vector<ScriptSchedule> m_schedules;
	QTimer* m_timer = nullptr;
	unsigned m_nextId = 1;
	quint64 m_busySkips = 0;
	quint64 m_regrids = 0;
	quint64 m_coalesced = 0;

	// The last observation, for stateJson (the grid holds the grid side).
	bool m_playing = false;
	qint64 m_pos = 0;
	qint64 m_ticksPerBar = 0;
	qint64 m_ticksPerBeat = 0;
};

} // namespace lmms

#endif // LMMS_SCRIPT_CLOCK_H
