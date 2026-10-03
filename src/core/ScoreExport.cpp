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

//! One chord (or a rest, with no keys) over [start, start + length) in clip ticks.
struct Event
{
	int start = 0;
	int length = 0;
	std::vector<int> keys;
};

//! The notes as a single voice: a chord per start, each cut where the next begins, rests in the gaps.
std::vector<Event> voiceOf(const MidiClip& clip)
{
	std::map<int, Event> chords;
	for (const Note* note : clip.notes())
	{
		if (note->muted()) { continue; }
		const int start = snapped(static_cast<int>(note->pos().getTicks()));
		Event& chord = chords[start];
		chord.start = start;
		chord.length = std::max(chord.length, std::max(Grid, snapped(static_cast<int>(note->length().getTicks()))));
		chord.keys.push_back(note->key());
	}
	std::vector<Event> voice;
	int at = 0;
	for (auto it = chords.begin(); it != chords.end(); ++it)
	{
		Event chord = it->second;
		if (chord.start > at) { voice.push_back({at, chord.start - at, {}}); }
		const auto next = std::next(it);
		if (next != chords.end()) { chord.length = std::min(chord.length, next->first - chord.start); }
		std::sort(chord.keys.begin(), chord.keys.end());
		voice.push_back(chord);
		at = chord.start + chord.length;
	}
	return voice;
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

//! One <note> (a chord's member after the first gets <chord/>); @a tieIn / @a tieOut join it to its neighbours.
void writeNote(QXmlStreamWriter& xml, int key, bool chord, const Value& value, bool tieIn, bool tieOut)
{
	const bool tied = key >= 0 && (tieIn || tieOut);
	xml.writeStartElement(QStringLiteral("note"));
	if (chord) { xml.writeEmptyElement(QStringLiteral("chord")); }
	if (key < 0) { xml.writeEmptyElement(QStringLiteral("rest")); } else { writePitch(xml, key); }
	xml.writeTextElement(QStringLiteral("duration"), QString::number(value.ticks));
	if (tied) { writeTies(xml, QStringLiteral("tie"), tieIn, tieOut); }
	xml.writeTextElement(QStringLiteral("voice"), QStringLiteral("1"));
	xml.writeTextElement(QStringLiteral("type"), QLatin1String(value.type));
	if (value.dotted) { xml.writeEmptyElement(QStringLiteral("dot")); }
	if (tied)
	{
		// <tie> alone plays tied but is drawn as separate notes: the score's tie is <notations><tied> (review L17 #8).
		xml.writeStartElement(QStringLiteral("notations"));
		writeTies(xml, QStringLiteral("tied"), tieIn, tieOut);
		xml.writeEndElement();
	}
	xml.writeEndElement();
}

//! The part of @a event that falls in [from, to), tied to what came before and what follows.
void writeSpan(QXmlStreamWriter& xml, const Event& event, int from, int to)
{
	const std::vector<const Value*> values = valuesOf(to - from);
	for (std::size_t v = 0; v < values.size(); ++v)
	{
		const bool tieIn = from > event.start || v > 0;
		const bool tieOut = to < event.start + event.length || v + 1 < values.size();
		if (event.keys.empty()) { writeNote(xml, -1, false, *values[v], false, false); continue; }
		for (std::size_t k = 0; k < event.keys.size(); ++k) { writeNote(xml, event.keys[k], k > 0, *values[v], tieIn, tieOut); }
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

//! The measures: every event cut at the bar lines, the last bar filled with a rest.
void writeMeasures(QXmlStreamWriter& xml, std::vector<Event> voice, int barTicks, int numerator, int denominator)
{
	const int end = voice.empty() ? barTicks : voice.back().start + voice.back().length;
	const int bars = std::max(1, (end + barTicks - 1) / barTicks);
	if (end < bars * barTicks) { voice.push_back({end, bars * barTicks - end, {}}); }
	std::size_t next = 0;
	for (int bar = 0; bar < bars; ++bar)
	{
		xml.writeStartElement(QStringLiteral("measure"));
		xml.writeAttribute(QStringLiteral("number"), QString::number(bar + 1));
		if (bar == 0) { writeAttributes(xml, numerator, denominator); }
		const int barStart = bar * barTicks;
		for (std::size_t e = next; e < voice.size() && voice[e].start < barStart + barTicks; ++e)
		{
			const int from = std::max(voice[e].start, barStart);
			const int to = std::min(voice[e].start + voice[e].length, barStart + barTicks);
			if (to > from) { writeSpan(xml, voice[e], from, to); }
			if (voice[e].start + voice[e].length <= barStart + barTicks) { next = e + 1; }
		}
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
	writeMeasures(xml, voiceOf(clip), barTicks, std::max(1, numerator), std::max(1, denominator));
	xml.writeEndElement();
	xml.writeEndElement();
	xml.writeEndDocument();
	return out;
}

} // namespace lmms
