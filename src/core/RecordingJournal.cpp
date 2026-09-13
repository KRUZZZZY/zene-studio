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
 *   version=1
 *   state=in_progress
 *   take=/home/me/takes/take-01.wav
 *   sample_rate=48000
 *   channels=1
 *   frames_on_disk=96000
 *   updated_utc=2026-09-13T20:15:01Z
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
void applyLine(const QString& line, TakeJournal* journal)
{
	const int equals = line.indexOf(QLatin1Char('='));
	if (equals <= 0) { return; }
	const QString key = line.left(equals);
	const QString value = line.mid(equals + 1);
	applyStringField(key, value, journal);
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
	out << "version=1\n";
	out << "state=" << (journal.state.isEmpty() ? StateInProgress : journal.state) << "\n";
	out << "take=" << journal.takePath << "\n";
	out << "sample_rate=" << journal.sampleRate << "\n";
	out << "channels=" << journal.channels << "\n";
	out << "frames_on_disk=" << static_cast<qulonglong>(journal.framesOnDisk) << "\n";
	if (!journal.projectPath.isEmpty()) { out << "project=" << journal.projectPath << "\n"; }
	if (!journal.track.isEmpty()) { out << "track=" << journal.track << "\n"; }
	if (journal.startTicks >= 0) { out << "start_ticks=" << journal.startTicks << "\n"; }
	// Stamped here rather than by the caller: the field's whole value is that it
	// is the moment the file was written, and a writer that forgets it would
	// make a stale journal look current.
	out << "updated_utc="
		<< QDateTime::currentDateTimeUtc().toString(Qt::ISODate) << "\n";
	out.flush();
	return file.commit();
}

bool read(const QString& journalPath, TakeJournal* journal)
{
	QFile file(journalPath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { return false; }
	QTextStream in(&file);
	TakeJournal parsed;
	while (!in.atEnd())
	{
		const QString line = in.readLine().trimmed();
		if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) { continue; }
		applyLine(line, &parsed);
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
		if (id == QByteArrayLiteral("data")) { return framesFromDataSize(size, blockAlign); }
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
	return sampleRate > 0 ? static_cast<std::uint64_t>(sampleRate) : 1u;
}

QString boundText(int sampleRate)
{
	return QStringLiteral("recoverable: min(frames_on_disk, frames in the take's file); "
		"not recoverable: audio written after the last journal update (at most %1 frames, "
		"one second at %2 Hz) and up to %3 frames still in the recorder's ring when the "
		"process died")
		.arg(updateIntervalFrames(sampleRate))
		.arg(sampleRate)
		.arg(RingFramesNotRecoverable);
}

} // namespace recordingjournal
} // namespace lmms
