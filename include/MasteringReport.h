/*
 * MasteringReport.h - the machine-readable shape of ONE auto-mastering run, and
 *                     the human table the CLI prints for it.
 *
 * Why this exists as its own translation unit. Wave 1 of auto-mastering
 * (docs/AUTO-MASTERING.md, task #610) produces, per run, a source render's own
 * BS.1770-4 readings and one measured row per candidate. Two consumers need
 * that shape and must never disagree about it:
 *
 *   * `zene master ... --report <path>` writes it as JSON, which is the only
 *     machine-readable half of the CLI. The control surface's mastering.run
 *     runs that child process (an in-process render would drive THIS
 *     instance's audio engine - the reason ControlCommandsProject.cpp's
 *     render.render and include/BounceInPlace.h both use a child process) and
 *     reads this document back, so the candidate set an agent sees over the
 *     socket is the run's own measurements rather than a re-measurement of the
 *     files or a parse of a printed table;
 *   * `mastering.list_candidates` publishes the SET those rows are measured
 *     against (MasteringJob::defaultCandidates()).
 *
 * The human printer (printMasteringReport) was moved here VERBATIM from
 * src/core/main.cpp so the two shapes live side by side and main.cpp - an
 * upstream-inherited file this fork may change but not grow - shrinks by the
 * move rather than gaining a second report writer.
 *
 * Nothing here ranks, scores for preference or calls a candidate best: the
 * document is measurements and verdicts against named, cited targets.
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

#ifndef LMMS_MASTERING_REPORT_H
#define LMMS_MASTERING_REPORT_H

#include <QJsonObject>
#include <QString>

#include "MasteringChain.h"
#include "MasteringJob.h"
#include "lmms_export.h"

namespace lmms
{

/*! The four BS.1770-4 readings of one signal, as JSON.
 *
 * A reading the meter could not make (silence, or a signal shorter than one
 * gated block) is reported as JSON null: neither JSON nor a consumer can carry
 * an infinity, and a fabricated number where there is no measurement would be a
 * measurement the meter never made. `lufs_i`, `short_term_max_lufs`,
 * `true_peak_dbtp` and `crest_factor_db` are the keys.
 */
LMMS_EXPORT QJsonObject masteringMetricsJson(const MasteringMetrics& metrics);

/*! One candidate's SETTINGS: its name, the target it is graded against (with
 *  the standard the numbers come from), the dynamics stage's five numbers and
 *  the chain settings they imply (MasteringCandidate::chainSettings()).
 */
LMMS_EXPORT QJsonObject masteringCandidateJson(const MasteringCandidate& candidate);

/*! One candidate's MEASURED row: the metrics, the residual against its own
 *  target, the two verdicts (loudness inside the target's tolerance, measured
 *  true peak at or below the ceiling) and the level-consistency FLAG (a
 *  lane-defined warning, never a standard's verdict).
 */
LMMS_EXPORT QJsonObject masteringCandidateReportJson(const MasteringCandidateReport& report);

/*! The whole run: the candidate rows, how many project renders it cost
 *  (MasteringJob's own counted number), the sample rate, the single source
 *  render's file and own readings, and the sentence that no candidate is
 *  preferred.
 */
LMMS_EXPORT QJsonObject masteringRunReportJson(const MasteringJob& job);

/*! Writes masteringRunReportJson() to \p path as an indented JSON document.
 *  False (with \p error filled) when the file cannot be written or is short.
 *  \p error may be nullptr.
 */
LMMS_EXPORT bool writeMasteringReport(const MasteringJob& job, const QString& path, QString* error);

/*! The one line per candidate the CLI prints: the three readings, the verdict
 *  against that candidate's named target and the file it was written to. Moved
 *  here verbatim from src/core/main.cpp; nothing here ranks the candidates or
 *  picks one - that is not measured.
 */
LMMS_EXPORT void printMasteringReport(const MasteringJob& job);

} // namespace lmms

#endif // LMMS_MASTERING_REPORT_H
