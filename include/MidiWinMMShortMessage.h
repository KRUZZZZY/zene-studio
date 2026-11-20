/*
 * MidiWinMMShortMessage.h - platform-neutral packing of a WinMM short MIDI message
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio - an LMMS-derived DAW
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

#ifndef LMMS_MIDI_WINMM_SHORT_MESSAGE_H
#define LMMS_MIDI_WINMM_SHORT_MESSAGE_H

#include <cstdint>

#include "MidiEvent.h"

namespace lmms
{

// Packs a MidiEvent into the 32-bit word that midiOutShortMsg() expects.
// Byte 0 = status (type + channel), byte 1 = param(0), byte 2 = param(1).
inline std::uint32_t winmmShortMessage(const MidiEvent& event) noexcept
{
	const std::uint32_t status = static_cast<std::uint32_t>(event.type() + event.channel());
	return status | ((static_cast<std::uint32_t>(event.param(0)) & 0xFFu) << 8)
	              | ((static_cast<std::uint32_t>(event.param(1)) & 0xFFu) << 16);
}

} // namespace lmms

#endif // LMMS_MIDI_WINMM_SHORT_MESSAGE_H
