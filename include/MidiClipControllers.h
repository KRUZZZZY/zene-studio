/*
 * MidiClipControllers.h - a MIDI clip's controller lanes: CC, pitch bend and channel pressure curves
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

#ifndef LMMS_MIDI_CLIP_CONTROLLERS_H
#define LMMS_MIDI_CLIP_CONTROLLERS_H

#include <vector>

#include <QString>

#include "LmmsTypes.h"
#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

//! One breakpoint of a controller lane: a value at a tick inside the clip.
struct ControllerPoint
{
	tick_t tick = 0;
	int value = 0;
	bool operator==(const ControllerPoint& other) const { return tick == other.tick && value == other.value; }
};

enum class ControllerKind
{
	ControlChange,   //!< CC 0..127, value 0..127
	PitchBend,       //!< 0..16383, 8192 centre
	ChannelPressure, //!< aftertouch 0..127
	ProgramChange    //!< 0.5.0 L07 (ED-131): program 0..127, held from each point to the next (stepped)
};

/*! 0.5.0 L07 (ED-130): one curve the clip plays to its instrument as MIDI - linear between the
 *  points, held before the first and after the last. Its id names it: "cc:<n>", "pitchbend",
 *  "aftertouch", "program" (stepped, not linear). */
struct LMMS_EXPORT ControllerLane
{
	ControllerKind kind = ControllerKind::ControlChange;
	int controller = 0;                   //!< the CC number (ControlChange only)
	std::vector<ControllerPoint> points;  //!< sorted by tick, one point per tick

	QString id() const;
	int maximum() const;
	//! The value at @a tick: linear between the points around it.
	int valueAt(tick_t tick) const;
	//! @a id parsed; false for anything that names no lane.
	static bool fromId(const QString& id, ControllerKind* kind, int* controller);

	// The audio thread's own: what it last sent and at which tick, so a value is sent only when it
	// changes, and again after a jump (a seek, a loop, the clip starting over).
	mutable int lastSent = -1;
	mutable tick_t lastTick = -2;
};

/*! A MidiClip's controller lanes. Written on the UI thread with the instrument track locked (as
 *  MidiClip::addNote does) and read by InstrumentTrack::play under its tryLock, so the audio thread
 *  never sees a lane change half done and never waits for one. Saved as <controllers metadata="1">
 *  inside the clip's element - MidiClip::loadSettings, in this build and older ones, skips
 *  metadata children - and only when there is a lane. */
class LMMS_EXPORT MidiClipControllers
{
public:
	const std::vector<ControllerLane>& lanes() const { return m_lanes; }
	bool empty() const { return m_lanes.empty(); }
	const ControllerLane* lane(ControllerKind kind, int controller) const;
	//! Replace one lane's points (sorted and made unique per tick here); no points removes the lane.
	void setPoints(ControllerKind kind, int controller, std::vector<ControllerPoint> points);
	void clear() { m_lanes.clear(); }

	void save(QDomDocument& doc, QDomElement& clipElement) const;
	void load(const QDomElement& clipElement);

private:
	std::vector<ControllerLane> m_lanes;
};

} // namespace lmms

#endif // LMMS_MIDI_CLIP_CONTROLLERS_H
