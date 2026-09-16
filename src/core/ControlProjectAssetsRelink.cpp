/*
 * ControlProjectAssetsRelink.cpp - the WRITE half of feature row 38: point a
 *                                   project's references at the file that was
 *                                   found. The READ half (scan, hash, digest)
 *                                   is ControlProjectAssets.cpp.
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
 * WHAT A RELINK IS, AND WHAT IT IS NOT
 *
 * It is ONE attribute's value, on the elements that were already wrong, changed
 * to the value this product would have written had the sample been picked from
 * the browser: PathUtil::toShortestRelative, the same call SampleBuffer::fromFile
 * stores with. Nothing else in the document moves - not the root's name, not the
 * creator attributes, not one other element - because the diff a user reads in
 * git is the point of a file-level repair (GIT-FRIENDLY-MMPZ.md).
 *
 * It is NOT the portable bundle. Copying the media beside the project and
 * rewriting every reference to the copy is Bar 3 and is deliberately absent: no
 * verb in this group copies a file, creates a directory or writes anything
 * except the one project file it was given. That limit is in
 * docs/KNOWN-LIMITATIONS.md and the release notes.
 *
 * REVERSIBILITY (SPEC A16). The project file is outside the project's own
 * journal, so the inverse is a RECORDED ACTION, the shape chain.save and
 * mastering.run use for their file writes: the command layer captures the
 * file's previous bytes BEFORE the rewrite and records a step that writes them
 * back. The engine half here only rewrites; it never records - the
 * transaction is the command's (src/core/ControlCommandsProjectArchive.cpp).
 */

#include "ControlProjectAssets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "ControlDeviceSupport.h" // controlWriteFileBytes / controlSha256OfBytes
#include "ControlProjectAssetsShared.h"

namespace lmms
{
namespace control
{

using namespace projectassets;

namespace
{

//! The value as something an absolute path can be compared with.
QString forwardPath(const QString& value)
{
	return QDir::cleanPath(QString(value).replace(QLatin1Char('\\'), QLatin1Char('/')));
}

/*! Resolves `to` and, when the caller passed `expect_sha256`, refuses unless
 *  the file really is that media. The check happens BEFORE the document is
 *  read, so a refusal writes nothing and leaves no undo step behind.
 *
 *  `to`'s emptiness is checked by controlRelinkAddressOk() - one place for both
 *  addresses, because the command layer runs the same check before it reads the
 *  project file for the recorded inverse. */
bool resolveRelinkTarget(const QString& to, const QString& expectSha256, QString* target,
	ControlResult* error)
{
	const QFileInfo info(to);
	if (!info.exists() || !info.isFile())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no such file to relink to: %1").arg(to));
		return false;
	}
	*target = QDir::cleanPath(info.absoluteFilePath());
	if (expectSha256.isEmpty()) { return true; }

	QFile file(*target);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot read %1 to check its hash (%2)")
				.arg(*target, file.errorString()));
		return false;
	}
	const QString found = controlSha256OfBytes(file.readAll());
	if (found.compare(expectSha256, Qt::CaseInsensitive) == 0) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1 hashes to %2, not to the expected %3: it is not the media this "
			"reference names, so nothing was rewritten").arg(*target, found, expectSha256));
	return false;
}

//! True when this element's reference is the one the caller means: by the value
//! as stored, or by the path that value resolves to.
bool matchesFrom(const QDomElement& element, const AssetElement& spec,
	const QString& projectDir, const QString& from, const QString& fromForward)
{
	ProjectAssetReference ref;
	fillReference(element, spec, -1, projectDir, &ref);
	return ref.raw == from || (ref.resolved && ref.path == fromForward);
}

/*! The rewrite walk: every reference that is the one the caller named gets
 *  \a stored written into its attribute. Everything else is left as read. */
void collectReferencesAndRewrite(const QDomElement& parent, const QString& parentChain,
	const QString& projectDir, const QString& from, const QString& fromForward,
	const QString& stored, ProjectAssetRelink* out)
{
	for (QDomElement element = parent.firstChildElement(); !element.isNull();
		element = element.nextSiblingElement())
	{
		const QString tag = element.tagName();
		for (const AssetElement& spec : kAssetElements)
		{
			if (!elementMatches(spec, tag, parentChain)) { continue; }
			if (!element.hasAttribute(QLatin1String(spec.attribute))) { continue; }
			if (element.attribute(QLatin1String(spec.attribute)).isEmpty()) { continue; }
			if (!matchesFrom(element, spec, projectDir, from, fromForward)) { continue; }
			const QString previous = element.attribute(QLatin1String(spec.attribute));
			element.setAttribute(QLatin1String(spec.attribute), stored);
			out->before.append(previous);
			out->after.append(stored);
			++out->replaced;
		}
		const QString chain = parentChain.isEmpty() ? tag : parentChain + QLatin1Char('/') + tag;
		collectReferencesAndRewrite(element, chain, projectDir, from, fromForward, stored, out);
	}
}

//! Writes the rewritten document, or (dry run) reports it without writing.
bool commitRelink(const QString& projectPath, bool dryRun, ProjectAssetRelink* out,
	ControlResult* error)
{
	if (dryRun) { return true; }
	if (!controlWriteFileBytes(projectPath, out->bytes, true, error)) { return false; }
	out->path = projectPath;
	return true;
}

} // namespace

bool controlRelinkAddressOk(const QString& from, const QString& to, ControlResult* error)
{
	if (from.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'from' is the reference to point at a new file: it is the value "
				"project.missing_assets reported in 'raw' (or the path it resolved to), and it "
				"cannot be empty"));
		return false;
	}
	if (to.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'to' is the file the reference should point at; it cannot be "
				"empty"));
		return false;
	}
	return true;
}

bool controlRelinkProjectAsset(const QString& projectPath, const QString& from, const QString& to,
	const QString& expectSha256, bool dryRun, ProjectAssetRelink* out, ControlResult* error)
{
	out->replaced = 0;
	out->before.clear();
	out->after.clear();
	out->path.clear();
	out->sha256.clear();
	out->bytes.clear();

	if (!controlRelinkAddressOk(from, to, error)) { return false; }
	QString target;
	if (!resolveRelinkTarget(to, expectSha256, &target, error)) { return false; }

	QDomDocument document;
	QString format;
	if (!readProjectDocument(projectPath, &document, &format, error)) { return false; }

	const QString projectDir = QFileInfo(projectPath).absolutePath();
	collectReferencesAndRewrite(document.documentElement(), QString(), projectDir, from,
		forwardPath(from), controlProjectAssetStoredPath(target), out);
	if (out->replaced == 0)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1 references no '%2': nothing in the file matches that value, as "
				"stored or as resolved").arg(projectPath, from));
		return false;
	}

	out->bytes = serialiseDocument(&document);
	// An .mmpz is the same XML, qCompress'd (DataFile::writeFile writes exactly
	// this for the mmpz/xptz extensions): writing the plain XML back into a
	// .mmpz would produce a file the loader's qUncompress refuses, so the format
	// the file arrived in is the format it leaves in.
	if (format == QLatin1String("mmpz")) { out->bytes = qCompress(out->bytes); }
	out->sha256 = controlSha256OfBytes(out->bytes);
	return commitRelink(projectPath, dryRun, out, error);
}

} // namespace control
} // namespace lmms
