/*
 * WasmWorkerPoolTest.cpp - CODE-5: the shared worker pool, its wake-ups, and
 *                          the deterministic offline render (feature row 73)
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * WHAT THIS PROVES, and how it avoids proving nothing:
 *
 *  * the pool is SHARED and BOUNDED: three workers are hosted at once and the
 *    process still runs laneCount() lanes, not three threads.
 *  * a queued block is picked up by a WAKE-UP, not a poll tick: with every lane
 *    parked, a submit() makes the pool report a wake-up and the block comes
 *    back through collect() - the counters in the pool, not a sleep, are what
 *    the assertion is made on.
 *  * the offline render is DETERMINISTIC WITHIN A MEASURED TOLERANCE. This
 *    project has a recorded render non-determinism (docs/RENDER-DETERMINISM.md:
 *    period-boundary differences, up to one frame of start jitter, and two
 *    bundled projects that still differ run to run), so a test may not use
 *    byte identity or a fixed threshold. renderOffline() renders the same input
 *    twice on the inline control path to MEASURE this build's run-to-run floor
 *    in the same call, renders it three times through the pool as the subject,
 *    and the assertion is `subject <= floor` in both differing frames and
 *    largest absolute difference.
 *  * the comparator CAN fail: compareRenders() is required to report a
 *    one-sample perturbation, and a floor of zero is required NOT to cover a
 *    real difference. Without those two the verdict "deterministic" would be
 *    the answer of a function that always says yes.
 *
 * The subject module is modules/wasm/gain.wasm (built into WASM_MODULE_DIR):
 * stereo, out[i] = in[i] * host_get_param(0), and the render drives parameter 0
 * with 0.5 - so a correct render is 0.5 * the stimulus and a render that
 * dropped or reordered blocks is visible in the samples.
 */

#include <QtTest>

#include <QFileInfo>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "WasmOfflineRender.h"
#include "WasmWorker.h"
#include "WasmWorkerPool.h"

#ifndef WASM_MODULE_DIR
#define WASM_MODULE_DIR ""
#endif

namespace lmms::wasm
{

namespace
{
constexpr std::uint32_t TestFrames = 8192;
constexpr std::uint32_t TestBlockFrames = 256;
constexpr float TestSampleRate = 48000.0f;
//! What the render drives host_get_param(0) with (WasmOfflineRender.cpp).
constexpr float RenderParam0 = 0.5f;
constexpr float StimulusGain = 0.25f;

//! The stimulus: a rising ramp inside one block, repeated - so a block that
//! was dropped, reordered or processed twice shows up as a displaced sample.
std::vector<float> stimulus(std::uint32_t frames, float scale = StimulusGain)
{
	std::vector<float> out(frames, 0.0f);
	for (std::uint32_t i = 0; i < frames; ++i)
	{
		out[i] = scale * static_cast<float>(i % TestBlockFrames) / static_cast<float>(TestBlockFrames);
	}
	return out;
}
} // namespace

class WasmWorkerPoolTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void testPoolIsSharedAndBounded();
	void testAQueuedBlockIsPickedUpByAWakeUp();
	void testOfflineRenderIsDeterministicWithinTheMeasuredFloor();
	void testComparatorDetectsADifference();
	void testADifferentStimulusRendersDifferently();

private:
	std::string m_module;
};

void WasmWorkerPoolTest::initTestCase()
{
	const QString path = QStringLiteral(WASM_MODULE_DIR) + QStringLiteral("/gain.wasm");
	if (!QFileInfo::exists(path))
	{
		QSKIP("no assembled module: build the wasm-modules target (modules/wasm/gain.wat)");
	}
	m_module = path.toStdString();
}

void WasmWorkerPoolTest::testPoolIsSharedAndBounded()
{
	const std::size_t lanes = WasmWorkerPool::laneCount();
	QVERIFY2(lanes >= 1, "the pool must run at least one lane");
	QVERIFY2(lanes <= WasmWorkerPool::maxLanes, "the pool must stay within its lane bound");

	// Three modules hosted at once, one pool: this is the property that costs
	// the process one set of threads instead of one thread per module.
	std::vector<std::unique_ptr<WasmWorker>> workers;
	for (int i = 0; i < 3; ++i)
	{
		auto worker = std::make_unique<WasmWorker>();
		std::string error;
		QVERIFY2(worker->start(m_module, error), error.c_str());
		QVERIFY(worker->isReady());
		workers.push_back(std::move(worker));
	}
	const WasmWorkerPool::Stats stats = WasmWorkerPool::instance().stats();
	QCOMPARE(stats.workers, std::size_t{3});
	QVERIFY2(stats.lanes <= WasmWorkerPool::maxLanes,
		qPrintable(QStringLiteral("lanes %1 (max %2)").arg(stats.lanes).arg(WasmWorkerPool::maxLanes)));
	qInfo("MEASURED %llu hosted workers on %llu shared lane(s) (pool bound %zu)",
		static_cast<unsigned long long>(stats.workers),
		static_cast<unsigned long long>(stats.lanes), WasmWorkerPool::maxLanes);

	for (auto& worker : workers) { worker->stop(); }
	QCOMPARE(WasmWorkerPool::instance().stats().workers, std::size_t{0});
}

void WasmWorkerPoolTest::testAQueuedBlockIsPickedUpByAWakeUp()
{
	WasmWorker worker;
	std::string error;
	QVERIFY2(worker.start(m_module, error), error.c_str());
	QVERIFY(worker.isReady());
	// the same convention every render uses: parameter 0 is the gain
	worker.setParam(0, RenderParam0);

	std::vector<SampleFrame> input(TestBlockFrames);
	std::vector<SampleFrame> output(TestBlockFrames);
	for (std::uint32_t i = 0; i < TestBlockFrames; ++i)
	{
		input[i][0] = 0.25f;
		input[i][1] = 0.25f;
		output[i][0] = -1.0f;
		output[i][1] = -1.0f;
	}

	// Let every lane go to sleep first: the point of the next submit() is that
	// it WAKES one rather than waiting for a timer.
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	const WasmWorkerPool::Stats before = WasmWorkerPool::instance().stats();

	QVERIFY2(worker.submit(input.data(), TestBlockFrames, TestSampleRate),
		"the worker refused the first block");
	QVERIFY(worker.waitForIdle(5000));
	QVERIFY(worker.collect(output.data(), TestBlockFrames));

	const WasmWorkerPool::Stats after = WasmWorkerPool::instance().stats();
	QVERIFY2(after.wakeups > before.wakeups,
		qPrintable(QStringLiteral("a parked lane was not woken (wakeups %1 -> %2, suppressed %3)")
					   .arg(before.wakeups)
					   .arg(after.wakeups)
					   .arg(after.wakesSuppressed)));
	QVERIFY2(after.parks > before.parks || after.parks > 0,
		"no lane ever parked, so the wake-up path was not the one exercised");
	QVERIFY(after.blocks > before.blocks);
	qInfo("MEASURED wake-ups %llu (suppressed %llu), parks %llu, blocks %llu",
		static_cast<unsigned long long>(after.wakeups),
		static_cast<unsigned long long>(after.wakesSuppressed),
		static_cast<unsigned long long>(after.parks),
		static_cast<unsigned long long>(after.blocks));

	for (std::uint32_t i = 0; i < TestBlockFrames; ++i)
	{
		QVERIFY2(std::fabs(output[i][0] - 0.25f * RenderParam0) < 1e-6f,
			qPrintable(QStringLiteral("frame %1 is %2, not %3")
						   .arg(i)
						   .arg(output[i][0])
						   .arg(0.25f * RenderParam0)));
	}
	worker.stop();
}

void WasmWorkerPoolTest::testOfflineRenderIsDeterministicWithinTheMeasuredFloor()
{
	const std::vector<float> input = stimulus(TestFrames);
	const OfflineRenderOutcome outcome =
		renderOffline(m_module, TestFrames, TestBlockFrames, 3, TestSampleRate, input);
	QVERIFY2(outcome.ok, outcome.error.c_str());
	QCOMPARE(outcome.runs.size(), std::size_t{3});
	QCOMPARE(outcome.control.size(), std::size_t{2});
	QCOMPARE(outcome.tolerance.controlPairs, std::uint64_t{1});
	QVERIFY2(!outcome.tolerance.source.empty(), "the floor must say where it came from");

	const std::uint32_t expectedBlocks = TestFrames / TestBlockFrames;
	for (const OfflineRender& run : outcome.runs)
	{
		QVERIFY2(run.ok, run.error.c_str());
		QCOMPARE(run.path, std::string("pool"));
		QCOMPARE(run.droppedBlocks, std::uint64_t{0});
		QCOMPARE(run.trappedBlocks, std::uint64_t{0});
		QCOMPARE(run.blocks, static_cast<std::uint64_t>(expectedBlocks));
		QCOMPARE(run.output.size(), static_cast<std::size_t>(TestFrames));
	}
	for (const OfflineRender& run : outcome.control)
	{
		QVERIFY2(run.ok, run.error.c_str());
		QCOMPARE(run.path, std::string("inline"));
		QCOMPARE(run.blocks, static_cast<std::uint64_t>(expectedBlocks));
	}

	// The render itself is right: out = 0.5 * in, sample for sample. This is
	// what stops a "deterministic" verdict from being earned by silence.
	for (const OfflineRender& run : outcome.runs)
	{
		for (std::uint32_t i = 0; i < TestFrames; i += 97)
		{
			QVERIFY2(std::fabs(run.output[i] - RenderParam0 * input[i]) < 1e-6f,
				qPrintable(QStringLiteral("frame %1 is %2, not %3")
							   .arg(i)
							   .arg(run.output[i])
							   .arg(RenderParam0 * input[i])));
		}
	}

	// The verdict, with both sides measured in this call.
	qInfo("MEASURED floor (same build, run to run, inline control pair): %llu of %llu frames "
		  "differ, max |d| %g; subject (through the pool): %llu of %llu frames differ, max |d| %g",
		static_cast<unsigned long long>(outcome.tolerance.floor.differingFrames),
		static_cast<unsigned long long>(outcome.tolerance.floor.frames),
		outcome.tolerance.floor.maxAbsDifference,
		static_cast<unsigned long long>(outcome.subject.differingFrames),
		static_cast<unsigned long long>(outcome.subject.frames),
		outcome.subject.maxAbsDifference);
	QVERIFY2(outcome.deterministic,
		qPrintable(QStringLiteral("the pooled render is outside the measured floor: subject %1 "
								  "frames differ / max |d| %2, floor %3 / %4")
					   .arg(outcome.subject.differingFrames)
					   .arg(outcome.subject.maxAbsDifference)
					   .arg(outcome.tolerance.floor.differingFrames)
					   .arg(outcome.tolerance.floor.maxAbsDifference)));

	// The pooled path against the single-threaded one, within the same floor.
	QVERIFY2(outcome.tolerance.floor.covers(outcome.subjectAgainstControl),
		qPrintable(QStringLiteral("the pooled render differs from the inline one by more than the "
								  "floor: %1 frames / max |d| %2")
					   .arg(outcome.subjectAgainstControl.differingFrames)
					   .arg(outcome.subjectAgainstControl.maxAbsDifference)));

	// The comparator was tested on a real perturbation inside the render call.
	QVERIFY2(outcome.comparatorDetectsADifference,
		"the comparator did not report the one-sample perturbation it was given");
	QVERIFY(outcome.comparatorPerturbation > 0.0);
}

void WasmWorkerPoolTest::testComparatorDetectsADifference()
{
	const std::vector<float> a{0.0f, 0.5f, -0.25f, 0.75f};
	QVERIFY(compareRenders(a, a).equal());
	QCOMPARE(compareRenders(a, a).maxAbsDifference, 0.0);

	std::vector<float> b = a;
	b[1] = 0.5f + 1e-7f;
	const RenderDifference oneSample = compareRenders(a, b);
	QVERIFY2(!oneSample.equal(),
		"a one-sample perturbation must be a difference: the comparator is exact, not tolerant");
	QCOMPARE(oneSample.differingFrames, std::uint64_t{1});
	QCOMPARE(oneSample.worstFrame, std::int64_t{1});
	QVERIFY(oneSample.maxAbsDifference > 0.0);

	// A floor of zero covers nothing: this is why `deterministic` cannot be
	// vacuous when the control pair is bit-identical.
	const RenderDifference zeroFloor = compareRenders(a, a);
	QVERIFY(!zeroFloor.covers(oneSample));
	QVERIFY(compareRenders(a, b).covers(compareRenders(a, b)));

	// A length difference counts as a difference, too.
	std::vector<float> shorter{a.begin(), a.end() - 1};
	QVERIFY(!compareRenders(a, shorter).equal());
}

void WasmWorkerPoolTest::testADifferentStimulusRendersDifferently()
{
	// The render is not a constant: a different stimulus gives different
	// samples, so a verdict of "the same" is a measurement of the two renders.
	const std::vector<float> quiet = stimulus(TestFrames, 0.25f);
	const std::vector<float> loud = stimulus(TestFrames, 0.75f);
	const OfflineRenderOutcome first =
		renderOffline(m_module, TestFrames, TestBlockFrames, 2, TestSampleRate, quiet);
	const OfflineRenderOutcome second =
		renderOffline(m_module, TestFrames, TestBlockFrames, 2, TestSampleRate, loud);
	QVERIFY2(first.ok, first.error.c_str());
	QVERIFY2(second.ok, second.error.c_str());

	const RenderDifference difference =
		compareRenders(first.runs[0].output, second.runs[0].output);
	QVERIFY2(!difference.equal(), "two different stimuli rendered identically");
	QVERIFY(difference.differingFrames > 0);
	qInfo("MEASURED 0.25-scale vs 0.75-scale stimulus: %llu frames differ, max |d| %g",
		static_cast<unsigned long long>(difference.differingFrames), difference.maxAbsDifference);

	// ... and each of them is still reproducible within its own floor.
	QVERIFY(first.deterministic);
	QVERIFY(second.deterministic);
}

} // namespace lmms::wasm

QTEST_GUILESS_MAIN(lmms::wasm::WasmWorkerPoolTest)

#include "WasmWorkerPoolTest.moc"
