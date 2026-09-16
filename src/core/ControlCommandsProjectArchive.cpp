/*
 * ControlCommandsProjectArchive.cpp - the project.* half of feature row 38
 *                                     (docs/FEATURE-LIST-0.3.0.md section 8):
 *                                     missing-asset DETECTION, content HASHING
 *                                     for the referenced media, and RELINK.
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
 * THE THREE VERBS AND WHAT EACH ONE IS FOR
 *
 *   project.missing_assets  READ. Everything the named project file references,
 *                           and which of those references have nothing on disk
 *                           at them. No session, no GUI, no load: the answer
 *                           survives a project that cannot be opened at all.
 *   project.hash_assets     READ. The same list plus a sha256 per reference that
 *                           is on disk - the identity of the media, so a caller
 *                           can tell "the same sample, moved" from "a different
 *                           sample with the same name" before it rewrites
 *                           anything.
 *   project.relink          WRITE. Point the references whose value is `from` at
 *                           the file `to`, optionally refusing unless that file
 *                           hashes to the expected sha256. One recorded action
 *                           checkpoint holds the project file's previous bytes,
 *                           so one control.undo puts the file back byte for byte.
 *
 * OUT OF SCOPE, deliberately and by the row's own reading: the PORTABLE-BUNDLE
 * half (copying the media beside the project and rewriting every reference to
 * the copy) is Bar 3 and is NOT implemented by these verbs - nothing here
 * copies a file. Named in docs/KNOWN-LIMITATIONS.md and
 * docs/RELEASE-NOTES-v0.3.0-alpha.md, not silently dropped.
 */

#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h" // controlReadFileBytes / controlWriteFileBytes
#include "ControlEdit.h"          // transactionPayload (SPEC A16)
#include "ControlProjectAssets.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "ProjectJournal.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The transaction record's mechanism, one sentence, for every successful
//! relink: the previous bytes of the project file are recorded BEFORE the write
//! and the recorded step puts them back.
const char* const kRelinkMechanism = "action checkpoint: the previous bytes of the project file "
	"are captured before the rewrite and the recorded step writes them back, so one control.undo "
	"restores the file byte for byte (the project's own journal does not carry a file-level edit)";

QJsonObject referenceJson(const ProjectAssetReference& ref, bool withHash)
{
	QJsonObject out;
	out.insert(QStringLiteral("index"), ref.index);
	out.insert(QStringLiteral("tag"), ref.tag);
	out.insert(QStringLiteral("attribute"), ref.attribute);
	out.insert(QStringLiteral("raw"), ref.raw);
	out.insert(QStringLiteral("path"), ref.path);
	out.insert(QStringLiteral("resolved"), ref.resolved);
	out.insert(QStringLiteral("resolved_via"), ref.resolvedVia);
	out.insert(QStringLiteral("exists"), ref.exists);
	out.insert(QStringLiteral("embedded"), ref.embedded);
	out.insert(QStringLiteral("missing"), ref.missing());
	out.insert(QStringLiteral("bytes"), ref.bytes);
	out.insert(QStringLiteral("mtime_ms"), ref.mtimeMs);
	if (!ref.error.isEmpty()) { out.insert(QStringLiteral("error"), ref.error); }
	if (withHash)
	{
		out.insert(QStringLiteral("hashed"), ref.hashed);
		out.insert(QStringLiteral("sha256"), ref.sha256);
	}
	return out;
}

QJsonArray referencesJson(const ProjectAssetScan& scan, bool withHash)
{
	QJsonArray array;
	for (const ProjectAssetReference& ref : scan.references)
	{
		array.append(referenceJson(ref, withHash));
	}
	return array;
}

QJsonArray missingJson(const ProjectAssetScan& scan)
{
	QJsonArray array;
	for (const ProjectAssetReference& ref : scan.references)
	{
		if (ref.missing()) { array.append(referenceJson(ref, false)); }
	}
	return array;
}

QJsonObject scanHeader(const ProjectAssetScan& scan)
{
	QJsonObject out;
	out.insert(QStringLiteral("file"), scan.file);
	out.insert(QStringLiteral("format"), scan.format);
	out.insert(QStringLiteral("digest"), scan.digest);
	out.insert(QStringLiteral("reference_count"), scan.references.size());
	out.insert(QStringLiteral("missing_count"), scan.missingCount());
	out.insert(QStringLiteral("present_count"), scan.presentCount());
	out.insert(QStringLiteral("unresolved_count"), scan.unresolvedCount());
	out.insert(QStringLiteral("embedded_count"), scan.embeddedCount);
	return out;
}

//! The project path from the args, refusing a missing/empty one before anything
//! touches the filesystem.
bool projectArg(const QJsonObject& args, QString* path, ControlResult* error)
{
	*path = args.value(QStringLiteral("project")).toString();
	if (path->isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'project' is the project file to read (.mmp or .mmpz); it cannot be "
				"empty. The verb reads the FILE, so the project does not have to be the open "
				"one"));
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// project.missing_assets
// ---------------------------------------------------------------------------

void registerProjectMissingAssets(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.missing_assets");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("missing_assets");
	cmd.description = QStringLiteral("Every file a project file references - sample clips, "
		"AudioFileProcessor and SF2 instruments, session-view audio slots - and which of those "
		"references have nothing on disk at them. Reads the project FILE, so it needs no session "
		"and answers for a project that cannot be loaded; nothing is written. The list is empty "
		"for an intact project. Sample paths are resolved the way the engine resolves them, with "
		"one stated difference: 'local:' and an unresolvable legacy relative path resolve against "
		"the project file's own directory rather than the open project's. Copying the media into "
		"a portable bundle is NOT this verb (Bar 3).");
	cmd.argsSchema = objectSchema({{QStringLiteral("project"), stringProperty()}},
		{QStringLiteral("project")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("format"), stringProperty()},
		{QStringLiteral("digest"), stringProperty()},
		{QStringLiteral("reference_count"), integerProperty()},
		{QStringLiteral("missing_count"), integerProperty()},
		{QStringLiteral("present_count"), integerProperty()},
		{QStringLiteral("unresolved_count"), integerProperty()},
		{QStringLiteral("embedded_count"), integerProperty()},
		{QStringLiteral("referenced_elements"), arrayProperty()},
		{QStringLiteral("references"), arrayProperty()},
		{QStringLiteral("missing"), arrayProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		QString project;
		ControlResult error;
		if (!projectArg(args, &project, &error)) { return error; }

		ProjectAssetScan scan;
		if (!controlScanProjectAssets(project, &scan, &error)) { return error; }

		QJsonObject result = scanHeader(scan);
		QJsonArray elements;
		for (const QString& tag : {QStringLiteral("sampleclip"), QStringLiteral("audiofileprocessor"),
				QStringLiteral("sf2player"), QStringLiteral("session clip")})
		{
			elements.append(tag);
		}
		result.insert(QStringLiteral("referenced_elements"), elements);
		result.insert(QStringLiteral("references"), referencesJson(scan, false));
		result.insert(QStringLiteral("missing"), missingJson(scan));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// project.hash_assets
// ---------------------------------------------------------------------------

void registerProjectHashAssets(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.hash_assets");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("hash_assets");
	cmd.description = QStringLiteral("The same reference list as project.missing_assets, plus a "
		"sha256 and a size per reference that is on disk - the identity of the media, so 'the "
		"same sample, moved' is distinguishable from 'a different sample with the same name' "
		"before anything is rewritten. Also returns one digest over the whole reference set, so "
		"two projects that reference byte-identical media compare equal whatever the files are "
		"named. Reads only; a reference over the per-file cap (1 GiB) is reported unhashed rather "
		"than quietly skipped.");
	cmd.argsSchema = objectSchema({{QStringLiteral("project"), stringProperty()}},
		{QStringLiteral("project")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("format"), stringProperty()},
		{QStringLiteral("digest"), stringProperty()},
		{QStringLiteral("reference_count"), integerProperty()},
		{QStringLiteral("hashed_count"), integerProperty()},
		{QStringLiteral("unhashed_present"), integerProperty()},
		{QStringLiteral("missing_count"), integerProperty()},
		{QStringLiteral("embedded_count"), integerProperty()},
		{QStringLiteral("references"), arrayProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		QString project;
		ControlResult error;
		if (!projectArg(args, &project, &error)) { return error; }

		ProjectAssetScan scan;
		if (!controlHashProjectAssets(project, &scan, &error)) { return error; }

		int hashed = 0;
		int unhashedPresent = 0;
		for (const ProjectAssetReference& ref : scan.references)
		{
			if (ref.hashed) { ++hashed; }
			else if (ref.exists) { ++unhashedPresent; }
		}
		QJsonObject result = scanHeader(scan);
		result.insert(QStringLiteral("references"), referencesJson(scan, true));
		result.insert(QStringLiteral("hashed_count"), hashed);
		result.insert(QStringLiteral("unhashed_present"), unhashedPresent);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

// ---------------------------------------------------------------------------
// project.relink
// ---------------------------------------------------------------------------

/*! The recorded inverse of a project-file rewrite: the revision the command
 *  replaced, put back byte for byte; the redo half re-writes what this command
 *  wrote, so a redo is the command again rather than a dropped step. */
void recordProjectFileWrite(const QString& path, const QByteArray& previous,
	const QByteArray& written)
{
	addUndoStep(
		[path, previous]() {
			ControlResult ignored;
			controlWriteFileBytes(path, previous, true, &ignored);
		},
		[path, written]() {
			ControlResult ignored;
			controlWriteFileBytes(path, written, true, &ignored);
		});
}

void registerProjectRelink(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.relink");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("relink");
	cmd.description = QStringLiteral("Point the references of a project file whose value is "
		"'from' - as stored, or resolved to a path - at the file 'to', so the project finds its "
		"media again. 'from' is a value project.missing_assets reported; pass "
		"'expect_sha256' to refuse unless the file really is that media (the hash "
		"project.hash_assets reported). dry_run previews the change and writes nothing. The "
		"document is re-serialised with the product's own serialiser and no other attribute, "
		"element or the root's name is touched. Reversible: one recorded action checkpoint holds "
		"the previous bytes of the project file, so one control.undo restores it byte for byte. "
		"The media is not copied anywhere - the portable-bundle half is Bar 3 and is not this "
		"verb.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("from"), stringProperty()},
		{QStringLiteral("to"), stringProperty()},
		{QStringLiteral("expect_sha256"), stringProperty()},
		{QStringLiteral("dry_run"), booleanProperty()},
	}, {QStringLiteral("project"), QStringLiteral("from"), QStringLiteral("to")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("file"), stringProperty()},
		{QStringLiteral("replaced"), integerProperty()},
		{QStringLiteral("before"), arrayProperty()},
		{QStringLiteral("after"), arrayProperty()},
		{QStringLiteral("stored_value"), stringProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("dry_run"), booleanProperty()},
		{QStringLiteral("bytes"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		QString project;
		ControlResult error;
		if (!projectArg(args, &project, &error)) { return error; }

		const QString from = args.value(QStringLiteral("from")).toString();
		const QString to = args.value(QStringLiteral("to")).toString();
		const QString expectSha = args.value(QStringLiteral("expect_sha256")).toString();
		const bool dryRun = args.value(QStringLiteral("dry_run")).toBool(false);

		// The ADDRESS arguments are checked BEFORE the project file is read. The
		// read below exists to capture the recorded inverse's bytes, but it must
		// not decide what a junk 'from'/'to' means: with dry_run nothing is read
		// and an empty 'from' already answered invalid_args, while without it the
		// missing project answered not_found first - the same arguments, two
		// different kinds, because of a preview flag. One check, one answer.
		ControlResult addressError;
		if (!controlRelinkAddressOk(from, to, &addressError)) { return addressError; }

		// The recorded inverse is the project file's PREVIOUS BYTES, so they are
		// captured before the write - and the command refuses rather than run
		// without an inverse in an instance that has no journal to record one
		// in (SPEC A16: an inverse the engine cannot keep must not be claimed).
		QByteArray previous;
		if (!dryRun)
		{
			if (Engine::projectJournal() == nullptr)
			{
				return ControlResult::failure(ControlErrorKind::Refused,
					QStringLiteral("this instance has no project journal, so a relink could not "
						"be undone; fallback: keep a copy of %1 and edit it with an external "
						"tool").arg(project));
			}
			if (!controlReadFileBytes(project, &previous, &error)) { return error; }
		}

		ProjectAssetRelink relink;
		if (!controlRelinkProjectAsset(project, from, to, expectSha, dryRun, &relink, &error))
		{
			return error;
		}

		QJsonObject result;
		result.insert(QStringLiteral("file"), project);
		result.insert(QStringLiteral("replaced"), relink.replaced);
		result.insert(QStringLiteral("before"), QJsonArray::fromStringList(relink.before));
		result.insert(QStringLiteral("after"), QJsonArray::fromStringList(relink.after));
		result.insert(QStringLiteral("stored_value"), relink.after.isEmpty()
			? QString() : relink.after.first());
		result.insert(QStringLiteral("sha256"), relink.sha256);
		result.insert(QStringLiteral("dry_run"), dryRun);
		result.insert(QStringLiteral("bytes"), relink.bytes.size());

		if (dryRun)
		{
			result.insert(QStringLiteral("__transaction"), transactionPayload(
				QJsonObject{{QStringLiteral("previous_sha256"), QString()},
					{QStringLiteral("path"), project}},
				QStringLiteral("project.relink"), QJsonObject(), false,
				QStringLiteral("dry_run preview: nothing was written")));
			return ControlResult::success(result);
		}

		recordProjectFileWrite(project, previous, relink.bytes);
		result.insert(QStringLiteral("__transaction"), transactionPayload(
			QJsonObject{{QStringLiteral("path"), project},
				{QStringLiteral("previous_bytes"), previous.size()},
				{QStringLiteral("previous_sha256"), controlSha256OfBytes(previous)},
				{QStringLiteral("previous_content"), QString::fromUtf8(previous.left(
					ControlSnapshotLimit))},
				{QStringLiteral("previous_truncated"), previous.size() > ControlSnapshotLimit}},
			QStringLiteral("project.relink"),
			QJsonObject{{QStringLiteral("project"), project},
				{QStringLiteral("note"), QStringLiteral("a relink is undone through "
					"control.undo, which restores the file's previous bytes; a project that "
					"needs its media moved instead is the portable bundle (Bar 3)")}},
			true, QString::fromLatin1(kRelinkMechanism)));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerProjectArchiveCommands(ControlRegistry& registry)
{
	registerProjectMissingAssets(registry);
	registerProjectHashAssets(registry);
	registerProjectRelink(registry);
}

} // namespace lmms
