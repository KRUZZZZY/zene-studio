/*
 * SmfInterchangeTestSupport.h - fixtures for the Standard MIDI File
 *                               interchange proofs (feature row 33).
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

#ifndef LMMS_TESTS_SMF_INTERCHANGE_TEST_SUPPORT_H
#define LMMS_TESTS_SMF_INTERCHANGE_TEST_SUPPORT_H

//! The fixtures both halves of the interchange proof use
//! (SmfInterchangeTest = the surface and the file's own bytes;
//! SmfInterchangeRoundTripTest = the round trip that compares the MAP).
//!
//! TWO of these deliberately share no code with the module under test:
//!
//!  - parseConductorFile() is this test suite's OWN Standard MIDI File parser.
//!    The claim it serves is "the file is well formed in the format's own terms,
//!    so another DAW can read it", and asking the writer's twin whether the
//!    writer was right would not measure that.
//!  - plainFile() is a minimal writer for the files the module must READ
//!    (foreign divisions, a file past the map's capacity).
//!
//! It lives in a header rather than in one of the two .cpp files because the
//! file-length ratchet allows 500 lines and the two proofs together are more
//! than that - the same seam tests/src/core/StemExportTestSupport.h documents.

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include <vector>

#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "TempoMap.h"
#include "TimePos.h"

namespace smfsupport
{

// The engine's vocabulary lives in namespace lmms; the tests that include this
// header all `using namespace lmms` themselves, and the header cannot rely on
// that (it is included before their using-directive), so it says so here.
using namespace lmms;

//! Invoke a command through the registry, exactly as the socket does.
inline ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A path inside the caller's own temp directory (removed with it).
inline QString path(const QTemporaryDir& directory, const QString& name)
{
	return directory.filePath(name);
}

// --- this suite's OWN Standard MIDI File parser ----------------------------

struct MetaEvent
{
	quint32 tick = 0;
	//! The event's own delta time, as the file encodes it: the format's unit.
	//! `tick` is the absolute position this parser ACCUMULATES from it, and the
	//! two answer different questions - "where does this event sit" (tick) and
	//! "how far after the previous one is it" (delta).
	quint32 delta = 0;
	unsigned char type = 0;
	QByteArray payload;
};

inline quint32 be32(const QByteArray& bytes, int offset)
{
	return (static_cast<quint32>(static_cast<unsigned char>(bytes[offset])) << 24)
		| (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 1])) << 16)
		| (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 2])) << 8)
		| static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 3]));
}

inline int be16(const QByteArray& bytes, int offset)
{
	return (static_cast<int>(static_cast<unsigned char>(bytes[offset])) << 8)
		| static_cast<int>(static_cast<unsigned char>(bytes[offset + 1]));
}

/*! Walk a single-track conductor file. Strict on purpose: the writer must emit
 *  meta events only, so anything else is a failure of THIS claim. */
inline bool parseConductorFile(const QByteArray& bytes, int* format, int* trackCount,
	int* division, std::vector<MetaEvent>* events, QString* error)
{
	if (bytes.size() < 14)
	{
		*error = QStringLiteral("the file is shorter than a header chunk");
		return false;
	}
	if (bytes.left(4) != QByteArrayLiteral("MThd"))
	{
		*error = QStringLiteral("the first four bytes are not MThd");
		return false;
	}
	const int headerLength = static_cast<int>(be32(bytes, 4));
	if (headerLength != 6)
	{
		*error = QStringLiteral("the header chunk is %1 bytes, not 6").arg(headerLength);
		return false;
	}
	*format = be16(bytes, 8);
	*trackCount = be16(bytes, 10);
	*division = be16(bytes, 12);
	if (*trackCount != 1)
	{
		*error = QStringLiteral("the file declares %1 tracks").arg(*trackCount);
		return false;
	}
	int offset = 8 + headerLength;
	if (bytes.mid(offset, 4) != QByteArrayLiteral("MTrk"))
	{
		*error = QStringLiteral("the first chunk after the header is not MTrk");
		return false;
	}
	const int trackLength = static_cast<int>(be32(bytes, offset + 4));
	offset += 8;
	if (offset + trackLength != bytes.size())
	{
		*error = QStringLiteral("the track chunk's declared length %1 does not reach the end "
			"of the file").arg(trackLength);
		return false;
	}

	quint32 tick = 0;
	while (offset < bytes.size())
	{
		quint32 delta = 0;
		for (int i = 0; i < 4; i++)
		{
			const unsigned char byte = static_cast<unsigned char>(bytes[offset++]);
			delta = (delta << 7) | static_cast<quint32>(byte & 0x7F);
			if ((byte & 0x80) == 0) { break; }
		}
		tick += delta;
		const unsigned char status = static_cast<unsigned char>(bytes[offset++]);
		if (status != 0xFF)
		{
			*error = QStringLiteral("the conductor track carries status 0x%1, which a tempo "
				"map has no use for").arg(status, 2, 16, QLatin1Char('0'));
			return false;
		}
		const unsigned char type = static_cast<unsigned char>(bytes[offset++]);
		quint32 length = 0;
		for (int i = 0; i < 4; i++)
		{
			const unsigned char byte = static_cast<unsigned char>(bytes[offset++]);
			length = (length << 7) | static_cast<quint32>(byte & 0x7F);
			if ((byte & 0x80) == 0) { break; }
		}
		if (offset + static_cast<int>(length) > bytes.size())
		{
			*error = QStringLiteral("a meta event's payload runs past the end of the file");
			return false;
		}
		MetaEvent event;
		event.tick = tick;
		event.delta = delta;
		event.type = type;
		event.payload = bytes.mid(offset, static_cast<int>(length));
		offset += static_cast<int>(length);
		events->push_back(event);
	}
	return true;
}

// --- a minimal writer for the files the module must READ --------------------

struct PlainEvent
{
	quint32 smfTick = 0;
	int bpm = 0;            //!< 0 = no tempo half
	int numerator = 0;      //!< 0 = no metre half
	int denominator = 0;

	//! The same event in LMMS' tick domain (48 per quarter, 480 per bar).
	tick_t lmmsTick() const { return static_cast<tick_t>(smfTick / 10); }
};

inline void appendVlq(QByteArray& out, quint32 value)
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

inline void appendMeta(QByteArray& out, unsigned char type, const QByteArray& payload)
{
	out.append(static_cast<char>(0xFF));
	out.append(static_cast<char>(type));
	appendVlq(out, static_cast<quint32>(payload.size()));
	out.append(payload);
}

inline QByteArray plainFile(int division, const std::vector<PlainEvent>& events)
{
	QByteArray body;
	quint32 cursor = 0;
	for (const PlainEvent& event : events)
	{
		if (event.numerator > 0)
		{
			appendVlq(body, event.smfTick - cursor);
			cursor = event.smfTick;
			int power = 0;
			for (int value = event.denominator; value > 1; value >>= 1) { power++; }
			QByteArray payload;
			payload.append(static_cast<char>(event.numerator));
			payload.append(static_cast<char>(power));
			payload.append(static_cast<char>(24));
			payload.append(static_cast<char>(8));
			appendMeta(body, 0x58, payload);
		}
		if (event.bpm > 0)
		{
			appendVlq(body, event.smfTick - cursor);
			cursor = event.smfTick;
			const qint64 microseconds = (60000000 + event.bpm / 2) / event.bpm;
			QByteArray payload;
			payload.append(static_cast<char>((microseconds >> 16) & 0xFF));
			payload.append(static_cast<char>((microseconds >> 8) & 0xFF));
			payload.append(static_cast<char>(microseconds & 0xFF));
			appendMeta(body, 0x51, payload);
		}
	}
	appendVlq(body, 0);
	appendMeta(body, 0x2F, QByteArray());

	QByteArray bytes;
	bytes.append("MThd", 4);
	bytes.append(static_cast<char>(0));
	bytes.append(static_cast<char>(0));
	bytes.append(static_cast<char>(0));
	bytes.append(static_cast<char>(6));
	bytes.append(static_cast<char>(0));
	bytes.append(static_cast<char>(0));  // format 0
	bytes.append(static_cast<char>(0));
	bytes.append(static_cast<char>(1));  // one track
	bytes.append(static_cast<char>((division >> 8) & 0xFF));
	bytes.append(static_cast<char>(division & 0xFF));
	bytes.append("MTrk", 4);
	const quint32 length = static_cast<quint32>(body.size());
	for (int shift : {24, 16, 8, 0})
	{
		bytes.append(static_cast<char>((length >> shift) & 0xFF));
	}
	bytes.append(body);
	return bytes;
}

inline bool writeFile(const QString& target, const QByteArray& bytes)
{
	QFile file(target);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	return file.write(bytes) == bytes.size();
}

// --- reading the session's own state ---------------------------------------

inline QJsonArray mapEvents()
{
	return run(QStringLiteral("transport.tempo_map_get")).result
		.value(QStringLiteral("events")).toArray();
}

//! "tick|bpm|num/den" per event, joined: one string to compare, and a failure
//! message that names the map rather than a JSON blob.
inline QString digest(const QJsonArray& events)
{
	QStringList parts;
	for (const QJsonValue& value : events)
	{
		const QJsonObject event = value.toObject();
		parts << QStringLiteral("%1|%2|%3/%4")
			.arg(event.value(QStringLiteral("tick")).toVariant().toLongLong())
			.arg(event.value(QStringLiteral("bpm")).toInt())
			.arg(event.value(QStringLiteral("numerator")).toInt())
			.arg(event.value(QStringLiteral("denominator")).toInt());
	}
	return parts.join(QStringLiteral(" "));
}

//! The oracle: the step functions a list of AUTHORED events implies. It reads
//! the authored list, never the engine, so a comparison against it is a
//! comparison against what the test asked for.
inline int oracleTempoAt(const std::vector<PlainEvent>& events, int globalTempo, tick_t tick)
{
	int tempo = globalTempo;
	for (const PlainEvent& event : events)
	{
		if (event.bpm > 0 && event.lmmsTick() <= tick) { tempo = event.bpm; }
	}
	return tempo;
}

inline QString oracleMeterAt(const std::vector<PlainEvent>& events, const QString& global,
	tick_t tick)
{
	QString meter = global;
	for (const PlainEvent& event : events)
	{
		if (event.numerator > 0 && event.lmmsTick() <= tick)
		{
			meter = QStringLiteral("%1/%2").arg(event.numerator).arg(event.denominator);
		}
	}
	return meter;
}

/*! The metre the session's map answers at \a tick, as "n/d": the map's own
 *  query, with the session's GLOBAL time signature as the out-of-range
 *  answer - i.e. what any part of the timeline obeys. */
inline QString meterAt(tick_t tick)
{
	const TempoMapTimeSignature global{
		Engine::getSong()->getTimeSigModel().numeratorModel().value(),
		Engine::getSong()->getTimeSigModel().denominatorModel().value() };
	const TempoMapTimeSignature answered =
		Engine::getSong()->tempoMap().map().timeSignatureAtTick(tick, global);
	return QStringLiteral("%1/%2").arg(answered.numerator).arg(answered.denominator);
}

} // namespace smfsupport

#endif // LMMS_TESTS_SMF_INTERCHANGE_TEST_SUPPORT_H
