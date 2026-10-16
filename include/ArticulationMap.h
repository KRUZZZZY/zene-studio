/*
 * ArticulationMap.h - a track's articulations: the keyswitch, program or controller each one sends
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
#ifndef LMMS_ARTICULATION_MAP_H
#define LMMS_ARTICULATION_MAP_H

#include <utility>
#include <vector>

#include <QJsonArray>
#include <QString>

#include "ArticulationEvents.h"
#include "lmms_export.h"

class QDomElement;

namespace lmms
{

//! One articulation: its name and the MIDI it sends before a note that asks for it (-1: not that).
struct Articulation
{
	QString name;
	int keyswitch = -1;  //!< a key played, very short, just before the note (a sample library's keyswitch)
	int program = -1;    //!< a program change
	int controller = -1; //!< a control change: this CC ...
	int value = 0;       //!< ... to this value

	ArticulationEvents events() const { return {keyswitch, program, controller, value}; }
};

/*! 0.5.0 L07 (ED-121): an instrument track's expression map. A note names one of these by its
 *  1-based index (NoteConditions::articulation); when it starts, the track first sends what the
 *  articulation says. Written on the UI thread with the track locked and read by the audio thread
 *  under play()'s lock, as the clip's controller lanes are. Saved as one attribute of the track's
 *  element, only when there is an articulation. */
class LMMS_EXPORT ArticulationMap
{
public:
	const std::vector<Articulation>& entries() const { return m_entries; }
	bool empty() const { return m_entries.empty(); }
	//! Articulation @a index (1-based), or nullptr.
	const Articulation* at(int index) const;
	//! The 1-based index of the articulation called @a name, or 0.
	int indexOf(const QString& name) const;
	void set(std::vector<Articulation> entries) { m_entries = std::move(entries); }

	QJsonArray toJson() const;
	//! @a array read into @a out; false (and @a error says why) for an entry that is not one.
	static bool fromJson(const QJsonArray& array, std::vector<Articulation>* out, QString* error);

	void save(QDomElement& trackElement) const;
	void load(const QDomElement& trackElement);

private:
	std::vector<Articulation> m_entries;
};

} // namespace lmms

#endif // LMMS_ARTICULATION_MAP_H
