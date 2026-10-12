#ifndef MIDIFILE_HPP
#define MIDIFILE_HPP

/**
 * Name:        MidiFile.hpp
 * Purpose:     C++ re-write of the python module MidiFile.py
 * Author:      Mohamed Abdel Maksoud <mohamed at amaksoud.com>
 *-----------------------------------------------------------------------------
 * Name:        MidiFile.py
 * Purpose:     MIDI file manipulation utilities
 *
 * Author:      Mark Conway Wirt <emergentmusics) at (gmail . com>
 *
 * Created:     2008/04/17
 * Copyright:   (c) 2009 Mark Conway Wirt
 * License:     Please see License.txt for the terms under which this
 *              software is distributed.
 *-----------------------------------------------------------------------------
 */

#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <assert.h>
#include <array>

using std::string;
using std::vector;
using std::set;

namespace MidiFile
{

//! 480 ticks per quarter note: ten file ticks per LMMS tick (48 per quarter), so every LMMS tick
//! position is written exactly (the old 128 truncated 3 of every 8 positions).
const int TICKSPERBEAT = 480;

// Every writer below APPENDS to a growable byte vector. Nothing is written into a fixed-size buffer,
// so a track of any size is written whole: there is no cap to exceed and nothing to truncate.


inline int writeVarLength(uint32_t val, vector<uint8_t> &out)
{
	/*
	Accept an input, and append a MIDI-compatible variable length stream

	The MIDI format is a little strange, and makes use of so-called variable
	length quantities. These quantities are a stream of bytes. If the most
	significant bit is 1, then more bytes follow. If it is zero, then the
	byte in question is the last in the stream
	*/
	uint8_t groups[5];
	int size = 0;
	groups[size++] = val & 0x7F;
	val = val >> 7;
	while (val > 0)
	{
		groups[size++] = (val & 0x7F) | 0x80;
		val = val >> 7;
	}
	// most significant group first
	for (int i = size - 1; i >= 0; i--)
	{
		out.push_back(groups[i]);
	}
	return size;
}

inline void writeBigEndian4(uint32_t val, vector<uint8_t> &out)
{
	out.push_back(uint8_t(val >> 24));
	out.push_back(uint8_t(val >> 16));
	out.push_back(uint8_t(val >> 8));
	out.push_back(uint8_t(val));
}

inline void writeBigEndian2(uint16_t val, vector<uint8_t> &out)
{
	out.push_back(uint8_t(val >> 8));
	out.push_back(uint8_t(val));
}


class MIDIHeader
{
	// Class to encapsulate the MIDI header structure.
	uint16_t numTracks;
	uint16_t ticksPerBeat;

	public:

	MIDIHeader(uint16_t nTracks, uint16_t ticksPB=TICKSPERBEAT): numTracks(nTracks), ticksPerBeat(ticksPB) {}

	inline void writeToBuffer(vector<uint8_t> &out) const
	{
		// chunk ID
		out.push_back('M'); out.push_back('T'); out.push_back('h'); out.push_back('d');
		// chunk size (6 bytes always)
		writeBigEndian4(6, out);
		// format: 1 (multitrack)
		writeBigEndian2(1, out);
		writeBigEndian2(numTracks, out);
		writeBigEndian2(ticksPerBeat, out);
	}

};


struct Event
{
	uint32_t time;
	uint32_t tempo;
	string trackName;
	enum {NOTE_ON, NOTE_OFF, TEMPO, TIME_SIG, PROG_CHANGE, TRACK_NAME} type;
	// TODO make a union to save up space
	uint8_t pitch;
	uint8_t programNumber;
	uint8_t duration;
	uint8_t volume;
	uint8_t channel;
	uint8_t numerator;
	uint8_t denominator;

	Event() {time=tempo=pitch=programNumber=duration=volume=channel=numerator=denominator=0; trackName="";}

	inline void writeToBuffer(vector<uint8_t> &out) const
	{
		switch (type)
		{
			case NOTE_ON:
			{
				writeVarLength(time, out);
				out.push_back(uint8_t(0x9 << 4 | channel));
				out.push_back(pitch);
				out.push_back(volume);
				break;
			}
			case NOTE_OFF:
			{
				writeVarLength(time, out);
				out.push_back(uint8_t(0x8 << 4 | channel));
				out.push_back(pitch);
				out.push_back(volume);
				break;
			}
			case TEMPO:
			{
				writeVarLength(time, out);
				out.push_back(0xFF);
				out.push_back(0x51);
				out.push_back(0x03);
				// microseconds per quarter note, ROUNDED: the same encoding as the interchange
				// writer (src/core/SmfInterchange.cpp), exact for every bpm 10..999.
				const auto usecs = uint32_t(60000000.0 / tempo + 0.5);
				out.push_back(uint8_t(usecs >> 16));
				out.push_back(uint8_t(usecs >> 8));
				out.push_back(uint8_t(usecs));
				break;
			}
			case TIME_SIG:
			{
				writeVarLength(time, out);
				out.push_back(0xFF);
				out.push_back(0x58);
				out.push_back(0x04);
				uint8_t power = 0;
				for (uint32_t d = denominator; d > 1; d >>= 1) { power++; }
				out.push_back(numerator);
				out.push_back(power);
				out.push_back(24); // MIDI clocks per metronome click
				out.push_back(8);  // 32nd notes per quarter note
				break;
			}
			case PROG_CHANGE:
			{
				writeVarLength(time, out);
				out.push_back(uint8_t(0xC << 4 | channel));
				out.push_back(programNumber);
				break;
			}
			case TRACK_NAME:
			{
				writeVarLength(time, out);
				out.push_back(0xFF);
				out.push_back(0x03);
				writeVarLength(uint32_t(trackName.size()), out);
				out.insert(out.end(), trackName.begin(), trackName.end());
				break;
			}
		}
	} // writeToBuffer


	// events are sorted by their time
	inline bool operator < (const Event& b) const {
		return this->time < b.time ||
			(this->time == b.time && this->type > b.type);
	}
};

class MIDITrack
{
	// A class that encapsulates a MIDI track
	// Nested class definitions.
	vector<Event> events;

	public:
	uint8_t channel;

	MIDITrack(): channel(0) {}

	inline void addEvent(const Event &e)
	{
		Event E = e;
		events.push_back(E);
	}

	inline void addNote(uint8_t pitch, uint8_t volume, double time, double duration)
	{
		Event event; event.channel = channel;
		event.volume = volume;

		event.type = Event::NOTE_ON; event.pitch = pitch; event.time= (uint32_t) (time * TICKSPERBEAT);
		addEvent(event);

		event.type = Event::NOTE_OFF; event.pitch = pitch; event.time=(uint32_t) ((time+duration) * TICKSPERBEAT);
		addEvent(event);
	}

	//! A note at exact file ticks (480 per quarter note); no floating-point rounding.
	inline void addNoteTicks(uint8_t pitch, uint8_t volume, uint32_t startTick, uint32_t endTick)
	{
		Event event; event.channel = channel;
		event.volume = volume;
		event.type = Event::NOTE_ON; event.pitch = pitch; event.time = startTick;
		addEvent(event);
		event.type = Event::NOTE_OFF; event.time = endTick;
		addEvent(event);
	}

	inline void addTimeSignature(uint8_t numerator, uint8_t denominator, uint32_t time)
	{
		Event event;
		event.type = Event::TIME_SIG; event.time = time;
		event.numerator = numerator; event.denominator = denominator;
		addEvent(event);
	}

	inline void addName(const string &name, uint32_t time)
	{
		Event event; event.channel = channel;
		event.type = Event::TRACK_NAME; event.time=time; event.trackName = name;
		addEvent(event);
	}

	inline void addProgramChange(uint8_t prog, uint32_t time)
	{
		Event event; event.channel = channel;
		event.type = Event::PROG_CHANGE; event.time=time; event.programNumber = prog;
		addEvent(event);
	}

	inline void addTempo(uint32_t tempo, uint32_t time)
	{
		Event event;
		event.channel = channel;

		event.type = Event::TEMPO;
		event.time = time;
		event.tempo = tempo;

		addEvent(event);
	}

	//! Appends the events to out as delta-timed MIDI events, sorted by time (no end-of-track event).
	inline void writeEventsToBuffer(vector<uint8_t> &out) const
	{
		vector<Event> _events = events;
		std::sort(_events.begin(), _events.end());
		uint32_t time_last = 0;
		for (const Event &e : _events)
		{
			Event delta = e;
			delta.time = e.time - time_last;
			time_last = e.time;
			delta.writeToBuffer(out);
		}
	}

	//! Appends one complete MTrk chunk: the events, then the end-of-track meta event.
	//! Every event is written, however many there are; the chunk length is the real length.
	inline void writeToBuffer(vector<uint8_t> &out) const
	{
		vector<uint8_t> body;
		writeEventsToBuffer(body);
		// end of track
		body.push_back(0x00);
		body.push_back(0xFF);
		body.push_back(0x2F);
		body.push_back(0x00);

		// chunk ID
		out.push_back('M'); out.push_back('T'); out.push_back('r'); out.push_back('k');
		// chunk size
		writeBigEndian4(uint32_t(body.size()), out);
		out.insert(out.end(), body.begin(), body.end());
	}
};

}; // namespace

#endif
