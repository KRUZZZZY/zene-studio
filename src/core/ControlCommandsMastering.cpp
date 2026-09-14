/*
 * ControlCommandsMastering.cpp - the READ half of the `mastering.*` command
 *                                group (SPEC A11-A16): mastering.list_candidates
 *                                and mastering.get_state.
 *
 * Feature rows 25 and 72 of docs/FEATURE-LIST-0.3.0.md. The engine half of
 * auto-mastering wave 1 (docs/AUTO-MASTERING.md, task #610) is in the tree and
 * proven: MasteringJob's render-once/branch-many candidate generation and its
 * objective scoring against named targets (include/MasteringJob.h,
 * src/core/MasteringJob.cpp), the chain and the BS.1770-4 measurement it reports
 * through (include/MasteringChain.h), the CLI action `zene master`
 * (src/core/main.cpp), and the end-to-end ctest MasteringTest. What was missing
 * was the SURFACE: AGENT-TOOLING.md section 1 makes "a feature with no command"
 * a defect, and the audit's Table B counts this one of the sixteen features that
 * are in the tree and not drivable through the socket.
 *
 * WHAT THIS FILE REGISTERS, and what it deliberately does not:
 *
 *  * `mastering.list_candidates` - the CANDIDATE SET wave 1 generates
 *    (MasteringJob::defaultCandidates(), the engine's own set, read here rather
 *    than restated): each candidate's target with the published document its
 *    numbers come from, the dynamics stage's five numbers and the chain settings
 *    they imply. No render, no write.
 *  * `mastering.get_state` - the LAST run this instance performed: the report the
 *    run itself produced, and the live state of the files it wrote. Before any
 *    run it reports has_run false rather than an invented empty set.
 *
 *  * There is NO per-candidate override verb and no ranking verb. Wave 1's
 *    candidate generation IS the engine's set, and the doc's own section 8
 *    records why nothing ranks: no validated preference scorer exists for master
 *    variants of one song (the feasibility study measures FAD/CLAP rank
 *    correlation at 0.14 against 0.62 for human judgement), so a ranker is wave
 *    3 and is gated on real user pick-logs, which do not exist yet. This group
 *    therefore publishes measurements and never an order.
 *
 * The mutating verb is in ControlCommandsMasteringRun.cpp (the automation, warp,
 * vca and chain-preset groups' read/edit split). The contract rows are in
 * ControlReversibilityTableMastering.cpp, the proof is the registered ctest
 * ControlMasteringCommands (tests/control-mastering-commands.py) plus the
 * extended tests/src/core/MasteringTest.cpp, and the UI absence is in
 * docs/KNOWN-LIMITATIONS.md and docs/RELEASE-NOTES-v0.3.0-alpha.md.
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

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include "ControlMasteringSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"

#include "Engine.h"
#include "MasteringJob.h"
#include "MasteringReport.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The candidate set wave 1 generates, in the engine's own order, with the
//! chain settings each one implies.
QJsonArray candidateSetJson()
{
	QJsonArray candidates;
	for (const MasteringCandidate& candidate : MasteringJob::defaultCandidates())
	{
		candidates.append(masteringCandidateJson(candidate));
	}
	return candidates;
}

/*! Where the wave-1 candidate set comes from and what it is NOT. The numbers
 *  are quoted from the engine's own records (MasteringJob::defaultCandidates
 *  carries the same sentences per target), and the absence of a ranking is the
 *  doc's finding rather than this group's choice.
 */
QString candidateSetNote()
{
	return QStringLiteral(
		"wave 1's candidate set, read from the engine (MasteringJob::defaultCandidates) rather "
		"than restated here: it varies target loudness (-14 and -16 LUFS-I, -23 for EBU R 128), "
		"the true-peak ceiling (-1 and -2 dBTP) and the dynamics stage, so the candidates are "
		"objectively distinguishable. Every number is a measurement against a NAMED target: EBU "
		"R 128's published -23 LUFS-I +/- 0.5 LU and -1 dBTP, and a -14 LUFS-I streaming "
		"CONVENTION with the +/- 1.0 LU tolerance this project chose and states (no service "
		"publishes one). NOTHING RANKS THESE CANDIDATES and nothing calls one best: no validated "
		"preference scorer exists for master variants of one song (docs/AUTO-MASTERING.md "
		"section 8), so the choice is the user's. Mastering them writes files: see mastering.run");
}

//! Every output path the last run reported, source render first.
QStringList lastRunFiles(const QJsonObject& report)
{
	QStringList files;
	const QString source = report.value(QStringLiteral("source_render_file")).toString();
	if (!source.isEmpty()) { files.append(source); }
	const QJsonArray candidates = report.value(QStringLiteral("candidates")).toArray();
	for (const QJsonValue& value : candidates)
	{
		const QString file = value.toObject().value(QStringLiteral("file")).toString();
		if (!file.isEmpty()) { files.append(file); }
	}
	return files;
}

ControlResult handleMasteringListCandidates()
{
	const QJsonArray candidates = candidateSetJson();

	QJsonObject result;
	result.insert(QStringLiteral("candidates"), candidates);
	result.insert(QStringLiteral("count"), candidates.size());
	result.insert(QStringLiteral("note"), candidateSetNote());
	return ControlResult::success(result);
}

ControlResult handleMasteringGetState()
{
	const QJsonObject report = masteringLastRun();
	const QStringList files = report.isEmpty() ? QStringList() : lastRunFiles(report);
	const QJsonArray facts = masteringFileFacts(files);

	Song* song = Engine::getSong();

	// Counted from the hashed facts, not asserted: a deleted or replaced output
	// is visible both here and per file.
	int present = 0;
	for (const QJsonValue& value : facts)
	{
		if (value.toObject().value(QStringLiteral("exists")).toBool()) { ++present; }
	}

	QJsonObject result;
	// Before any run this is false and `last_run` is null: an empty candidate
	// list would read as "the run measured nothing", which is a different fact.
	result.insert(QStringLiteral("has_run"), !report.isEmpty());
	result.insert(QStringLiteral("last_run"),
		report.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(report));
	result.insert(QStringLiteral("files"), facts);
	result.insert(QStringLiteral("files_present"), present);
	result.insert(QStringLiteral("session_empty"), song == nullptr || song->isEmpty());
	result.insert(QStringLiteral("note"),
		QStringLiteral("`last_run` is the report the last mastering.run in THIS process produced, "
			"verbatim (the same document the run returns, and the same one the CLI writes with "
			"`zene master ... --report <path>`); `files` is the LIVE state of the files it wrote, "
			"hashed now, so a candidate set that was edited or deleted after the run is visible "
			"as such. It is not project state and is not saved or restored with a project: a "
			"fresh instance has no last run (has_run false). `session_empty` is the pre-flight "
			"fact mastering.run refuses on"));
	return ControlResult::success(result);
}

void registerMasteringListCandidates(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("mastering.list_candidates");
	cmd.group = QStringLiteral("mastering");
	cmd.verb = QStringLiteral("list_candidates");
	cmd.description = QStringLiteral("The mastering candidate set wave 1 generates: each "
		"candidate's name, the delivery target it is graded against (its integrated loudness, "
		"tolerance and true-peak ceiling, plus the published document the numbers come from), "
		"the dynamics stage's five numbers and the chain settings they imply. The set is the "
		"engine's own (MasteringJob::defaultCandidates) - it varies target loudness, ceiling "
		"and dynamics so the candidates are objectively distinguishable. This command writes "
		"nothing and starts no render: mastering.run is what masters and measures.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("candidates"), arrayProperty()},
		{QStringLiteral("count"), integerProperty()},
		{QStringLiteral("note"), stringProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) { return handleMasteringListCandidates(); };
	registry.registerCommand(cmd);
}

void registerMasteringGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("mastering.get_state");
	cmd.group = QStringLiteral("mastering");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("The last auto-mastering run this instance performed: the "
		"report the run itself produced (one measured row per candidate - LUFS-I, the loudest 3 s "
		"window, measured dBTP, crest, the residual against that candidate's target and the two "
		"verdicts - plus the source render's own readings and the counted number of project "
		"renders), and the live state of the files it wrote, hashed now. `has_run` is false and "
		"`last_run` is null before the first run. `session_empty` is the fact mastering.run "
		"refuses on. Read-only.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("has_run"), booleanProperty()},
		{QStringLiteral("last_run"), objectProperty()},
		{QStringLiteral("files"), arrayProperty()},
		{QStringLiteral("files_present"), integerProperty()},
		{QStringLiteral("session_empty"), booleanProperty()},
		{QStringLiteral("note"), stringProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) { return handleMasteringGetState(); };
	registry.registerCommand(cmd);
}

} // namespace

void registerMasteringCommands(ControlRegistry& registry)
{
	registerMasteringListCandidates(registry);
	registerMasteringGetState(registry);
}

} // namespace lmms
