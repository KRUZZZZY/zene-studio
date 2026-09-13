/*
 * ControlCommandsSessionShared.h - what the two halves of the session.*
 *                                  command group share (SPEC A11-A16).
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

/* Why this header exists, and why it is private to src/core.
 *
 * The session.* group is the session MODEL edits (ControlCommandsSession.cpp)
 * and the LAUNCH requests (ControlCommandsSessionLaunch.cpp). Both halves need
 * the same three things: the names of the two closed vocabularies the data
 * layer persists (LaunchMode, LaunchQuantisation), the grid bounds check, and
 * the launch-selection rule that resolves Global against the session default.
 *
 * The alternative - one file - does not fit: the group plus its emitters is
 * over the 500-line file ratchet, and that ratchet is not moved for
 * convenience. The other alternative - each half carrying its own copy - is
 * the defect this codebase has already paid for four times (see the header of
 * ControlVocabulary.cpp: two lanes re-deriving the same helper, ending in a
 * duplicate-symbol link failure). So the shared half lives here, exactly as
 * SessionModelPrivate.h holds the session serialisation shared by
 * SessionModel.cpp and SessionClip.cpp.
 *
 * Nothing is exported: these are inline definitions in a private namespace, and
 * every name list is function-local so no translation unit depends on another
 * one's static initialisation order.
 */

#ifndef LMMS_CONTROL_COMMANDS_SESSION_SHARED_H
#define LMMS_CONTROL_COMMANDS_SESSION_SHARED_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlRegistry.h"
#include "Engine.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{
namespace sessioncontrol
{

// ---------------------------------------------------------------------------
// The closed vocabularies. Each list is in the ORDER of its enum, so the index
// IS the value and a name can never drift from what SessionModel serialises.
// ---------------------------------------------------------------------------

inline const QStringList& modeNames()
{
	static const QStringList names{
		QStringLiteral("trigger"), QStringLiteral("gate"),
		QStringLiteral("toggle"), QStringLiteral("repeat")};
	return names;
}

//! The session default's own vocabulary: "global" is excluded on purpose,
//! because a default that defers to itself has no meaning.
inline const QStringList& quantisationNames()
{
	static const QStringList names{
		QStringLiteral("none"), QStringLiteral("bar"),
		QStringLiteral("two_bars"), QStringLiteral("four_bars")};
	return names;
}

inline const QStringList& followActionNames()
{
	static const QStringList names{
		QStringLiteral("none"), QStringLiteral("stop"), QStringLiteral("play_again"),
		QStringLiteral("previous"), QStringLiteral("next"), QStringLiteral("first"),
		QStringLiteral("last"), QStringLiteral("any"), QStringLiteral("other"),
		QStringLiteral("jump")};
	return names;
}

/*! The per-clip quantisation vocabulary: the session default's set plus
 *  "global", which is only legal on a CLIP (a default that deferred to itself
 *  would mean nothing). Built by appending, NOT by adding two QStringLists:
 *  under Qt5 a QStringList sum resolves through QList<QString>'s own operator+
 *  and comes back as a QList<QString>, which is not a QStringList and does not
 *  convert to one, while under Qt6 the two are the same type. A Qt6-only
 *  compile is a red build on the five Qt5 jobs, so the construct is not used. */
inline const QStringList& perClipQuantisationNames()
{
	static const QStringList names = []() {
		QStringList list;
		list << QStringLiteral("global");
		list << quantisationNames();
		return list;
	}();
	return names;
}

//! The clip-content vocabulary (ClipSlot::Type), for session.set_slot's 'type'.
inline const QStringList& slotTypeNames()
{
	static const QStringList names = []() {
		QStringList list;
		list << QStringLiteral("empty") << QStringLiteral("midi") << QStringLiteral("audio");
		return list;
	}();
	return names;
}

inline QString modeName(LaunchMode mode)
{
	const int index = static_cast<int>(mode);
	return index >= 0 && index < modeNames().size() ? modeNames().at(index) : modeNames().at(0);
}

inline LaunchMode modeFromName(const QString& name)
{
	const int index = modeNames().indexOf(name);
	return index < 0 ? LaunchMode::Trigger : static_cast<LaunchMode>(index);
}

inline QString quantisationName(LaunchQuantisation value)
{
	switch (value)
	{
		case LaunchQuantisation::None:     return QStringLiteral("none");
		case LaunchQuantisation::Bar:      return QStringLiteral("bar");
		case LaunchQuantisation::TwoBars:  return QStringLiteral("two_bars");
		case LaunchQuantisation::FourBars: return QStringLiteral("four_bars");
		case LaunchQuantisation::Global:   break;
	}
	return QStringLiteral("global");
}

inline LaunchQuantisation quantisationFromName(const QString& name)
{
	const int index = quantisationNames().indexOf(name);
	return index < 0 ? LaunchQuantisation::Global : static_cast<LaunchQuantisation>(index);
}

// ---------------------------------------------------------------------------
// addressing a cell of the grid
// ---------------------------------------------------------------------------

inline SessionModel* sessionModelOrNull(ControlResult* error)
{
	Song* song = Engine::getSong();
	if (song == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("this instance has no song: no project is loaded, so there is no "
				"Session View to address"));
		return nullptr;
	}
	return &song->sessionModel();
}

/*! The session grid is its own model with its own dimensions, so every cell
 *  address is bounds-checked BEFORE the mutable slot() accessor is used:
 *  out-of-range writes land in a throwaway slot rather than corrupting the
 *  grid, and a caller that got an ok reply would never know. */
inline bool gridContains(const SessionModel& model, int track, int scene, ControlResult* error)
{
	if (track >= 0 && track < model.trackCount()
		&& scene >= 0 && scene < model.sceneCount())
	{
		return true;
	}
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("slot (track %1, scene %2) is outside the grid (%3 tracks x %4 scenes); "
			"session.set_grid resizes it").arg(track).arg(scene)
			.arg(model.trackCount()).arg(model.sceneCount()));
	return false;
}

//! The song's view of the clock, which is what the launch decision is made
//! against on the model thread (the audio thread re-reads it as its own).
inline SessionClockContext clockOf(Song& song)
{
	SessionClockContext ctx;
	ctx.positionTicks = song.getPlayPos(Song::PlayMode::Song).getTicks();
	ctx.ticksPerBar = song.ticksPerBar();
	ctx.transportRunning = song.isPlaying();
	return ctx;
}

// ---------------------------------------------------------------------------
// the launch-selection rule
// ---------------------------------------------------------------------------

//! The settings a launch actually uses: the slot's own unless the request
//! overrides them, with Global always resolved against the session default (the
//! only legal meaning of Global).
struct LaunchRequest
{
	LaunchMode mode = LaunchMode::Trigger;
	LaunchQuantisation quantisation = LaunchQuantisation::Bar;
};

inline LaunchRequest launchRequestOf(const SessionModel& model, const ClipSlot& slot,
	const QJsonObject& args)
{
	LaunchRequest request;
	request.mode = args.contains(QStringLiteral("mode"))
		? modeFromName(args.value(QStringLiteral("mode")).toString())
		: slot.launchMode();
	const LaunchQuantisation perClip = args.contains(QStringLiteral("quantisation"))
		? quantisationFromName(args.value(QStringLiteral("quantisation")).toString())
		: slot.launchQuantisation();
	request.quantisation = resolveQuantisation(perClip, model.globalLaunchQuantisation());
	return request;
}

// ---------------------------------------------------------------------------
// state emitters
//
// Shared because BOTH halves report slot state: the model half in
// session.get_state, the launch half as the echo of what it just launched. One
// emitter, so the two can never describe a slot differently.
// ---------------------------------------------------------------------------

inline QString clipSlotTypeName(const ClipSlot& slot)
{
	switch (slot.type())
	{
		case ClipSlot::Type::Midi:  return QStringLiteral("midi");
		case ClipSlot::Type::Audio: return QStringLiteral("audio");
		case ClipSlot::Type::Empty: break;
	}
	return QStringLiteral("empty");
}

inline QJsonObject followActionState(const FollowAction& action)
{
	const int type = static_cast<int>(action.type);
	const bool known = type >= 0 && type < followActionNames().size();
	QJsonObject state;
	state.insert(QStringLiteral("type"),
		known ? followActionNames().at(type) : followActionNames().at(0));
	state.insert(QStringLiteral("chance"), action.chance);
	state.insert(QStringLiteral("linked"), action.linked);
	state.insert(QStringLiteral("time_bars"), action.timeBars);
	state.insert(QStringLiteral("jump_to"), action.jumpTo);
	return state;
}

inline QJsonObject clipSlotState(int track, int scene, const ClipSlot& slot)
{
	QJsonObject state;
	state.insert(QStringLiteral("track"), track);
	state.insert(QStringLiteral("scene"), scene);
	state.insert(QStringLiteral("type"), clipSlotTypeName(slot));
	state.insert(QStringLiteral("name"), slot.name());
	state.insert(QStringLiteral("pattern"), slot.patternId());
	state.insert(QStringLiteral("source"), slot.audioSource());
	state.insert(QStringLiteral("mode"), modeName(slot.launchMode()));
	state.insert(QStringLiteral("quantisation"), quantisationName(slot.launchQuantisation()));
	state.insert(QStringLiteral("legato"), slot.legato());
	state.insert(QStringLiteral("loop_start"), slot.loopStart());
	state.insert(QStringLiteral("loop_length"), slot.loopLength());
	state.insert(QStringLiteral("gain_db"), static_cast<double>(slot.gainDb()));
	state.insert(QStringLiteral("transpose"), slot.transpose());
	state.insert(QStringLiteral("detune"), slot.detune());
	state.insert(QStringLiteral("ram"), slot.ramMode());
	QJsonArray follow;
	for (const FollowAction& action : slot.followActions())
	{
		follow.append(followActionState(action));
	}
	state.insert(QStringLiteral("follow_actions"), follow);
	return state;
}

inline QJsonObject sceneState(int index, const Scene& scene)
{
	QJsonObject state;
	state.insert(QStringLiteral("scene"), index);
	state.insert(QStringLiteral("name"), scene.name());
	state.insert(QStringLiteral("tempo_enabled"), scene.tempoEnabled());
	state.insert(QStringLiteral("tempo"), scene.tempo());
	state.insert(QStringLiteral("timesig_enabled"), scene.timeSigEnabled());
	state.insert(QStringLiteral("numerator"), scene.timeSigNumerator());
	state.insert(QStringLiteral("denominator"), scene.timeSigDenominator());
	return state;
}

//! Every non-empty slot, one array, in (track, scene) order. `clips` in the grid
//! the model half reports is this array's size, so the two cannot disagree.
inline QJsonArray clipSlots(const SessionModel& model)
{
	QJsonArray array;
	for (int track = 0; track < model.trackCount(); ++track)
	{
		for (int scene = 0; scene < model.sceneCount(); ++scene)
		{
			const ClipSlot& slot = model.slot(track, scene);
			if (!slot.isEmpty()) { array.append(clipSlotState(track, scene, slot)); }
		}
	}
	return array;
}

//! NOTE the parameter is `entries`, not `slots`: Qt defines `slots` as a keyword
//! macro (Q_SLOTS), so a variable of that name is erased at preprocessing time -
//! the same trap SessionModel.cpp records for its own local.
inline QJsonObject gridState(const SessionModel& model, const QJsonArray& entries)
{
	QJsonObject grid;
	grid.insert(QStringLiteral("tracks"), model.trackCount());
	grid.insert(QStringLiteral("scenes"), model.sceneCount());
	grid.insert(QStringLiteral("clips"), entries.size());
	return grid;
}

inline QJsonArray sceneStates(const SessionModel& model)
{
	QJsonArray scenes;
	for (int scene = 0; scene < model.sceneCount(); ++scene)
	{
		scenes.append(sceneState(scene, model.scene(scene)));
	}
	return scenes;
}

/*! The launch engine's read-back - the half of the Session View a client cannot
 *  see in the model: how many clips have started, the grid line the newest start
 *  events fired on and HOW MANY fired on that one line, and the audio clock when
 *  they were noticed. Every field comes from an atomic that is safe to read from
 *  the model thread (SessionScheduler.h); the audio thread's slot table is
 *  deliberately NOT read here. */
inline QJsonObject launchState(Song& song, const SessionScheduler& scheduler)
{
	const SessionClockContext ctx = clockOf(song);
	QJsonObject launch;
	launch.insert(QStringLiteral("transport_running"), ctx.transportRunning);
	launch.insert(QStringLiteral("position"), ctx.positionTicks);
	launch.insert(QStringLiteral("ticks_per_bar"), ctx.ticksPerBar);
	launch.insert(QStringLiteral("next_bar"), launchTickAt(LaunchQuantisation::Bar, ctx));
	launch.insert(QStringLiteral("completed_launches"),
		static_cast<double>(scheduler.completedLaunches()));
	launch.insert(QStringLiteral("processed_commands"),
		static_cast<double>(scheduler.processedCommands()));
	launch.insert(QStringLiteral("dropped_commands"),
		static_cast<double>(scheduler.droppedCommands()));
	const std::uint64_t line = scheduler.lastStartLine();
	launch.insert(QStringLiteral("start_line"), static_cast<int>(startLineTick(line)));
	launch.insert(QStringLiteral("start_line_starts"), static_cast<int>(startLineStarts(line)));
	launch.insert(QStringLiteral("start_observed"),
		static_cast<int>(scheduler.lastStartObservedTick()));
	return launch;
}

} // namespace sessioncontrol
} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_SESSION_SHARED_H
