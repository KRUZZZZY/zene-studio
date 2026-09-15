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
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
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
		if (entry.first.isEmpty())
		{
			if (error) { *error = QStringLiteral("a container entry has an empty name"); }
			return false;
		}
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

	QFile target(path);
	if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate))
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
		target.close();
		QFile::remove(path);
		return false;
	}
	target.close();
	return true;
}

bool dawProjectZipRead(const QString& path, std::vector<DawProjectEntry>* entries,
	QString* error)
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
	const QByteArray bytes = source.readAll();
	source.close();

	if (bytes.size() < EndOfCentralSize)
	{
		if (error) { *error = QStringLiteral("%1 is too short to be a ZIP container").arg(path); }
		return false;
	}
	// The end record is found from the back, because an archive comment may
	// follow it - this module writes none, but a foreign container may have one.
	int endOffset = -1;
	for (int offset = bytes.size() - EndOfCentralSize; offset >= 0; offset--)
	{
		if (readU32(bytes, offset) == EndOfCentralSignature) { endOffset = offset; break; }
	}
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
		if (localOffset + LocalHeaderSize > static_cast<quint32>(bytes.size()))
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
