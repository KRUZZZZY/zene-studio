/*
 * ControlCommandsStemModel.cpp - the model-store verbs of the `stem.*` group:
 *                                `stem.model_get_state` and
 *                                `stem.model_download`.
 *
 * Split out of ControlCommandsStems.cpp for the reason this tree splits every
 * command group's halves: the file-length ratchet reads a file as a unit, and
 * "the two verbs that describe and fetch the model" is a half. The group's read
 * + job half (stem.get_state, stem.job_start / job_status / job_result /
 * job_cancel) stays in ControlCommandsStems.cpp, and one registration point
 * (registerStemCommands) calls into both.
 *
 * THE POLICY IS THE ENGINE'S, NOT THIS FILE'S: StemModelStore refuses a spec
 * that is not pinned with an HTTPS URL, a SHA-256 and a size. Since owner
 * decision 14 the default spec IS pinned (one commit of the model card's
 * repository), so the default call fetches it, and a stem job fetches it on
 * first use; offline mode (LMMS_STEM_OFFLINE) refuses both. Models are still
 * never bundled, always verified (include/StemSeparation/StemModelStore.h,
 * doc/STEM-SPLIT.md).
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

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ControlStemSupport.h"
#include "ControlVocabulary.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

ControlResult stemModelGetState(const QJsonObject& args)
{
	return ControlResult::success(stemModelState(args.value(QStringLiteral("hash")).toBool()));
}

/*! `stem.model_download`: the store's one policy-preserving entry point.
 *
 *  With no arguments it fetches the pinned default spec (owner decision 14);
 *  with any pin argument it fetches a caller spec that must be pinned in full.
 *  A performing call's transfer BLOCKS the control surface
 *  for its duration: a declared bound, the same category as the child-process
 *  renders, stated in the command's own description, in its A16 row and in
 *  docs/KNOWN-LIMITATIONS.md. A stem job needs none of this: it fetches on its
 *  own worker thread. No registered proof performs a transfer (the proof runs
 *  offline); the refusal paths and the verified-in-place success are what it covers.
 *
 *  Named ...Command because the engine function it wraps has the same name and
 *  an unqualified call would find this one first.
 */
ControlResult stemModelDownloadCommand(const QJsonObject& args)
{
	QJsonObject result;
	QString error;
	const bool ok = control::stemModelDownload(
		args.value(QStringLiteral("url")).toString(),
		args.value(QStringLiteral("sha256")).toString(),
		static_cast<qint64>(args.value(QStringLiteral("size_bytes")).toDouble()),
		args.value(QStringLiteral("name")).toString(),
		args.value(QStringLiteral("dest_dir")).toString(),
		&result,
		&error);
	if (!ok)
	{
		// A URL that is not HTTPS, a spec that is not pinned, and the unpinned
		// default spec are all refusals of POLICY, not malformed calls; only a
		// dest_dir that is not absolute is an argument error.
		const QString destDir = args.value(QStringLiteral("dest_dir")).toString();
		const QString kind = destDir.isEmpty() ? QString()
			: stemRequireAbsolutePath(destDir, QStringLiteral("dest_dir"));
		if (!kind.isEmpty())
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, kind);
		}
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	return ControlResult::success(result);
}

void registerStemModelGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("stem.model_get_state");
	cmd.group = QStringLiteral("stem");
	cmd.verb = QStringLiteral("model_get_state");
	cmd.description = QStringLiteral("The model store's own facts: the directory and path it "
		"resolves (honouring LMMS_STEM_MODEL and LMMS_STEM_MODEL_DIR), whether the file is present "
		"and its size, the spec it would download (`name`, `url`, `sha256`, `size_bytes`, "
		"`license`, `license_url`, `model_card_url`, `pinned`) and whether that spec could be "
		"downloaded at all (`download_allowed` + `download_reason`). The default spec is PINNED "
		"(HTDemucs fp16, MIT, one commit of the model card's repository, SHA-256 and size) and a "
		"stem job fetches it on first use, off the control thread; LMMS_STEM_OFFLINE turns that "
		"off and reports `download_allowed: false`. Models are never bundled with the product. With `hash: true` the "
		"file's SHA-256 is computed and reported (`matches_spec` is null when there is nothing "
		"pinned to compare against) - it is off by default because hashing a 166 MB model must be "
		"a decision, not a side effect of a read.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("hash"), booleanProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("dir"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("present"), booleanProperty()},
		{QStringLiteral("bytes"), numberProperty()},
		{QStringLiteral("spec"), objectProperty()},
		{QStringLiteral("download_allowed"), booleanProperty()},
		{QStringLiteral("download_reason"), stringProperty()},
		{QStringLiteral("env"), objectProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("hash_error"), stringProperty()},
		// null when nothing is pinned to compare against, else the verdict.
		{QStringLiteral("matches_spec"), nullable(booleanProperty())},
	});
	cmd.handler = [](const QJsonObject& args) { return stemModelGetState(args); };
	registry.registerCommand(cmd);
}

void registerStemModelDownload(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("stem.model_download");
	cmd.group = QStringLiteral("stem");
	cmd.verb = QStringLiteral("model_download");
	cmd.description = QStringLiteral("Fetch a model file into the store (default directory, or "
		"`dest_dir`), HTTPS only, verifying the pinned SHA-256 and size BEFORE the file is moved "
		"into place - a partial or mismatched download never replaces a good file. With no "
		"arguments it fetches the store's own pinned default spec; with any of `url`, `sha256` or "
		"`size_bytes` it fetches a caller spec, which must be pinned in full. A file already in "
		"place that verifies against the pinned spec is not fetched again (`fetched: false`), "
		"offline or not; any other call is refused in offline mode (LMMS_STEM_OFFLINE). A stem job does not need this verb: it fetches the default "
		"model itself on first use, on the job's worker thread. DECLARED BOUND: a performing call is a real "
		"network transfer on the control surface's own thread, so the surface does not answer - "
		"`control.ping` included - until it finishes or fails (the same defect the child-process "
		"renders carry, docs/RENDER-CHILD-WAIT.md); the transfer is not exercised by any "
		"registered proof, which runs offline so CI never fetches 166 MB.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("url"), stringProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("size_bytes"), integerProperty(1, 1 << 30)},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("dest_dir"), stringProperty()},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("bytes"), numberProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("verified"), booleanProperty()},
		{QStringLiteral("fetched"), booleanProperty()},
		{QStringLiteral("model_card_url"), stringProperty()},
	});
	cmd.handler = [](const QJsonObject& args) { return stemModelDownloadCommand(args); };
	registry.registerCommand(cmd);
}
} // namespace

void registerStemModelCommands(ControlRegistry& registry)
{
	registerStemModelGetState(registry);
	registerStemModelDownload(registry);
}

} // namespace lmms
