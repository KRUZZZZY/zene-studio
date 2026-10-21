/*
 * MidiEventToByteSeq.cpp - writeToByteSeq implementation
 *
 * Copyright (c) 2020-2020 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#include "MidiEventToByteSeq.h"

#include <QtGlobal>

#include "MidiEvent.h"


namespace lmms
{


namespace
{

//! A MIDI data byte carries 7 bits: 0..127.
constexpr int kMaxDataByte = 0x7F;

/*! A 7-bit data field of a MIDI message (a key, a velocity, a controller number
 *  and value, a program, a channel pressure): 0..127. A value outside that range
 *  is not MIDI: writing it either masks it into a different message or puts a
 *  byte with the high bit set into the stream, so the message is refused. */
bool isDataByte( int value )
{
	return value >= 0 && value <= kMaxDataByte;
}

/*! The pitch bend is the one 14-bit field of a MIDI message: 0..16383. Its high
 *  byte is a data byte like any other, so the value has to fit before it is
 *  split into two. */
bool isPitchBendValue( int value )
{
	return value >= MidiMinPitchBend && value <= MidiMaxPitchBend;
}

/*! A note message (note on, note off, key pressure): the key is a 7-bit field
 *  and the velocity - the release time of a note off - is a 7-bit data byte. */
bool isValidNoteMessage( const MidiEvent& ev )
{
	return ev.key() >= 0 && ev.key() <= MidiMaxKey && isDataByte( ev.param( 1 ) );
}

} // namespace


std::size_t writeToByteSeq(
	const MidiEvent& ev, uint8_t *data, std::size_t bufsize)
{
	Q_ASSERT(bufsize >= 3);

	std::size_t size = 0;
	data[0] = ev.type() | (ev.channel() & 0x0F);

	switch (ev.type())
	{
		case MidiNoteOn:
			if (ev.velocity() > 0)
			{
				if (!isValidNoteMessage(ev))
					break;

				data[1] = ev.key();
				data[2] = ev.velocity();
				size    = 3;
				break;
			}
			else
			{
				// Lv2 MIDI specs:
				// "Note On messages with velocity 0 are not allowed.
				// These messages are equivalent to Note Off in standard
				// MIDI streams, but here only proper Note Off messages
				// are allowed."
				data[0] = MidiNoteOff | (ev.channel() & 0x0F);
				// nobreak
			}

		case MidiNoteOff:
			if (!isValidNoteMessage(ev))
				break;
			data[1] = ev.key();
			data[2] = ev.velocity(); // release time
			size    = 3;
			break;

		case MidiKeyPressure:
			if (!isValidNoteMessage(ev))
				break;
			data[1] = ev.key();
			data[2] = ev.velocity();
			size    = 3;
			break;

		case MidiControlChange:
			// the controller number and the controller value are 7-bit data bytes
			if (!isDataByte(ev.param(0)) || !isDataByte(ev.param(1)))
				break;
			data[1] = ev.controllerNumber();
			data[2] = ev.controllerValue();
			size    = 3;
			break;

		case MidiProgramChange:
			if (!isDataByte(ev.param(0)))
				break;
			data[1] = ev.program();
			size    = 2;
			break;

		case MidiChannelPressure:
			if (!isDataByte(ev.param(0)))
				break;
			data[1] = ev.channelPressure();
			size    = 2;
			break;

		case MidiPitchBend:
			if (!isPitchBendValue(ev.param(0)))
				break;
			data[1] = ev.pitchBend() & 0x7f;
			data[2] = ev.pitchBend() >> 7;
			size    = 3;
			break;

		default:
			// unhandled
			break;
	}

	return size;
}


} // namespace lmms
