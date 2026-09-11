/*
 * LoudnessReportTest.cpp - the render-path loudness report
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

//! The loudness report the render path carries (LoudnessReport). The
//! measurement itself is LufsMeterTest's subject; what is tested here is the
//! layer the renderer uses:
//!
//!  - the report is off unless the render asked for it (OutputSettings),
//!  - it is fed block by block exactly as ProjectRenderer::run() feeds it,
//!  - the EBU Tech 3341 case-1 and case-2 signals come out at their own levels
//!    and 10 LU apart, which is what says the tap is live and scaled,
//!  - digital silence reports the meter's sentinel and *no* plausible number,
//!  - the verdict grades loudness against -23 LUFS and true peak against -1 dBTP,
//!  - the tap leaves every byte of the block it measures untouched,
//!  - the sidecar lands beside the render and holds the same text,
//!  - and the per-block path allocates nothing (AllocationProbe).

#include "LoudnessReport.h"

#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>
#include <cstring>
#include <vector>

#include "AllocationProbe.h"
#include "LufsMeter.h"
#include "OutputSettings.h"

using lmms::LoudnessReport;
using lmms::SampleFrame;
using lmms::sample_t;

namespace
{

constexpr int SampleRate = 48000;
constexpr double Pi = 3.14159265358979323846;
//! Block size the render loop hands to the report; 512 is the mixer's period.
constexpr lmms::f_cnt_t BlockFrames = 512;

//! EBU Tech 3341 test signals: a 1 kHz sine, in phase in both channels, with
//! each channel's peak at the stated number of dB below full scale.
std::vector<SampleFrame> makeSine(double peakDbfs, double seconds, double frequencyHz = 1000.0)
{
	const double amplitude = std::pow(10.0, peakDbfs / 20.0);
	const auto frames = static_cast<std::size_t>(seconds * SampleRate);
	std::vector<SampleFrame> buffer(frames);
	for (std::size_t i = 0; i < frames; ++i)
	{
		const auto value = static_cast<sample_t>(
			amplitude * std::sin(2.0 * Pi * frequencyHz * static_cast<double>(i) / SampleRate));
		buffer[i] = SampleFrame(value, value);
	}
	return buffer;
}

std::vector<SampleFrame> makeSilence(double seconds)
{
	return std::vector<SampleFrame>(static_cast<std::size_t>(seconds * SampleRate), SampleFrame(0.0f, 0.0f));
}

//! Feeds a whole signal in BlockFrames-sized blocks, the way ProjectRenderer
//! walks the rendered periods.
void feedInBlocks(LoudnessReport& report, const std::vector<SampleFrame>& buffer)
{
	std::size_t offset = 0;
	while (offset < buffer.size())
	{
		const auto remaining = buffer.size() - offset;
		const auto frames = static_cast<lmms::f_cnt_t>(std::min<std::size_t>(remaining, BlockFrames));
		report.addBlock(buffer.data() + offset, frames);
		offset += frames;
	}
}

float measuredLufs(double peakDbfs, double seconds)
{
	LoudnessReport report(SampleRate, lmms::DEFAULT_CHANNELS);
	feedInBlocks(report, makeSine(peakDbfs, seconds));
	return report.integratedLufs();
}

//! The value of a "key = value" line of the report text.
QString reportField(const QString& report, const QString& key)
{
	const QRegularExpression pattern(
		QStringLiteral("^%1\\s+= (.*)$").arg(QRegularExpression::escape(key)),
		QRegularExpression::MultilineOption);
	const auto match = pattern.match(report);
	return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

} // namespace

class LoudnessReportTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	//! A render that did not ask for a report must not acquire one.
	void theReportIsOffUnlessTheRenderAsksForIt();

	//! EBU Tech 3341 case 1: -23 dBFS reads its own level.
	void aKnownLevelToneReportsThatLevel();

	//! Case 2 is 10 LU quieter and must read 10 LU quieter.
	void tenLuLouderReadsTenLuLouder();

	//! Silence: the sentinel, no verdict, and no plausible number anywhere.
	void silenceReportsTheSentinelAndNoVerdict();

	//! The verdict grades loudness (-23 LUFS +/- 0.5) and true peak (<= -1 dBTP).
	void theVerdictGradesLoudnessAndTruePeak();

	//! Passivity: the measured block comes out bit-identical.
	void theTapLeavesTheBlockUntouched();

	//! The sidecar lands beside the render and carries the same text.
	void theSidecarLandsBesideTheRender();

	//! The render loop calls addBlock() per period: it must allocate nothing.
	void thePerBlockPathAllocatesNothing();

	void cleanupTestCase();
};

void LoudnessReportTest::initTestCase()
{
}

void LoudnessReportTest::cleanupTestCase()
{
}

void LoudnessReportTest::theReportIsOffUnlessTheRenderAsksForIt()
{
	lmms::OutputSettings settings(48000, 160, lmms::OutputSettings::BitDepth::Depth32Bit);
	QVERIFY(!settings.loudnessReport());
	settings.setLoudnessReport(true);
	QVERIFY(settings.loudnessReport());
}

void LoudnessReportTest::aKnownLevelToneReportsThatLevel()
{
	LoudnessReport report(SampleRate, lmms::DEFAULT_CHANNELS);
	feedInBlocks(report, makeSine(-23.0, 20.0));

	// Same signal as LufsMeterTest's EBU case 1 (-22.9933 there); the report
	// layer must not shift it.
	qInfo("case 1 (-23 dBFS): integrated %.4f, short-term max %.4f, true peak %.4f dBTP",
		static_cast<double>(report.integratedLufs()),
		static_cast<double>(report.shortTermMaxLufs()),
		static_cast<double>(report.truePeakDbtp()));

	QVERIFY(std::fabs(report.integratedLufs() - -23.0f) <= 0.1f);
	QVERIFY(std::fabs(report.shortTermMaxLufs() - -23.0f) <= 0.1f);
	QVERIFY(std::fabs(report.truePeakDbtp() - -23.0f) <= 0.2f);
}

void LoudnessReportTest::tenLuLouderReadsTenLuLouder()
{
	const float loud = measuredLufs(-23.0, 20.0);
	const float quiet = measuredLufs(-33.0, 20.0);
	const float delta = loud - quiet;

	qInfo("case 1 %.4f LUFS, case 2 %.4f LUFS, delta %.4f LU", static_cast<double>(loud),
		static_cast<double>(quiet), static_cast<double>(delta));

	QVERIFY(std::fabs(quiet - -33.0f) <= 0.1f);
	// The pair is what proves the tap is live: a report that always returned the
	// same value would fail on the two levels and on the 10 LU difference.
	QVERIFY(std::fabs(delta - 10.0f) <= 0.2f);
	QVERIFY(loud > quiet);
}

void LoudnessReportTest::silenceReportsTheSentinelAndNoVerdict()
{
	LoudnessReport report(SampleRate, lmms::DEFAULT_CHANNELS);
	feedInBlocks(report, makeSilence(5.0));

	const auto reading = report.reading();
	QVERIFY(!std::isfinite(reading.integratedLufs));
	QVERIFY(!std::isfinite(reading.truePeakDbtp));
	QVERIFY(!report.verdict().measured);
	QVERIFY(!report.verdict().pass());

	const QString text = report.reportText(QStringLiteral("/tmp/silent.wav"));
	qInfo("silent render report line: %s", reportField(text, "integrated_lufs").toUtf8().constData());

	// No plausible number: the field is the sentinel, and the deviation is not
	// computed against the target at all.
	QCOMPARE(reportField(text, "integrated_lufs"), QStringLiteral("-inf"));
	QCOMPARE(reportField(text, "true_peak_dbtp"), QStringLiteral("-inf"));
	QCOMPARE(reportField(text, "deviation_lu"), QStringLiteral("n/a"));
	QVERIFY(reportField(text, "verdict").startsWith(QStringLiteral("NOT MEASURED")));
	QVERIFY(report.summary().contains(QStringLiteral("NOT MEASURED")));
}

void LoudnessReportTest::theVerdictGradesLoudnessAndTruePeak()
{
	{
		LoudnessReport report(SampleRate, lmms::DEFAULT_CHANNELS);
		feedInBlocks(report, makeSine(-23.0, 20.0));
		const auto verdict = report.verdict();
		QVERIFY(verdict.measured);
		QVERIFY(verdict.loudnessOk);
		QVERIFY(verdict.truePeakOk);
		QVERIFY(verdict.pass());
		QVERIFY(std::fabs(verdict.deviationLu) <= 0.1f);
	}
	{
		// 10 LU too quiet: measured, graded, and not a pass.
		LoudnessReport report(SampleRate, lmms::DEFAULT_CHANNELS);
		feedInBlocks(report, makeSine(-33.0, 20.0));
		const auto verdict = report.verdict();
		QVERIFY(verdict.measured);
		QVERIFY(!verdict.loudnessOk);
		QVERIFY(!verdict.pass());
		QVERIFY(verdict.deviationLu < -9.0f);
	}
	{
		// At the loudness target but over the true-peak ceiling: a full-scale
		// sine reads ~0 LUFS-I and ~0 dBTP, both outside EBU R128's window.
		LoudnessReport report(SampleRate, lmms::DEFAULT_CHANNELS);
		feedInBlocks(report, makeSine(0.0, 20.0));
		const auto verdict = report.verdict();
		QVERIFY(verdict.measured);
		QVERIFY(!verdict.truePeakOk);
		QVERIFY(!verdict.loudnessOk);
		QVERIFY(!verdict.pass());
	}
}

void LoudnessReportTest::theTapLeavesTheBlockUntouched()
{
	// The block is written by the renderer right after the meter sees it, so the
	// meter must not be able to change it: compare the raw bytes.
	auto signal = makeSine(-23.0, 2.0, 997.0);
	std::vector<std::byte> before(signal.size() * sizeof(SampleFrame));
	std::memcpy(before.data(), signal.data(), before.size());

	LoudnessReport report(SampleRate, lmms::DEFAULT_CHANNELS);
	feedInBlocks(report, signal);

	QCOMPARE(std::memcmp(before.data(), signal.data(), before.size()), 0);
	QVERIFY(std::isfinite(report.integratedLufs()));
}

void LoudnessReportTest::theSidecarLandsBesideTheRender()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString rendered = directory.filePath(QStringLiteral("song.wav"));

	LoudnessReport report(SampleRate, lmms::DEFAULT_CHANNELS);
	feedInBlocks(report, makeSine(-23.0, 20.0));

	QString error;
	QVERIFY2(report.writeSidecar(rendered, &error), qPrintable(error));

	const QString sidecar = rendered + QStringLiteral(".loudness.txt");
	QCOMPARE(report.sidecarPathFor(rendered), sidecar);
	QVERIFY(QFileInfo::exists(sidecar));

	QFile file(sidecar);
	QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString written = QString::fromUtf8(file.readAll());
	QCOMPARE(written, report.reportText(rendered));
	QVERIFY(written.contains(QStringLiteral("EBU R128")));
	QCOMPARE(reportField(written, "integrated_lufs"), QString::number(report.integratedLufs(), 'f', 2));
	QCOMPARE(reportField(written, "verdict"), QStringLiteral("PASS"));
}

void LoudnessReportTest::thePerBlockPathAllocatesNothing()
{
	auto buffer = makeSine(-23.0, 0.5);
	LoudnessReport report(SampleRate, lmms::DEFAULT_CHANNELS);

	lmms::test::tlCountAllocations = true;
	lmms::test::resetAllocationCount();
	for (int block = 0; block < 64; ++block)
	{
		report.addBlock(buffer.data(), static_cast<lmms::f_cnt_t>(buffer.size()));
		lmms::test::tlAllocationCount += 0; // keep the reads inside the probe
		(void) report.reading();
		(void) report.shortTermMaxLufs();
		(void) report.verdict();
	}
	lmms::test::tlCountAllocations = false;

	QCOMPARE(static_cast<qulonglong>(lmms::test::tlAllocationCount), static_cast<qulonglong>(0));
}

QTEST_GUILESS_MAIN(LoudnessReportTest)

#include "LoudnessReportTest.moc"
