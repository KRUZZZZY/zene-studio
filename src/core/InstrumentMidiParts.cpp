/*
 * InstrumentMidiParts.cpp - 0.5.0 MX-114: one instrument track addressing a
 *                           multitimbral plug-in on several MIDI channels.
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

#include "InstrumentMidiParts.h"

#include <QDomDocument>
#include <QDomElement>

#include "Midi.h"
#include "volume.h"

namespace lmms
{

namespace
{

//! A MIDI velocity in the zones' velocity space (the engine's note volume: velocity 64 is volume 100).
int volumeOfVelocity(int velocity)
{
	return velocity * DefaultVolume / MidiDefaultVelocity;
}

bool validKey(int key) { return key >= 0 && key < NumKeys; }

} // namespace

InstrumentMidiParts::InstrumentMidiParts()
{
	for (auto& channel : m_keyChannel) { channel.store(-1, std::memory_order_relaxed); }
}

auto InstrumentMidiParts::noteOnChannel(int key, int velocity, int fallback) -> int
{
	if (!active() || !validKey(key)) { return fallback; }
	const int zone = m_zones.resolve(key, volumeOfVelocity(velocity));
	const RackZone* part = zone >= 0 ? m_zones.zone(zone) : nullptr;
	const int channel = part != nullptr ? part->chain : fallback;
	m_keyChannel[static_cast<std::size_t>(key)].store(static_cast<std::int8_t>(channel), std::memory_order_relaxed);
	return channel;
}

auto InstrumentMidiParts::noteOffChannel(int key, int fallback) -> int
{
	if (!validKey(key)) { return fallback; }
	// Answered even after the parts were removed, so a note that started on a part still ends there.
	const int channel = m_keyChannel[static_cast<std::size_t>(key)].exchange(-1, std::memory_order_relaxed);
	return channel >= 0 ? channel : fallback;
}

auto InstrumentMidiParts::channelOfKey(int key, int fallback) const -> int
{
	if (!validKey(key)) { return fallback; }
	const int channel = m_keyChannel[static_cast<std::size_t>(key)].load(std::memory_order_relaxed);
	return channel >= 0 ? channel : fallback;
}

auto InstrumentMidiParts::channelMask() const -> std::uint16_t
{
	std::uint16_t mask = 0;
	for (int i = 0; i < m_zones.zoneCount(); ++i)
	{
		const int channel = m_zones.zone(i)->chain;
		if (channel >= 0 && channel < Channels) { mask = static_cast<std::uint16_t>(mask | (1u << channel)); }
	}
	return mask;
}

void InstrumentMidiParts::saveSettings(QDomDocument& doc, QDomElement& parent) const
{
	if (!active()) { return; }
	QDomElement element = doc.createElement(QStringLiteral("midiparts"));
	m_zones.saveSettings(doc, element);
	parent.appendChild(element);
}

void InstrumentMidiParts::loadSettings(const QDomElement& parent)
{
	// A missing element reads as no zones, which clears the parts a preset or an undo step replaced.
	m_zones.loadSettings(parent.firstChildElement(QStringLiteral("midiparts")));
}

} // namespace lmms
