/*
 * ScriptClock.cpp - scheduled Lua evaluation: the grid, the poll and the
 *                   bounded fire behind the `livecode.*` group (board card #708).
 *
 * See include/ScriptClock.h for where this sits and what it deliberately does
 * not touch. The one rule every function here keeps: the audio thread is never
 * on this object's path, and the only script route out of it is
 * ScriptEngine::runString - the explicit-trigger path, with the per-fire
 * instruction budget applied and restored exactly the way script.run applies
 * and restores its own `budget` argument (src/core/ControlCommandsScript.cpp).
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

#include "ScriptClock.h"

#include <algorithm>

#include <QJsonArray>
#include <QTimer>

#include "Engine.h"
#include "ScriptEngine.h"
#include "Song.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

namespace lmms
{

namespace
{

//! The three hooks, spelled the way livecode.schedule's schema spells them.
const char* const HookBar = "bar";
const char* const HookBeat = "beat";
const char* const HookTransport = "transport";

//! How many of a failure's Lua log lines are folded into lastError, so the
//! read-back shows what the script said without the record growing forever.
constexpr int MaxFoldedLogLines = 4;

//! A failed fire's message: the typed error, then the first few log lines the
//! script printed before it failed (capped, so one bad fire cannot grow the
//! schedule record without bound).
QString foldLogs(const QString& error, const QStringList& logs)
{
	if (logs.isEmpty()) { return error; }
	return error + QStringLiteral(" [log: ")
		+ logs.mid(0, MaxFoldedLogLines).join(QStringLiteral(" | ")) + QLatin1Char(']');
}

bool knownHook(const QString& hook)
{
	return hook == QLatin1String(HookBar) || hook == QLatin1String(HookBeat)
		|| hook == QLatin1String(HookTransport);
}

//! The typed name for a failed fire. The budget case is recognised by the
//! engine's own message (ScriptEngine.cpp: "instruction budget exceeded
//! (... instructions, budget ...)"), so the bound a runaway hit is readable
//! from livecode.get_state without re-running anything.
QString errorKindFor(const QString& error)
{
	return error.contains(QLatin1String("instruction budget exceeded"))
		? QStringLiteral("budget")
		: QStringLiteral("script");
}

} // namespace


// ---------------------------------------------------------------------------
// ScriptClockGrid - the pure detector
// ---------------------------------------------------------------------------

int ScriptClockGrid::consume(qint64 pos, qint64 stride, qint64* next, int* coalesced)
{
	if (stride <= 0 || pos < *next) { return 0; }
	const qint64 crossed = (pos - *next) / stride + 1;
	if (crossed > CrossCap)
	{
		*coalesced += static_cast<int>(crossed - CrossCap);
		*next = ((pos / stride) + 1) * stride;
		return CrossCap;
	}
	*next += crossed * stride;
	return static_cast<int>(crossed);
}

void ScriptClockGrid::regrid(qint64 pos, qint64 ticksPerBar, qint64 ticksPerBeat)
{
	m_ticksPerBar = ticksPerBar;
	m_nextBar = ((pos / ticksPerBar) + 1) * ticksPerBar;
	m_nextBeat = ((pos / ticksPerBeat) + 1) * ticksPerBeat;
}

ScriptTickEvents ScriptClockGrid::advance(bool playing, qint64 pos, qint64 ticksPerBar,
	qint64 ticksPerBeat)
{
	ScriptTickEvents events;
	if (ticksPerBar <= 0 || ticksPerBeat <= 0) { return events; }

	if (playing != m_playing)
	{
		events.playEdge = playing;
		events.stopEdge = !playing;
	}
	if (!playing)
	{
		m_playing = false;
		m_pos = pos;
		m_nextBar = 0;
		m_nextBeat = 0;
		return events;
	}
	// First observation, a seek backwards, or a meter change: re-anchor where
	// playback IS. Nothing crossed a boundary, so nothing fires.
	if (!m_playing || pos < m_pos || ticksPerBar != m_ticksPerBar)
	{
		events.regrids = 1;
		regrid(pos, ticksPerBar, ticksPerBeat);
		m_playing = true;
		m_pos = pos;
		return events;
	}
	events.bars = consume(pos, ticksPerBar, &m_nextBar, &events.coalesced);
	events.beats = consume(pos, ticksPerBeat, &m_nextBeat, &events.coalesced);
	m_pos = pos;
	return events;
}


// ---------------------------------------------------------------------------
// ScriptClock - registry, poll and bounded fire
// ---------------------------------------------------------------------------

ScriptClock& ScriptClock::instance()
{
	// Deliberately never destroyed: command handlers, the timer's lambda and
	// a test may reach this for the whole life of the process.
	static ScriptClock* s_instance = new ScriptClock();
	return *s_instance;
}

ScriptClock::ScriptClock() = default;

bool ScriptClock::parses(const QString& source, QString* error)
{
	// A scratch state, opened for luaL_loadbuffer only: the parser compiles
	// the chunk and never runs it, so no sandbox is needed (nothing executes)
	// and the engine's own state, worker and budgets are not involved. The
	// source is capped by the caller (SourceByteCap), which bounds what the
	// parser may hold.
	lua_State* state = luaL_newstate();
	if (state == nullptr)
	{
		*error = QStringLiteral("could not open a Lua state to check the source with");
		return false;
	}
	const QByteArray bytes = source.toUtf8();
	const int status = luaL_loadbuffer(state, bytes.constData(), static_cast<std::size_t>(bytes.size()),
		"=(livecode)");
	if (status != LUA_OK)
	{
		const char* message = lua_tostring(state, -1);
		*error = QStringLiteral("the source does not compile: %1")
			.arg(message != nullptr ? QString::fromUtf8(message) : QStringLiteral("parse error"));
	}
	lua_close(state);
	return status == LUA_OK;
}

void ScriptClock::syncTimer()
{
	if (m_timer == nullptr)
	{
		if (m_schedules.empty()) { return; }
		m_timer = new QTimer();
		m_timer->setInterval(PollMs);
		QObject::connect(m_timer, &QTimer::timeout, [this]() { pollOnce(); });
		m_timer->start();
		return;
	}
	if (m_schedules.empty()) { m_timer->stop(); }
	else if (!m_timer->isActive()) { m_timer->start(); }
}

bool ScriptClock::setSchedule(const QString& id, const QString& source, const QString& hook,
	int budget, QString* error)
{
	if (id.isEmpty())
	{
		*error = QStringLiteral("a schedule id must not be empty");
		return false;
	}
	if (!knownHook(hook))
	{
		*error = QStringLiteral("unknown hook '%1': expected bar, beat or transport").arg(hook);
		return false;
	}
	if (source.isEmpty())
	{
		*error = QStringLiteral("the source is empty: there is nothing to evaluate on the hook");
		return false;
	}
	if (source.size() > SourceByteCap)
	{
		*error = QStringLiteral("the source is %1 bytes; a scheduled script is capped at %2")
			.arg(source.size()).arg(SourceByteCap);
		return false;
	}
	if (!parses(source, error)) { return false; }

	for (ScriptSchedule& entry : m_schedules)
	{
		if (entry.id != id) { continue; }
		// A live edit: the definition moves, the slot's counters stay, so a
		// caller watching fires does not lose them to an edit.
		entry.source = source;
		entry.hook = hook;
		entry.budget = budget;
		syncTimer();
		return true;
	}
	ScriptSchedule entry;
	entry.id = id;
	entry.source = source;
	entry.hook = hook;
	entry.budget = budget;
	m_schedules.push_back(entry);
	syncTimer();
	return true;
}

bool ScriptClock::removeSchedule(const QString& id, QString* error)
{
	for (std::size_t index = 0; index < m_schedules.size(); ++index)
	{
		if (m_schedules[index].id != id) { continue; }
		m_schedules.erase(m_schedules.begin() + static_cast<std::ptrdiff_t>(index));
		syncTimer();
		return true;
	}
	*error = QStringLiteral("no scheduled script '%1'").arg(id);
	return false;
}

bool ScriptClock::hasSchedule(const QString& id) const
{
	for (const ScriptSchedule& entry : m_schedules)
	{
		if (entry.id == id) { return true; }
	}
	return false;
}

QJsonObject ScriptClock::scheduleState(const QString& id) const
{
	for (const ScriptSchedule& entry : m_schedules)
	{
		if (entry.id != id) { continue; }
		QJsonObject out;
		out.insert(QStringLiteral("id"), entry.id);
		out.insert(QStringLiteral("hook"), entry.hook);
		out.insert(QStringLiteral("budget"), entry.budget);
		out.insert(QStringLiteral("source"), entry.source);
		out.insert(QStringLiteral("source_bytes"), entry.source.size());
		return out;
	}
	return QJsonObject();
}

QString ScriptClock::newScheduleId()
{
	return QStringLiteral("lc-%1").arg(m_nextId++);
}

void ScriptClock::runEntry(ScriptSchedule& entry, int count, qint64 pos)
{
	ScriptEngine* engine = ScriptEngine::instance();
	// The budget is per fire, applied and put back the way script.run applies
	// and puts back its own `budget` argument: an override shapes these runs
	// and not the process, and budget 0 means "inherit what the engine holds".
	const quint64 previous = engine->instructionBudget();
	if (entry.budget > 0)
	{
		engine->setInstructionBudget(static_cast<quint64>(entry.budget));
	}
	for (int fired = 0; fired < count; ++fired)
	{
		QString error;
		const ScriptEngine::RunResult result = engine->runString(entry.source, &error,
			QStringLiteral("=(livecode:%1)").arg(entry.id));
		const QStringList logs = engine->takeLogMessages();
		entry.lastFirePos = pos;
		entry.fires++;
		if (result == ScriptEngine::RunResult::Ok) { continue; }
		if (result == ScriptEngine::RunResult::Busy)
		{
			// Another run was already on the worker; the boundary is dropped,
			// counted, and the schedule stays armed for the next one.
			entry.fires--;
			m_busySkips++;
			continue;
		}
		entry.errors++;
		entry.lastError = foldLogs(error, logs);
		entry.lastErrorKind = errorKindFor(entry.lastError);
		if (entry.lastErrorKind == QLatin1String("budget")) { entry.budgetExceeded++; }
	}
	if (entry.budget > 0) { engine->setInstructionBudget(previous); }
}

void ScriptClock::fireHook(const QString& hook, int count, qint64 pos)
{
	for (ScriptSchedule& entry : m_schedules)
	{
		if (entry.hook == hook) { runEntry(entry, count, pos); }
	}
}

void ScriptClock::pollOnce()
{
	Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	const bool playing = song->isPlaying();
	const qint64 pos = static_cast<qint64>(song->getPlayPos().getTicks());
	const qint64 ticksPerBar = song->ticksPerBar();
	const qint64 ticksPerBeat = std::max<qint64>(1, ticksPerBar / 4);
	const ScriptTickEvents events = m_grid.advance(playing, pos, ticksPerBar, ticksPerBeat);

	m_playing = playing;
	m_pos = pos;
	m_ticksPerBar = ticksPerBar;
	m_ticksPerBeat = ticksPerBeat;
	m_regrids += static_cast<quint64>(events.regrids);
	m_coalesced += static_cast<quint64>(events.coalesced);
	if (m_schedules.empty()) { return; }

	if (events.playEdge || events.stopEdge)
	{
		fireHook(QString::fromLatin1(HookTransport),
			(events.playEdge ? 1 : 0) + (events.stopEdge ? 1 : 0), pos);
	}
	if (events.bars > 0) { fireHook(QString::fromLatin1(HookBar), events.bars, pos); }
	if (events.beats > 0) { fireHook(QString::fromLatin1(HookBeat), events.beats, pos); }
}

QJsonObject ScriptClock::stateJson() const
{
	QJsonObject out;
	out.insert(QStringLiteral("poll_ms"), PollMs);
	out.insert(QStringLiteral("playing"), m_playing);
	out.insert(QStringLiteral("position_ticks"), m_pos);
	out.insert(QStringLiteral("ticks_per_bar"), m_ticksPerBar);
	out.insert(QStringLiteral("ticks_per_beat"), m_ticksPerBeat);
	out.insert(QStringLiteral("next_bar_ticks"), m_grid.nextBar());
	out.insert(QStringLiteral("next_beat_ticks"), m_grid.nextBeat());
	out.insert(QStringLiteral("count"), static_cast<qint64>(m_schedules.size()));

	QJsonArray schedules;
	quint64 fires = 0;
	quint64 errors = 0;
	quint64 budgetExceeded = 0;
	for (const ScriptSchedule& entry : m_schedules)
	{
		QJsonObject one;
		one.insert(QStringLiteral("id"), entry.id);
		one.insert(QStringLiteral("hook"), entry.hook);
		one.insert(QStringLiteral("budget"), entry.budget);
		one.insert(QStringLiteral("source_bytes"), entry.source.size());
		one.insert(QStringLiteral("fires"), static_cast<qint64>(entry.fires));
		one.insert(QStringLiteral("errors"), static_cast<qint64>(entry.errors));
		one.insert(QStringLiteral("budget_exceeded"), static_cast<qint64>(entry.budgetExceeded));
		one.insert(QStringLiteral("last_fire_ticks"), entry.lastFirePos);
		one.insert(QStringLiteral("last_error"), entry.lastError);
		one.insert(QStringLiteral("last_error_kind"), entry.lastErrorKind);
		schedules.append(one);
		fires += entry.fires;
		errors += entry.errors;
		budgetExceeded += entry.budgetExceeded;
	}
	out.insert(QStringLiteral("schedules"), schedules);

	QJsonObject counters;
	counters.insert(QStringLiteral("fires"), static_cast<qint64>(fires));
	counters.insert(QStringLiteral("errors"), static_cast<qint64>(errors));
	counters.insert(QStringLiteral("budget_exceeded"), static_cast<qint64>(budgetExceeded));
	counters.insert(QStringLiteral("busy_skips"), static_cast<qint64>(m_busySkips));
	counters.insert(QStringLiteral("regrids"), static_cast<qint64>(m_regrids));
	counters.insert(QStringLiteral("coalesced"), static_cast<qint64>(m_coalesced));
	out.insert(QStringLiteral("counters"), counters);
	return out;
}

} // namespace lmms
