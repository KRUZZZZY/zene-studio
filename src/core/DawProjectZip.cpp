/*
 * DawProjectZip.cpp - the DAWproject container's WRITER: a ZIP with STORE entries.
 *
 * The container half of feature row 37 (docs/FEATURE-LIST-0.3.0.md section 7).
 * README.md, "Format Specification": "Container: ZIP", "Format: XML
 * (project.xml, metadata.xml)", "Text encoding: UTF-8".
 *
 * THE WRITER'S METHOD IS STORE, AND WHY. The format constrains the ENTRIES
 * (their names and their encoding), not the compression method; a stored entry
 * is a valid entry in every ZIP reader, and storing keeps the writer simple and
 * its output byte-for-byte reproducible. What the format needs from the
 * container is that another DAW can open it, and every ZIP reader opens a
 * stored entry. (DataFile.cpp's mmpz path uses Qt's qCompress/qUncompress,
 * which is zlib's own framed format and is NOT a raw DEFLATE stream, so it is
 * not reused for the container.)
 *
 * THE READER IS DawProjectZipRead.cpp, and it reads STORE and raw DEFLATE
 * (method 8, the method Bitwig, Studio One and Cubase write) under a per-entry
 * and a total size bound, reporting every deviation instead of stopping at the
 * first. This file keeps the writer and the CRC-32 both halves share.
 *
 * THIS IS A WRITER, NOT A ZIP LIBRARY. It emits the three records a ZIP needs -
 * a local file header per entry, one central directory, one end-of-central-
 * directory record. It deliberately does NOT write the features an archive tool
 * has: no directory entries, no data descriptors, no spanning, no encryption,
 * no compression and no ZIP64.
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

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "DawProjectInterchange.h"
#include "DawProjectZipShared.h"

namespace lmms
{
namespace interchange
{

namespace zipdetail
{

quint32 crc32Update(quint32 state, const char* data, qsizetype size)
{
	static const std::vector<quint32> table = [] {
		std::vector<quint32> values(256);
		for (quint32 index = 0; index < 256; index++)
		{
			quint32 value = index;
			for (int bit = 0; bit < 8; bit++)
			{
				value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
			}
			values[index] = value;
		}
		return values;
	}();
	for (qsizetype at = 0; at < size; at++)
	{
		state = table[(state ^ static_cast<quint32>(static_cast<unsigned char>(data[at]))) & 0xFFu]
			^ (state >> 8);
	}
	return state;
}

quint32 crc32Of(const QByteArray& bytes)
{
	return crc32Update(CrcInitial, bytes.constData(), bytes.size()) ^ CrcInitial;
}

} // namespace zipdetail

QString dawProjectSha256(const QByteArray& bytes)
{
	return QString::fromLatin1(
		QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

namespace
{

// The three record signatures, and the sizes of the fixed parts.
constexpr quint32 LocalHeaderSignature = 0x04034B50;
constexpr quint32 CentralHeaderSignature = 0x02014B50;
constexpr quint32 EndOfCentralSignature = 0x06054B50;
constexpr int LocalHeaderSize = 30;
constexpr int CentralHeaderSize = 46;
constexpr int EndOfCentralSize = 22;
//! 2.0: the version that introduced the STORE and DEFLATE methods.
constexpr quint16 VersionNeeded = 20;
//! Bit 11: the entry name is UTF-8. The format is UTF-8 throughout.
constexpr quint16 Utf8NameFlag = 0x0800;
//! The one method this module writes.
constexpr quint16 MethodStore = 0;

void appendU16(QByteArray& out, quint16 value)
{
	out.append(static_cast<char>(value & 0xFFu));
	out.append(static_cast<char>((value >> 8) & 0xFFu));
}

void appendU32(QByteArray& out, quint32 value)
{
	for (int shift : {0, 8, 16, 24}) { out.append(static_cast<char>((value >> shift) & 0xFFu)); }
}

//! A DOS date/time for the entry records. The format carries no timestamp, so
//! this is the fixed 1980-01-01 00:00:00 epoch a ZIP reader shows as "unknown".
constexpr quint16 DosTime = 0;
constexpr quint16 DosDate = 0x0021;

//! The largest value the 16- and 32-bit fields of a CLASSIC ZIP (this writer
//! emits no ZIP64) can hold. An entry name and the entry count live in 16-bit
//! fields, an entry's body and every offset in 32-bit ones, so a value past one
//! of them would be narrowed: the number changes silently and the container
//! written is corrupt. Such an archive is refused instead.
constexpr qint64 MaxU16 = 0xFFFF;
constexpr qint64 MaxU32 = 0xFFFFFFFFLL;

/*! The largest ENTRY COUNT a classic ZIP (this writer emits no ZIP64) can
 *  state: one below the 16-bit field's range, because 0xFFFF in the end
 *  record's count fields is the ZIP64 SENTINEL. A reader that follows the
 *  format reads it as "the real counts are in the ZIP64 end-of-central-
 *  directory record", and this writer writes no such record - the archive it
 *  produced could be rejected or misread by exactly the readers that follow
 *  the format, so that one count is refused rather than written. */
constexpr qint64 MaxEntryCount = 0xFFFE;

//! Why one entry cannot be written, or an empty string when it can.
QString entryRefusal(const DawProjectEntry& entry)
{
	if (entry.first.isEmpty())
	{
		return QStringLiteral("a container entry has an empty name");
	}
	if (entry.first.toUtf8().size() > MaxU16)
	{
		return QStringLiteral("the container entry name %1 is %2 bytes, past the %3 a classic "
			"ZIP can hold").arg(entry.first).arg(entry.first.toUtf8().size()).arg(MaxU16);
	}
	if (entry.second.size() > MaxU32)
	{
		return QStringLiteral("the container entry %1 is %2 bytes, past the %3 a classic ZIP can "
			"hold").arg(entry.first).arg(entry.second.size()).arg(MaxU32);
	}
	return {};
}

//! Why the archive as a whole cannot be written, or an empty string when it can.
QString containerRefusal(const std::vector<DawProjectEntry>& entries)
{
	if (entries.size() > static_cast<std::size_t>(MaxEntryCount))
	{
		return QStringLiteral("%1 container entries exceed the %2 a classic ZIP can hold")
			.arg(entries.size()).arg(MaxEntryCount);
	}
	return {};
}

//! Why the complete archive would exceed the size this module's reader supports.
QString archiveSizeRefusal(const std::vector<DawProjectEntry>& entries)
{
	qint64 archiveSize = EndOfCentralSize;
	const qint64 readerLimit = std::numeric_limits<int>::max();
	for (const DawProjectEntry& entry : entries)
	{
		const qint64 nameSize = entry.first.toUtf8().size();
		archiveSize += LocalHeaderSize + nameSize + entry.second.size();
		archiveSize += CentralHeaderSize + nameSize;
		if (archiveSize > readerLimit)
		{
			return QStringLiteral("the container would be %1 bytes, past this reader's %2-byte "
				"size limit").arg(archiveSize).arg(readerLimit);
		}
	}
	return {};
}

//! Writes \a file to \a path beside the target and renames it over it only once complete:
//! a failed or short write must not destroy the file that was there before.
bool writeWhole(const QString& path, const QByteArray& file, QString* error)
{
	QSaveFile target(path);
	if (!target.open(QIODevice::WriteOnly))
	{
		if (error)
		{
			*error = QStringLiteral("cannot write %1: %2").arg(path, target.errorString());
		}
		return false;
	}
	const qint64 written = target.write(file);
	if (written != file.size())
	{
		if (error)
		{
			*error = QStringLiteral("wrote %1 of %2 bytes to %3")
				.arg(written).arg(file.size()).arg(path);
		}
		target.cancelWriting();
		return false;
	}
	if (!target.commit())
	{
		if (error) { *error = QStringLiteral("cannot write %1: %2").arg(path, target.errorString()); }
		return false;
	}
	return true;
}

} // namespace

bool dawProjectZipWrite(const QString& path, const std::vector<DawProjectEntry>& entries,
	QString* error)
{
	if (path.isEmpty())
	{
		if (error) { *error = QStringLiteral("the container path is empty"); }
		return false;
	}
	for (const DawProjectEntry& entry : entries)
	{
		const QString refusal = entryRefusal(entry);
		if (!refusal.isEmpty())
		{
			if (error) { *error = refusal; }
			return false;
		}
	}
	const QString tooMany = containerRefusal(entries);
	if (!tooMany.isEmpty())
	{
		if (error) { *error = tooMany; }
		return false;
	}
	const QString tooBig = archiveSizeRefusal(entries);
	if (!tooBig.isEmpty())
	{
		if (error) { *error = tooBig; }
		return false;
	}

	QByteArray file;
	QVector<quint32> offsets;
	offsets.reserve(static_cast<int>(entries.size()));

	for (const DawProjectEntry& entry : entries)
	{
		const QByteArray name = entry.first.toUtf8();
		const QByteArray& body = entry.second;
		const quint32 crc = zipdetail::crc32Of(body);
		offsets.append(static_cast<quint32>(file.size()));

		appendU32(file, LocalHeaderSignature);
		appendU16(file, VersionNeeded);
		appendU16(file, Utf8NameFlag);
		appendU16(file, MethodStore);
		appendU16(file, DosTime);
		appendU16(file, DosDate);
		appendU32(file, crc);
		appendU32(file, static_cast<quint32>(body.size()));  // compressed
		appendU32(file, static_cast<quint32>(body.size()));  // uncompressed
		appendU16(file, static_cast<quint16>(name.size()));
		appendU16(file, 0);  // no extra field
		file.append(name);
		file.append(body);
	}

	// The size preflight includes the complete central directory and end record,
	// so this offset fits the int range used by the reader.
	const int centralOffset = file.size();
	for (int index = 0; index < static_cast<int>(entries.size()); index++)
	{
		const QByteArray name = entries[static_cast<std::size_t>(index)].first.toUtf8();
		const QByteArray& body = entries[static_cast<std::size_t>(index)].second;
		appendU32(file, CentralHeaderSignature);
		appendU16(file, VersionNeeded);  // version made by
		appendU16(file, VersionNeeded);  // version needed
		appendU16(file, Utf8NameFlag);
		appendU16(file, MethodStore);
		appendU16(file, DosTime);
		appendU16(file, DosDate);
		appendU32(file, zipdetail::crc32Of(body));
		appendU32(file, static_cast<quint32>(body.size()));
		appendU32(file, static_cast<quint32>(body.size()));
		appendU16(file, static_cast<quint16>(name.size()));
		appendU16(file, 0);  // no extra field
		appendU16(file, 0);  // no comment
		appendU16(file, 0);  // first disk
		appendU16(file, 0);  // internal attributes
		appendU32(file, 0);  // external attributes
		appendU32(file, offsets[index]);
		file.append(name);
	}
	const int centralSize = file.size() - centralOffset;

	appendU32(file, EndOfCentralSignature);
	appendU16(file, 0);  // this disk
	appendU16(file, 0);  // the disk the central directory starts on
	appendU16(file, static_cast<quint16>(entries.size()));
	appendU16(file, static_cast<quint16>(entries.size()));
	appendU32(file, static_cast<quint32>(centralSize));
	appendU32(file, static_cast<quint32>(centralOffset));
	appendU16(file, 0);  // no archive comment

	return writeWhole(path, file, error);
}

} // namespace interchange
} // namespace lmms
