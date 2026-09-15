/*
 * SmfInterchangeReader.cpp - the Standard MIDI File conductor track: the READER
 *                           half of include/SmfInterchange.h.
 *
 * Split out of src/core/SmfInterchange.cpp because the file-length ratchet
 * allows 500 lines. The writer is the other file, and the two share nothing but
 * the header's declarations and the convention constants: a writer decides what
 * THIS engine means, a reader has to survive whatever a foreign program meant,
 * so they are two programs that happen to agree on a format.
 *
 * The reader's own rules - and why each of them is a decision rather than an
 * accident - are recorded in docs/SMF-INTERCHANGE.md section 1. In one line:
 * every track is read, merging is per half with the first in file order winning,
 * a foreign division is scaled onto LMMS' 48-ticks-per-quarter grid with every
 * rounded event COUNTED, the walk is bounds-checked (running-status channel
 * events, sysex and unknown meta events are skipped, not refused), and a file
 * that is not a Standard MIDI File or whose division is an SMPTE rate is refused
 * with a reason. The decomposition below is not decoration either: the fork's
 * complexity ratchet holds a function to CCN <= 10, and one 60-branch event loop
 * is not reviewable.
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

#include <QFile>

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

/*! Everything the reader does goes through this cursor, so an out-of-range read
 *  is one bool and never a wild pointer: a foreign file is INPUT, and input is
 *  not trusted. */
struct Cursor
{
	const unsigned char* p = nullptr;
	const unsigned char* end = nullptr;

	bool atEnd() const { return p >= end; }
	qint64 remaining() const { return end - p; }

	bool byte(unsigned char* out)
	{
		if (remaining() < 1) { return false; }
		*out = *p++;
		return true;
	}

	bool u16(int* out)
	{
		if (remaining() < 2) { return false; }
		*out = (static_cast<int>(p[0]) << 8) | p[1];
		p += 2;
		return true;
	}

	bool u32(quint32* out)
	{
		if (remaining() < 4) { return false; }
		*out = (static_cast<quint32>(p[0]) << 24) | (static_cast<quint32>(p[1]) << 16)
			| (static_cast<quint32>(p[2]) << 8) | static_cast<quint32>(p[3]);
		p += 4;
		return true;
	}

	//! A variable-length quantity: seven bits per byte, high bit set on every
	//! byte but the last. False when it runs off the end.
	bool vlq(quint32* out)
	{
		quint32 value = 0;
		for (int i = 0; i < 4; i++)
		{
			unsigned char b = 0;
			if (!byte(&b)) { return false; }
			value = (value << 7) | static_cast<quint32>(b & 0x7F);
			if ((b & 0x80) == 0)
			{
				*out = value;
				return true;
			}
		}
		return false;
	}

	bool skip(qint64 count)
	{
		if (count < 0 || remaining() < count) { return false; }
		p += count;
		return true;
	}
};

//! The running-status state of ONE track.
struct TrackState
{
	unsigned char runningStatus = 0;
	bool running = false;
};

//! One candidate event, in the FILE's tick domain. The vector keeps file order,
//! so two tracks naming the same tick resolve the same way every run.
struct Candidate
{
	quint32 smfTick = 0;
	bool hasTempo = false;
	int microseconds = 0;
	bool hasTimeSignature = false;
	int numerator = 0;
	int denominator = 0;
};

//! What one event turned out to be: anything but EndOfTrack keeps reading.
enum class EventOutcome { Continue, EndOfTrack };

bool skipVariablePayload(Cursor& track, const char* what, QString* error)
{
	quint32 length = 0;
	if (!track.vlq(&length) || !track.skip(length))
	{
		*error = QStringLiteral("%1 is truncated").arg(QLatin1String(what));
		return false;
	}
	return true;
}

bool skipFixed(Cursor& track, qint64 bytes, const char* what, QString* error)
{
	if (track.skip(bytes)) { return true; }
	*error = QStringLiteral("%1 is truncated").arg(QLatin1String(what));
	return false;
}

bool skipChannelEvent(Cursor& track, unsigned char status, QString* error)
{
	const int high = status & 0xF0;
	return skipFixed(track, (high == 0xC0 || high == 0xD0) ? 1 : 2, "a channel event", error);
}

/*! Skip the payload of a status that carries no tempo or metre: sysex,
 *  channel and system-common events. A status this format never puts inside a
 *  track is the one case that is refused rather than skipped. */
bool skipEventPayload(Cursor& track, unsigned char status, QString* error)
{
	if (status == 0xF0 || status == 0xF7) { return skipVariablePayload(track, "a system-exclusive event", error); }
	if (status <= 0xEF) { return skipChannelEvent(track, status, error); }
	if (status == 0xF1 || status == 0xF3) { return skipFixed(track, 1, "a system-common event", error); }
	if (status == 0xF2) { return skipFixed(track, 2, "a system-common event", error); }
	if (status < 0xF6)
	{
		*error = QStringLiteral("unexpected status byte 0x%1")
			.arg(status, 2, 16, QLatin1Char('0'));
		return false;
	}
	return true;  // 0xF6 and the real-time statuses carry no data bytes
}

bool readMetaEvent(Cursor& track, quint32 smfTick, std::vector<Candidate>* candidates,
	EventOutcome* outcome, QString* error)
{
	unsigned char type = 0;
	quint32 length = 0;
	if (!track.byte(&type) || !track.vlq(&length))
	{
		*error = QStringLiteral("a meta event is truncated");
		return false;
	}
	const unsigned char* payload = track.p;
	if (!track.skip(length))
	{
		*error = QStringLiteral("a meta event's payload runs past the track");
		return false;
	}
	if (type == SmfMetaTempo && length == 3)
	{
		Candidate candidate;
		candidate.smfTick = smfTick;
		candidate.hasTempo = true;
		candidate.microseconds = (payload[0] << 16) | (payload[1] << 8) | payload[2];
		candidates->push_back(candidate);
	}
	else if (type == SmfMetaTimeSignature && length >= 2)
	{
		Candidate candidate;
		candidate.smfTick = smfTick;
		candidate.hasTimeSignature = true;
		candidate.numerator = payload[0];
		candidate.denominator = 1 << (payload[1] & 0x1F);
		candidates->push_back(candidate);
	}
	else if (type == SmfMetaEndOfTrack) { *outcome = EventOutcome::EndOfTrack; }
	return true;
}

/*! The status byte of one event, running status included (SMF 1.0: a data byte
 *  where a status byte is expected repeats the last channel status). */
bool resolveStatus(Cursor& track, TrackState* state, unsigned char* status, QString* error)
{
	unsigned char byte = 0;
	if (!track.byte(&byte))
	{
		*error = QStringLiteral("a track ends without an end-of-track event");
		return false;
	}
	if (byte >= 0x80)
	{
		*status = byte;
		state->running = byte < 0xF0;
		if (state->running) { state->runningStatus = byte; }
		return true;
	}
	if (!state->running)
	{
		*error = QStringLiteral("a data byte appears before any status byte");
		return false;
	}
	*status = state->runningStatus;
	track.p -= 1;  // running status: this byte is the event's first data byte
	return true;
}

bool readEvent(Cursor& track, quint32 smfTick, TrackState* state,
	std::vector<Candidate>* candidates, EventOutcome* outcome, QString* error)
{
	unsigned char status = 0;
	if (!resolveStatus(track, state, &status, error)) { return false; }
	if (status == 0xFF)
	{
		return readMetaEvent(track, smfTick, candidates, outcome, error);
	}
	return skipEventPayload(track, status, error);
}

//! Walk one MTrk chunk's events.
bool readTrack(Cursor& track, std::vector<Candidate>* candidates, QString* error)
{
	TrackState state;
	quint32 absolute = 0;
	while (!track.atEnd())
	{
		quint32 delta = 0;
		if (!track.vlq(&delta))
		{
			*error = QStringLiteral("a delta time is malformed");
			return false;
		}
		absolute += delta;
		EventOutcome outcome = EventOutcome::Continue;
		if (!readEvent(track, absolute, &state, candidates, &outcome, error)) { return false; }
		if (outcome == EventOutcome::EndOfTrack) { return true; }
	}
	return true;
}

bool expectChunkId(Cursor& cursor, const char* expected, const QString& message, QString* error)
{
	unsigned char id[4];
	for (unsigned char& c : id)
	{
		if (!cursor.byte(&c))
		{
			*error = QStringLiteral("the file is empty or truncated");
			return false;
		}
	}
	if (std::memcmp(id, expected, 4) != 0)
	{
		*error = message;
		return false;
	}
	return true;
}

//! The division must be a positive ticks-per-quarter-note count: the high bit
//! set would be an SMPTE rate (frames/subframes, no tick grid at all).
bool validateDivision(int division, QString* error)
{
	if ((division & 0x8000) != 0)
	{
		*error = QStringLiteral("SMPTE time division (division 0x%1) is not a ticks-per-quarter-note "
			"grid, so the tempo map cannot be read from this file").arg(division, 4, 16, QLatin1Char('0'));
		return false;
	}
	if (division <= 0)
	{
		*error = QStringLiteral("the file declares division 0 - there is no tick grid to map");
		return false;
	}
	return true;
}

bool parseHeader(Cursor& cursor, SmfReadReport* report, QString* error)
{
	if (!expectChunkId(cursor, "MThd",
			QStringLiteral("not a Standard MIDI File: the first four bytes are not 'MThd'"),
			error))
	{
		return false;
	}
	quint32 headerLength = 0;
	int format = 0;
	int trackCount = 0;
	int division = 0;
	if (!cursor.u32(&headerLength) || headerLength < 6 || !cursor.u16(&format)
		|| !cursor.u16(&trackCount) || !cursor.u16(&division))
	{
		*error = QStringLiteral("the MThd chunk is truncated");
		return false;
	}
	if (headerLength > 6) { cursor.skip(headerLength - 6); }
	if (!validateDivision(division, error)) { return false; }
	report->format = format;
	report->trackCount = trackCount;
	report->ticksPerQuarterNote = division;
	return true;
}

//! Every track's candidates, in file order.
bool readTracks(Cursor& cursor, int* tracksRead, std::vector<Candidate>* candidates, QString* error)
{
	while (!cursor.atEnd())
	{
		if (cursor.remaining() < 4) { break; }
		unsigned char id[4];
		for (unsigned char& c : id) { cursor.byte(&c); }
		quint32 chunkLength = 0;
		if (!cursor.u32(&chunkLength))
		{
			*error = QStringLiteral("a chunk header is truncated");
			return false;
		}
		if (cursor.remaining() < static_cast<qint64>(chunkLength))
		{
			*error = QStringLiteral("a chunk claims %1 bytes and the file has %2 left")
				.arg(chunkLength).arg(cursor.remaining());
			return false;
		}
		if (std::memcmp(id, "MTrk", 4) != 0)
		{
			cursor.skip(chunkLength);
			continue;
		}
		Cursor track{cursor.p, cursor.p + chunkLength};
		cursor.skip(chunkLength);
		(*tracksRead)++;
		if (!readTrack(track, candidates, error)) { return false; }
	}
	return true;
}

//! The file tick -> LMMS tick conversion: round-to-nearest on LMMS' 48-per-
//! quarter grid, with \a rounded set when the tick was not representable exactly.
tick_t lmmsTickFor(quint32 smfTick, int ticksPerQuarterNote, bool* rounded)
{
	const qint64 scaled = static_cast<qint64>(smfTick) * LmmsTicksPerQuarterNote;
	const qint64 quotient = scaled / ticksPerQuarterNote;
	const qint64 remainder = scaled % ticksPerQuarterNote;
	if (remainder != 0) { *rounded = true; }
	return static_cast<tick_t>(quotient + (2 * remainder >= ticksPerQuarterNote ? 1 : 0));
}

//! The tempo a microseconds-per-quarter event names, rounded to whole bpm.
int tempoOf(const Candidate& candidate)
{
	return static_cast<int>(std::llround(
		static_cast<double>(SmfMicrosecondsPerMinute) / std::max(1, candidate.microseconds)));
}

void applyTempo(const Candidate& candidate, SmfInterchangeEvent* event, SmfReadReport* report)
{
	if (event->hasTempo)
	{
		report->supersededEvents++;
		return;
	}
	event->hasTempo = true;
	event->tempo = tempoOf(candidate);
}

void applyTimeSignature(const Candidate& candidate, SmfInterchangeEvent* event,
	SmfReadReport* report)
{
	if (event->hasTimeSignature)
	{
		report->supersededEvents++;
		return;
	}
	event->hasTimeSignature = true;
	event->numerator = candidate.numerator;
	event->denominator = candidate.denominator;
}

/*! One entry per tick, merged PER HALF. The candidates are already in file
 *  order, so "first wins" is simply "the first one seen", which is the rule a
 *  player follows. */
void mergeCandidates(const std::vector<Candidate>& candidates, SmfReadReport* report,
	std::vector<SmfInterchangeEvent>* merged)
{
	for (const Candidate& candidate : candidates)
	{
		bool rounded = false;
		const tick_t tick = lmmsTickFor(candidate.smfTick, report->ticksPerQuarterNote, &rounded);
		if (rounded) { report->roundedEvents++; }
		if (merged->empty() || merged->back().tick != tick)
		{
			SmfInterchangeEvent event;
			event.tick = tick;
			merged->push_back(event);
		}
		SmfInterchangeEvent& last = merged->back();
		if (candidate.hasTempo) { applyTempo(candidate, &last, report); }
		if (candidate.hasTimeSignature) { applyTimeSignature(candidate, &last, report); }
	}
}

void summarize(const std::vector<SmfInterchangeEvent>& merged, SmfReadReport* report)
{
	for (const SmfInterchangeEvent& event : merged)
	{
		if (event.hasTempo) { report->tempoEvents++; }
		if (event.hasTimeSignature) { report->meterEvents++; }
	}
	if (static_cast<int>(merged.size()) > TempoMap::MaxEvents)
	{
		report->capacityEvents = static_cast<int>(merged.size()) - TempoMap::MaxEvents;
	}
}

bool readAllTracks(const QByteArray& bytes, SmfReadReport* report, QString* error)
{
	const unsigned char* raw = reinterpret_cast<const unsigned char*>(bytes.constData());
	Cursor cursor{raw, raw + bytes.size()};
	if (!parseHeader(cursor, report, error)) { return false; }

	std::vector<Candidate> candidates;
	int tracksRead = 0;
	if (!readTracks(cursor, &tracksRead, &candidates, error)) { return false; }
	if (tracksRead == 0)
	{
		*error = QStringLiteral("the file carries no MTrk track");
		return false;
	}

	std::stable_sort(candidates.begin(), candidates.end(),
		[](const Candidate& a, const Candidate& b) { return a.smfTick < b.smfTick; });
	mergeCandidates(candidates, report, &report->events);
	summarize(report->events, report);
	return true;
}

} // namespace

SmfReadReport readConductorTrack(const QString& path)
{
	SmfReadReport report;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		report.error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
		return report;
	}
	const QByteArray bytes = file.readAll();
	file.close();

	const bool ok = readAllTracks(bytes, &report, &report.error);
	report.ok = ok;
	if (ok) { report.error.clear(); }
	return report;
}

} // namespace interchange
} // namespace lmms
