/*
 * DawProjectZip.cpp - the DAWproject container: a ZIP with STORE entries.
 *
 * The container half of feature row 37 (docs/FEATURE-LIST-0.3.0.md section 7).
 * README.md, "Format Specification": "Container: ZIP", "Format: XML
 * (project.xml, metadata.xml)", "Text encoding: UTF-8".
 *
 * THE METHOD IS STORE, AND WHY. The format constrains the ENTRIES (their names
 * and their encoding), not the compression method; a stored entry is a valid
 * entry in every ZIP reader, and storing keeps the container free of a
 * compression dependency this tree does not otherwise carry (DataFile.cpp's
 * mmpz path uses Qt's qCompress/qUncompress, which is zlib's own framed format
 * and is NOT a raw DEFLATE stream, so it cannot be reused here). What the
 * format needs from the container is that another DAW can open it, and every
 * ZIP reader opens a stored entry.
 *
 * THIS IS A WRITER AND A READER, NOT A ZIP LIBRARY. It emits the three records
 * a ZIP needs - a local file header per entry, one central directory, one
 * end-of-central-directory record - and reads them back with every offset and
 * size bounds-checked against the file's own length, because the input is a
 * file a foreign program wrote. It deliberately does NOT support the features
 * an archive tool has: no directory entries, no data descriptors, no spanning
 * (the end record's disk numbers must both be zero), no encryption, and no
 * compression. An entry that uses any of them is refused with a message naming
 * it rather than decoded wrongly.
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

namespace lmms
{
namespace interchange
{

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
constexpr quint16 MethodDeflate = 8;

//! The CRC-32 every ZIP entry carries (reflected, polynomial 0xEDB88320).
quint32 crc32Of(const QByteArray& bytes)
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
	quint32 crc = 0xFFFFFFFFu;
	for (char byte : bytes)
	{
		crc = table[(crc ^ static_cast<quint32>(static_cast<unsigned char>(byte))) & 0xFFu]
			^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFFu;
}

void appendU16(QByteArray& out, quint16 value)
{
	out.append(static_cast<char>(value & 0xFFu));
	out.append(static_cast<char>((value >> 8) & 0xFFu));
}

void appendU32(QByteArray& out, quint32 value)
{
	for (int shift : {0, 8, 16, 24}) { out.append(static_cast<char>((value >> shift) & 0xFFu)); }
}

quint16 readU16(const QByteArray& bytes, int offset)
{
	return static_cast<quint16>(static_cast<unsigned char>(bytes[offset]))
		| static_cast<quint16>(static_cast<unsigned char>(bytes[offset + 1]) << 8);
}

quint32 readU32(const QByteArray& bytes, int offset)
{
	return static_cast<quint32>(static_cast<unsigned char>(bytes[offset]))
		| (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 1])) << 8)
		| (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 2])) << 16)
		| (static_cast<quint32>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
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

//! Why the local records assembled so far are already past what the central
//! directory's 32-bit offset can address, or an empty string when they fit.
QString assembledRefusal(const QByteArray& file)
{
	if (file.size() > MaxU32)
	{
		return QStringLiteral("the container would already be %1 bytes before its central "
			"directory, past the %2 a classic ZIP can address").arg(file.size()).arg(MaxU32);
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

//! Reads the whole of \a path into \a bytes. Every offset the reader uses is an int, so a
//! file past INT_MAX bytes is refused before it is read: its offsets would not fit, and it
//! would be read whole into memory.
bool readWhole(const QString& path, QByteArray* bytes, QString* error)
{
	QFile source(path);
	if (!source.open(QIODevice::ReadOnly))
	{
		if (error)
		{
			*error = QStringLiteral("cannot read %1: %2").arg(path, source.errorString());
		}
		return false;
	}
	if (source.size() > std::numeric_limits<int>::max())
	{
		if (error) { *error = QStringLiteral("%1 is too large for this reader (over 2 GiB)").arg(path); }
		return false;
	}
	*bytes = source.readAll();
	return true;
}

//! Returns the offset of the end-of-central-directory record in \a bytes, or -1.
//! It is searched for from the back, because an archive comment may follow it -
//! this module writes none, but a foreign container may have one. A candidate is
//! only the end record when (a) its OWN comment length carries it to the end of
//! the file, and (b) the central directory it declares ends exactly where the
//! record itself starts. A comment can hold bytes that look like an empty end
//! record; (a) rejects one that declares a comment of its own, and (b) separates
//! the rest, because such a record names no directory at all.
int findEndOfCentralDirectory(const QByteArray& bytes)
{
	for (int offset = bytes.size() - EndOfCentralSize; offset >= 0; offset--)
	{
		if (readU32(bytes, offset) != EndOfCentralSignature) { continue; }
		if (offset + EndOfCentralSize + readU16(bytes, offset + 20) != bytes.size()) { continue; }
		const qint64 directoryEnd = static_cast<qint64>(readU32(bytes, offset + 12))
			+ static_cast<qint64>(readU32(bytes, offset + 16));
		if (directoryEnd != offset) { continue; }
		return offset;
	}
	return -1;
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

	QByteArray file;
	QVector<quint32> offsets;
	offsets.reserve(static_cast<int>(entries.size()));

	for (const DawProjectEntry& entry : entries)
	{
		const QByteArray name = entry.first.toUtf8();
		const QByteArray& body = entry.second;
		const quint32 crc = crc32Of(body);
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

	// Every offset above, and the central directory's own offset and size, are
	// 32-bit fields, so the archive has to fit before the directory is built.
	const QString tooBig = assembledRefusal(file);
	if (!tooBig.isEmpty())
	{
		if (error) { *error = tooBig; }
		return false;
	}

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
		appendU32(file, crc32Of(body));
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

bool dawProjectZipRead(const QString& path, std::vector<DawProjectEntry>* entries,
	QString* error)
{
	QByteArray bytes;
	if (!readWhole(path, &bytes, error)) { return false; }

	if (bytes.size() < EndOfCentralSize)
	{
		if (error) { *error = QStringLiteral("%1 is too short to be a ZIP container").arg(path); }
		return false;
	}
	// The end record is found from the back, because an archive comment may
	// follow it - this module writes none, but a foreign container may have one.
	const int endOffset = findEndOfCentralDirectory(bytes);
	if (endOffset < 0)
	{
		if (error)
		{
			*error = QStringLiteral("%1 has no end-of-central-directory record, so it is not a "
				"ZIP container").arg(path);
		}
		return false;
	}
	const quint16 thisDisk = readU16(bytes, endOffset + 4);
	const quint16 centralDisk = readU16(bytes, endOffset + 6);
	const quint16 entryCount = readU16(bytes, endOffset + 10);
	const quint32 centralSize = readU32(bytes, endOffset + 12);
	const quint32 centralOffset = readU32(bytes, endOffset + 16);
	if (thisDisk != 0 || centralDisk != 0)
	{
		if (error)
		{
			*error = QStringLiteral("%1 is a SPANNED ZIP (the end record names disk %2), which this "
				"reader does not support").arg(path).arg(thisDisk);
		}
		return false;
	}
	if (static_cast<qint64>(centralOffset) + static_cast<qint64>(centralSize) > bytes.size())
	{
		if (error)
		{
			*error = QStringLiteral("%1 declares a central directory of %2 bytes at offset %3, "
				"past the end of the file").arg(path).arg(centralSize).arg(centralOffset);
		}
		return false;
	}

	entries->clear();
	int cursor = static_cast<int>(centralOffset);
	for (int index = 0; index < entryCount; index++)
	{
		if (cursor + CentralHeaderSize > bytes.size()
			|| readU32(bytes, cursor) != CentralHeaderSignature)
		{
			if (error)
			{
				*error = QStringLiteral("%1's central directory ends after %2 of the %3 entries it "
					"declares").arg(path).arg(index).arg(entryCount);
			}
			return false;
		}
		const quint16 flags = readU16(bytes, cursor + 8);
		const quint16 method = readU16(bytes, cursor + 10);
		const quint32 crc = readU32(bytes, cursor + 16);
		const quint32 compressed = readU32(bytes, cursor + 20);
		const quint32 uncompressed = readU32(bytes, cursor + 24);
		const quint16 nameLength = readU16(bytes, cursor + 28);
		const quint16 extraLength = readU16(bytes, cursor + 30);
		const quint16 commentLength = readU16(bytes, cursor + 32);
		const quint32 localOffset = readU32(bytes, cursor + 42);
		const int nameAt = cursor + CentralHeaderSize;
		if (nameAt + nameLength > bytes.size())
		{
			if (error)
			{
				*error = QStringLiteral("%1's entry name runs past the end of the file").arg(path);
			}
			return false;
		}
		const QByteArray name = bytes.mid(nameAt, nameLength);
		cursor = nameAt + nameLength + extraLength + commentLength;

		if ((flags & 0x0001) != 0)
		{
			if (error)
			{
				*error = QStringLiteral("%1's entry '%2' is ENCRYPTED, which this reader does not "
					"support").arg(path, QString::fromUtf8(name));
			}
			return false;
		}
		// In 64 bits: a quint32 sum wraps for an offset near 4 GiB and would pass.
		if (static_cast<qint64>(localOffset) + LocalHeaderSize > bytes.size())
		{
			if (error)
			{
				*error = QStringLiteral("%1's entry '%2' names a local header past the end of the "
					"file").arg(path, QString::fromUtf8(name));
			}
			return false;
		}
		if (readU32(bytes, static_cast<int>(localOffset)) != LocalHeaderSignature)
		{
			if (error)
			{
				*error = QStringLiteral("%1's entry '%2' has no local file header")
					.arg(path, QString::fromUtf8(name));
			}
			return false;
		}
		const quint16 localNameLength = readU16(bytes, static_cast<int>(localOffset) + 26);
		const quint16 localExtraLength = readU16(bytes, static_cast<int>(localOffset) + 28);
		const quint32 bodyAt = localOffset + LocalHeaderSize + localNameLength + localExtraLength;
		if (static_cast<qint64>(bodyAt) + compressed > bytes.size())
		{
			if (error)
			{
				*error = QStringLiteral("%1's entry '%2' declares %3 bytes at offset %4, past the "
					"end of the file").arg(path, QString::fromUtf8(name)).arg(compressed)
					.arg(bodyAt);
			}
			return false;
		}
		if (method == MethodDeflate)
		{
			if (error)
			{
				*error = QStringLiteral("%1's entry '%2' is DEFLATE-compressed; this build's reader "
					"handles STORE containers only").arg(path, QString::fromUtf8(name));
			}
			return false;
		}
		if (method != MethodStore)
		{
			if (error)
			{
				*error = QStringLiteral("%1's entry '%2' uses compression method %3, which this "
					"reader does not support").arg(path, QString::fromUtf8(name)).arg(method);
			}
			return false;
		}
		if (compressed != uncompressed)
		{
			if (error)
			{
				*error = QStringLiteral("%1's entry '%2' stores %3 bytes but declares %4")
					.arg(path, QString::fromUtf8(name)).arg(compressed).arg(uncompressed);
			}
			return false;
		}
		const QByteArray body = bytes.mid(static_cast<int>(bodyAt), static_cast<int>(compressed));
		const quint32 actual = crc32Of(body);
		if (actual != crc)
		{
			if (error)
			{
				*error = QStringLiteral("%1's entry '%2' fails its own CRC (declared %3, measured "
					"%4)").arg(path, QString::fromUtf8(name))
					.arg(crc, 8, 16, QLatin1Char('0')).arg(actual, 8, 16, QLatin1Char('0'));
			}
			return false;
		}
		entries->push_back({QString::fromUtf8(name), body});
	}
	return true;
}

} // namespace interchange
} // namespace lmms
