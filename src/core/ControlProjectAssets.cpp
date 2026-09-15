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
	const QString absolute = PathUtil::toAbsolute(forward);
	if (QFileInfo(absolute).isAbsolute())
	{
		out.path = QDir::cleanPath(absolute);
		out.via = QStringLiteral("absolute");
		out.resolved = true;
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
	QFile file(projectPath);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot read %1 (%2)").arg(projectPath, file.errorString()));
		return false;
	}
	const QByteArray stored = file.readAll();

	*format = info.suffix().toLower();
	QByteArray xml = stored;
	if (*format == QLatin1String("mmpz"))
	{
		xml = qUncompress(stored);
		if (xml.isEmpty())
		{
			*error = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("%1 is not a readable .mmpz: the compressed body did not "
					"inflate (a .mmpz is qCompress'd XML)").arg(projectPath));
			return false;
		}
	}
	else if (*format != QLatin1String("mmp"))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a project file this verb reads: the extension must "
				"be .mmp (XML) or .mmpz (compressed XML)").arg(projectPath));
		return false;
	}

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
		const QString tag = element.tagName();
		for (const AssetElement& spec : kAssetElements)
		{
			if (!elementMatches(spec, tag, parentChain)) { continue; }
			if (!element.hasAttribute(QLatin1String(spec.attribute))) { continue; }
			// An element whose path attribute is empty names no file. It is
			// counted, not listed: inline media is not a missing asset.
			if (element.attribute(QLatin1String(spec.attribute)).isEmpty())
			{
				++out->embeddedCount;
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
 *  whatever the projects or the files are named. */
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
		else { material += "missing"; }
		material += '\n';
	}
	return QString::fromLatin1(QCryptographicHash::hash(material,
		QCryptographicHash::Sha256).toHex());
}

//! Hashes one present reference. An over-cap or unreadable file is a REPORTED
//! state, not a failed command: the caller sees hashed=false and the reason.
void hashOneReference(ProjectAssetReference* ref)
{
	if (ref->bytes > MaxProjectAssetHashBytes)
	{
		ref->error = QStringLiteral("not hashed: %1 bytes is over the %2-byte cap")
			.arg(ref->bytes).arg(MaxProjectAssetHashBytes);
		return;
	}
	QFile file(ref->path);
	if (!file.open(QIODevice::ReadOnly))
	{
		ref->error = QStringLiteral("cannot read %1 (%2)").arg(ref->path, file.errorString());
		return;
	}
	QCryptographicHash hasher(QCryptographicHash::Sha256);
	if (!hasher.addData(&file))
	{
		ref->error = QStringLiteral("could not hash %1").arg(ref->path);
		return;
	}
	ref->sha256 = QString::fromLatin1(hasher.result().toHex());
	ref->hashed = true;
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
			QStringLiteral("%1 carries %2 file references, over the %3 this verb reports in one "
				"answer; nothing was returned as a partial list")
				.arg(projectPath).arg(out->references.size()).arg(MaxProjectAssetReferences));
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
		if (!ref.hashed && ref.exists) { hashOneReference(&ref); }
	}
	out->digest = digestOf(*out);
	return true;
}

} // namespace control
} // namespace lmms
