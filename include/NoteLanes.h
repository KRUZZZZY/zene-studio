/*
 * NoteLanes.h - the note editor's per-note expression lanes, as data
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
 *
 */

#ifndef LMMS_GUI_NOTE_LANES_H
#define LMMS_GUI_NOTE_LANES_H

#include <QJsonObject>
#include <QList>
#include <QString>

#include "lmms_export.h"

namespace lmms
{
class Note;
}

namespace lmms::gui
{

//! One editable per-note value. Each lane reads the note and writes through ONE registered
//! command, so an edit in the lane and the same edit over the socket are the same call.
enum class NoteLane
{
	Velocity,  //!< note volume 0..200 (the engine's velocity; 100 = default) - note.edit
	Pan,       //!< -100..100 - note.edit
	Chance,    //!< play probability 0..100 % - note.edit
	Timbre,    //!< MPE timbre (CC74) 0..127 - note.expression_set
	Pressure,  //!< MPE pressure 0..127 - note.expression_set
	Pitch      //!< micro-pitch offset in cents -4800..4800 (drawn +-200) - note.expression_set
};

struct NoteLaneSpec
{
	NoteLane lane;
	QString id;      //!< stable name: "velocity", "pan", "chance", "timbre", "pressure", "pitch"
	QString title;
	int minimum;
	int maximum;
	int neutral;     //!< where a lane's bars grow from (0 for pan and pitch, the minimum otherwise)
	int drawRange;   //!< the span the lane draws (pitch draws +-200 cents, not +-4800)
	QString unit;
};

namespace notelanes
{

LMMS_EXPORT QList<NoteLaneSpec> all();
LMMS_EXPORT NoteLaneSpec spec(NoteLane lane);
//! false for an unknown id; @a out is untouched then.
LMMS_EXPORT bool fromId(const QString& id, NoteLane& out);

//! The lane's value for @a note, in the lane's own units.
LMMS_EXPORT int valueOf(const Note& note, NoteLane lane);
//! A value clamped to the lane's range.
LMMS_EXPORT int clamp(NoteLane lane, int value);

//! The command that writes @a value to the note @a noteId of @a clipId, and its arguments.
struct Write
{
	QString command;
	QJsonObject args;
};
LMMS_EXPORT Write writeFor(NoteLane lane, const QString& clipId, const QString& noteId, int value);

} // namespace notelanes

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_LANES_H
