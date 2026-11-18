/*
 * ArticulationEvents.h - what an articulation sends ahead of a note, as plain integers
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
#ifndef LMMS_ARTICULATION_EVENTS_H
#define LMMS_ARTICULATION_EVENTS_H

namespace lmms
{

/*! 0.5.0 L07 (Q050-015): the MIDI an articulation sends before a note that asks for it (-1: not that), copied
 *  out of the track's map when the note is triggered. Plain integers, so the note's play handle carries it to
 *  the audio thread - and to each layer that plays the note - without sharing the map, which the UI thread may
 *  replace, and without allocating. */
struct ArticulationEvents
{
	int keyswitch = -1;   //!< a key pressed with the note's start and let go a period later
	int program = -1;     //!< a program change
	int controller = -1;  //!< a control change: this CC ...
	int value = 0;        //!< ... to this value

	bool any() const { return keyswitch >= 0 || program >= 0 || controller >= 0; }
};

} // namespace lmms

#endif // LMMS_ARTICULATION_EVENTS_H
