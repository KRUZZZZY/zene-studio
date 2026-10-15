/*
 * ControlProjectAssets.cpp - the READ half of feature row 38 (project
 *                            collection / archive, hashing, relink): what a
 *                            project FILE references, and the content hash of
 *                            the media it references. The WRITE half (the
 *                            relink rewrite) is ControlProjectAssetsRelink.cpp.
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

/*
 * WHY THIS READS THE FILE AND NOT THE SESSION
 *
 * A loaded session has already thrown away what it could not load: a
 * SampleClip whose `src` is gone keeps neither the buffer nor the path
 * (SampleClip::loadSettings collects "Sample not found: <src>" and sets no
 * sample), and a project that refuses to open has no session at all. The
 * question "what does this project reference that is not on disk" therefore has
 * to be asked of the document, and the answer has to survive the project being
 * unloadable, plugin-less or headless. That is also what makes the verbs useful
 * for a COLLECTION of projects: a path is enough, the project need not be open,
 * and nothing is mutated by looking.
 *
 * The engine's own two errata on the way are deliberate:
 *
 *   - `local:` resolves against THE PROJECT FILE'S OWN DIRECTORY, not against
 *     the open project (PathUtil resolves Base::LocalDir through
 *     Engine::getSong()->projectFileName()). Those are the same thing only when
 *     the file being scanned is the open one, and the whole point of this verb
 *     is scanning a file that is not. DataFile::copyResources makes the same
 *     call for the same reason ("If we are running without the project loaded
 *     (from CLI), 'local:' base prefixes aren't converted, so we need to convert
 *     it ourselves", src/core/DataFile.cpp:569-575).
 *   - a legacy relative path (no prefix) that names no user/factory sample comes
 *     back from PathUtil::toAbsolute still relative (PathUtil::oldRelativeUpgrade
 *     returns the input unchanged when nothing matched). It is then resolved
 *     against the project's directory - again the rule copyResources uses.
 */

#include "ControlProjectAssets.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>
#include <QtEndian>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QByteArrayView>
#endif

#include <algorithm>

#include "ControlProjectAssetsShared.h"
#include "PathUtil.h"

#if defined(ZENE_HAVE_ZLIB)
#include <zlib.h>
#endif

namespace lmms
{
namespace control
{

namespace projectassets
{

//! Defined once, declared in the shared header: the elements a project file
//! references a file from. See AssetElement's own comment for the four sites
//! that write these attributes.
const AssetElement kAssetElements[4] = {
	{"sampleclip", "src", "data", ""},
	{"audiofileprocessor", "src", "sampledata", ""},
	{"sf2player", "src", "", ""},
	{"clip", "src", "", "session/clips"},
};

bool elementMatches(const AssetElement& spec, const QString& tag, const QString& parentChain)
{
	if (tag != QLatin1String(spec.tag)) { return false; }
	const QString required = QString::fromLatin1(spec.parentPath);
	if (required.isEmpty()) { return true; }
	return parentChain == required || parentChain.endsWith(QLatin1Char('/') + required);
}

Resolution resolveAssetPath(const QString& projectDir, const QString& raw)
{
	Resolution out;
	if (raw.isEmpty())
	{
		out.via = QStringLiteral("empty");
		return out;
	}
	const QString forward = QString(raw).replace(QLatin1Char('\\'), QLatin1Char('/'));
	if (forward.startsWith(QLatin1String("local:"), Qt::CaseInsensitive))
	{
		out.path = QDir::cleanPath(projectDir + QLatin1Char('/') + forward.mid(6));
		out.via = QStringLiteral("local:");
		out.resolved = QFileInfo(out.path).isAbsolute();
		return out;
	}

	// PathUtil owns every other prefix (factorysample:, usersample:, ...) and
	// the legacy no-prefix form; it returns the input unchanged when none of
	// its bases holds the file.
	const PathUtil::Base base = PathUtil::baseLookup(forward);
	const QString absolute = PathUtil::toAbsolute(forward);
	if (QFileInfo(absolute).isAbsolute())
	{
		out.path = QDir::cleanPath(absolute);
		out.via = QStringLiteral("absolute");
		out.resolved = true;
		return out;
	}
	if (base != PathUtil::Base::Absolute)
	{
		out.via = QStringLiteral("unresolved-prefix");
		return out;
	}
	out.path = QDir::cleanPath(projectDir + QLatin1Char('/') + absolute);
	out.via = QStringLiteral("project-dir");
	out.resolved = QFileInfo(out.path).isAbsolute();
	return out;
}

void fillReference(const QDomElement& element, const AssetElement& spec, int index,
	const QString& projectDir, ProjectAssetReference* ref)
{
	ref->index = index;
	ref->tag = element.tagName();
	ref->attribute = QString::fromLatin1(spec.attribute);
	ref->raw = element.attribute(ref->attribute);
	ref->embedded = spec.embedAttribute[0] != '\0'
		&& element.hasAttribute(QString::fromLatin1(spec.embedAttribute));

	const Resolution resolution = resolveAssetPath(projectDir, ref->raw);
	ref->path = resolution.path;
	ref->resolvedVia = resolution.via;
	ref->resolved = resolution.resolved;

	const QFileInfo info(ref->path);
	ref->exists = ref->resolved && info.exists() && info.isFile();
	if (ref->exists)
	{
		ref->bytes = info.size();
		ref->mtimeMs = info.lastModified().toMSecsSinceEpoch();
		return;
	}
	ref->error = ref->resolved ? QStringLiteral("not on disk")
		: QStringLiteral("cannot be resolved to a path on this machine");
}

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

namespace
{

/*! Walks the document in order and records every reference. \a parentChain is
 *  the slash-joined chain of ancestor tags of the element being examined
 *  (empty for a direct child of the root). */
void collectReferences(const QDomElement& parent, const QString& parentChain,
	const QString& projectDir, ProjectAssetScan* out)
{
	for (QDomElement element = parent.firstChildElement(); !element.isNull();
		element = element.nextSiblingElement())
	{
		// Past the cap the scan is refused whole, so stop here: every further
		// reference would cost a path resolution and a stat for nothing.
		if (out->references.size() > MaxProjectAssetReferences) { return; }
		const QString tag = element.tagName();
		for (const AssetElement& spec : kAssetElements)
		{
			if (!elementMatches(spec, tag, parentChain)) { continue; }
			if (!element.hasAttribute(QLatin1String(spec.attribute))) { continue; }
			// An element whose path attribute is empty names no file, so it is
			// never listed. It counts as embedded media ONLY when its element type
			// has an inline form and that media is actually present (the same
			// test fillReference uses): `sf2player` and the session `clip` carry no
			// inline attribute at all, so `<sf2player src="">` is an inert reference
			// and counting it would report inline content the project does not hold.
			if (element.attribute(QLatin1String(spec.attribute)).isEmpty())
			{
				if (spec.embedAttribute[0] != '\0'
					&& element.hasAttribute(QString::fromLatin1(spec.embedAttribute)))
				{
					++out->embeddedCount;
				}
				continue;
			}
			ProjectAssetReference ref;
			fillReference(element, spec, out->references.size(), projectDir, &ref);
			out->references.append(ref);
		}
		const QString chain = parentChain.isEmpty() ? tag : parentChain + QLatin1Char('/') + tag;
		collectReferences(element, chain, projectDir, out);
	}
}

/*! sha256 over the reference set, in document order: one line per reference, so
 *  two projects that reference byte-identical media have the same digest
 *  whatever the projects or the files are named.
 *
 *  The third column is the reference's HASH STATUS, and the three unhashed states
 *  are kept apart: a present-but-unhashed file (project.missing_assets hashes
 *  nothing, and project.hash_assets leaves an over-cap file unhashed) is not the
 *  same state as a missing one, and folding the two together made a project with
 *  its media on disk digest exactly like the same project with its media gone. */
QString digestOf(const ProjectAssetScan& scan)
{
	QByteArray material;
	for (const ProjectAssetReference& ref : scan.references)
	{
		material += ref.tag.toUtf8();
		material += '\t';
		material += ref.raw.toUtf8();
		material += '\t';
		if (ref.hashed) { material += ref.sha256.toUtf8(); }
		else if (ref.embedded) { material += "embedded"; }
		else if (ref.exists) { material += "unhashed"; }
		else { material += "missing"; }
		material += '\n';
	}
	return QString::fromLatin1(QCryptographicHash::hash(material,
		QCryptographicHash::Sha256).toHex());
}

} // namespace

//! Hashes a scanned reference. An over-cap or unreadable file is a REPORTED
//! state, not a failed command: the caller sees hashed=false and the reason.
void projectassets::hashReference(ProjectAssetReference* ref, qint64 byteCap)
{
	if (ref->bytes > byteCap)
	{
		ref->error = QStringLiteral("not hashed: %1 bytes is over the %2-byte cap")
			.arg(ref->bytes).arg(byteCap);
		return;
	}
	QFile file(ref->path);
	if (!file.open(QIODevice::ReadOnly))
	{
		ref->error = QStringLiteral("cannot read %1 (%2)").arg(ref->path, file.errorString());
		return;
	}
	QCryptographicHash hasher(QCryptographicHash::Sha256);
	char buffer[64 * 1024];
	qint64 bytesRead = 0;
	const qint64 bufferSize = static_cast<qint64>(sizeof(buffer));
	for (;;)
	{
		const qint64 remaining = byteCap - bytesRead;
		const qint64 request = remaining >= bufferSize ? bufferSize : remaining + 1;
		const qint64 count = file.read(buffer, request);
		if (count < 0)
		{
			ref->error = QStringLiteral("could not hash %1").arg(ref->path);
			return;
		}
		if (count == 0) { break; }
		if (count > remaining)
		{
			ref->error = QStringLiteral("not hashed: at least %1 bytes exceeds the %2-byte cap")
				.arg(byteCap + 1).arg(byteCap);
			return;
		}
		bytesRead += count;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		hasher.addData(QByteArrayView(buffer, static_cast<qsizetype>(count)));
#else
		hasher.addData(buffer, static_cast<int>(count));   // Qt 5 (hosted CI); count <= the read buffer's size
#endif
	}
	ref->sha256 = QString::fromLatin1(hasher.result().toHex());
	ref->hashed = true;
}

int ProjectAssetScan::missingCount() const
{
	int count = 0;
	for (const ProjectAssetReference& ref : references) { if (ref.missing()) { ++count; } }
	return count;
}

int ProjectAssetScan::presentCount() const
{
	int count = 0;
	for (const ProjectAssetReference& ref : references) { if (ref.exists) { ++count; } }
	return count;
}

int ProjectAssetScan::unresolvedCount() const
{
	int count = 0;
	for (const ProjectAssetReference& ref : references)
	{
		if (!ref.embedded && !ref.resolved) { ++count; }
	}
	return count;
}

QString controlProjectAssetStoredPath(const QString& path)
{
	return PathUtil::toShortestRelative(path);
}

bool controlScanProjectAssets(const QString& projectPath, ProjectAssetScan* out,
	ControlResult* error)
{
	QDomDocument document;
	QString format;
	if (!readProjectDocument(projectPath, &document, &format, error)) { return false; }

	out->file = projectPath;
	out->format = format;
	out->references.clear();
	out->embeddedCount = 0;
	out->digest.clear();

	const QString projectDir = QFileInfo(projectPath).absolutePath();
	const QDomElement root = document.documentElement();
	if (root.isNull())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 carries no root element, so it is not a project")
				.arg(projectPath));
		return false;
	}
	collectReferences(root, QString(), projectDir, out);
	if (out->references.size() > MaxProjectAssetReferences)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 carries more than %2 file references, the most this verb reports "
				"in one answer; nothing was returned as a partial list")
				.arg(projectPath).arg(MaxProjectAssetReferences));
		return false;
	}
	out->digest = digestOf(*out);
	return true;
}

bool controlHashProjectAssets(const QString& projectPath, ProjectAssetScan* out,
	ControlResult* error)
{
	if (!controlScanProjectAssets(projectPath, out, error)) { return false; }
	for (ProjectAssetReference& ref : out->references)
	{
		if (!ref.hashed && ref.exists)
		{
			projectassets::hashReference(&ref, MaxProjectAssetHashBytes);
		}
	}
	out->digest = digestOf(*out);
	return true;
}

} // namespace control
} // namespace lmms
