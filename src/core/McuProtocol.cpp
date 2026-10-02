/*
 * McuProtocol.cpp - the Mackie Control Universal protocol: decoding a surface, encoding its feedback
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

#include "McuProtocol.h"

#include <algorithm>
#include <cmath>

namespace lmms::mcu
{

namespace
{

constexpr int TouchFirst = 104;
constexpr int VPotFirst = 16;
constexpr int VPotPushFirst = 32;
constexpr int JogController = 60;

struct StripButton
{
	int first;
	Kind kind;
};

constexpr StripButton StripButtons[] = {{0, Kind::Rec}, {8, Kind::Solo}, {16, Kind::Mute}, {24, Kind::Select},
	{VPotPushFirst, Kind::VPotPush}};

struct GlobalButton
{
	int note;
	Kind kind;
};

constexpr GlobalButton GlobalButtons[] = {{46, Kind::BankLeft}, {47, Kind::BankRight}, {48, Kind::ChannelLeft},
	{49, Kind::ChannelRight}, {91, Kind::Rewind}, {92, Kind::Forward}, {93, Kind::Stop}, {94, Kind::Play},
	{95, Kind::Record}};

//! A relative encoder's turn: 1..63 clockwise, 65..127 counter-clockwise (bit 6 the direction).
int relative(int value)
{
	const int ticks = value & 0x3F;
	return (value & 0x40) != 0 ? -ticks : ticks;
}

Input decodeNote(int note, bool pressed)
{
	if (note >= TouchFirst && note <= TouchFirst + MasterStrip)
	{
		return {Kind::FaderTouch, note - TouchFirst, 0, pressed};
	}
	if (!pressed) { return {}; }
	for (const StripButton& button : StripButtons)
	{
		if (note >= button.first && note < button.first + Strips) { return {button.kind, note - button.first, 0, true}; }
	}
	for (const GlobalButton& button : GlobalButtons)
	{
		if (note == button.note) { return {button.kind, -1, 0, true}; }
	}
	return {};
}

Input decodeController(int controller, int value)
{
	if (controller >= VPotFirst && controller < VPotFirst + Strips) { return {Kind::VPot, controller - VPotFirst, relative(value), false}; }
	if (controller == JogController) { return {Kind::Jog, -1, relative(value), false}; }
	return {};
}

} // namespace

Input decode(const MidiEvent& event)
{
	switch (event.type())
	{
	case MidiPitchBend:
		if (event.channel() > MasterStrip) { return {}; }
		return {Kind::Fader, event.channel(), std::clamp<int>(event.pitchBend(), 0, FaderMax), false};
	case MidiNoteOn: return decodeNote(event.key(), event.velocity() > 0);
	case MidiNoteOff: return decodeNote(event.key(), false);
	case MidiControlChange: return decodeController(event.controllerNumber(), event.controllerValue());
	default: return {};
	}
}

int faderPosition(float level)
{
	return static_cast<int>(std::lround(std::clamp(level, 0.f, 1.f) * FaderMax));
}

float faderLevel(int position)
{
	return static_cast<float>(std::clamp(position, 0, FaderMax)) / FaderMax;
}

MidiEvent faderEvent(int strip, int position)
{
	return MidiEvent(MidiPitchBend, static_cast<int8_t>(std::clamp(strip, 0, MasterStrip)),
		static_cast<int16_t>(std::clamp(position, 0, FaderMax)));
}

MidiEvent ledEvent(Kind kind, int strip, bool lit)
{
	int note = -1;
	for (const StripButton& button : StripButtons)
	{
		if (button.kind == kind) { note = button.first + std::clamp(strip, 0, Strips - 1); }
	}
	for (const GlobalButton& button : GlobalButtons)
	{
		if (button.kind == kind) { note = button.note; }
	}
	return MidiEvent(MidiNoteOn, 0, static_cast<int16_t>(std::max(note, 0)), static_cast<int16_t>(lit ? 127 : 0));
}

} // namespace lmms::mcu
