/*
 * MasteringJob.h - render-once/branch-many mastering candidate generation
 *
 * Copyright (c) 2026 Zene Studio developers
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
 *
 */

#ifndef LMMS_MASTERING_JOB_H
#define LMMS_MASTERING_JOB_H

#include <QObject>
#include <QString>
#include <QVector>

#include <vector>

#include "MasteringChain.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"

namespace lmms
{

/**
 * The delivery target a candidate is graded against. Both fields are published
 * numbers, named with their source, so the pass/warn verdict is checkable
 * instead of being the tool's opinion.
 */
struct MasteringTarget
{
	//! e.g. "streaming-14" / "ebu-r128".
	QString name;
	//! Target integrated loudness in LUFS-I.
	float integratedLufs = -14.0f;
	//! Pass window around integratedLufs, in LU. EBU R 128 publishes +/- 0.5 LU;
	//! no streaming service publishes a tolerance, so those targets state one.
	float toleranceLu = 1.0f;
	//! Maximum true peak in dBTP (-1.0 for both targets below).
	float ceilingDbtp = -1.0f;
	//! The published document the numbers come from, quoted for the report.
	QString standard;
};

/**
 * One mastering candidate: a target plus the chain settings used to reach it.
 * The candidate set is the product of these settings, nothing else - there is
 * no search and no ranking, so adding a candidate adds an output and never
 * changes a sibling's output.
 */
struct MasteringCandidate
{
	QString name;
	MasteringTarget target;
	bool dynamicsEnabled = false;
	float dynamicsThresholdDb = -20.0f;
	float dynamicsRatio = 2.0f;
	float dynamicsAttackMs = 10.0f;
	float dynamicsReleaseMs = 150.0f;
	float dynamicsKneeDb = 6.0f;

	//! The chain settings implied by this candidate (target loudness + ceiling).
	MasteringChainSettings chainSettings() const;
};

/**
 * One candidate's measured outcome.
 */
struct MasteringCandidateReport
{
	QString name;
	QString targetName;
	float targetLufs = 0.0f;
	float toleranceLu = 0.0f;
	float ceilingDbtp = 0.0f;
	//! Measured from the processed signal, in memory (not re-read from the file).
	MasteringMetrics metrics;
	//! metrics.integratedLufs - targetLufs.
	float lufsResidual = 0.0f;
	bool lufsPass = false;
	bool truePeakPass = false;
	//! Lane-defined level-consistency flag; no published short-term limit is claimed.
	bool shortTermWarn = false;
	QString outputFile;
};

/**
 * Render-once / branch-many mastering.
 *
 * run() renders the project's mix ONCE through the normal render machinery
 * (ProjectRenderer, 32-bit intermediate), loads that one render into memory,
 * and then processes N in-memory copies - one per candidate - writing one
 * file each and measuring each with the BS.1770-4 meter. The value of the
 * shape is that ten candidates do not cost ten project renders, and the
 * measurement is made on the same bytes the file holds.
 *
 * Threading: run() is blocking and executes on the calling (CLI or test)
 * thread. The render itself happens on the ProjectRenderer thread it drives.
 * Nothing here touches the audio callback, and no part of it is reachable
 * from AudioEngine::renderNextPeriod().
 */
class LMMS_EXPORT MasteringJob : public QObject
{
	Q_OBJECT
public:
	MasteringJob(const OutputSettings& outputSettings, ProjectRenderer::ExportFileFormat format,
		const QString& outputDirectory, const QVector<MasteringCandidate>& candidates);

	/**
	 * The wave-1 candidate set, with the numbers each target comes from:
	 *  - `streaming-14`: -14 LUFS-I, -1 dBTP - the loudness the major streaming
	 *    services are documented to normalise to (Spotify's published figure),
	 *    with the -1 dBTP ceiling their delivery guidance gives. No service
	 *    publishes a tolerance, so this target states +/- 1.0 LU and says so.
	 *  - `ebu-r128`: -23 LUFS-I +/- 0.5 LU, -1 dBTP - the EBU R 128 programme
	 *    loudness spec, measured per ITU-R BS.1770-4.
	 * The set varies target loudness, true-peak ceiling and the dynamics stage
	 * so that the candidates are objectively distinguishable; which one a user
	 * prefers is not measured and not claimed.
	 */
	static QVector<MasteringCandidate> defaultCandidates();

	//! Renders once, then branches every candidate off that render. Blocking.
	//! Returns false and fills \p error when the render or a candidate fails.
	bool run(QString* error = nullptr);

	//! The one render's own readings (the programme before any mastering).
	const MasteringMetrics& sourceMetrics() const { return m_sourceMetrics; }
	const QVector<MasteringCandidateReport>& reports() const { return m_reports; }
	//! Project renders performed by this job. The design's whole claim is that
	//! this is 1 whatever the candidate count is; it is counted, not asserted.
	int renderCount() const { return m_renderCount; }
	//! Path of the single source render (kept as evidence, not deleted).
	const QString& sourceRenderFile() const { return m_sourceRenderFile; }
	//! Sample rate of the render, which is the rate every candidate is processed at.
	sample_rate_t sampleRate() const { return m_sampleRate; }

private:
	bool renderSourceMix(QString* error);
	bool loadSourceMix(QString* error);
	MasteringCandidateReport measureCandidate(const MasteringCandidate& candidate,
		const std::vector<SampleFrame>& frames) const;
	bool writeCandidate(const MasteringCandidate& candidate, const std::vector<SampleFrame>& frames,
		const QString& path, QString* error) const;
	QString pathForCandidate(int index, const QString& name) const;

	const OutputSettings m_outputSettings;
	ProjectRenderer::ExportFileFormat m_format;
	QString m_outputDirectory;
	QVector<MasteringCandidate> m_candidates;

	QString m_sourceRenderFile;
	sample_rate_t m_sampleRate = 44100;
	std::vector<SampleFrame> m_source;
	MasteringMetrics m_sourceMetrics;
	QVector<MasteringCandidateReport> m_reports;
	int m_renderCount = 0;
};

} // namespace lmms

#endif // LMMS_MASTERING_JOB_H
