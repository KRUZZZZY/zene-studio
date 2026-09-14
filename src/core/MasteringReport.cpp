/*
 * MasteringReport.cpp - the JSON shape of one auto-mastering run, and the human
 *                      table the CLI prints. See MasteringReport.h for why this
 *                      is a translation unit rather than two copies.
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

#include "MasteringReport.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStringList>

#include <cmath>
#include <cstdio>

namespace lmms
{

namespace
{

/*! One reading as JSON. An unmeasurable reading is null, never a number: the
 *  meter reports -infinity for silence, JSON cannot carry it, and a consumer
 *  that received a substituted 0.0 would read a measurement that was never
 *  made. Every caller of this helper is a measurement field.
 */
QJsonValue readingJson(float value)
{
	if (!std::isfinite(value))
	{
		return QJsonValue(QJsonValue::Null);
	}
	return QJsonValue(static_cast<double>(value));
}

} // namespace

QJsonObject masteringMetricsJson(const MasteringMetrics& metrics)
{
	QJsonObject out;
	out.insert(QStringLiteral("lufs_i"), readingJson(metrics.integratedLufs));
	out.insert(QStringLiteral("short_term_max_lufs"), readingJson(metrics.shortTermMaxLufs));
	out.insert(QStringLiteral("true_peak_dbtp"), readingJson(metrics.truePeakDbtp));
	out.insert(QStringLiteral("crest_factor_db"), readingJson(metrics.crestFactorDb));
	return out;
}

QJsonObject masteringCandidateJson(const MasteringCandidate& candidate)
{
	QJsonObject target;
	target.insert(QStringLiteral("name"), candidate.target.name);
	target.insert(QStringLiteral("integrated_lufs"),
		static_cast<double>(candidate.target.integratedLufs));
	target.insert(QStringLiteral("tolerance_lu"),
		static_cast<double>(candidate.target.toleranceLu));
	target.insert(QStringLiteral("ceiling_dbtp"),
		static_cast<double>(candidate.target.ceilingDbtp));
	target.insert(QStringLiteral("standard"), candidate.target.standard);

	QJsonObject dynamics;
	dynamics.insert(QStringLiteral("enabled"), candidate.dynamicsEnabled);
	dynamics.insert(QStringLiteral("threshold_db"),
		static_cast<double>(candidate.dynamicsThresholdDb));
	dynamics.insert(QStringLiteral("ratio"), static_cast<double>(candidate.dynamicsRatio));
	dynamics.insert(QStringLiteral("attack_ms"),
		static_cast<double>(candidate.dynamicsAttackMs));
	dynamics.insert(QStringLiteral("release_ms"),
		static_cast<double>(candidate.dynamicsReleaseMs));
	dynamics.insert(QStringLiteral("knee_db"), static_cast<double>(candidate.dynamicsKneeDb));

	// The chain settings the candidate implies, from the engine's OWN mapping
	// (MasteringCandidate::chainSettings) rather than a second copy of it: what
	// the command reports is what MasteringChain::process() was given.
	const MasteringChainSettings chain = candidate.chainSettings();
	QJsonObject chainJson;
	chainJson.insert(QStringLiteral("target_lufs"), static_cast<double>(chain.targetLufs));
	chainJson.insert(QStringLiteral("ceiling_dbtp"), static_cast<double>(chain.ceilingDbtp));
	chainJson.insert(QStringLiteral("dynamics_enabled"), chain.dynamicsEnabled);
	chainJson.insert(QStringLiteral("dynamics_threshold_db"),
		static_cast<double>(chain.dynamicsThresholdDb));
	chainJson.insert(QStringLiteral("dynamics_ratio"), static_cast<double>(chain.dynamicsRatio));
	chainJson.insert(QStringLiteral("dynamics_attack_ms"),
		static_cast<double>(chain.dynamicsAttackMs));
	chainJson.insert(QStringLiteral("dynamics_release_ms"),
		static_cast<double>(chain.dynamicsReleaseMs));
	chainJson.insert(QStringLiteral("dynamics_knee_db"),
		static_cast<double>(chain.dynamicsKneeDb));

	QJsonObject out;
	out.insert(QStringLiteral("name"), candidate.name);
	out.insert(QStringLiteral("target"), target);
	out.insert(QStringLiteral("dynamics"), dynamics);
	out.insert(QStringLiteral("chain_settings"), chainJson);
	return out;
}

QJsonObject masteringCandidateReportJson(const MasteringCandidateReport& report)
{
	QJsonObject target;
	target.insert(QStringLiteral("name"), report.targetName);
	target.insert(QStringLiteral("integrated_lufs"), static_cast<double>(report.targetLufs));
	target.insert(QStringLiteral("tolerance_lu"), static_cast<double>(report.toleranceLu));
	target.insert(QStringLiteral("ceiling_dbtp"), static_cast<double>(report.ceilingDbtp));

	QJsonObject out;
	out.insert(QStringLiteral("name"), report.name);
	out.insert(QStringLiteral("target"), target);
	out.insert(QStringLiteral("metrics"), masteringMetricsJson(report.metrics));
	out.insert(QStringLiteral("loudness_residual_lu"), readingJson(report.lufsResidual));
	out.insert(QStringLiteral("loudness_pass"), report.lufsPass);
	out.insert(QStringLiteral("true_peak_pass"), report.truePeakPass);
	out.insert(QStringLiteral("short_term_flag"), report.shortTermWarn);
	out.insert(QStringLiteral("file"), report.outputFile);
	return out;
}

QJsonObject masteringRunReportJson(const MasteringJob& job)
{
	QJsonArray candidates;
	for (const MasteringCandidateReport& report : job.reports())
	{
		candidates.append(masteringCandidateReportJson(report));
	}

	QJsonObject out;
	out.insert(QStringLiteral("candidate_count"), candidates.size());
	out.insert(QStringLiteral("candidates"), candidates);
	// The design's whole claim, and it is COUNTED by the render machinery
	// (ProjectRenderer::renderCount) rather than by the job's bookkeeping.
	out.insert(QStringLiteral("render_count"), job.renderCount());
	out.insert(QStringLiteral("sample_rate"), static_cast<int>(job.sampleRate()));
	out.insert(QStringLiteral("source_render_file"), job.sourceRenderFile());
	out.insert(QStringLiteral("source"), masteringMetricsJson(job.sourceMetrics()));
	out.insert(QStringLiteral("format"), QStringLiteral("wav"));
	out.insert(QStringLiteral("note"),
		QStringLiteral("measurements only: every candidate is scored against its own named "
			"target (EBU R 128's published -23 LUFS-I +/- 0.5 LU and -1 dBTP, or a -14 LUFS "
			"streaming CONVENTION with a tolerance this project states). Nothing here ranks "
			"the candidates, scores a preference or calls one best - no validated preference "
			"scorer exists for master variants of one song, which is why the choice is the "
			"user's (docs/AUTO-MASTERING.md section 8)"));
	return out;
}

bool writeMasteringReport(const MasteringJob& job, const QString& path, QString* error)
{
	QString sink;
	if (error == nullptr)
	{
		error = &sink;
	}
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		*error = QStringLiteral("cannot write the mastering report to %1").arg(path);
		return false;
	}
	const QByteArray bytes = QJsonDocument(masteringRunReportJson(job))
		.toJson(QJsonDocument::Indented);
	if (file.write(bytes) != bytes.size())
	{
		*error = QStringLiteral("writing the mastering report to %1 failed").arg(path);
		return false;
	}
	return true;
}

// Print one line per candidate: the three BS.1770-4 readings the task asks for,
// the verdict against that candidate's named target, and the file it was written
// to. Nothing here ranks the candidates or picks one - that is not measured.
void printMasteringReport( const MasteringJob& job )
{
	const auto& source = job.sourceMetrics();
	printf( "\nAuto-mastering: %d candidates from %d project render\n",
		static_cast<int>( job.reports().size() ), job.renderCount() );
	printf( "one render: %s\n", job.sourceRenderFile().toUtf8().constData() );
	printf( "%-26s %-10s %8s %8s %8s  %s\n",
		"candidate", "target", "LUFS-I", "ST-max", "dBTP", "verdict" );
	printf( "%-26s %-10s %8.2f %8.2f %8.2f  %s\n", "source (no mastering)", "-",
		source.integratedLufs, source.shortTermMaxLufs, source.truePeakDbtp, "-" );

	for( const auto& report : job.reports() )
	{
		QStringList issues;
		if( !report.lufsPass )
		{
			issues << QStringLiteral( "loudness %1 off target" )
				.arg( report.lufsResidual, 0, 'f', 2 );
		}
		if( !report.truePeakPass )
		{
			issues << QStringLiteral( "over ceiling" );
		}
		if( report.shortTermWarn )
		{
			issues << QStringLiteral( "short-term flag" );
		}
		const QString verdict = issues.isEmpty() ? QStringLiteral( "pass" )
			: QStringLiteral( "warn: " ) + issues.join( QStringLiteral( ", " ) );
		printf( "%-26s %-10s %8.2f %8.2f %8.2f  %s\n",
			report.name.toUtf8().constData(), report.targetName.toUtf8().constData(),
			report.metrics.integratedLufs, report.metrics.shortTermMaxLufs,
			report.metrics.truePeakDbtp, verdict.toUtf8().constData() );
		printf( "%-26s %s\n", "", report.outputFile.toUtf8().constData() );
	}
	printf( "\nNo candidate is preferred: the readings above are the measurements, "
		"the choice is the user's.\n" );
}

} // namespace lmms
