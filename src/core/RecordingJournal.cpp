/*
 * RecordingJournal.cpp - the take journal's data, its file format and its
 *                        honest bound (see include/RecordingJournal.h).
 *
 * The format is a small `key=value` text file, the same shape the autosave
 * recovery sidecar uses (src/core/ProjectRecovery.cpp), because it has to be
 * readable by a human looking at a crashed session's directory and writable by
 * a thread that must not allocate more than a few hundred bytes:
 *
 *   # zene-studio recording journal - a side file, never part of a project
 *   version=2
 *   state=in_progress
 *   take=/home/me/takes/take-01.wav
 *   sample_rate=48000
 *   channels=1
 *   frames_on_disk=96000
 *   updated_utc=2026-09-13T20:15:01Z
 *
 * A path is a legal POSIX filename, and a legal POSIX filename may hold a
 * newline. Version 2 therefore ESCAPES the string fields (a backslash, an LF
 * and a CR become `\\`, `\n` and `\r`), because a raw newline in a value
 * split it across two lines and the reader - which reads one line at a time -
 * then looked for a take that was never recorded. Version 1 journals, written
 * before the escaping existed, hold literal values and are still read
 * literally: decoding them would rewrite a path that merely contains a
 * backslash.
 *
 * `state=in_progress` is the whole point: a journal that is still in progress
 * when a process starts is a capture that died. A clean stop REMOVES the
 * journal rather than writing `state=finished`, so "there is a journal" and
 * "something went wrong" are the same fact and cannot drift apart; the
 * `finished` state exists only so a writer that prefers to leave a tombstone
 * can, and `scan()` treats it as not recoverable.
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

#include "RecordingJournal.h"

#include <algorithm>

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include "lmms_constants.h"

namespace lmms
{
namespace recordingjournal
{

namespace
{

//! The one state a crashed capture leaves.
const QString StateInProgress = QStringLiteral("in_progress");

//! The format version this build writes: the string fields are escaped.
const int CurrentJournalVersion = 2;

/*! Escapes the characters that would break a line-oriented field: a backslash,
 *  a line feed and a carriage return. A path is a legal POSIX filename and may
 *  hold any of them, and a raw line feed in a value split the value across
 *  two lines - which the reader, one line at a time, could not put back
 *  together. */
QString escapeFieldValue(const QString& value)
{
	QString escaped;
	escaped.reserve(value.size());
	for (const QChar ch : value)
	{
		if (ch == QLatin1Char('\\')) { escaped += QStringLiteral("\\\\"); }
		else if (ch == QLatin1Char('\n')) { escaped += QStringLiteral("\\n"); }
		else if (ch == QLatin1Char('\r')) { escaped += QStringLiteral("\\r"); }
		else { escaped += ch; }
	}
	return escaped;
}

/*! Decodes an escaped field value. An escape this reader does not know is kept
 *  as it was written, so a value that was never escaped by us is not mangled
 *  into something else. */
QString decodeFieldValue(const QString& value)
{
	QString decoded;
	decoded.reserve(value.size());
	for (qsizetype i = 0; i < value.size(); ++i)
	{
		const QChar ch = value.at(i);
		if (ch != QLatin1Char('\\') || i + 1 >= value.size())
		{
			decoded += ch;
			continue;
		}
		const QChar escaped = value.at(++i);
		if (escaped == QLatin1Char('n')) { decoded += QLatin1Char('\n'); }
		else if (escaped == QLatin1Char('r')) { decoded += QLatin1Char('\r'); }
		else if (escaped == QLatin1Char('\\')) { decoded += QLatin1Char('\\'); }
		else { decoded += QLatin1Char('\\'); decoded += escaped; }
	}
	return decoded;
}

//! The format version a journal's `version=` line names (0 when it names none).
int journalVersion(const QString& line)
{
	if (!line.startsWith(QLatin1String("version="))) { return 0; }
	return line.mid(8).toInt();
}

/*! Reads a RIFF chunk's 4-byte id. An empty id at EOF or on a short read, so a
 *  truncated file cannot be mistaken for a chunk. */
QByteArray readChunkId(QDataStream& in)
{
	char raw[4] = {0, 0, 0, 0};
	if (in.readRawData(raw, 4) != 4) { return QByteArray(); }
	return QByteArray(raw, 4);
}

//! The next \a count bytes of the stream, as a QByteArray (empty on a short read).
QByteArray nextBytes(QDataStream& in, int count)
{
	QByteArray out(count, '\0');
	if (in.readRawData(out.data(), count) != count) { return QByteArray(); }
	return out;
}

//! A `fmt ` chunk's block align (bytes per frame), or 0 when it is too short.
int blockAlignOf(const QByteArray& fmt)
{
	if (fmt.size() < 16) { return 0; }
	return static_cast<unsigned char>(fmt[12])
		| (static_cast<unsigned char>(fmt[13]) << 8);
}

//! A `data` chunk's size in frames: its byte count over the block align. 0 when
//! the block align is unknown - this refuses to invent a number rather than
//! assuming a frame size.
std::uint64_t framesFromDataSize(quint32 size, int blockAlign)
{
	if (blockAlign <= 0) { return 0; }
	return static_cast<std::uint64_t>(size) / static_cast<std::uint64_t>(blockAlign);
}

//! The STRING fields of a journal line. False when \a key is not one of them.
void applyStringField(const QString& key, const QString& value, TakeJournal* journal)
{
	if (key == QLatin1String("state")) { journal->state = value; }
	else if (key == QLatin1String("take")) { journal->takePath = value; }
	else if (key == QLatin1String("project")) { journal->projectPath = value; }
	else if (key == QLatin1String("track")) { journal->track = value; }
	else if (key == QLatin1String("updated_utc")) { journal->updatedUtc = value; }
}

//! The NUMERIC fields of a journal line; an unknown key is ignored.
void applyNumberField(const QString& key, const QString& value, TakeJournal* journal)
{
	if (key == QLatin1String("sample_rate")) { journal->sampleRate = value.toInt(); }
	else if (key == QLatin1String("channels")) { journal->channels = value.toInt(); }
	else if (key == QLatin1String("frames_on_disk")) { journal->framesOnDisk = value.toULongLong(); }
	else if (key == QLatin1String("start_ticks")) { journal->startTicks = value.toLongLong(); }
}

//! Parses one `key=value` line into \a journal.
/*!
 * Split by field TYPE rather than left as one nine-branch chain: the ratchet
 * measures a method, and a chain of one branch per field is the shape that walks
 * past the limit every time a field is added (Gate 4 caught this at CCN 11).
 */
void applyLine(const QString& line, TakeJournal* journal, bool escapedFields)
{
	const int equals = line.indexOf(QLatin1Char('='));
	if (equals <= 0) { return; }
	// Only the KEY is trimmed - the value is the take path as written, and
	// leading or trailing whitespace in it is part of where the take went.
	const QString key = line.left(equals).trimmed();
	const QString value = line.mid(equals + 1);
	// A version 2 value is escaped (a path may hold a newline); a version 1
	// value is literal and stays exactly as it was written.
	applyStringField(key, escapedFields ? decodeFieldValue(value) : value, journal);
	applyNumberField(key, value, journal);
}

} // namespace

QString journalSuffix()
{
	return QStringLiteral(".rec-journal");
}

QString journalPathFor(const QString& takePath)
{
	return takePath + journalSuffix();
}

bool write(const QString& journalPath, const TakeJournal& journal)
{
	QSaveFile file(journalPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { return false; }
	QTextStream out(&file);
	out << "# zene-studio recording journal - a side file, never part of a project\n";
	out << "version=" << CurrentJournalVersion << "\n";
	// Every string field is escaped: a path may hold a newline, and a raw one
	// would split the value across two lines.
	out << "state="
		<< escapeFieldValue(journal.state.isEmpty() ? StateInProgress : journal.state) << "\n";
	out << "take=" << escapeFieldValue(journal.takePath) << "\n";
	out << "sample_rate=" << journal.sampleRate << "\n";
	out << "channels=" << journal.channels << "\n";
	out << "frames_on_disk=" << static_cast<qulonglong>(journal.framesOnDisk) << "\n";
	if (!journal.projectPath.isEmpty()) { out << "project=" << escapeFieldValue(journal.projectPath) << "\n"; }
	if (!journal.track.isEmpty()) { out << "track=" << escapeFieldValue(journal.track) << "\n"; }
	if (journal.startTicks >= 0) { out << "start_ticks=" << journal.startTicks << "\n"; }
	// Stamped here rather than by the caller: the field's whole value is that it
	// is the moment the file was written, and a writer that forgets it would
	// make a stale journal look current.
	out << "updated_utc="
		<< escapeFieldValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)) << "\n";
	out.flush();
	return file.commit();
}

bool read(const QString& journalPath, TakeJournal* journal)
{
	QFile file(journalPath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { return false; }
	QTextStream in(&file);
	TakeJournal parsed;
	// A version 1 journal (written before the escaping) holds LITERAL values;
	// from version 2 on, the string fields are escaped and are decoded here.
	bool escapedFields = false;
	while (!in.atEnd())
	{
		// The COMMENT and blankness tests want the trimmed line, but the line
		// handed on must not be: a take path may legally begin or end in
		// whitespace, and trimming it here rewrote the path, so recovery looked
		// for a take that was never recorded. applyLine() trims its own key.
		const QString line = in.readLine();
		const QString probe = line.trimmed();
		if (probe.isEmpty() || probe.startsWith(QLatin1Char('#'))) { continue; }
		// The version line is written first, so it is read before the fields
		// it describes. A journal without one is a version 1 journal.
		const int version = journalVersion(probe);
		if (version > 0)
		{
			escapedFields = version >= 2;
			continue;
		}
		applyLine(line, &parsed, escapedFields);
	}
	if (parsed.takePath.isEmpty()) { return false; }
	if (journal != nullptr) { *journal = parsed; }
	return true;
}

bool remove(const QString& journalPath)
{
	return QFile::remove(journalPath) || !QFile::exists(journalPath);
}

bool isRecoverable(const TakeJournal& journal)
{
	return journal.state == StateInProgress
		&& !journal.takePath.isEmpty()
		&& QFileInfo::exists(journal.takePath);
}

QList<TakeJournal> scan(const QString& directory)
{
	QList<TakeJournal> found;
	const QDir dir(directory);
	const QStringList names = dir.entryList(QStringList{QStringLiteral("*") + journalSuffix()},
		QDir::Files, QDir::Name);
	for (const QString& name : names)
	{
		TakeJournal journal;
		if (read(dir.filePath(name), &journal) && isRecoverable(journal))
		{
			found.append(journal);
		}
	}
	std::sort(found.begin(), found.end(), [](const TakeJournal& left, const TakeJournal& right) {
		return left.takePath < right.takePath;
	});
	return found;
}

/*! The frames a `data` chunk of \a declaredSize bytes can back, bounded by the bytes
 *  physically present after its header. The declared size is what the writer INTENDED
 *  to write; a crash or a truncated write leaves fewer bytes on disk, and a count taken
 *  from the header alone offers audio that is not there. */
std::uint64_t framesInDataChunk(QDataStream& in, const QFile& file, quint32 declaredSize, int blockAlign)
{
	const qint64 remaining = file.size() - in.device()->pos();
	if (remaining <= 0) { return 0; }
	const auto present = static_cast<quint64>(remaining);
	return framesFromDataSize(static_cast<quint32>(std::min<quint64>(declaredSize, present)), blockAlign);
}

std::uint64_t framesInFile(const QString& takePath)
{
	QFile file(takePath);
	if (!file.open(QIODevice::ReadOnly)) { return 0; }
	QDataStream in(&file);
	in.setByteOrder(QDataStream::LittleEndian);
	if (readChunkId(in) != QByteArrayLiteral("RIFF")) { return 0; }
	in.skipRawData(4);                                  // the RIFF size field
	if (readChunkId(in) != QByteArrayLiteral("WAVE")) { return 0; }

	int blockAlign = 0;
	while (!in.atEnd())
	{
		const QByteArray id = readChunkId(in);
		if (id.size() != 4) { return 0; }
		quint32 size = 0;
		in >> size;
		if (id == QByteArrayLiteral("data")) { return framesInDataChunk(in, file, size, blockAlign); }
		// A `fmt ` chunk is partly read (the block align) and the rest skipped;
		// every other chunk is skipped whole. Skipping `size` AFTER reading the
		// 16 bytes is the defect this costs: it walks 16 bytes past the chunk
		// into its payload, every later chunk id is garbage, and a valid take
		// reads as zero frames - which is a recovery offer that promises nothing.
		quint32 consumed = 0;
		if (id == QByteArrayLiteral("fmt "))
		{
			blockAlign = blockAlignOf(nextBytes(in, 16));
			consumed = size < 16u ? size : 16u;
		}
		// Chunks are word-aligned: an odd size is followed by one pad byte.
		const quint32 remaining = (size - consumed) + (size % 2u);
		if (in.skipRawData(static_cast<int>(remaining)) < 0) { return 0; }
	}
	return 0;
}

std::uint64_t recoverableFrames(const TakeJournal& journal)
{
	return std::min(journal.framesOnDisk, framesInFile(journal.takePath));
}

std::uint64_t updateIntervalFrames(int sampleRate)
{
	// A tenth of a second of audio (Q050-045). It was a whole second, which left a
	// second of already-written audio uncounted and, with the unflushed WAV header,
	// made a SIGKILLed take read as empty.
	return sampleRate >= 10 ? static_cast<std::uint64_t>(sampleRate) / 10u : 1u;
}

std::uint64_t lossBoundFrames(int sampleRate)
{
	return updateIntervalFrames(sampleRate) + WriterBatchFrames;
}

QString boundText(int sampleRate)
{
	const double seconds = sampleRate > 0
		? static_cast<double>(lossBoundFrames(sampleRate)) / sampleRate : 0.0;
	return QStringLiteral("recoverable: min(frames_on_disk, frames in the take's file); "
		"a crash loses at most %1 frames (%2 s at %3 Hz) while the disk-writer is healthy: "
		"audio written after the last commit (every %4 frames) plus one writer batch in "
		"flight; if the writer is stalled, up to %5 frames still in the recorder's ring are "
		"also lost")
		.arg(lossBoundFrames(sampleRate))
		.arg(seconds, 0, 'f', 3)
		.arg(sampleRate)
		.arg(updateIntervalFrames(sampleRate))
		.arg(RingFramesNotRecoverable);
}

} // namespace recordingjournal
} // namespace lmms
