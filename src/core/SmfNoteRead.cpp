/*
 * SmfNoteRead.cpp - the notes of a Standard MIDI File, read defensively
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
/*! See SmfNoteRead.h. The walk is one cursor over the bytes; every read checks the end first, so a truncated or
 *  hostile file fails with a message and never reads outside the buffer. */

#include "SmfNoteRead.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <map>

#include "TimePos.h"

namespace lmms::interchange
{

namespace
{

constexpr int LmmsTicksPerQuarter = DefaultTicksPerBar / 4;
constexpr int MaxTracks = 4096;
constexpr int HeaderBytes = 14;  // "MThd", length 6, format, tracks, division

//! A bounds-checked cursor over one chunk of the file.
class Cursor
{
public:
	Cursor(const QByteArray& bytes, int from, int to) : m_bytes(bytes), m_at(from), m_end(to) {}

	bool atEnd() const { return m_at >= m_end; }

	bool u8(int* value)
	{
		if (atEnd()) { return false; }
		*value = static_cast<unsigned char>(m_bytes[m_at++]);
		return true;
	}

	void unread() { --m_at; }

	bool skip(long long count)
	{
		if (count < 0 || count > m_end - m_at) { return false; }
		m_at += static_cast<int>(count);
		return true;
	}

	//! A variable-length quantity: at most four bytes, as the format allows.
	bool vlq(long long* value)
	{
		long long result = 0;
		for (int i = 0; i < 4; ++i)
		{
			int byte = 0;
			if (!u8(&byte)) { return false; }
			result = (result << 7) | (byte & 0x7F);
			if ((byte & 0x80) == 0)
			{
				*value = result;
				return true;
			}
		}
		return false;
	}

private:
	const QByteArray& m_bytes;
	int m_at;
	int m_end;
};

int be16(const QByteArray& bytes, int at)
{
	return (static_cast<unsigned char>(bytes[at]) << 8) | static_cast<unsigned char>(bytes[at + 1]);
}

long long be32(const QByteArray& bytes, int at)
{
	return (static_cast<long long>(be16(bytes, at)) << 16) | be16(bytes, at + 2);
}

bool isZeroPadding(const QByteArray& bytes, long long from)
{
	if (from >= bytes.size()) { return false; }
	for (long long at = from; at < bytes.size(); ++at)
	{
		if (bytes[static_cast<int>(at)] != '\0') { return false; }
	}
	return true;
}

std::atomic<long long> g_noteQueueShifts{0};

//! One still-open note in its pitch's queue, counting its own place changes (see smfNoteQueueShiftCount()): the
//! count a test reads to hold closing the oldest note free for the notes behind it, whatever holds the queue.
struct QueuedNote
{
	SmfNote note{};
	QueuedNote() = default;
	explicit QueuedNote(const SmfNote& value) : note(value) {}

	QueuedNote(const QueuedNote& other) : note(other.note) { countShift(); }
	QueuedNote(QueuedNote&& other) noexcept : note(other.note) { countShift(); }
	QueuedNote& operator=(const QueuedNote& other) noexcept
	{
		note = other.note;
		countShift();
		return *this;
	}
	QueuedNote& operator=(QueuedNote&& other) noexcept
	{
		note = other.note;
		countShift();
		return *this;
	}

	static void countShift() noexcept { g_noteQueueShifts.fetch_add(1, std::memory_order_relaxed); }
};

//! One track being read: where it is in file ticks, its running status, and the notes still sounding.
struct TrackWalk
{
	int division = 480;
	long long tick = 0;
	int running = 0;
	bool ended = false;
	std::map<int, std::deque<QueuedNote>> open;  //!< (channel * 128 + key) -> notes not yet closed, oldest first
	std::vector<SmfNote>* out = nullptr;

	long long lmmsTicks(long long fileTicks) const
	{
		return (fileTicks * LmmsTicksPerQuarter + division / 2) / division;
	}

	void close(SmfNote note)
	{
		note.length = std::max<long long>(1, lmmsTicks(tick) - note.start);
		out->push_back(note);
	}

	void closeAll()
	{
		for (auto& entry : open)
		{
			for (const QueuedNote& queued : entry.second) { close(queued.note); }
		}
		open.clear();
	}
};

//! A note-on (velocity > 0) opens a note; anything else that reaches here closes the oldest open note of its key.
void noteMessage(int status, int key, int velocity, TrackWalk* walk)
{
	std::deque<QueuedNote>& queue = walk->open[(status & 0x0F) * 128 + key];
	if ((status & 0xF0) == 0x90 && velocity > 0)
	{
		SmfNote note;
		note.start = walk->lmmsTicks(walk->tick);
		note.key = key;
		note.velocity = velocity;
		note.channel = status & 0x0F;
		queue.emplace_back(note);
	}
	else if (!queue.empty())  // a note-off with no note is ignored
	{
		walk->close(queue.front().note);
		queue.pop_front();  // the notes queued behind the closed one must not be moved (new-p14-314)
	}
}

//! One channel message (its status is read); false on a truncated or malformed one.
bool channelEvent(Cursor& cursor, int status, TrackWalk* walk)
{
	const int kind = status & 0xF0;
	const int dataBytes = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
	int key = 0;
	int velocity = 0;
	if (!cursor.u8(&key) || (dataBytes == 2 && !cursor.u8(&velocity))) { return false; }
	if ((key & 0x80) != 0 || (velocity & 0x80) != 0) { return false; }
	if (kind == 0x80 || kind == 0x90) { noteMessage(status, key, velocity, walk); }
	return true;
}

//! A meta event (type, length, data) or a system-exclusive one (length, data): skipped, the length checked.
bool skippedEvent(Cursor& cursor, int status, TrackWalk* walk)
{
	int type = 0;
	long long length = 0;
	if (status == 0xFF && !cursor.u8(&type)) { return false; }
	if (!cursor.vlq(&length) || !cursor.skip(length)) { return false; }
	if (status == 0xFF && type == 0x2F)  // SMF 1.0: the end-of-track marker carries no payload and is the track's last event
	{
		if (length != 0 || !cursor.atEnd()) { return false; }
		walk->ended = true;
	}
	walk->running = 0;
	return true;
}

bool trackEvent(Cursor& cursor, TrackWalk* walk)
{
	long long delta = 0;
	int status = 0;
	if (!cursor.vlq(&delta) || !cursor.u8(&status)) { return false; }
	walk->tick += delta;
	if (status < 0x80)
	{
		if (walk->running == 0) { return false; }  // data with no status to run on
		cursor.unread();
		status = walk->running;
	}
	if (status == 0xFF || status == 0xF0 || status == 0xF7) { return skippedEvent(cursor, status, walk); }
	if (status >= 0xF0) { return false; }  // system common/real-time has no place in a file
	walk->running = status;
	return channelEvent(cursor, status, walk);
}

bool readTrack(const QByteArray& bytes, int from, int to, int division, std::vector<SmfNote>* out)
{
	Cursor cursor(bytes, from, to);
	TrackWalk walk;
	walk.division = division;
	walk.out = out;
	while (!cursor.atEnd() && !walk.ended)
	{
		if (!trackEvent(cursor, &walk)) { return false; }
		if (out->size() > static_cast<std::size_t>(SmfNoteMaxCount)) { return false; }
	}
	walk.closeAll();  // a note never closed ends where its track does
	return out->size() <= static_cast<std::size_t>(SmfNoteMaxCount);  // counted after the unclosed ones are closed
}

bool failWith(SmfNoteRead* read, const QString& message)
{
	read->ok = false;
	read->error = message;
	read->notes.clear();
	return false;
}

bool readHeader(const QByteArray& bytes, SmfNoteRead* read)
{
	if (bytes.size() < HeaderBytes || bytes.left(4) != QByteArrayLiteral("MThd"))
	{
		return failWith(read, QStringLiteral("it is not a Standard MIDI File (no MThd header)"));
	}
	if (be32(bytes, 4) < 6) { return failWith(read, QStringLiteral("the MIDI file header is too short")); }
	read->format = be16(bytes, 8);
	read->trackCount = be16(bytes, 10);
	read->division = be16(bytes, 12);
	if (read->format > 2) { return failWith(read, QStringLiteral("the MIDI file format %1 is not 0, 1 or 2").arg(read->format)); }
	if (read->trackCount < 1 || read->trackCount > MaxTracks)
	{
		return failWith(read, QStringLiteral("the MIDI file declares %1 tracks").arg(read->trackCount));
	}
	if ((read->division & 0x8000) != 0)
	{
		return failWith(read, QStringLiteral("the MIDI file counts time in SMPTE frames, not beats, so it cannot be placed on the arranger"));
	}
	if (read->division == 0) { return failWith(read, QStringLiteral("the MIDI file's division is 0")); }
	return true;
}

//! Walks the chunks after the header; false (with read->error) on a truncated or malformed track.
bool readChunks(const QByteArray& bytes, SmfNoteRead* read)
{
	long long at = 8 + be32(bytes, 4);
	int tracksRead = 0;
	while (at + 8 <= bytes.size())
	{
		const long long length = be32(bytes, static_cast<int>(at) + 4);
		const long long body = at + 8;
		if (body + length > bytes.size()) { return failWith(read, QStringLiteral("the MIDI file is truncated inside a chunk")); }
		if (bytes.mid(static_cast<int>(at), 4) == QByteArrayLiteral("MTrk"))
		{
			if (!readTrack(bytes, static_cast<int>(body), static_cast<int>(body + length), read->division, &read->notes))
			{
				return failWith(read, read->notes.size() > static_cast<std::size_t>(SmfNoteMaxCount)
					? QStringLiteral("the MIDI file holds more than %1 notes").arg(SmfNoteMaxCount)
					: QStringLiteral("track %1 of the MIDI file is malformed").arg(tracksRead + 1));
			}
			++tracksRead;
		}
		at = body + length;
	}
	if (at != bytes.size() && !isZeroPadding(bytes, at))
	{
		return failWith(read, QStringLiteral("the MIDI file has %1 stray bytes after its last chunk").arg(bytes.size() - at));
	}
	if (tracksRead == 0) { return failWith(read, QStringLiteral("the MIDI file holds no track")); }
	if (tracksRead != read->trackCount)
	{
		return failWith(read, QStringLiteral("the MIDI file declares %1 tracks but holds %2").arg(read->trackCount).arg(tracksRead));
	}
	return true;
}

} // namespace

SmfNoteRead readSmfNotes(const QByteArray& bytes)
{
	SmfNoteRead read;
	if (bytes.size() > SmfNoteMaxBytes)
	{
		failWith(&read, QStringLiteral("the file is larger than the %1 MiB a MIDI file may be here").arg(SmfNoteMaxBytes / (1024 * 1024)));
		return read;
	}
	if (!readHeader(bytes, &read) || !readChunks(bytes, &read)) { return read; }
	if (read.notes.empty())
	{
		failWith(&read, QStringLiteral("the MIDI file holds no notes"));
		return read;
	}
	std::sort(read.notes.begin(), read.notes.end(), [](const SmfNote& a, const SmfNote& b) {
		return a.start != b.start ? a.start < b.start : a.key < b.key;
	});
	for (const SmfNote& note : read.notes) { read.endTick = std::max(read.endTick, note.start + note.length); }
	read.ok = true;
	return read;
}

long long smfNoteQueueShiftCount()
{
	return g_noteQueueShifts.load(std::memory_order_relaxed);
}

} // namespace lmms::interchange
