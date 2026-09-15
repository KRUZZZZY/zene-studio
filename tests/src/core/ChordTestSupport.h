/*
 * ChordTestSupport.h - what the chord tests share: the command driver, the
 *                      fixture builders, and the read-backs they assert on.
 *
 * ONE copy, in the repo's own convention for a shared test helper
 * (GrooveTestSupport.h, ReversibilityTestSupport.h, RackTestSupport.h): the
 * engine tests (ChordTrackTest.cpp, ChordDetectTest.cpp,
 * ChordProgressionTest.cpp) and the surface test (ControlChordCommandsTest.cpp)
 * all need the same "what notes does this clip hold" and "what does the chord
 * track hold" readings, and two copies of either would be two chances to assert
 * on the wrong field.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_TESTS_CHORD_TEST_SUPPORT_H
#define LMMS_TESTS_CHORD_TEST_SUPPORT_H

#include <tuple>
#include <utility>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ChordTrack.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Note.h"

namespace chordtest
{

//! The command group's ids, in the order the contract lists them.
inline QStringList chordIds()
{
	return {QStringLiteral("chord.get_state"), QStringLiteral("chord.detect"),
		QStringLiteral("chord.progression_list"), QStringLiteral("chord.set"),
		QStringLiteral("chord.remove"), QStringLiteral("chord.clear"),
		QStringLiteral("chord.detect_to_track"), QStringLiteral("chord.track_write"),
		QStringLiteral("chord.progression_generate")};
}

//! Invoke a command through the registry, exactly as the socket does.
inline lmms::ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return lmms::ControlRegistry::instance()->invoke(id, args);
}

/*! A fresh track and one empty clip on it, created through the product's own
 *  commands, so no fixture file is needed and the clip id comes back from the
 *  command rather than from a position a test guesses. */
inline QString makeClip(const QString& type = QStringLiteral("instrument"))
{
	const lmms::ControlResult track =
		run(QStringLiteral("track.add"), {{QStringLiteral("type"), type}});
	if (!track.ok) { return QString(); }
	const lmms::ControlResult clip = run(QStringLiteral("clip.add"),
		{{QStringLiteral("track"), track.result.value(QStringLiteral("track"))},
			{QStringLiteral("position"), 0}});
	if (!clip.ok) { return QString(); }
	return clip.result.value(QStringLiteral("clip")).toString();
}

inline bool addNote(const QString& clip, int key, int position, int velocity)
{
	return run(QStringLiteral("note.add"),
		{{QStringLiteral("clip"), clip}, {QStringLiteral("key"), key},
			{QStringLiteral("position"), position}, {QStringLiteral("length"), 96},
			{QStringLiteral("velocity"), velocity}}).ok;
}

/*! One note as a whole value, so a take can be compared in one assertion and a
 *  failure prints the take rather than two addresses. */
struct NoteAt
{
	int key = 0;
	int position = 0;
	int length = 0;
	int velocity = 0;

	bool operator==(const NoteAt& other) const
	{
		return key == other.key && position == other.position
			&& length == other.length && velocity == other.velocity;
	}
};

inline QString describe(const QVector<NoteAt>& notes)
{
	QStringList parts;
	for (const NoteAt& note : notes)
	{
		parts << QStringLiteral("%1@%2:%3/v%4").arg(note.key).arg(note.position)
			.arg(note.length).arg(note.velocity);
	}
	return QStringLiteral("[") + parts.join(QStringLiteral(" ")) + QStringLiteral("]");
}

//! Every note of a clip, through the product's own roll.get_state.
inline QVector<NoteAt> notesOf(const QString& clip)
{
	QVector<NoteAt> out;
	const lmms::ControlResult state =
		run(QStringLiteral("roll.get_state"), {{QStringLiteral("clip"), clip}});
	for (const QJsonValue& value : state.result.value(QStringLiteral("notes")).toArray())
	{
		const QJsonObject note = value.toObject();
		out.append(NoteAt{note.value(QStringLiteral("key")).toInt(),
			note.value(QStringLiteral("position")).toInt(),
			note.value(QStringLiteral("length")).toInt(),
			note.value(QStringLiteral("velocity")).toInt()});
	}
	return out;
}

/*! One chord-track event as the wire reports it, so the track's whole state can
 *  be compared in one assertion and a failure prints the track. */
struct EventAt
{
	int pos = 0;
	int length = 0;
	int root = 0;
	int octave = 0;
	int key = 0;
	QString chord;
	QString scale;

	bool operator==(const EventAt& other) const
	{
		return pos == other.pos && length == other.length && root == other.root
			&& octave == other.octave && key == other.key && chord == other.chord
			&& scale == other.scale;
	}
};

inline QString describe(const QVector<EventAt>& events)
{
	QStringList parts;
	for (const EventAt& event : events)
	{
		parts << QStringLiteral("%1:%2@%3/%4").arg(event.chord).arg(event.key)
			.arg(event.pos).arg(event.length);
	}
	return QStringLiteral("[") + parts.join(QStringLiteral(" ")) + QStringLiteral("]");
}

//! The chord track, through chord.get_state.
inline QVector<EventAt> trackOf()
{
	QVector<EventAt> out;
	for (const QJsonValue& value : run(QStringLiteral("chord.get_state"))
		.result.value(QStringLiteral("events")).toArray())
	{
		const QJsonObject event = value.toObject();
		out.append(EventAt{event.value(QStringLiteral("pos")).toInt(),
			event.value(QStringLiteral("length")).toInt(),
			event.value(QStringLiteral("root")).toInt(),
			event.value(QStringLiteral("octave")).toInt(),
			event.value(QStringLiteral("key")).toInt(),
			event.value(QStringLiteral("chord")).toString(),
			event.value(QStringLiteral("scale")).toString()});
	}
	return out;
}

/*! The contract table's row for \a id, or nullptr - the SPEC A16
 *  classification both chord tests hold the group to. */
inline const lmms::control::ReversibilityEntry* contractRow(const QString& id)
{
	return lmms::control::ReversibilityTable::instance().lookup(id);
}

//! A bare note vector the engine tests can hand to the detector directly.
inline lmms::NoteVector notes(std::initializer_list<std::tuple<int, int, int>> entries)
{
	lmms::NoteVector out;
	for (const auto& entry : entries)
	{
		const int key = std::get<0>(entry);
		const int position = std::get<1>(entry);
		const int length = std::get<2>(entry);
		out.push_back(new lmms::Note(lmms::TimePos(length), lmms::TimePos(position), key, 100));
	}
	return out;
}

inline void freeNotes(lmms::NoteVector& notes)
{
	for (lmms::Note* note : notes) { delete note; }
	notes.clear();
}

} // namespace chordtest

#endif // LMMS_TESTS_CHORD_TEST_SUPPORT_H
