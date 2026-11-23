/*
 * ControlProjectAssets.cpp - the SCAN and HASH half of feature row 38 (project
 *                            collection / archive, hashing, relink): what a
 *                            project FILE references, and the content hash of
 *                            the media it references. The bounded DOCUMENT read
 *                            (both legacy .mmp/.mmpz formats) is
 *                            ControlProjectAssetsDocument.cpp. The WRITE half
 *                            (the relink rewrite) is
 *                            ControlProjectAssetsRelink.cpp.
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
 *   - a legacy relative path (no prefix) that names no user/factory sample is
 *     resolved against the PROJECT'S OWN DIRECTORY - again the rule
 *     copyResources uses. It used to fall out of PathUtil::toAbsolute handing
 *     such a path back still relative (PathUtil::oldRelativeUpgrade returns the
 *     input unchanged when nothing matched); toAbsolute now resolves an
 *     unmatched relative path against the working directory, so the project
 *     directory is applied here instead - see resolveAssetPath.
 */

#include "ControlProjectAssets.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QByteArrayView>
#endif

#include <algorithm>

#include "ControlProjectAssetsShared.h"
#include "PathUtil.h"

namespace lmms
{
namespace control
{

namespace projectassets
{

//! Defined once, declared in the shared header: the elements a project file
//! references a file from. See AssetElement's own comment for the four sites
//! that write these attributes.
const AssetElement kAssetElements[6] = {
	{"sampleclip", "src", "data", ""},
	{"audiofileprocessor", "src", "sampledata", ""},
	{"sf2player", "src", "", ""},
	{"sfzplayer", "src", "", ""},  // SfzPlayer::saveSettings (the .sfz; its samples are read by the SFZ itself)
	{"clip", "src", "", "session/clips"},
	{"track", "frozenAudio", "", ""},  // Track::saveTrack: a frozen take's render
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
	// the legacy no-prefix form.
	const PathUtil::Base base = PathUtil::baseLookup(forward);
	// A legacy relative path (no prefix) that names no user/factory sample is
	// relative to the PROJECT's own directory. toAbsolute resolves such a path
	// against the working directory, so the project directory has to be applied
	// here - before toAbsolute is asked - or the reference would be taken away
	// from the project that carries it.
	if (base == PathUtil::Base::Absolute && !QFileInfo(forward).isAbsolute()
		&& PathUtil::baseLookup(PathUtil::oldRelativeUpgrade(forward)) == PathUtil::Base::Absolute)
	{
		out.path = QDir::cleanPath(projectDir + QLatin1Char('/') + forward);
		out.via = QStringLiteral("project-dir");
		out.resolved = QFileInfo(out.path).isAbsolute();
		return out;
	}

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

QString digestOf(const ProjectAssetScan& scan);

} // namespace projectassets

using namespace projectassets;

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

} // namespace

/*! sha256 over the reference set, in document order: one line per reference, so
 *  two projects that reference byte-identical media have the same digest
 *  whatever the projects or the files are named.
 *
 *  The third column is the reference's HASH STATUS, and the three unhashed states
 *  are kept apart: a present-but-unhashed file (project.missing_assets hashes
 *  nothing, and project.hash_assets leaves an over-cap file unhashed) is not the
 *  same state as a missing one, and folding the two together made a project with
 *  its media on disk digest exactly like the same project with its media gone. */
QString projectassets::digestOf(const ProjectAssetScan& scan)
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

namespace
{

//! The identity the per-command hash cache is keyed by. canonicalFilePath resolves
//! links, so two references that spell the same file differently (a symlink, or one
//! `local:` and one absolute path that clean to one path) cost one read. A path that
//! no longer resolves - a link that dangled after the scan - falls back to its
//! absolute form, which is still one key per reference path.
QString hashIdentity(const QString& path)
{
	const QFileInfo info(path);
	const QString canonical = info.canonicalFilePath();
	return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

/*! TEST SEAM (compiled only under LMMS_TESTING): when
 *  ZENE_TEST_PROJECT_ASSET_HASH_READ_LOG names a file, every file the hasher is
 *  about to READ is appended to it, one path per line. A reference answered from
 *  the per-command cache logs nothing, so a test observes "read once, reused"
 *  deterministically, without wall-clock timing and without a second product
 *  path. Nothing in the product sets the variable. */
void logTestHashRead(const QString& path)
{
#ifdef LMMS_TESTING
	const QString logPath = qEnvironmentVariable("ZENE_TEST_PROJECT_ASSET_HASH_READ_LOG");
	if (logPath.isEmpty()) { return; }
	QFile log(logPath);
	if (log.open(QIODevice::WriteOnly | QIODevice::Append)) { log.write(path.toUtf8() + '\n'); }
#else
	Q_UNUSED(path)
#endif
}

/*! Streams \a path through sha256, reading no more than \a readCap bytes plus the
 *  one byte that proves there are more. On success sets \a sha256 and leaves \a error
 *  empty; on failure sets \a error naming the read limit as "<n>-byte <capLabel>".
 *  Returns the bytes the device consumed, which is what the caller charges to the
 *  command's total-work budget (an over-limit read is charged in full). */
qint64 hashFileWithin(const QString& path, qint64 readCap, const QString& capLabel,
	QString* sha256, QString* error)
{
	logTestHashRead(path);
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = QStringLiteral("cannot read %1 (%2)").arg(path, file.errorString());
		return 0;
	}
	QCryptographicHash hasher(QCryptographicHash::Sha256);
	char buffer[64 * 1024];
	qint64 bytesRead = 0;
	const qint64 bufferSize = static_cast<qint64>(sizeof(buffer));
	for (;;)
	{
		const qint64 remaining = readCap - bytesRead;
		const qint64 request = remaining >= bufferSize ? bufferSize : remaining + 1;
		const qint64 count = file.read(buffer, request);
		if (count < 0)
		{
			*error = QStringLiteral("could not hash %1").arg(path);
			return bytesRead;
		}
		if (count == 0) { break; }
		bytesRead += count;
		if (count > remaining)
		{
			*error = QStringLiteral("not hashed: at least %1 bytes exceeds the %2")
				.arg(readCap + 1).arg(capLabel);
			return bytesRead;
		}
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		hasher.addData(QByteArrayView(buffer, static_cast<qsizetype>(count)));
#else
		hasher.addData(buffer, static_cast<int>(count));   // Qt 5 (hosted CI); count <= the read buffer's size
#endif
	}
	*sha256 = QString::fromLatin1(hasher.result().toHex());
	error->clear();
	return bytesRead;
}

//! The total budget this process gives one hash command: MaxProjectAssetTotalHashBytes,
//! or less when a test lowers it (the same test-seam style as
//! ZENE_TEST_MAX_RECOVERED_BYTES in DataFile.cpp).
qint64 totalHashBudget()
{
#ifdef LMMS_TESTING
	bool ok = false;
	const qint64 lowered = qEnvironmentVariable("ZENE_TEST_PROJECT_ASSET_HASH_TOTAL_BYTES").toLongLong(&ok);
	if (ok && lowered > 0) { return lowered; }
#endif
	return MaxProjectAssetTotalHashBytes;
}

//! The cached answer for the last reference resolved to one file identity.
struct CachedHash
{
	bool hashed = false;
	QString sha256;
	QString error;
};

//! Hash one present reference under the per-file cap AND the command's remaining total
//! budget, charging what it read to \a remaining. A reference past either bound is a
//! reported answer, not an error; the per-command cache is the caller's, so that the
//! next reference to the same file is not read again.
CachedHash hashOneWithin(const ProjectAssetReference& ref, qint64 budget, qint64* remaining)
{
	CachedHash entry;
	if (ref.bytes > MaxProjectAssetHashBytes)
	{
		entry.error = QStringLiteral("not hashed: %1 bytes is over the %2-byte cap")
			.arg(ref.bytes).arg(MaxProjectAssetHashBytes);
	}
	else if (*remaining <= 0)
	{
		entry.error = QStringLiteral("not hashed: the %1-byte total cap for one hash_assets "
			"command was used up before this file").arg(budget);
	}
	else if (ref.bytes > *remaining)
	{
		entry.error = QStringLiteral("not hashed: %1 bytes would pass the %2 bytes left under "
			"the %3-byte total cap for one command").arg(ref.bytes).arg(*remaining).arg(budget);
	}
	else
	{
		const bool perFile = *remaining >= MaxProjectAssetHashBytes;
		const qint64 readCap = perFile ? MaxProjectAssetHashBytes : *remaining;
		const QString capLabel = perFile
			? QStringLiteral("%1-byte cap").arg(MaxProjectAssetHashBytes)
			: QStringLiteral("%1-byte total cap for one command").arg(readCap);
		const qint64 read = hashFileWithin(ref.path, readCap, capLabel, &entry.sha256, &entry.error);
		*remaining = std::max<qint64>(0, *remaining - read);
		entry.hashed = entry.error.isEmpty();
		if (!entry.hashed) { entry.sha256.clear(); }
	}
	return entry;
}

} // namespace

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
	out->digest = projectassets::digestOf(*out);
	return true;
}

//! Hashes a scanned reference. An over-cap or unreadable file is a REPORTED
//! state, not a failed command: the caller sees hashed=false and the reason.
void projectassets::hashReference(ProjectAssetReference* ref, qint64 byteCap)
{
	const QString capLabel = QStringLiteral("%1-byte cap").arg(byteCap);
	if (ref->bytes > byteCap)
	{
		ref->error = QStringLiteral("not hashed: %1 bytes is over the %2")
			.arg(ref->bytes).arg(capLabel);
		return;
	}
	QString sha256;
	QString error;
	hashFileWithin(ref->path, byteCap, capLabel, &sha256, &error);
	if (!error.isEmpty())
	{
		ref->error = error;
		return;
	}
	ref->sha256 = sha256;
	ref->hashed = true;
}

bool controlHashProjectAssets(const QString& projectPath, ProjectAssetScan* out,
	ControlResult* error)
{
	if (!controlScanProjectAssets(projectPath, out, error)) { return false; }

	// One read per DISTINCT file, and a total read budget for the command - see the
	// bounds in ControlProjectAssets.h. Both are answers the caller still receives:
	// a reference over either bound is reported unhashed (with the reason), never
	// read "for a while" and never a silently partial command.
	const qint64 budget = totalHashBudget();
	qint64 remaining = budget;
	QHash<QString, CachedHash> cache;
	for (ProjectAssetReference& ref : out->references)
	{
		if (ref.hashed || !ref.exists) { continue; }
		const QString identity = hashIdentity(ref.path);
		const auto cached = cache.constFind(identity);
		if (cached != cache.constEnd())
		{
			ref.hashed = cached->hashed;
			ref.sha256 = cached->sha256;
			ref.error = cached->error;
			continue;
		}
		const CachedHash entry = hashOneWithin(ref, budget, &remaining);
		ref.hashed = entry.hashed;
		ref.sha256 = entry.sha256;
		ref.error = entry.error;
		cache.insert(identity, entry);
	}

	out->digest = projectassets::digestOf(*out);
	return true;
}

} // namespace control
} // namespace lmms
