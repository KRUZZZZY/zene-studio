/*
 * ControlProjectAssetsDocument.cpp - the DOCUMENT half of feature row 38
 *                                     (project collection / archive, hashing,
 *                                     relink): reading a project FILE, bounded,
 *                                     without needing a session.
 *
 * Split out of ControlProjectAssets.cpp when that file reached the 500-code-line
 * ratchet. The SCAN half (what the document references and the digest over that
 * set) and the HASH half (the content hash of the media) stay in
 * ControlProjectAssets.cpp; the WRITE half (the relink rewrite) is in
 * ControlProjectAssetsRelink.cpp. The three share ControlProjectAssetsShared.h.
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

#include "ControlProjectAssets.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>
#include <QtEndian>
#include <QtGlobal>

#include <algorithm>

#include "ControlProjectAssetsShared.h"

#if defined(ZENE_HAVE_ZLIB)
#include <zlib.h>
#endif

namespace lmms
{
namespace control
{

namespace projectassets
{

//! The size on disk is measured BEFORE anything is read, as a first refusal. The bytes that are
// read are capped again by readBoundedBody, because the file can change after this stat.
// Same cap as the engine's own document recovery (MaxRecoveredDocumentBytes,
// src/core/DataFile.cpp:202).
bool storedSizeWithinCap(const QFileInfo& info, const QString& projectPath, ControlResult* error)
{
	if (info.size() <= MaxProjectAssetDocumentBytes) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1 is %2 bytes, over the %3-byte limit this verb reads; it was not "
			"loaded into memory").arg(projectPath).arg(info.size()).arg(MaxProjectAssetDocumentBytes));
	return false;
}

//! Reads the body of a file into \a body, never holding more than the cap plus one byte.
//! The stat taken by storedSizeWithinCap is only a first refusal: a file that grows or is
//! replaced between that stat and this read is still stopped at the cap while it is read.
bool readBoundedBody(QFile* file, const QString& projectPath, QByteArray* body, ControlResult* error)
{
	constexpr qint64 chunkBytes = 1024LL * 1024LL;
	body->clear();
	while (!file->atEnd() && body->size() <= MaxProjectAssetDocumentBytes)
	{
		const qint64 room = MaxProjectAssetDocumentBytes + 1 - body->size();
		const QByteArray chunk = file->read(std::min(chunkBytes, room));
		if (chunk.isEmpty())
		{
			*error = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("cannot read %1 (%2)").arg(projectPath, file->errorString()));
			return false;
		}
		body->append(chunk);
	}
	if (body->size() > MaxProjectAssetDocumentBytes)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 grew past the %2-byte limit while it was read; it was not loaded "
				"into memory").arg(projectPath).arg(MaxProjectAssetDocumentBytes));
		return false;
	}
	return true;
}

//! Reads a .mmpz body after measuring the size it DECLARES. A qCompress stream carries its
//! expanded size in the leading 4 bytes. That header is a hint, not a bound (qUncompress grows
//! its output past it), so the declared size is refused here only as a fast path; the real
//! bound is enforced on the output actually produced by inflateWithinCap.
bool readBoundedCompressed(QFile* file, const QString& projectPath, QByteArray* stored,
	ControlResult* error)
{
	const QByteArray header = file->read(4);
	const quint32 declared = header.size() >= 4
		? qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(header.constData())) : 0u;
	if (declared > MaxProjectAssetDocumentBytes)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 declares %2 bytes of expanded project, over the %3-byte limit "
				"this verb reads; it was not inflated").arg(projectPath).arg(declared)
				.arg(MaxProjectAssetDocumentBytes));
		return false;
	}
	if (!file->seek(0))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot rewind %1 (%2)").arg(projectPath, file->errorString()));
		return false;
	}
	return readBoundedBody(file, projectPath, stored, error);
}

//! Why a .mmpz body could not be turned into XML within the cap.
enum class InflateOutcome { Done, Malformed, OverCap };

#if defined(ZENE_HAVE_ZLIB)
//! The zlib half of inflateWithinCap: \a stored is a qCompress stream (4-byte big-endian
//! expanded size, then zlib), and the output is counted chunk by chunk, so the cap holds on
//! the bytes actually produced, whatever the header said.
InflateOutcome inflateBodyWithinCap(const QByteArray& stored, QByteArray* xml, qint64 cap)
{
	z_stream stream{};
	if (inflateInit(&stream) != Z_OK) { return InflateOutcome::Malformed; }
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(stored.constData() + 4));
	stream.avail_in = static_cast<uInt>(stored.size() - 4);
	QByteArray chunk(1024 * 1024, Qt::Uninitialized);
	xml->clear();
	int status = Z_OK;
	InflateOutcome outcome = InflateOutcome::Malformed;
	while (status != Z_STREAM_END)
	{
		stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
		stream.avail_out = static_cast<uInt>(chunk.size());
		status = inflate(&stream, Z_NO_FLUSH);  // Z_BUF_ERROR: truncated input, no progress possible
		if (status != Z_OK && status != Z_STREAM_END) { break; }
		const qint64 produced = chunk.size() - static_cast<qint64>(stream.avail_out);
		if (xml->size() + produced > cap)
		{
			outcome = InflateOutcome::OverCap;
			break;
		}
		xml->append(chunk.constData(), produced);
	}
	inflateEnd(&stream);
	if (outcome == InflateOutcome::OverCap) { return outcome; }
	return status == Z_STREAM_END ? InflateOutcome::Done : InflateOutcome::Malformed;
}
#endif

bool refuseNotReadable(const QString& projectPath, ControlResult* error)
{
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1 is not a readable .mmpz: the compressed body did not "
			"inflate (a .mmpz is qCompress'd XML)").arg(projectPath));
	return false;
}

bool refuseOverCap(const QString& projectPath, ControlResult* error)
{
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1 inflates to more than the %2-byte limit this verb reads; it was not "
			"loaded in full").arg(projectPath).arg(MaxProjectAssetDocumentBytes));
	return false;
}

//! A .mmpz can only be read when zlib is linked: its body is then inflated under the cap. Without zlib
//! the only inflate is qUncompress, which grows its output past the header, so the read is refused
//! here, before any compressed byte is read. This fails closed on purpose.
bool zlibAvailable(const QString& projectPath, ControlResult* error)
{
#if defined(ZENE_HAVE_ZLIB)
	Q_UNUSED(projectPath)
	Q_UNUSED(error)
	return true;
#else
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1 is a compressed .mmpz, and this build has no zlib to inflate it under the "
			"%2-byte limit this verb reads, so it was not read (configure with zlib available)")
			.arg(projectPath).arg(MaxProjectAssetDocumentBytes));
	return false;
#endif
}

//! Inflates a .mmpz body (a qCompress stream: 4-byte big-endian expanded size, then zlib) and
//! refuses as soon as the bytes it PRODUCES would pass the cap. Only reached with zlib linked.
bool inflateWithinCap(const QString& projectPath, const QByteArray& stored, QByteArray* xml,
	ControlResult* error)
{
	if (stored.size() < 4) { return refuseNotReadable(projectPath, error); }
#if defined(ZENE_HAVE_ZLIB)
	const InflateOutcome outcome = inflateBodyWithinCap(stored, xml, MaxProjectAssetDocumentBytes);
	if (outcome == InflateOutcome::OverCap) { return refuseOverCap(projectPath, error); }
	return outcome == InflateOutcome::Done ? true : refuseNotReadable(projectPath, error);
#else
	return zlibAvailable(projectPath, error);  // unreachable from readProjectDocument; refuses without zlib
#endif
}

//! The inflate-and-parse half, once the bytes in hand are already bounded.
bool parseStoredDocument(const QString& projectPath, const QByteArray& stored, bool compressed,
	QDomDocument* document, ControlResult* error)
{
	QByteArray xml = stored;
	if (compressed && !inflateWithinCap(projectPath, stored, &xml, error)) { return false; }

	QString parseError;
	int line = 0;
	int column = 0;
	if (!document->setContent(xml, false, &parseError, &line, &column))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is not readable as a project: %2 (line %3, column %4)")
				.arg(projectPath, parseError).arg(line).arg(column));
		return false;
	}
	return true;
}

//! The bounded body of an open project file. A .mmpz is read only when zlib is available to inflate it.
bool readStoredBody(QFile* file, const QString& projectPath, bool compressed, QByteArray* stored,
	ControlResult* error)
{
	if (!compressed) { return readBoundedBody(file, projectPath, stored, error); }
	return zlibAvailable(projectPath, error) && readBoundedCompressed(file, projectPath, stored, error);
}

bool readProjectDocument(const QString& projectPath, QDomDocument* document, QString* format,
	ControlResult* error)
{
	const QFileInfo info(projectPath);
	if (!info.exists() || !info.isFile())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no such project file: %1").arg(projectPath));
		return false;
	}
	// The extension first: a file this verb does not read is refused before any of
	// it is read into memory.
	*format = info.suffix().toLower();
	if (*format != QLatin1String("mmpz") && *format != QLatin1String("mmp"))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a project file this verb reads: the extension must "
				"be .mmp (XML) or .mmpz (compressed XML)").arg(projectPath));
		return false;
	}
	if (!storedSizeWithinCap(info, projectPath, error)) { return false; }
	QFile file(projectPath);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot read %1 (%2)").arg(projectPath, file.errorString()));
		return false;
	}

	// Q050-007: a v2 container is a ZIP, which neither the bounded qCompress read below
	// nor the plain-XML read can follow - the first would call its magic a 1.3 GB
	// "declared size". Refused by name instead of by that accident; this verb reads the
	// legacy .mmpz and .mmp (saving with format "legacy" gives it one).
	if (file.peek(4) == QByteArrayLiteral("PK\x03\x04"))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is a v2 container, which this verb does not read yet: it reads the "
				"legacy .mmpz and .mmp (project.save with format \"legacy\" writes one)").arg(projectPath));
		return false;
	}

	const bool compressed = *format == QLatin1String("mmpz");
	QByteArray stored;
	if (!readStoredBody(&file, projectPath, compressed, &stored, error)) { return false; }
	return parseStoredDocument(projectPath, stored, compressed, document, error);
}

QByteArray serialiseDocument(QDomDocument* document)
{
	QString xml;
	{
		QTextStream stream(&xml);
		document->save(stream, 2);
	}
	if (!xml.contains(QLatin1String("<!DOCTYPE"))) { return xml.toUtf8(); }
	QString rebuilt;
	const QStringList lines = xml.split(QLatin1Char('\n'));
	for (const QString& line : lines)
	{
		if (line.trimmed().startsWith(QLatin1String("<!DOCTYPE"))) { continue; }
		rebuilt += line + QLatin1Char('\n');
	}
	return rebuilt.toUtf8();
}

} // namespace projectassets

using namespace projectassets;

bool inflateQtCompressedWithinCap(const QByteArray& stored, qint64 cap, QByteArray* out)
{
	if (stored.size() < 4 || out == nullptr) { return false; }
#if defined(ZENE_HAVE_ZLIB)
	return projectassets::inflateBodyWithinCap(stored, out, cap) == projectassets::InflateOutcome::Done;
#else
	// Fail closed: without zlib there is no inflate that honours the cap, so nothing is inflated.
	Q_UNUSED(cap)
	out->clear();
	return false;
#endif
}

} // namespace control
} // namespace lmms
