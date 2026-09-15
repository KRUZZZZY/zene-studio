/*
 * SmfInterchange.cpp - the Standard MIDI File conductor track: the writer and
 *                      the reader behind the `interchange.*` command group.
 *
 * The convention this file implements is stated once, as data, in
 * include/SmfInterchange.h (SmfConvention) and in docs/SMF-INTERCHANGE.md; the
 * two places that matter here are the DIVISION (480 ticks per quarter note,
 * with LMMS' 48 per quarter mapping in exactly, ×10) and the TICK-0 SEED (an
 * SMF has no "global tempo before the first event", so the file carries the
 * tempo and the metre the timeline obeys at tick 0).
 *
 * WHY A HAND-WRITTEN ENCODER AND NOT THE VENDORED LIBRARIES IN THE TREE. The
 * file this module writes has to be readable by another DAW and has to be
 * provable byte-for-byte by the registered ctest, and the two formats are
 * small: a chunk header, four meta events (name, tempo, time signature, end of
 * track) and a variable-length delta time. plugins/MidiExport's MidiFile.hpp is
 * a note-oriented header-only encoder with a fixed track buffer, and
 * plugins/MidiImport vendors portsmf behind a plugin boundary; neither is
 * reachable from src/core, which is where a tempo map lives. A conductor track
 * has no notes, no channels and no running status, so the honest implementation
 * is this one - and the reader DOES need running-status handling, because it
 * reads foreign files.
 *
 * THE READER IS THE OTHER HALF: src/core/SmfInterchangeReader.cpp. It was split
 * out because the file-length ratchet allows 500 lines, and the split is real
 * rather than mechanical: a writer decides what THIS engine means, while a
 * reader has to survive whatever a foreign program meant, so the reader is the
 * place a foreign division, running-status channel events, sysex and unknown
 * meta events are handled (and the place they must not crash).
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

#include "SmfInterchange.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace lmms
{
namespace interchange
{

namespace
{

// --- the write side: big-endian fields and the variable-length quantity -----

void appendU16(QByteArray& out, int value)
{
	out.append(static_cast<char>((value >> 8) & 0xFF));
	out.append(static_cast<char>(value & 0xFF));
}

void appendU32(QByteArray& out, quint32 value)
{
	out.append(static_cast<char>((value >> 24) & 0xFF));
	out.append(static_cast<char>((value >> 16) & 0xFF));
	out.append(static_cast<char>((value >> 8) & 0xFF));
	out.append(static_cast<char>(value & 0xFF));
}

/*! A variable-length quantity: seven bits per byte, most significant group
 *  first, the high bit set on every byte but the last (SMF 1.0's own encoding
 *  of a delta time). */
void appendVlq(QByteArray& out, quint32 value)
{
	unsigned char buffer[5];
	int first = 4;
	buffer[first] = static_cast<unsigned char>(value & 0x7F);
	while ((value >>= 7) > 0)
	{
		buffer[--first] = static_cast<unsigned char>((value & 0x7F) | 0x80);
	}
	out.append(reinterpret_cast<const char*>(buffer + first), 5 - first);
}

void appendMeta(QByteArray& out, unsigned char type, const QByteArray& payload)
{
	out.append(static_cast<char>(0xFF));
	out.append(static_cast<char>(type));
	appendVlq(out, static_cast<quint32>(payload.size()));
	out.append(payload);
}

//! The tempo meta event's payload: microseconds per QUARTER note, 3 bytes.
QByteArray tempoPayload(int bpm)
{
	// The engine's own bounds keep bpm >= 10, so the quotient is > 0 and the
	// 24 bits are plenty (6000000 at 10 bpm is 0x5B8D80).
	const qint64 microseconds = (SmfMicrosecondsPerMinute + bpm / 2) / bpm;
	QByteArray payload;
	payload.append(static_cast<char>((microseconds >> 16) & 0xFF));
	payload.append(static_cast<char>((microseconds >> 8) & 0xFF));
	payload.append(static_cast<char>(microseconds & 0xFF));
	return payload;
}

//! The time-signature meta event's payload: nn dd cc bb.
QByteArray timeSignaturePayload(int numerator, int denominator)
{
	int power = 0;
	for (int value = denominator; value > 1; value >>= 1) { power++; }
	QByteArray payload;
	payload.append(static_cast<char>(numerator & 0xFF));
	payload.append(static_cast<char>(power & 0xFF));
	payload.append(static_cast<char>(24));  // MIDI clocks per metronome click
	payload.append(static_cast<char>(8));   // 32nd notes per quarter note
	return payload;
}

QString sha256Of(const QByteArray& bytes)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(bytes);
	return QString::fromLatin1(hash.result().toHex());
}


// --- the tick-0 seed --------------------------------------------------------

SmfInterchangeEvent eventFromMap(const TempoMapEvent& source)
{
	SmfInterchangeEvent event;
	event.tick = source.tick;
	event.hasTempo = source.hasTempo;
	event.tempo = source.tempo;
	event.hasTimeSignature = source.hasTimeSignature;
	event.numerator = source.numerator;
	event.denominator = source.denominator;
	return event;
}

//! Which halves the list already carries at tick 0.
void tickZeroHalves(const std::vector<SmfInterchangeEvent>& events, bool* hasTempo,
	bool* hasTimeSignature)
{
	for (const SmfInterchangeEvent& event : events)
	{
		if (event.tick != 0) { continue; }
		if (event.hasTempo) { *hasTempo = true; }
		if (event.hasTimeSignature) { *hasTimeSignature = true; }
	}
}

/*! What the timeline obeys AT tick 0: the seed a file needs, because an SMF has
 *  no "global tempo before the first event" and a player assumes 120 bpm there.
 *  An inactive or empty map answers the caller's global values here, which is
 *  exactly what such a session sounds like. */
SmfInterchangeEvent tickZeroSeed(const TempoMap& map, int globalTempo,
	const TempoMapTimeSignature& globalSignature, bool needTempo, bool needTimeSignature)
{
	SmfInterchangeEvent seed;
	seed.tick = 0;
	seed.hasTempo = needTempo;
	if (needTempo) { seed.tempo = map.tempoAtTick(0, globalTempo); }
	seed.hasTimeSignature = needTimeSignature;
	if (needTimeSignature)
	{
		const TempoMapTimeSignature atZero = map.timeSignatureAtTick(0, globalSignature);
		seed.numerator = atZero.numerator;
		seed.denominator = atZero.denominator;
	}
	return seed;
}

//! Copy \a source's halves onto \a target (the seed and the map's own tick-0
//! event are ONE event in the file, not two).
void mergeHalves(SmfInterchangeEvent* target, const SmfInterchangeEvent& source)
{
	if (source.hasTempo)
	{
		target->hasTempo = true;
		target->tempo = source.tempo;
	}
	if (source.hasTimeSignature)
	{
		target->hasTimeSignature = true;
		target->numerator = source.numerator;
		target->denominator = source.denominator;
	}
}


// --- the track chunk, the file and the report -------------------------------

/*! The track chunk's body: delta times are relative to the previous event, so
 *  the list is walked once with a cursor. At one tick the metre is written
 *  BEFORE the tempo - the order every DAW writes a conductor track in. */
QByteArray trackBody(const std::vector<SmfInterchangeEvent>& events)
{
	QByteArray body;
	const QByteArray name(QByteArrayLiteral("Zene Studio tempo map"));
	const quint32 first = events.empty() ? 0 : static_cast<quint32>(events.front().tick);
	appendVlq(body, first);
	appendMeta(body, SmfMetaTrackName, name);

	quint32 cursor = first;
	for (const SmfInterchangeEvent& event : events)
	{
		const quint32 tick = static_cast<quint32>(event.tick);
		if (event.hasTimeSignature)
		{
			appendVlq(body, tick - cursor);
			cursor = tick;
			appendMeta(body, SmfMetaTimeSignature,
				timeSignaturePayload(event.numerator, event.denominator));
		}
		if (event.hasTempo)
		{
			appendVlq(body, tick - cursor);
			cursor = tick;
			appendMeta(body, SmfMetaTempo, tempoPayload(event.tempo));
		}
	}
	appendVlq(body, 0);
	appendMeta(body, SmfMetaEndOfTrack, QByteArray());
	return body;
}

//! The whole file: MThd (format 1, one track, division 480) then one MTrk.
QByteArray conductorFile(const QByteArray& body)
{
	QByteArray bytes;
	bytes.append("MThd", 4);
	appendU32(bytes, 6);
	appendU16(bytes, SmfWrittenFormat);
	appendU16(bytes, 1);
	appendU16(bytes, SmfTicksPerQuarterNote);
	bytes.append("MTrk", 4);
	appendU32(bytes, static_cast<quint32>(body.size()));
	bytes.append(body);
	return bytes;
}

bool writeBytes(const QString& path, const QByteArray& bytes, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
		}
		return false;
	}
	if (file.write(bytes) != bytes.size())
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
		}
		file.close();
		QFile::remove(path);
		return false;
	}
	file.close();
	return true;
}

void fillWriteReport(const QString& path, const QByteArray& bytes,
	const std::vector<SmfInterchangeEvent>& events, SmfWriteReport* report)
{
	report->path = path;
	report->bytes = bytes.size();
	report->sha256 = sha256Of(bytes);
	report->format = SmfWrittenFormat;
	report->trackCount = 1;
	report->ticksPerQuarterNote = SmfTicksPerQuarterNote;
	report->eventCount = static_cast<int>(events.size());
	report->tempoEvents = 0;
	report->meterEvents = 0;
	for (const SmfInterchangeEvent& event : events)
	{
		if (event.hasTempo) { report->tempoEvents++; }
		if (event.hasTimeSignature) { report->meterEvents++; }
	}
	report->firstTick = events.empty() ? 0 : events.front().tick;
	report->lastTick = events.empty() ? 0 : events.back().tick;
}

} // namespace

std::vector<SmfInterchangeEvent> exportEvents(const TempoMap& map, int globalTempo,
	const TempoMapTimeSignature& globalSignature, int* seedCount)
{
	std::vector<SmfInterchangeEvent> events;
	events.reserve(static_cast<std::size_t>(map.size()) + 1);
	for (const TempoMapEvent& source : map.all()) { events.push_back(eventFromMap(source)); }

	bool hasTempoAtZero = false;
	bool hasTimeSignatureAtZero = false;
	tickZeroHalves(events, &hasTempoAtZero, &hasTimeSignatureAtZero);
	int seeds = 0;
	if (!hasTempoAtZero) { seeds++; }
	if (!hasTimeSignatureAtZero) { seeds++; }
	if (seeds > 0)
	{
		const SmfInterchangeEvent seed = tickZeroSeed(map, globalTempo, globalSignature,
			!hasTempoAtZero, !hasTimeSignatureAtZero);
		if (!events.empty() && events.front().tick == 0) { mergeHalves(&events.front(), seed); }
		else { events.insert(events.begin(), seed); }
	}
	if (seedCount != nullptr) { *seedCount = seeds; }
	return events;
}

const SmfConvention& smfConvention()
{
	static const SmfConvention convention = [] {
		SmfConvention c;
		c.tempoUnit = QStringLiteral("microseconds per QUARTER note (meta FF 51 03 tttttt), "
			"independent of the metre in force - which is what this engine's own bpm means, "
			"because a time-signature event changes bar/beat arithmetic and not the "
			"tick-to-frame rate");
		c.timeSignatureEncoding = QStringLiteral("meta FF 58 04 nn dd cc bb: nn = numerator, "
			"denominator = 2^dd, cc = 24 MIDI clocks per metronome click, bb = 8 32nd notes "
			"per quarter note");
		c.trackShape = QStringLiteral("format 1 (written), one conductor track at index 0; the "
			"reader accepts format 0 and format 1 and reads the tempo and metre meta events of "
			"EVERY track, merging by tick, first in file order wins per half");
		c.tickZeroRule = QStringLiteral("the file always names the step in force at tick 0: the "
			"writer seeds tick 0 with map.tempoAtTick(0, global) and "
			"map.timeSignatureAtTick(0, global) unless the map already carries that half at "
			"tick 0 (an SMF has no global tempo before its first event - before one, a player "
			"assumes 120 bpm - so without the seed a map starting later would export a "
			"different session)");
		c.statedLimits = QStringLiteral("events are STEPS, so no tempo curve is representable "
			"and none is written; only the conductor track is written, so notes, clips, "
			"automation and markers are not in the file; on read only the tempo and metre meta "
			"events are used, so the rest of a foreign file is ignored rather than refused");
		return c;
	}();
	return convention;
}


bool writeConductorTrack(const QString& path, const TempoMap& map, int globalTempo,
	const TempoMapTimeSignature& globalSignature, SmfWriteReport* report, QString* error)
{
	const std::vector<SmfInterchangeEvent> events = exportEvents(map, globalTempo, globalSignature,
		report != nullptr ? &report->seedEvents : nullptr);
	const QByteArray bytes = conductorFile(trackBody(events));
	if (!writeBytes(path, bytes, error)) { return false; }
	if (report != nullptr) { fillWriteReport(path, bytes, events, report); }
	return true;
}


bool mapFromEvents(const std::vector<SmfInterchangeEvent>& events, TempoMap* map,
	QString* error)
{
	if (map == nullptr) { return false; }
	if (static_cast<int>(events.size()) > TempoMap::MaxEvents)
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("the file carries %1 conductor ticks and the tempo map holds "
				"%2 events; nothing was applied")
				.arg(events.size()).arg(TempoMap::MaxEvents);
		}
		return false;
	}
	TempoMap candidate;
	for (const SmfInterchangeEvent& source : events)
	{
		TempoMapEvent event;
		event.tick = source.tick;
		event.hasTempo = source.hasTempo;
		event.tempo = source.tempo;
		event.hasTimeSignature = source.hasTimeSignature;
		event.numerator = source.numerator;
		event.denominator = source.denominator;
		if (!TempoMap::validEvent(event))
		{
			if (error != nullptr)
			{
				*error = QStringLiteral("the file's event at tick %1 is not representable in this "
					"engine (tempo %2, time signature %3/%4): the map accepts tempi %5..%6 and a "
					"denominator that is a power of two up to %7")
					.arg(static_cast<qint64>(event.tick)).arg(event.tempo).arg(event.numerator)
					.arg(event.denominator).arg(TempoMapMinTempo).arg(TempoMapMaxTempo)
					.arg(TempoMapLargestDenominator);
			}
			return false;
		}
		if (!candidate.addEvent(event))
		{
			if (error != nullptr)
			{
				*error = QStringLiteral("the file's event at tick %1 could not be added (the event "
					"is invalid or the map is full)")
					.arg(static_cast<qint64>(event.tick));
			}
			return false;
		}
	}
	// A conductor track IS a tempo map: importing one and leaving the map
	// switched off would import tempo changes the timeline does not obey.
	candidate.setActive(true);
	*map = candidate;
	return true;
}

} // namespace interchange
} // namespace lmms
