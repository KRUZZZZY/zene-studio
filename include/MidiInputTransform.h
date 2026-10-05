/*
 * MidiInputTransform.h - a track's transform of live MIDI input: channel, key range, transpose, scale snap, velocity
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

#ifndef LMMS_MIDI_INPUT_TRANSFORM_H
#define LMMS_MIDI_INPUT_TRANSFORM_H

#include <array>
#include <atomic>
#include <cstdint>

#include <QJsonObject>

#include "lmms_export.h"

class QDomElement;

namespace lmms
{

class MidiEvent;

/*! 0.5.0 L07 (ED-145, ED-113): what an instrument track does to live MIDI input before playing it -
 *  only one channel, only a key range, transpose, snap to a scale, and the velocity scaled, offset or
 *  fixed. Read on the MIDI input thread (relaxed atomics, no allocation, no lock), written by
 *  track.input_transform_set on the UI thread. A note-off takes the key its note-on was given, so a
 *  change while a key is held never leaves a note sounding. Saved as one attribute on the track's
 *  element, only when it is not the identity (and never in an instrument preset). */
class LMMS_EXPORT MidiInputTransform
{
public:
	MidiInputTransform();

	//! Whether any setting differs from the identity (nothing to do otherwise).
	bool active() const { return m_active.load(std::memory_order_relaxed); }
	//! Transform @a event in place; false when it is to be dropped.
	bool apply(MidiEvent* event);

	QJsonObject toJson() const;
	//! Set the fields @a json names (the others keep their values); false, with @a error, on a bad
	//! value. @a error is an output and may be null - a caller that wants no message is refused
	//! just the same.
	bool fromJson(const QJsonObject& json, QString* error);
	void reset();

	void save(QDomElement& trackElement) const;
	void load(const QDomElement& trackElement);

private:
	//! One integer setting: its JSON key, its range and its identity value.
	struct Setting
	{
		const char* key;
		std::atomic<int> MidiInputTransform::*field;
		int minimum;
		int maximum;
		int identity;
	};
	static const std::array<Setting, 8>& settings();

	bool passesChannel(const MidiEvent& event) const;
	//! The held entry of @a event's channel and key, null when either is out of range.
	std::atomic<int>* heldFor(const MidiEvent& event);
	bool noteOn(MidiEvent* event);
	bool follow(MidiEvent* event, bool ends);
	int clampedVelocity(int velocity) const;
	int snapped(int key) const;
	void updateActive();

	std::atomic<int> m_channel{0};         //!< 0 any, 1..16 only that channel
	std::atomic<int> m_keyLow{0};
	std::atomic<int> m_keyHigh{127};
	std::atomic<int> m_transpose{0};       //!< -48..48 semitones
	std::atomic<int> m_scaleMask{0};       //!< 12 bits, pitch class 0 = C; 0 = no snapping
	std::atomic<int> m_velocityScale{100}; //!< percent, 1..400
	std::atomic<int> m_velocityOffset{0};  //!< -127..127
	std::atomic<int> m_fixedVelocity{0};   //!< 0 off, 1..127
	std::atomic<bool> m_active{false};
	//! Incoming channel * 128 + key -> the key its note-on became: -1 dropped, -2 not held through the transform.
	std::array<std::atomic<int>, 16 * 128> m_heldAs;
};

} // namespace lmms

#endif // LMMS_MIDI_INPUT_TRANSFORM_H
