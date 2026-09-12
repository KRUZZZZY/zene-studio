/*
 * StemExportTest.cpp - per-track stem export (RenderManager::exportStems)
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

//! Stem-export acceptance tests.
//!
//! A real project is written to a temporary directory by
//! `stemsupport::writeDemoProject` and rendered through the real render path:
//! Engine, RenderManager, ProjectRenderer, AudioFileWave. Everything asserted
//! below is measured from the WAV files that come out, not from a mock.
//!
//! The four questions:
//!   1. is every stem non-silent, and none of them a copy of the mix?
//!   2. are the stems the same length as the mix, so they line up?
//!   3. does `tailBars` extend every stem, i.e. is a tail not truncated?
//!   4. does a stem export leave the whole-project render unchanged?
//!
//! Two renders inside one live process are not reproducible -- two *untouched*
//! whole-project renders differ by up to 26,204 frames and up to 9,506 LSB in
//! this test's own runs -- so the tolerances here are the renderer's own
//! repeatability rather than the ideal, and the control render is printed beside
//! the measurement. The tight figures (max 2 LSB over all 617,216 frames, energy
//! equality, -74.7 dB below the mix; and a branch run byte-identical to a base
//! run) come from one render per process and live in docs/STEM-EXPORT.md
//! sections 4.2 and 4.4.

#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "StemExportTestSupport.h"

#include "Engine.h"
#include "RenderManager.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace stemsupport;

//! One bar at 120 bpm in 4/4: what the whole-project render has always appended
//! past the project end, and therefore what a default stem carries too.
constexpr int kBarFrames = int(4 * 60.0 / kBpm * kRate);
constexpr int kProjectBars = 6;   // the demo project's content length

class StemExportTest : public QObject
{
	Q_OBJECT

private slots:

	//! Build the project, render the mix once, and prove the samples really
	//! loaded -- otherwise every measurement below would be measuring silence.
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY2(m_dir.isValid(), "no temporary directory for the demo project");
		// The renders are the evidence; keep them for inspection on request.
		if (qEnvironmentVariableIsSet("STEM_EXPORT_KEEP_TMP")) { m_dir.setAutoRemove(false); }
		std::fprintf(stderr, "STEM_EVIDENCE TMP %s\n", qPrintable(m_dir.path()));
		std::fflush(stderr);

		m_project = writeDemoProject(m_dir.path());
		QVERIFY2(!m_project.isEmpty(), "could not write the demo project");

		Engine::getSong()->loadProject(m_project);
		QVERIFY2(!Engine::getSong()->isEmpty(), "the demo project did not load");
		QCOMPARE(int(Engine::getSong()->tracks().size()), 3);

		int clips = 0;
		for (auto* track : Engine::getSong()->tracks())
		{
			for (auto* clip : track->getClips())
			{
				auto* sampleClip = dynamic_cast<SampleClip*>(clip);
				QVERIFY(sampleClip != nullptr);
				QVERIFY2(sampleClip->sample().sampleSize() > 0,
						qPrintable("sample did not load: " + sampleClip->sampleFile()));
				++clips;
			}
		}
		QCOMPARE(clips, 4);

		m_mixPath = QDir(m_dir.path()).filePath("mix.wav");
		QVERIFY(!renderMix(m_mixPath).isEmpty());
		m_mix = readWav(m_mixPath);
		QVERIFY2(m_mix.frameCount() > 0, "the whole-project render produced no audio");
		printTrack("MIX ", m_mix, "mix");
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! The naming contract: `<index>_<name><ext>`, the index numbered in track
	//! order, zero-padded, sanitised for a filesystem.
	void stemNamingContract()
	{
		QCOMPARE(RenderManager::stemFileName("Kick 01", 1, 3, ".wav"), QString("01_Kick 01.wav"));
		QCOMPARE(RenderManager::stemFileName("Kick 01", 3, 3, ".wav"), QString("03_Kick 01.wav"));
		// Illegal characters are removed, so a name can never break the path.
		QCOMPARE(RenderManager::stemFileName("a/b:c", 2, 3, ".wav"), QString("02_abc.wav"));
		// An empty name still produces a usable file name.
		QCOMPARE(RenderManager::stemFileName("   ", 1, 2, ".wav"), QString("01_track.wav"));
		// Two tracks with the same name differ by index, so nothing overwrites.
		QVERIFY(RenderManager::stemFileName("Synth", 1, 2, ".wav")
				!= RenderManager::stemFileName("Synth", 2, 2, ".wav"));
		// More than 99 stems widen the field instead of colliding.
		QCOMPARE(RenderManager::stemFileName("X", 7, 120, ".wav"), QString("007_X.wav"));
	}

	//! 1, plus the naming as it lands on disk: one file per track, named in track
	//! order, non-silent, distinct from the mix and from each other.
	void stemsAreExportedNonSilentAndDistinct()
	{
		const QString dir = QDir(m_dir.path()).filePath("stems");
		int stemBars = 0;
		exportStems(dir, StemExportOptions{}, &stemBars);
		QCOMPARE(stemBars, kProjectBars);

		const QStringList names = stemPaths(dir);
		QCOMPARE(int(names.size()), 3);
		QCOMPARE(names[0], QString("01_Bass.wav"));
		QCOMPARE(names[1], QString("02_Pad.wav"));
		QCOMPARE(names[2], QString("03_Lead.wav"));

		std::vector<Wav> stems;
		for (const auto& name : names)
		{
			stems.push_back(readWav(QDir(dir).filePath(name)));
			printTrack("STEM", stems.back(), name);
		}

		QString why;
		QVERIFY2(allStemsSane(stems, m_mix, &why), qPrintable(why));
	}

	//! 2: every stem is the length of the mix, so the stems line up. This is what
	//! the export-length override buys; without it each stem is trimmed to that
	//! one track's length (the case below measures 3, 5 and 7 bars).
	void alignedStemsAreTheMixsLength()
	{
		const QString dir = QDir(m_dir.path()).filePath("stems-aligned");
		exportStems(dir, StemExportOptions{});

		const std::vector<Wav> stems = readStems(dir);
		QCOMPARE(int(stems.size()), 3);
		QString why;
		QVERIFY2(allStemsAligned(stems, m_mix, &why), qPrintable(why));
	}

	//! The same project with alignment switched off reproduces the legacy
	//! per-track behaviour: 3, 5 and 7 bars — each track's own length plus the
	//! one-bar tail — which can be neither summed nor lined up.
	void unalignedStemsAreTrimmedToTheirOwnTrack()
	{
		StemExportOptions trimmed;
		trimmed.alignToProjectLength = false;
		const QString dir = QDir(m_dir.path()).filePath("stems-trimmed");
		exportStems(dir, trimmed);

		const std::vector<Wav> stems = readStems(dir);
		QCOMPARE(int(stems.size()), 3);

		const int expected[] = {3, 5, 7};
		const int period = int(Engine::audioEngine()->framesPerPeriod());
		for (int i = 0; i < 3; ++i)
		{
			std::fprintf(stderr, "STEM_EVIDENCE ALIGN trimmed %d frames=%d (expected %d bars)\n",
					i, stems[std::size_t(i)].frameCount(), expected[i]);
			QVERIFY2(std::abs(stems[std::size_t(i)].frameCount() - expected[i] * kBarFrames) <= period,
					qPrintable(QString("trimmed stem %1 is %2 frames, expected ~%3").arg(i)
						.arg(stems[std::size_t(i)].frameCount()).arg(expected[i] * kBarFrames)));
		}
		std::fflush(stderr);
	}

	//! The tail convention: `tailBars` bars are rendered past the project end, on
	//! every stem; the default (1) is the whole-project render's own convention.
	void tailBarsExtendEveryStem()
	{
		StemExportOptions none;
		none.tailBars = 0;
		const QString noneDir = QDir(m_dir.path()).filePath("stems-tail0");
		exportStems(noneDir, none);

		StemExportOptions longTail;
		longTail.tailBars = 4;
		const QString longDir = QDir(m_dir.path()).filePath("stems-tail4");
		exportStems(longDir, longTail);

		const QString defDir = QDir(m_dir.path()).filePath("stems-default");
		exportStems(defDir, StemExportOptions{});

		const int noneBass = readWav(QDir(noneDir).filePath("01_Bass.wav")).frameCount();
		const int tailBass = readWav(QDir(longDir).filePath("01_Bass.wav")).frameCount();
		const int noneLead = readWav(QDir(noneDir).filePath("03_Lead.wav")).frameCount();
		const int tailLead = readWav(QDir(longDir).filePath("03_Lead.wav")).frameCount();
		const int defBass = readWav(QDir(defDir).filePath("01_Bass.wav")).frameCount();
		QVERIFY(noneBass > 0 && tailBass > 0 && noneLead > 0 && tailLead > 0 && defBass > 0);

		const int period = int(Engine::audioEngine()->framesPerPeriod());
		std::fprintf(stderr, "STEM_EVIDENCE TAIL tail0=%d, tail4=%d, default=%d frames; "
				"1 bar = %d frames, period = %d\n", noneBass, tailBass, defBass, kBarFrames, period);
		std::fflush(stderr);

		// 4 extra bars of render time, within one render period.
		QVERIFY2(std::abs((tailBass - noneBass) - 4 * kBarFrames) <= period,
				"tailBars did not add exactly that many bars of render time");
		// The tail is added on *every* stem, not only on the longest track's.
		QCOMPARE(tailLead - noneLead, tailBass - noneBass);
		// With no tail a stem ends at the project end; the default adds one bar.
		QVERIFY2(std::abs((defBass - noneBass) - kBarFrames) <= period,
				"the default tail is not the whole-project render's one bar");
	}

	//! 1 again, numerically: the stems sum to the mix. A gate of 1 dB of summed
	//! energy, because the renderer is not reproducible run to run (see the file
	//! header); in fresh processes the same check is 2 LSB and energy-exact. 1 dB
	//! is far too loose to pass an acceptance failure: a silent, duplicated or
	//! mix-copied stem moves this by 3 dB or more.
	void stemsSumToTheMix()
	{
		const QString dir = QDir(m_dir.path()).filePath("stems-sum");
		exportStems(dir, StemExportOptions{});

		const std::vector<Wav> stems = readStems(dir);
		QCOMPARE(int(stems.size()), 3);

		const StemEvidence e = compareStemsToMix(m_mix, sumStems(stems), kRate / 2);
		printSumEvidence(e);

		QVERIFY2(e.compared > 2000, "too few frames carried audio to compare");
		QVERIFY2(std::abs(e.energyDb) <= 1.0,
				qPrintable(QString("stems do not sum to the mix: energy %1 dB").arg(e.energyDb)));
		QVERIFY(e.sumRms < e.mixRms * 0.2);
	}

	//! 4, in-process half: a stem export leaves the render-length settings at their
	//! defaults and the next whole-project render the length and the level it had
	//! before. Nothing here compares the two renders frame by frame, and the
	//! control render printed beside them shows why: the renderer's in-process
	//! jitter is larger than anything a stem export could add. The byte-level
	//! comparison is made in fresh processes in docs/STEM-EXPORT.md section 4.4,
	//! where one of the branch's runs is byte-identical to the base binary's.
	void wholeProjectRenderIsUnchangedByAStemExport()
	{
		const QByteArray before = renderMix(QDir(m_dir.path()).filePath("before.wav"));
		QVERIFY(!before.isEmpty());
		const QByteArray control = renderMix(QDir(m_dir.path()).filePath("control.wav"));
		QVERIFY(!control.isEmpty());

		exportStems(QDir(m_dir.path()).filePath("stems-mid"), StemExportOptions{});

		const int overrideBars = Engine::getSong()->exportLengthOverrideBars();
		const int tailBars = Engine::getSong()->exportTailBars();
		const QByteArray after = renderMix(QDir(m_dir.path()).filePath("after.wav"));
		QVERIFY(!after.isEmpty());

		const Wav wBefore = readWav(QDir(m_dir.path()).filePath("before.wav"));
		const Wav wControl = readWav(QDir(m_dir.path()).filePath("control.wav"));
		const Wav wAfter = readWav(QDir(m_dir.path()).filePath("after.wav"));
		const double deltaDb = 20.0 * std::log10(wAfter.rms() / wBefore.rms());
		std::fprintf(stderr, "STEM_EVIDENCE DEFAULT settings-override=%d tail=%d; frames=%d/%d/%d; "
				"dB(after vs before)=%.5f; before=%.12s control=%.12s after=%.12s\n", overrideBars,
				tailBars, wBefore.frameCount(), wControl.frameCount(), wAfter.frameCount(), deltaDb,
				before.toHex().constData(), control.toHex().constData(), after.toHex().constData());
		std::fflush(stderr);

		QCOMPARE(overrideBars, 0);
		QCOMPARE(tailBars, 1);
		QCOMPARE(wAfter.frameCount(), wBefore.frameCount());
		QVERIFY2(std::abs(deltaDb) < 0.5, "a stem export moved the next render's level");
	}

private:
	QTemporaryDir m_dir;
	QString m_project;
	QString m_mixPath;
	Wav m_mix;
};

QTEST_GUILESS_MAIN(StemExportTest)
#include "StemExportTest.moc"
