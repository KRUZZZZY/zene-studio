/*
 * ModulationRateTest.cpp - R1.1 + R1.3: the zipper, measured, and the per-sample route that removes it
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
 *
 */

/*! R1.1 asks for the number before any fix: a 20 Hz LFO on a gain, run through the real write
 *  path block by block, and the RESIDUAL - the RMS distance, per sample, between what the
 *  parameter reads (AutomatableModel::valueBuffer(), what a fader or filter multiplies with)
 *  and the ideal curve. At today's one-value-per-block rate that is the zipper. R1.3's
 *  per-sample route publishes knots across the block and must beat it by two orders of
 *  magnitude; the default route must stay the old write exactly; the per-sample path must not
 *  allocate; and the flag must round-trip a save, written only when on.
 */

#include <QtTest>

#include <QDomDocument>

#include <cmath>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "Engine.h"
#include "ModulationLayer.h"
#include "ValueBuffer.h"

using namespace lmms;

namespace
{

constexpr sample_rate_t kRate = 44100;

ModulatorSource lfo20Hz()
{
	ModulatorSource source;
	source.shape = ModulationShape::Sine;
	source.rateHz = 20.0f;
	return source;
}

ModulationRuntime runtimeFor(FloatModel* model, bool perSample)
{
	ModulationRuntime runtime;
	runtime.sources[0] = lfo20Hz();
	runtime.sourceCount = 1;
	ModulationRuntime::Entry entry;
	entry.model = model;
	entry.modulator = 0;
	entry.depth = 0.25f;
	entry.base = 0.5f;
	entry.minimum = 0.0f;
	entry.maximum = 1.0f;
	entry.perSample = perSample;
	runtime.entries[0] = entry;
	runtime.entryCount = 1;
	return runtime;
}

//! RMS distance per sample, over @a blocks blocks, between what the model reads and the curve.
double residual(bool perSample, int blocks)
{
	FloatModel model(0.5f, 0.0f, 1.0f, 0.0001f);
	const ModulationRuntime runtime = runtimeFor(&model, perSample);
	const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
	const ModulatorSource source = lfo20Hz();
	double sum = 0.0;
	long long count = 0;
	for (int block = 0; block < blocks; ++block)
	{
		AutomatableModel::incrementPeriodCounter();
		const double seconds = static_cast<double>(block) * frames / kRate;
		applyModulationBlock(runtime, seconds, ModulationBlock{frames, kRate});
		const ValueBuffer* buffer = model.valueBuffer();
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			const float read = buffer != nullptr ? buffer->value(f) : model.value();
			const float ideal = modulatedValue(source, 0.5f, 0.25f, 0.0f, 1.0f, seconds + static_cast<double>(f) / kRate);
			sum += static_cast<double>(read - ideal) * (read - ideal);
			++count;
		}
	}
	return std::sqrt(sum / static_cast<double>(count));
}

} // namespace

class ModulationRateTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The Dummy's render thread increments the period counter too; the test is the only
		// renderer, so the counter it reads is the one it moves.
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase() { Engine::destroy(); }

	void theZipperIsMeasuredAndThePerSampleRouteRemovesIt()
	{
		const int blocks = 2 * kRate / static_cast<int>(Engine::audioEngine()->framesPerPeriod());  // two seconds
		const double stepped = residual(false, blocks);
		const double smooth = residual(true, blocks);
		std::printf("RATE_EVIDENCE 20 Hz LFO, depth 0.25, %d-frame blocks: residual per block %.6f, per sample %.8f\n",
			static_cast<int>(Engine::audioEngine()->framesPerPeriod()), stepped, smooth);
		// R1.1's number: at one value per block a 20 Hz LFO is audibly stepped.
		QVERIFY2(stepped > 0.01, qPrintable(QStringLiteral("the block rate measured no zipper: %1").arg(stepped)));
		// R1.3 must beat it by two orders of magnitude.
		QVERIFY2(smooth < stepped / 100.0, qPrintable(QStringLiteral("per sample %1 vs per block %2").arg(smooth).arg(stepped)));
	}

	void theDefaultRouteIsTheOldWriteAndThePerSamplePathDoesNotAllocate()
	{
		FloatModel stepped(0.5f, 0.0f, 1.0f, 0.0001f);
		FloatModel legacy(0.5f, 0.0f, 1.0f, 0.0001f);
		AutomatableModel::incrementPeriodCounter();
		applyModulationBlock(runtimeFor(&stepped, false), 0.013, ModulationBlock{256, kRate});
		// A per-sample route handed no block (every pre-R1.3 caller) writes exactly once too.
		applyModulationBlock(runtimeFor(&legacy, true), 0.013);
		QCOMPARE(stepped.value(), modulatedValue(lfo20Hz(), 0.5f, 0.25f, 0.0f, 1.0f, 0.013));
		QCOMPARE(legacy.value(), stepped.value());
		QVERIFY(stepped.automationRamp() == nullptr);

		FloatModel smooth(0.5f, 0.0f, 1.0f, 0.0001f);
		const ModulationRuntime runtime = runtimeFor(&smooth, true);
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int block = 0; block < 64; ++block)
		{
			AutomatableModel::incrementPeriodCounter();
			applyModulationBlock(runtime, block * 256.0 / kRate, ModulationBlock{256, kRate});
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
	}

	void theFlagRoundTripsASaveAndIsWrittenOnlyWhenOn()
	{
		ModulationLayer layer;
		const int modulator = layer.addModulator(QStringLiteral("wobble"), lfo20Hz());
		ModulationRoute plain;
		plain.parameter = QStringLiteral("Gain");
		plain.depth = 0.5f;
		ModulationRoute armed = plain;
		armed.parameter = QStringLiteral("Cutoff");
		armed.perSample = true;
		QVERIFY(layer.addRoute(modulator, plain) >= 0);
		QVERIFY(layer.addRoute(modulator, armed) >= 0);
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		doc.appendChild(root);
		QVERIFY(layer.saveSettings(doc, root));
		const QString xml = doc.toString();
		QCOMPARE(xml.count(QStringLiteral("persample")), 1);

		ModulationLayer loaded;
		QVERIFY(loaded.loadSettings(root.firstChildElement()));
		QCOMPARE(loaded.modulator(0)->routes.size(), std::size_t{2});
		QVERIFY(!loaded.modulator(0)->routes[0].perSample);
		QVERIFY(loaded.modulator(0)->routes[1].perSample);
	}
};

QTEST_GUILESS_MAIN(ModulationRateTest)
#include "ModulationRateTest.moc"
