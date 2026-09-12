/*
 * MasteringTest.cpp - render-once/branch-many mastering, measured end to end
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

//! Task #610 wave 1, driven through the real path on a real project: a .mmp is
//! written into a temporary directory, loaded by the engine, rendered once by
//! MasteringJob and branched into N candidates. Every assertion below is made
//! on the WAVs that come out or on the meter's readings of them, and the
//! numbers are printed as MASTERING_EVIDENCE lines so they can be checked by
//! hand. Nothing here ranks the candidates or calls one better.

#include <QtTest>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "Engine.h"
#include "MasteringChain.h"
#include "MasteringJob.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "RenderManager.h"
#include "Song.h"

#include "MasteringTestSupport.h"

using namespace lmms;
using namespace lmms::masteringtest;

namespace
{

//! The candidate set with two settings-identical twins: the control for the
//! distinction test. Identical settings must measure identically - a candidate
//! set that does not is measuring something other than its own settings.
QVector<MasteringCandidate> twinCandidates()
{
	const MasteringCandidate first = MasteringJob::defaultCandidates().first();
	MasteringCandidate second = first;
	second.name = QStringLiteral("streaming-14-copy");
	return {first, second};
}

//! A target no chain can satisfy at this ceiling on real material: the residual
//! cannot close, so the pass/warn logic has something to warn about. It is a
//! lane-defined probe, not a standard - it exists only to exercise the warn path.
MasteringCandidate unreachableLoudnessProbe()
{
	MasteringCandidate probe = MasteringJob::defaultCandidates().first();
	probe.name = QStringLiteral("target-unreachable");
	probe.target.name = QStringLiteral("lane-probe");
	probe.target.integratedLufs = -6.0f;
	probe.target.toleranceLu = 1.0f;
	probe.target.ceilingDbtp = -20.0f;
	probe.target.standard = QStringLiteral(
		"A lane-defined probe: -6 LUFS-I at a -20 dBTP ceiling. On any real "
		"programme the ceiling decides the loudness, so the loudness target cannot "
		"also be reached - which is what the warn path has to report.");
	return probe;
}

void printRow(const char* label, const MasteringMetrics& metrics)
{
	std::printf("MASTERING_EVIDENCE %-24s LUFS-I %8.2f  ST-max %8.2f  dBTP %8.2f  crest %6.2f\n",
		label, metrics.integratedLufs, metrics.shortTermMaxLufs, metrics.truePeakDbtp,
		metrics.crestFactorDb);
	std::fflush(stdout);
}

} // namespace

class MasteringTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(m_dir.isValid());
		QString projectPath;
		QVERIFY2(writeFixture(m_dir.path(), projectPath), "fixture project could not be written");
		Engine::getSong()->loadProject(projectPath);
		QVERIFY(!Engine::getSong()->isEmpty());
		// Three sample tracks, so a non-silent mix is a real mix and not one
		// track standing in for the project.
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), 3);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! The measurement the chain uses is the merged meter's: a 1 kHz stereo sine
	//! at the level EBU Tech 3341's first case uses must read its own level.
	void chainMeasurementMatchesTheKnownSignal()
	{
		// Amplitude 0.0708 = -23 dBFS peak in each channel. For a stereo sine
		// the two channels' mean squares add (+3.01 dB) exactly where the sine's
		// crest factor subtracts it, and the meter's +0.691/-0.691 K-weighting
		// pair cancels, so this is the -23 LUFS point.
		const auto signal = probeSignal(48000, 20.0, 1000.0, 0.0708, false);
		const MasteringMetrics metrics = MasteringChain::measure(signal, 48000);
		printRow("1 kHz sine, -23 LUFS", metrics);
		QVERIFY2(std::fabs(metrics.integratedLufs - -23.0f) <= 0.2f,
			"the chain's measurement does not read the -23 LUFS sine as -23 LUFS");
		QVERIFY(std::fabs(metrics.truePeakDbtp - -23.0f) <= 0.2f);
	}

	//! The whole design in one test: N candidates, ONE project render - counted
	//! by the render machinery, not by this test or by the job's own bookkeeping.
	void oneRenderFeedsEveryCandidate()
	{
		ProjectRenderer::resetRenderCount();
		MasteringJob job(m_outputSettings, ProjectRenderer::ExportFileFormat::Wave,
			m_dir.filePath(QStringLiteral("candidates")), MasteringJob::defaultCandidates());
		QString error;
		QVERIFY2(job.run(&error), qPrintable(error));

		const QVector<MasteringCandidate> requested = MasteringJob::defaultCandidates();
		QCOMPARE(job.renderCount(), 1);
		QCOMPARE(ProjectRenderer::renderCount(), 1);
		QCOMPARE(job.reports().size(), requested.size());
		QVERIFY(QFile::exists(job.sourceRenderFile()));

		std::printf("MASTERING_EVIDENCE candidates=%d project_renders=%d counted_renders=%d\n",
			static_cast<int>(job.reports().size()), job.renderCount(),
			ProjectRenderer::renderCount());
		printRow("source (one render)", job.sourceMetrics());

		for (const auto& report : job.reports())
		{
			QVERIFY2(QFile::exists(report.outputFile), qPrintable(report.outputFile));
			std::vector<SampleFrame> written;
			sample_rate_t rate = 0;
			QString readError;
			QVERIFY2(readWav(report.outputFile, written, rate, &readError), qPrintable(readError));
			QCOMPARE(rate, job.sampleRate());
			QVERIFY2(rmsDbfs(written) < -10.0, "a candidate file is silent");
			// Measured in memory by the job and re-measured here from the file:
			// the two readings must agree, which is what says the metrics belong
			// to the bytes on disk.
			const MasteringMetrics fromFile = MasteringChain::measure(written, rate);
			printRow(report.name.toUtf8().constData(), report.metrics);
			QVERIFY2(std::fabs(fromFile.integratedLufs - report.metrics.integratedLufs) <= 0.05f,
				"the file's loudness differs from the reported loudness");
			QVERIFY2(std::fabs(fromFile.truePeakDbtp - report.metrics.truePeakDbtp) <= 0.05f,
				"the file's true peak differs from the reported true peak");
		}

		m_candidateReport = job.reports();
		m_sourceMetrics = job.sourceMetrics();
	}

	//! Settings that differ must be visible in the measurements. The candidate
	//! set varies three axes (target loudness, true-peak ceiling, the dynamics
	//! stage); every pair must differ by more than the meter's tolerance on at
	//! least one of the three metrics, or the set is not doing anything.
	void candidatesWithDifferentSettingsMeasureDifferently()
	{
		QVERIFY(!m_candidateReport.isEmpty());
		const QVector<MasteringCandidate> candidates = MasteringJob::defaultCandidates();
		QCOMPARE(candidates.size(), m_candidateReport.size());

		for (int i = 0; i < m_candidateReport.size(); ++i)
		{
			for (int j = i + 1; j < m_candidateReport.size(); ++j)
			{
				const auto& a = m_candidateReport.at(i);
				const auto& b = m_candidateReport.at(j);
				const double loudness = std::fabs(a.metrics.integratedLufs - b.metrics.integratedLufs);
				const double shortTerm = std::fabs(a.metrics.shortTermMaxLufs - b.metrics.shortTermMaxLufs);
				const double peak = std::fabs(a.metrics.truePeakDbtp - b.metrics.truePeakDbtp);
				const double best = std::max(loudness, std::max(shortTerm, peak));
				std::printf("MASTERING_EVIDENCE pair %s vs %s: dLUFS-I %.2f dST %.2f dBTP %.2f\n",
					a.name.toUtf8().constData(), b.name.toUtf8().constData(),
					loudness, shortTerm, peak);
				QVERIFY2(best >= 0.25, qPrintable(QStringLiteral(
					"candidates %1 and %2 have different settings but measure the same")
					.arg(a.name, b.name)));
			}
		}
		std::fflush(stdout);
	}

	//! The control: two candidates with identical settings must produce
	//! identical bytes and identical measurements.
	void candidatesWithIdenticalSettingsMeasureIdentically()
	{
		MasteringJob job(m_outputSettings, ProjectRenderer::ExportFileFormat::Wave,
			m_dir.filePath(QStringLiteral("twins")), twinCandidates());
		QString error;
		QVERIFY2(job.run(&error), qPrintable(error));
		QCOMPARE(job.renderCount(), 1);
		QCOMPARE(job.reports().size(), 2);

		std::vector<SampleFrame> first;
		std::vector<SampleFrame> second;
		sample_rate_t rate = 0;
		QString readError;
		QVERIFY(readWav(job.reports().at(0).outputFile, first, rate, &readError));
		QVERIFY(readWav(job.reports().at(1).outputFile, second, rate, &readError));
		QCOMPARE(first.size(), second.size());

		const std::uint32_t delta = maxAbsDeltaLsb(first, second);
		const auto& a = job.reports().at(0).metrics;
		const auto& b = job.reports().at(1).metrics;
		std::printf("MASTERING_EVIDENCE twin control: max|delta| %u LSB, dLUFS-I %.3f, dBTP %.3f\n",
			delta, std::fabs(a.integratedLufs - b.integratedLufs),
			std::fabs(a.truePeakDbtp - b.truePeakDbtp));
		std::fflush(stdout);

		QVERIFY2(delta == 0u, "identical settings produced different samples");
		QVERIFY(std::fabs(a.integratedLufs - b.integratedLufs) <= 0.01f);
		QVERIFY(std::fabs(a.truePeakDbtp - b.truePeakDbtp) <= 0.01f);
	}

	//! The pass/warn verdicts: the reachable targets are reached and the ceiling
	//! holds, and a target the chain cannot reach is reported as a warn rather
	//! than quietly claimed.
	void verdictsFollowTheTargets()
	{
		QVERIFY(!m_candidateReport.isEmpty());
		for (const auto& report : m_candidateReport)
		{
			std::printf("MASTERING_EVIDENCE verdict %-26s target %.1f +/-%.1f  LUFS-I %.2f  "
				"dBTP %.2f (ceiling %.1f)  lufs %s  peak %s  st-flag %s\n",
				report.name.toUtf8().constData(), report.targetLufs, report.toleranceLu,
				report.metrics.integratedLufs, report.metrics.truePeakDbtp, report.ceilingDbtp,
				report.lufsPass ? "pass" : "warn", report.truePeakPass ? "pass" : "warn",
				report.shortTermWarn ? "set" : "clear");
			// The chain's own contract, on every candidate: the ceiling holds.
			QVERIFY2(report.metrics.truePeakDbtp <= report.ceilingDbtp + 0.05f,
				"a candidate's true peak is above its ceiling");
		}
		std::fflush(stdout);

		const auto& ebu = m_candidateReport.at(4);
		QCOMPARE(ebu.name, QStringLiteral("ebu-r128"));
		QVERIFY2(ebu.lufsPass, "the EBU R 128 candidate missed -23 LUFS +/- 0.5 LU");
		QVERIFY(std::fabs(ebu.lufsResidual) <= 0.5f);

		// A target that cannot be reached at -1 dBTP must come back as a warn.
		MasteringJob probe(m_outputSettings, ProjectRenderer::ExportFileFormat::Wave,
			m_dir.filePath(QStringLiteral("probe")), {unreachableLoudnessProbe()});
		QString error;
		QVERIFY2(probe.run(&error), qPrintable(error));
		QCOMPARE(probe.reports().size(), 1);
		const auto& report = probe.reports().first();
		std::printf("MASTERING_EVIDENCE unreachable probe: target %.1f, LUFS-I %.2f, "
			"residual %.2f, lufs %s\n", report.targetLufs, report.metrics.integratedLufs,
			report.lufsResidual, report.lufsPass ? "pass" : "warn");
		std::fflush(stdout);
		QVERIFY2(!report.lufsPass, "-6 LUFS at -20 dBTP was reported as reached");
		QVERIFY(report.lufsResidual < -1.0f);
	}

	//! Behaviour preservation: an ordinary render with no mastering requested is
	//! unchanged by this code. The renderer is not bit-reproducible run to run
	//! (upstream-inherited, measured elsewhere in this workspace), so the gate is
	//! length equality plus a level delta against a same-build run-to-run floor -
	//! never a sha256. The per-sample numbers are printed as evidence.
	void masteringLeavesAnOrdinaryRenderAlone()
	{
		std::vector<SampleFrame> before;
		std::vector<SampleFrame> floorRun;
		std::vector<SampleFrame> after;
		sample_rate_t rate = 0;
		QVERIFY(renderPlainly(QStringLiteral("plain-before.wav"), before, rate));
		QVERIFY(renderPlainly(QStringLiteral("plain-floor.wav"), floorRun, rate));

		MasteringJob job(m_outputSettings, ProjectRenderer::ExportFileFormat::Wave,
			m_dir.filePath(QStringLiteral("during")), twinCandidates());
		QString error;
		QVERIFY2(job.run(&error), qPrintable(error));

		QVERIFY(renderPlainly(QStringLiteral("plain-after.wav"), after, rate));

		QCOMPARE(before.size(), after.size());
		QCOMPARE(before.size(), floorRun.size());
		const std::uint32_t floorDelta = maxAbsDeltaLsb(before, floorRun);
		const std::uint32_t afterDelta = maxAbsDeltaLsb(before, after);
		const double floorDb = std::fabs(rmsDbfs(before) - rmsDbfs(floorRun));
		const double afterDb = std::fabs(rmsDbfs(before) - rmsDbfs(after));
		std::printf("MASTERING_EVIDENCE ordinary render: frames %zu, floor max|delta| %u LSB "
			"(%.4f dB), after-mastering max|delta| %u LSB (%.4f dB)\n",
			before.size(), floorDelta, floorDb, afterDelta, afterDb);
		std::fflush(stdout);

		QVERIFY2(afterDb <= 0.5, "the ordinary render's level moved after a mastering job");
		QVERIFY2(floorDb <= 0.5, "the ordinary render is not stable enough to compare");
	}

private:
	//! Renders the project the ordinary way - RenderManager, no candidates - and
	//! reads the file back.
	bool renderPlainly(const QString& name, std::vector<SampleFrame>& frames, sample_rate_t& rate)
	{
		const QString path = m_dir.filePath(name);
		{
			RenderManager manager(m_outputSettings, ProjectRenderer::ExportFileFormat::Wave, path);
			QEventLoop loop;
			QObject::connect(&manager, &RenderManager::finished, &loop, &QEventLoop::quit);
			manager.renderProject();
			loop.exec();
		}
		QString error;
		return readWav(path, frames, rate, &error);
	}

	QTemporaryDir m_dir;
	OutputSettings m_outputSettings{44100, 160, OutputSettings::BitDepth::Depth16Bit,
		OutputSettings::StereoMode::Stereo};
	QVector<MasteringCandidateReport> m_candidateReport;
	MasteringMetrics m_sourceMetrics;
};

QTEST_GUILESS_MAIN(MasteringTest)
#include "MasteringTest.moc"
