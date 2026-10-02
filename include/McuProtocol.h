/*
 * McuProtocol.h - the Mackie Control Universal protocol: decoding a surface, encoding its feedback
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

#ifndef LMMS_MCU_PROTOCOL_H
#define LMMS_MCU_PROTOCOL_H

#include "MidiEvent.h"
#include "lmms_export.h"

namespace lmms::mcu
{

/*! WF-126: Mackie Control Universal - the protocol most control surfaces speak (Mackie, Behringer
 *  X-Touch, iCON, PreSonus FaderPort 8/16 in MCU mode, the HUI-compatible ones in MCU mode). Eight
 *  channel strips and a master fader, as pure functions over MidiEvent so they can be proven
 *  without a device:
 *    faders      pitch bend on channels 0..7 (master 8), 14-bit;
 *    fader touch notes 104..112;  V-pots CC 16..23, relative (bit 6 = counter-clockwise);
 *    V-pot push  notes 32..39;    rec / solo / mute / select notes 0..7 / 8..15 / 16..23 / 24..31;
 *    bank < >    notes 46 / 47;   channel < > notes 48 / 49;
 *    transport   rewind 91, forward 92, stop 93, play 94, record 95;  jog CC 60, relative.
 *  Feedback is the same notes (velocity 127 lit, 0 dark) and the faders' pitch bend. The strip
 *  LCD is SysEx, which the MIDI output path does not carry - not sent (docs/KNOWN-LIMITATIONS.md). */

constexpr int Strips = 8;
constexpr int MasterStrip = 8;
constexpr int FaderMax = 16383;

enum class Kind
{
	None, Fader, FaderTouch, VPot, VPotPush, Rec, Solo, Mute, Select,
	BankLeft, BankRight, ChannelLeft, ChannelRight, Rewind, Forward, Stop, Play, Record, Jog
};

//! One decoded surface input. `strip` is 0..7 (8 = master for a fader), `value` the fader's
//! 0..FaderMax position or a relative turn (negative = counter-clockwise), `pressed` a button's state.
struct Input
{
	Kind kind = Kind::None;
	int strip = -1;
	int value = 0;
	bool pressed = false;
};

//! What @a event means on a Mackie Control surface; Kind::None for anything else (and for a
//! button's release, apart from fader touch, whose release matters).
LMMS_EXPORT Input decode(const MidiEvent& event);

//! The fader position (0..FaderMax) of @a level in 0..1, and back.
LMMS_EXPORT int faderPosition(float level);
LMMS_EXPORT float faderLevel(int position);

//! The motor-fader event that moves strip @a strip (0..8) to @a position.
LMMS_EXPORT MidiEvent faderEvent(int strip, int position);
//! The LED event for button @a kind of strip @a strip (strip ignored for global buttons).
LMMS_EXPORT MidiEvent ledEvent(Kind kind, int strip, bool lit);

} // namespace lmms::mcu

#endif // LMMS_MCU_PROTOCOL_H
