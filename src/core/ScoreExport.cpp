/*
 * ScoreExport.cpp - a MIDI clip written out as a MusicXML score
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
#include "ScoreExport.h"

#include <algorithm>
#include <map>
#include <vector>

#include <QXmlStreamWriter>

#include "MidiClip.h"
#include "Note.h"
#include "TimePos.h"

namespace lmms
{

namespace
{

constexpr int Grid = 3;  // a 64th note in ticks: everything is quantised to it

int snapped(int ticks) { return (ticks + Grid / 2) / Grid * Grid; }

//! One chord (or a gap, with no keys) over [start, start + length) in clip ticks. A gap is a rest in the first
//! voice and a forward - nothing drawn - in the others.
struct Event
{
	int start = 0;
	int length = 0;
	std::vector<int> keys;
};

using Voice = std::vector<Event>;

//! The notes by snapped start, then by snapped length: the keys that start and end together.
using Starts = std::map<int, std::map<int, std::vector<int>>>;

Starts startsOf(const MidiClip& clip)
{
	Starts starts;
	for (const Note* note : clip.notes())
	{
		if (note->muted()) { continue; }
		// The start and the END are snapped, the length taken between them: two separately rounded
		// values could move the note's end a 64th off its own nearest grid line.
		const int start = snapped(static_cast<int>(note->pos().getTicks()));
		const int end = snapped(static_cast<int>(note->endPos().getTicks()));
		starts[start][std::max(Grid, end - start)].push_back(note->key());
	}
	return starts;
}

//! The chords of each voice. A chord's notes share one duration in MusicXML, so the notes starting together are
//! split by length (W6): the first voice takes, at every start, the shortest ones - with notes of one length that
//! is every note, the single voice this export always wrote - and each longer length goes to the next voice up.
std::vector<Voice> chordsByVoice(const Starts& starts)
{
	std::vector<Voice> voices(1);
	for (const auto& [start, lengths] : starts)
	{
		std::size_t voice = 0;
		for (const auto& [length, keys] : lengths)
		{
			if (voice == voices.size()) { voices.emplace_back(); }
			voices[voice++].push_back({start, length, keys});
		}
	}
	return voices;
}

//! @a chords laid out from tick 0: each cut where the voice's next one begins, gaps between them.
Voice laidOut(const Voice& chords)
{
	Voice voice;
	int at = 0;
	for (std::size_t i = 0; i < chords.size(); ++i)
	{
		Event chord = chords[i];
		if (chord.start > at) { voice.push_back({at, chord.start - at, {}}); }
		if (i + 1 < chords.size()) { chord.length = std::min(chord.length, chords[i + 1].start - chord.start); }
		std::sort(chord.keys.begin(), chord.keys.end());
		voice.push_back(chord);
		at = chord.start + chord.length;
	}
	return voice;
}

std::vector<Voice> voicesOf(const MidiClip& clip)
{
	std::vector<Voice> voices = chordsByVoice(startsOf(clip));
	for (Voice& voice : voices) { voice = laidOut(voice); }
	return voices;
}

struct Value
{
	int ticks;
	const char* type;
	bool dotted;
};

//! The note values a score can show, longest first (48 ticks a quarter).
const Value Values[] = {{192, "whole", false}, {144, "half", true}, {96, "half", false}, {72, "quarter", true}, {48, "quarter", false},
	{36, "eighth", true}, {24, "eighth", false}, {18, "16th", true}, {12, "16th", false}, {6, "32nd", false}, {3, "64th", false}};

//! @a ticks as standard values tied together, longest first.
std::vector<const Value*> valuesOf(int ticks)
{
	std::vector<const Value*> out;
	for (const Value& value : Values)
	{
		while (ticks >= value.ticks) { out.push_back(&value); ticks -= value.ticks; }
	}
	return out;
}

void writePitch(QXmlStreamWriter& xml, int key)
{
	static const char* const steps[] = {"C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B"};
	static const int alters[] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
	xml.writeStartElement(QStringLiteral("pitch"));
	xml.writeTextElement(QStringLiteral("step"), QLatin1String(steps[key % 12]));
	if (alters[key % 12] != 0) { xml.writeTextElement(QStringLiteral("alter"), QStringLiteral("1")); }
	xml.writeTextElement(QStringLiteral("octave"), QString::number(key / 12 - 1));  // keys are MIDI notes: 60 is C4
	xml.writeEndElement();
}

//! A note's ties as @a element: <tie> (what it sounds like) or <tied> (what the score draws).
void writeTies(QXmlStreamWriter& xml, const QString& element, bool tieIn, bool tieOut)
{
	for (const auto& [on, kind] : {std::pair{tieIn, "stop"}, std::pair{tieOut, "start"}})
	{
		if (!on) { continue; }
		xml.writeEmptyElement(element);
		xml.writeAttribute(QStringLiteral("type"), QLatin1String(kind));
	}
}

//! Where one <note> sits: a chord's member after the first, tied to its neighbours, in which voice.
struct Mark
{
	bool chord = false;
	bool tieIn = false;
	bool tieOut = false;
	int voice = 1;
};

//! One <note> (a rest when @a key is negative).
void writeNote(QXmlStreamWriter& xml, int key, const Value& value, const Mark& mark)
{
	const bool tied = key >= 0 && (mark.tieIn || mark.tieOut);
	xml.writeStartElement(QStringLiteral("note"));
	if (mark.chord) { xml.writeEmptyElement(QStringLiteral("chord")); }
	if (key < 0) { xml.writeEmptyElement(QStringLiteral("rest")); } else { writePitch(xml, key); }
	xml.writeTextElement(QStringLiteral("duration"), QString::number(value.ticks));
	if (tied) { writeTies(xml, QStringLiteral("tie"), mark.tieIn, mark.tieOut); }
	xml.writeTextElement(QStringLiteral("voice"), QString::number(mark.voice));
	xml.writeTextElement(QStringLiteral("type"), QLatin1String(value.type));
	if (value.dotted) { xml.writeEmptyElement(QStringLiteral("dot")); }
	if (tied)
	{
		// <tie> alone plays tied but is drawn as separate notes: the score's tie is <notations><tied> (review L17 #8).
		xml.writeStartElement(QStringLiteral("notations"));
		writeTies(xml, QStringLiteral("tied"), mark.tieIn, mark.tieOut);
		xml.writeEndElement();
	}
	xml.writeEndElement();
}

//! <backup> or <forward> by @a ticks: the measure's time cursor moved back (to write the next voice from the bar
//! line) or on (over a gap in a voice after the first, drawn as nothing).
void writeCursorMove(QXmlStreamWriter& xml, const QString& element, int ticks, int voice)
{
	xml.writeStartElement(element);
	xml.writeTextElement(QStringLiteral("duration"), QString::number(ticks));
	if (voice > 0) { xml.writeTextElement(QStringLiteral("voice"), QString::number(voice)); }
	xml.writeEndElement();
}

//! The part of @a event that falls in [from, to), tied to what came before and what follows, in @a voice.
void writeSpan(QXmlStreamWriter& xml, const Event& event, int voice, int from, int to)
{
	if (event.keys.empty() && voice > 1) { writeCursorMove(xml, QStringLiteral("forward"), to - from, voice); return; }
	const std::vector<const Value*> values = valuesOf(to - from);
	for (std::size_t v = 0; v < values.size(); ++v)
	{
		Mark mark{false, from > event.start || v > 0, to < event.start + event.length || v + 1 < values.size(), voice};
		if (event.keys.empty()) { writeNote(xml, -1, *values[v], Mark{false, false, false, voice}); continue; }
		for (std::size_t k = 0; k < event.keys.size(); ++k)
		{
			mark.chord = k > 0;
			writeNote(xml, event.keys[k], *values[v], mark);
		}
	}
}

void writeAttributes(QXmlStreamWriter& xml, int numerator, int denominator)
{
	xml.writeStartElement(QStringLiteral("attributes"));
	xml.writeTextElement(QStringLiteral("divisions"), QString::number(DefaultTicksPerBar / 4));
	xml.writeStartElement(QStringLiteral("time"));
	xml.writeTextElement(QStringLiteral("beats"), QString::number(numerator));
	xml.writeTextElement(QStringLiteral("beat-type"), QString::number(denominator));
	xml.writeEndElement();
	xml.writeStartElement(QStringLiteral("clef"));
	xml.writeTextElement(QStringLiteral("sign"), QStringLiteral("G"));
	xml.writeTextElement(QStringLiteral("line"), QStringLiteral("2"));
	xml.writeEndElement();
	xml.writeEndElement();
}

//! Whether @a voice sounds a note in [from, to).
bool soundsIn(const Voice& voice, int from, int to)
{
	return std::any_of(voice.begin(), voice.end(), [from, to](const Event& event) {
		return !event.keys.empty() && event.start < to && event.start + event.length > from;
	});
}

//! One voice's events in the bar from @a barStart, cut at its lines; *@a next is the first event not yet done.
void writeBar(QXmlStreamWriter& xml, const Voice& voice, int number, int barStart, int barTicks, std::size_t* next)
{
	for (std::size_t e = *next; e < voice.size() && voice[e].start < barStart + barTicks; ++e)
	{
		const int from = std::max(voice[e].start, barStart);
		const int to = std::min(voice[e].start + voice[e].length, barStart + barTicks);
		if (to > from) { writeSpan(xml, voice[e], number, from, to); }
		if (voice[e].start + voice[e].length <= barStart + barTicks) { *next = e + 1; }
	}
}

//! Every voice of one bar: the first in full, each other one that sounds in the bar after a <backup> to its start.
void writeVoices(QXmlStreamWriter& xml, const std::vector<Voice>& voices, std::vector<std::size_t>& next, int barStart, int barTicks)
{
	for (std::size_t v = 0; v < voices.size(); ++v)
	{
		if (v > 0 && !soundsIn(voices[v], barStart, barStart + barTicks)) { continue; }
		if (v > 0) { writeCursorMove(xml, QStringLiteral("backup"), barTicks, 0); }
		writeBar(xml, voices[v], static_cast<int>(v) + 1, barStart, barTicks, &next[v]);
	}
}

//! The measures: every event cut at the bar lines, each voice filled to the end of the last bar.
void writeMeasures(QXmlStreamWriter& xml, std::vector<Voice> voices, int barTicks, int numerator, int denominator)
{
	int end = barTicks;
	if (!voices.front().empty())
	{
		end = 0;
		for (const Voice& voice : voices) { end = std::max(end, voice.back().start + voice.back().length); }
	}
	const int bars = std::max(1, (end + barTicks - 1) / barTicks);
	for (Voice& voice : voices)
	{
		const int at = voice.empty() ? bars * barTicks : voice.back().start + voice.back().length;
		if (at < bars * barTicks) { voice.push_back({at, bars * barTicks - at, {}}); }
	}
	std::vector<std::size_t> next(voices.size(), 0);
	for (int bar = 0; bar < bars; ++bar)
	{
		xml.writeStartElement(QStringLiteral("measure"));
		xml.writeAttribute(QStringLiteral("number"), QString::number(bar + 1));
		if (bar == 0) { writeAttributes(xml, numerator, denominator); }
		writeVoices(xml, voices, next, bar * barTicks, barTicks);
		xml.writeEndElement();
	}
}

} // namespace

QString musicXmlOf(const MidiClip& clip, int numerator, int denominator, const QString& title)
{
	QString out;
	QXmlStreamWriter xml(&out);
	xml.setAutoFormatting(true);
	xml.writeStartDocument();
	xml.writeDTD(QStringLiteral("<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" "
		"\"http://www.musicxml.org/dtds/partwise.dtd\">"));
	xml.writeStartElement(QStringLiteral("score-partwise"));
	xml.writeAttribute(QStringLiteral("version"), QStringLiteral("4.0"));
	xml.writeStartElement(QStringLiteral("work"));
	xml.writeTextElement(QStringLiteral("work-title"), title);
	xml.writeEndElement();
	xml.writeStartElement(QStringLiteral("part-list"));
	xml.writeStartElement(QStringLiteral("score-part"));
	xml.writeAttribute(QStringLiteral("id"), QStringLiteral("P1"));
	xml.writeTextElement(QStringLiteral("part-name"), title);
	xml.writeEndElement();
	xml.writeEndElement();
	xml.writeStartElement(QStringLiteral("part"));
	xml.writeAttribute(QStringLiteral("id"), QStringLiteral("P1"));
	const int barTicks = std::max(Grid, snapped(DefaultTicksPerBar * std::max(1, numerator) / std::max(1, denominator)));
	writeMeasures(xml, voicesOf(clip), barTicks, std::max(1, numerator), std::max(1, denominator));
	xml.writeEndElement();
	xml.writeEndElement();
	xml.writeEndDocument();
	return out;
}

} // namespace lmms
