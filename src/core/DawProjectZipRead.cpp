/*
 * DawProjectZipRead.cpp - the DAWproject container's READER: STORE and raw
 *                         DEFLATE entries, bounded, reporting every deviation.
 *
 * The reader half of the container (the writer is DawProjectZip.cpp). It reads a
 * file a FOREIGN program wrote - Bitwig, Studio One and Cubase all write
 * DEFLATE-compressed DAWproject entries - so every decision here is about bytes
 * this program did not produce:
 *
 *   - Every offset and size is checked against the file's own length before it
 *     is used. Nothing is read past the end.
 *   - DEFLATE is decoded with zlib's raw mode (inflateInit2 with negative window
 *     bits: a ZIP entry is a bare RFC 1951 stream, no zlib header). The output
 *     is counted chunk by chunk as it is produced, so the bound holds on the
 *     bytes actually made, whatever the entry's header claimed - the same
 *     approach src/core/ControlProjectAssets.cpp takes for a .mmpz body.
 *   - Two bounds hold every entry: the most one entry may decode to, and the
 *     most all of them may decode to together (DawProjectZipLimits). An entry
 *     over either is skipped and reported; it is never half-read.
 *   - TOLERANT, not fail-fast. A corrupt DEFLATE stream, a CRC that does not
 *     match, a method this reader does not decode, an encrypted entry, an
 *     oversized one: each is a DawProjectDeviation in the report, and the
 *     entries around it are still read. Nothing is dropped without a line in
 *     the report saying so. Only a container with no usable directory at all
 *     (not a ZIP, spanned, a directory past the file's end) is refused outright.
 *   - The central directory is authoritative: sizes and the CRC come from it,
 *     so an entry written with a data descriptor (the streaming writers' habit,
 *     where the local header's sizes are zero) reads the same as any other.
 *
 * NOT supported, and said so per entry: encryption, ZIP64, spanning, and any
 * method other than STORE (0) and DEFLATE (8).
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

#include <QFile>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#if defined(ZENE_HAVE_ZLIB)
#include <zlib.h>
#endif

#include "DawProjectInterchange.h"
#include "DawProjectZipShared.h"

namespace lmms
{
namespace interchange
{

QJsonObject DawProjectDeviation::toJson() const
{
	return QJsonObject{{QStringLiteral("entry"), entry}, {QStringLiteral("kind"), kind},
		{QStringLiteral("detail"), detail}};
}

QJsonArray dawProjectDeviationsJson(const std::vector<DawProjectDeviation>& deviations)
{
	QJsonArray array;
	for (const DawProjectDeviation& deviation : deviations) { array.append(deviation.toJson()); }
	return array;
}

QString dawProjectDeviationText(const std::vector<DawProjectDeviation>& deviations)
{
	QStringList lines;
	for (const DawProjectDeviation& deviation : deviations)
	{
		lines << QStringLiteral("[%1] %2").arg(deviation.kind, deviation.detail);
	}
	return lines.join(QStringLiteral("; "));
}

namespace
{

using namespace zipdetail;

constexpr quint32 LocalHeaderSignature = 0x04034B50;
constexpr quint32 CentralHeaderSignature = 0x02014B50;
constexpr quint32 EndOfCentralSignature = 0x06054B50;
constexpr int LocalHeaderSize = 30;
constexpr int CentralHeaderSize = 46;
constexpr int EndOfCentralSize = 22;
constexpr quint16 MethodStore = 0;
constexpr quint16 MethodDeflate = 8;
//! General-purpose flag bit 0: the entry is encrypted.
constexpr quint16 EncryptedFlag = 0x0001;
//! A 32-bit field holding this value is a ZIP64 "see the extra field" marker.
constexpr quint32 Zip64Marker = 0xFFFFFFFFu;

//! One central-directory record, decoded.
struct CentralEntry
{
	QString name;
	quint16 flags = 0;
	quint16 method = 0;
	quint32 crc = 0;
	quint32 compressed = 0;
	quint32 uncompressed = 0;
	quint32 localOffset = 0;
	int bodyAt = 0;  //!< where the entry's bytes start, once the local header is checked
};

//! Everything one read needs, so the per-entry steps take one argument.
struct Scan
{
	const QByteArray& bytes;
	const DawProjectZipLimits& limits;
	const QStringList& retain;
	DawProjectZipReport* report;
	std::vector<DawProjectEntry>* entries;
};

void note(Scan& scan, const QString& entry, const char* kind, const QString& detail)
{
	scan.report->deviations.push_back({entry, QString::fromLatin1(kind), detail});
}

//! Test seam, ZENE_TEST_CONTAINER_READ_FAULT: closes the file after its open and
//! before the read, so the read gives back nothing while the open succeeded -
//! the second-read fault a recovery must keep a file through. Closing needs no
//! platform call, so every platform runs the same fault.
void armTestReadFault(QFile& source)
{
	if (!qgetenv("ZENE_TEST_CONTAINER_READ_FAULT").isEmpty()) { source.close(); }
}

//! Reads the whole of \a path. Every offset is an int, so a file past INT_MAX
//! bytes is refused before it is read. \a unreadable is set true when the file
//! could not be opened or read: its bytes were never checked, which is not the
//! same as the file being malformed or truncated, and a caller must not treat
//! the two alike.
//! Refuses a file that could not be opened or read, or that gave back less than
//! it holds. Such a file was never checked: it is unreadable, not malformed, and
//! \a unreadable says so for the caller. Always returns false.
bool refuseUnreadable(const QString& path, const QFile& source, qint64 gotBytes, qint64 expected,
	QString* error, bool* unreadable)
{
	if (unreadable) { *unreadable = true; }
	const QString why = source.error() != QFileDevice::NoError
		? source.errorString()
		: QStringLiteral("it gave back %1 of %2 bytes").arg(gotBytes).arg(expected);
	if (error) { *error = QStringLiteral("cannot read %1: %2").arg(path, why); }
	return false;
}

bool readWhole(const QString& path, QByteArray* bytes, QString* error, bool* unreadable)
{
	if (unreadable) { *unreadable = false; }
	QFile source(path);
	if (!source.open(QIODevice::ReadOnly)) { return refuseUnreadable(path, source, 0, 0, error, unreadable); }
	if (source.size() > std::numeric_limits<int>::max())
	{
		if (error) { *error = QStringLiteral("%1 is too large for this reader (over 2 GiB)").arg(path); }
		return false;
	}
	const qint64 expected = source.size();
	armTestReadFault(source);
	*bytes = source.readAll();
	if (source.error() != QFileDevice::NoError || bytes->size() != expected)
	{
		return refuseUnreadable(path, source, bytes->size(), expected, error, unreadable);
	}
	return true;
}

//! The offset of the end-of-central-directory record, or -1. Searched for from
//! the back (an archive comment may follow it). A candidate is only the end
//! record when its OWN comment length carries it to the end of the file and the
//! central directory it declares ends exactly where the record starts: a comment
//! can hold bytes that look like an empty end record, and this separates them.
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

//! Finds and checks the directory. False - with \a error - when there is none to read.
bool locateDirectory(const QString& path, const QByteArray& bytes, int* centralOffset, int* count,
	QString* error)
{
	const auto refuse = [&](const QString& why) {
		if (error) { *error = why; }
		return false;
	};
	if (bytes.size() < EndOfCentralSize)
	{
		return refuse(QStringLiteral("%1 is too short to be a ZIP container").arg(path));
	}
	const int endOffset = findEndOfCentralDirectory(bytes);
	if (endOffset < 0)
	{
		return refuse(QStringLiteral("%1 has no end-of-central-directory record, so it is not a "
			"ZIP container").arg(path));
	}
	const quint16 thisDisk = readU16(bytes, endOffset + 4);
	if (thisDisk != 0 || readU16(bytes, endOffset + 6) != 0)
	{
		return refuse(QStringLiteral("%1 is a SPANNED ZIP (the end record names disk %2), which this "
			"reader does not support").arg(path).arg(thisDisk));
	}
	const quint32 size = readU32(bytes, endOffset + 12);
	const quint32 offset = readU32(bytes, endOffset + 16);
	if (static_cast<qint64>(offset) + static_cast<qint64>(size) > bytes.size())
	{
		return refuse(QStringLiteral("%1 declares a central directory of %2 bytes at offset %3, "
			"past the end of the file").arg(path).arg(size).arg(offset));
	}
	*centralOffset = static_cast<int>(offset);
	*count = readU16(bytes, endOffset + 10);
	return true;
}

//! Decodes the record at \a cursor. False when the record or its name runs past
//! the file (the directory is truncated); \a next is where the following record starts.
bool decodeCentral(const QByteArray& bytes, int cursor, CentralEntry* entry, int* next)
{
	if (cursor + CentralHeaderSize > bytes.size() || readU32(bytes, cursor) != CentralHeaderSignature)
	{
		return false;
	}
	const quint16 nameLength = readU16(bytes, cursor + 28);
	const int nameAt = cursor + CentralHeaderSize;
	if (nameAt + nameLength > bytes.size()) { return false; }
	entry->flags = readU16(bytes, cursor + 8);
	entry->method = readU16(bytes, cursor + 10);
	entry->crc = readU32(bytes, cursor + 16);
	entry->compressed = readU32(bytes, cursor + 20);
	entry->uncompressed = readU32(bytes, cursor + 24);
	entry->localOffset = readU32(bytes, cursor + 42);
	entry->name = QString::fromUtf8(bytes.mid(nameAt, nameLength));
	*next = nameAt + nameLength + readU16(bytes, cursor + 30) + readU16(bytes, cursor + 32);
	return true;
}

//! Feature flags this reader does not decode. True (after a note) when \a entry has one.
bool refuseUnsupportedFeature(Scan& scan, const CentralEntry& entry)
{
	if ((entry.flags & EncryptedFlag) != 0)
	{
		note(scan, entry.name, DeviationUnsupportedFeature,
			QStringLiteral("entry '%1' is ENCRYPTED, which this reader does not support")
				.arg(entry.name));
		return true;
	}
	if (entry.compressed == Zip64Marker || entry.uncompressed == Zip64Marker
		|| entry.localOffset == Zip64Marker)
	{
		note(scan, entry.name, DeviationUnsupportedFeature,
			QStringLiteral("entry '%1' needs ZIP64, which this reader does not support")
				.arg(entry.name));
		return true;
	}
	return false;
}

//! Checks the local header and finds where the body starts; false (after a note) when the
//! entry cannot be located inside the file.
bool locateBody(Scan& scan, CentralEntry* entry)
{
	const QByteArray& bytes = scan.bytes;
	const qint64 headerAt = entry->localOffset;
	if (headerAt + LocalHeaderSize > bytes.size()
		|| readU32(bytes, static_cast<int>(headerAt)) != LocalHeaderSignature)
	{
		note(scan, entry->name, DeviationCorruptEntry,
			QStringLiteral("entry '%1' has no local file header where the directory says it is")
				.arg(entry->name));
		return false;
	}
	const qint64 bodyAt = headerAt + LocalHeaderSize
		+ readU16(bytes, static_cast<int>(headerAt) + 26)
		+ readU16(bytes, static_cast<int>(headerAt) + 28);
	if (bodyAt + static_cast<qint64>(entry->compressed) > bytes.size())
	{
		note(scan, entry->name, DeviationCorruptEntry,
			QStringLiteral("entry '%1' declares %2 bytes at offset %3, past the end of the file")
				.arg(entry->name).arg(entry->compressed).arg(bodyAt));
		return false;
	}
	entry->bodyAt = static_cast<int>(bodyAt);
	return true;
}

//! The method's name for a message.
QString methodName(quint16 method)
{
	switch (method)
	{
		case 1: return QStringLiteral("Shrink");
		case 6: return QStringLiteral("Implode");
		case 9: return QStringLiteral("Deflate64");
		case 12: return QStringLiteral("BZIP2");
		case 14: return QStringLiteral("LZMA");
		case 93: return QStringLiteral("Zstandard");
		case 95: return QStringLiteral("XZ");
		case 98: return QStringLiteral("PPMd");
		default: return QStringLiteral("method %1").arg(method);
	}
}

//! True when \a entry's method can be read; otherwise notes why.
bool methodReadable(Scan& scan, const CentralEntry& entry)
{
	if (entry.method == MethodStore) { return true; }
	if (entry.method == MethodDeflate)
	{
#if defined(ZENE_HAVE_ZLIB)
		if (scan.limits.allowDeflate) { return true; }
		note(scan, entry.name, DeviationUnsupportedMethod,
			QStringLiteral("entry '%1' is DEFLATE-compressed; this container's reader handles STORE "
				"entries only").arg(entry.name));
#else
		note(scan, entry.name, DeviationUnsupportedMethod,
			QStringLiteral("entry '%1' is DEFLATE-compressed, and this build has no zlib to inflate "
				"it").arg(entry.name));
#endif
		return false;
	}
	note(scan, entry.name, DeviationUnsupportedMethod,
		QStringLiteral("entry '%1' uses compression method %2 (%3), which this reader does not "
			"support; only STORE (0) and DEFLATE (8) are read")
			.arg(entry.name).arg(entry.method).arg(methodName(entry.method)));
	return false;
}

//! True when \a entry fits both bounds and (for STORE) declares consistent sizes;
//! otherwise notes why. Judged on the DECLARED size; the produced size is enforced again
//! while inflating.
bool sizeAcceptable(Scan& scan, const CentralEntry& entry)
{
	if (entry.method == MethodStore && entry.compressed != entry.uncompressed)
	{
		note(scan, entry.name, DeviationCorruptEntry,
			QStringLiteral("entry '%1' stores %2 bytes but declares %3")
				.arg(entry.name).arg(entry.compressed).arg(entry.uncompressed));
		return false;
	}
	if (entry.uncompressed > scan.limits.maxEntryBytes)
	{
		note(scan, entry.name, DeviationOversized,
			QStringLiteral("entry '%1' would decompress to %2 bytes, over the %3-byte limit for one "
				"entry; it was not read").arg(entry.name).arg(entry.uncompressed)
				.arg(scan.limits.maxEntryBytes));
		return false;
	}
	if (scan.report->inflatedBytes + entry.uncompressed > scan.limits.maxTotalBytes)
	{
		note(scan, entry.name, DeviationTotalOversized,
			QStringLiteral("entry '%1' (%2 bytes) would take the container past the %3-byte limit on "
				"everything it decompresses to (%4 already read); it was not read")
				.arg(entry.name).arg(entry.uncompressed).arg(scan.limits.maxTotalBytes)
				.arg(scan.report->inflatedBytes));
		return false;
	}
	return true;
}

//! The decoded bytes of one entry, counted and checksummed as they are produced.
struct Sink
{
	QByteArray* out = nullptr;  //!< null: decode and checksum only
	quint32 crc = CrcInitial;
	qint64 produced = 0;
};

enum class DecodeStatus { Ok, Damaged, Truncated, Overrun, Short };

#if defined(ZENE_HAVE_ZLIB)
//! The bytes one inflate() call may produce; the bound is checked per chunk.
constexpr int InflateChunkBytes = 64 * 1024;

//! Feeds \a count bytes into \a sink. False when they pass \a expected.
bool absorb(Sink* sink, const char* data, qint64 count, qint64 expected)
{
	if (sink->produced + count > expected) { return false; }
	sink->crc = crc32Update(sink->crc, data, count);
	sink->produced += count;
	if (sink->out != nullptr) { sink->out->append(data, static_cast<qsizetype>(count)); }
	return true;
}

//! Runs inflate() until the stream ends, an error stops it, or it passes \a expected.
DecodeStatus drain(z_stream* stream, qint64 expected, Sink* sink)
{
	QByteArray chunk(InflateChunkBytes, Qt::Uninitialized);
	int status = Z_OK;
	while (status != Z_STREAM_END)
	{
		// Never ask for more than one byte past the declared size: that byte is how an overrun is seen, and
		// it keeps the work on a lying entry from running ahead of the bound the entry was admitted under.
		const qint64 want = std::min<qint64>(chunk.size(), expected - sink->produced + 1);
		stream->next_out = reinterpret_cast<Bytef*>(chunk.data());
		stream->avail_out = static_cast<uInt>(want);
		status = inflate(stream, Z_NO_FLUSH);
		if (status == Z_BUF_ERROR) { return DecodeStatus::Truncated; }  // input exhausted, no progress
		if (status != Z_OK && status != Z_STREAM_END) { return DecodeStatus::Damaged; }
		const qint64 made = want - static_cast<qint64>(stream->avail_out);
		if (!absorb(sink, chunk.constData(), made, expected)) { return DecodeStatus::Overrun; }
	}
	return sink->produced == expected ? DecodeStatus::Ok : DecodeStatus::Short;
}

//! Inflates a raw DEFLATE stream (negative window bits: no zlib header) into \a sink.
DecodeStatus inflateRaw(const char* in, quint32 inSize, qint64 expected, Sink* sink)
{
	z_stream stream{};
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) { return DecodeStatus::Damaged; }
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in));
	stream.avail_in = inSize;
	const DecodeStatus status = drain(&stream, expected, sink);
	inflateEnd(&stream);
	return status;
}
#endif

//! Decodes the entry into \a sink. STORE copies; DEFLATE inflates under the entry's declared size.
DecodeStatus decode(const Scan& scan, const CentralEntry& entry, Sink* sink)
{
	const char* body = scan.bytes.constData() + entry.bodyAt;
#if defined(ZENE_HAVE_ZLIB)
	if (entry.method == MethodDeflate)
	{
		return inflateRaw(body, entry.compressed, entry.uncompressed, sink);
	}
#endif
	sink->crc = crc32Update(sink->crc, body, entry.compressed);
	sink->produced = entry.compressed;
	if (sink->out != nullptr) { sink->out->append(body, static_cast<qsizetype>(entry.compressed)); }
	return DecodeStatus::Ok;
}

//! The note a failed decode earns.
void noteDecodeFailure(Scan& scan, const CentralEntry& entry, DecodeStatus status, qint64 produced)
{
	QString why;
	switch (status)
	{
		case DecodeStatus::Truncated:
			why = QStringLiteral("its DEFLATE data ends before the stream does (truncated)");
			break;
		case DecodeStatus::Overrun:
			why = QStringLiteral("its DEFLATE stream inflates past the %1 bytes it declares; "
				"decoding stopped there").arg(entry.uncompressed);
			break;
		case DecodeStatus::Short:
			why = QStringLiteral("its DEFLATE stream ends after %1 bytes but it declares %2")
				.arg(produced).arg(entry.uncompressed);
			break;
		default:
			why = QStringLiteral("its DEFLATE stream is damaged");
			break;
	}
	note(scan, entry.name, DeviationCorruptEntry, QStringLiteral("entry '%1': %2").arg(entry.name, why));
}

bool wanted(const Scan& scan, const QString& name)
{
	return scan.retain.isEmpty() || scan.retain.contains(name);
}

//! Decodes one located, supported, in-bounds entry and files it (or notes why not).
void materialise(Scan& scan, const CentralEntry& entry)
{
	QByteArray body;
	Sink sink;
	if (wanted(scan, entry.name)) { sink.out = &body; }
	const DecodeStatus status = decode(scan, entry, &sink);
	// Every byte decoded counts against the total bound, whether or not the entry then proves good: a
	// container of entries that all fail their CRC or break mid-stream must not get to inflate past it.
	scan.report->inflatedBytes += sink.produced;
	if (status != DecodeStatus::Ok)
	{
		noteDecodeFailure(scan, entry, status, sink.produced);
		return;
	}
	const quint32 actual = sink.crc ^ CrcInitial;
	if (actual != entry.crc)
	{
		note(scan, entry.name, DeviationCrcMismatch,
			QStringLiteral("entry '%1' fails its own CRC (declared %2, measured %3)")
				.arg(entry.name).arg(entry.crc, 8, 16, QLatin1Char('0'))
				.arg(actual, 8, 16, QLatin1Char('0')));
		return;
	}
	scan.report->entriesRead++;
	if (entry.method == MethodDeflate) { scan.report->entriesDeflated++; }
	if (sink.out != nullptr) { scan.entries->push_back({entry.name, body}); }
}

//! Every check an entry must pass before its bytes are decoded.
bool readable(Scan& scan, CentralEntry* entry)
{
	return !refuseUnsupportedFeature(scan, *entry) && locateBody(scan, entry)
		&& methodReadable(scan, *entry) && sizeAcceptable(scan, *entry);
}

//! Walks \a count central-directory records, filing every entry that reads and noting every one that does not.
void scanDirectory(Scan& scan, int cursor, int count)
{
	for (int index = 0; index < count; index++)
	{
		CentralEntry entry;
		int next = 0;
		if (!decodeCentral(scan.bytes, cursor, &entry, &next))
		{
			note(scan, QString(), DeviationTruncatedDirectory,
				QStringLiteral("the central directory ends after %1 of the %2 entries it declares; "
					"the remaining entries are unreachable").arg(index).arg(count));
			return;
		}
		cursor = next;
		scan.report->entryNames.append(entry.name);
		if (readable(scan, &entry)) { materialise(scan, entry); }
	}
}

} // namespace

bool dawProjectZipReadTolerant(const QString& path, std::vector<DawProjectEntry>* entries,
	DawProjectZipReport* report, QString* error, const DawProjectZipLimits& limits,
	const QStringList& retain, bool* unreadable)
{
	QByteArray bytes;
	if (!readWhole(path, &bytes, error, unreadable)) { return false; }
	int centralOffset = 0;
	int count = 0;
	if (!locateDirectory(path, bytes, &centralOffset, &count, error)) { return false; }

	DawProjectZipReport local;
	std::vector<DawProjectEntry> found;
	Scan scan{bytes, limits, retain, &local, &found};
	scanDirectory(scan, centralOffset, count);
	if (entries != nullptr) { *entries = std::move(found); }
	if (report != nullptr) { *report = std::move(local); }
	return true;
}

bool dawProjectZipRead(const QString& path, std::vector<DawProjectEntry>* entries, QString* error,
	bool* unreadable)
{
	DawProjectZipLimits limits;
	limits.maxEntryBytes = std::numeric_limits<int>::max();
	limits.maxTotalBytes = std::numeric_limits<int>::max();
	limits.allowDeflate = false;  // the .mmpz v2 container is STORE; a deflated one stays refused
	DawProjectZipReport report;
	std::vector<DawProjectEntry> found;
	if (!dawProjectZipReadTolerant(path, &found, &report, error, limits, QStringList(), unreadable)) { return false; }
	if (!report.deviations.empty())
	{
		if (error) { *error = QStringLiteral("%1: %2").arg(path, report.deviations.front().detail); }
		return false;
	}
	*entries = std::move(found);
	return true;
}

} // namespace interchange
} // namespace lmms
