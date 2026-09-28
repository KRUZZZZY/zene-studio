/*
 * ModulationAudioViewTest.cpp - the audio thread's modulation snapshot carries no QPointer
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

//! ModulationLayerPublisher::snapshot() is called by the audio thread once per
//! block inside a seqlock. It used to copy ModulationRuntime, whose entries hold
//! QPointer<AutomatableModel>: copying a QPointer increments a shared weak
//! reference count (a write the seqlock cannot retry away, into a block the
//! control thread may be freeing), and destroying the copy can free on the audio
//! thread. The view the audio thread copies is now plain bytes, and a destroyed
//! target is found through a per-slot liveness token instead.
//!
//!  * the snapshot type is trivially copyable (the pre-fix type fails this);
//!  * a destroyed target is skipped by a view taken before it died;
//!  * a republish retires the tokens of every older view;
//!  * the view path writes exactly what the control-side runtime path writes;
//!  * hasLayer() follows the authored layer, so the audio thread's
//!    pay-nothing test no longer reads the control-only layer;
//!  * a block through the view allocates nothing.

#include <QtTest>

#include <cstdint>
#include <type_traits>
#include <utility>

#include "AllocationProbe.h"

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "Engine.h"
#include "ModulationLayer.h"

using namespace lmms;

namespace
{

//! QCOMPARE cannot take `model->value<float>()`: the macro parses the angle
//! brackets as relational operators (ModulationTestSupport.h says the same).
float valueOf(const AutomatableModel& model) { return model.value<float>(); }

ModulatorSource sine()
{
	ModulatorSource source;
	source.shape = ModulationShape::Sine;
	source.rateHz = 1.0f;
	source.phase = 0.0f;
	source.unipolar = false;
	return source;
}

//! Publishes one entry driving @a model from source 0.
void publishOne(ModulationLayerPublisher& publisher, FloatModel* model)
{
	publisher.edit([model](ModulationLayer&, ModulationRuntime& runtime) {
		runtime = ModulationRuntime{};
		ModulationRuntime::Entry entry;
		entry.model = model;
		entry.modulator = 0;
		entry.depth = 0.25f;
		entry.base = 0.0f;
		entry.minimum = -100.0f;
		entry.maximum = 100.0f;
		runtime.entries[0] = entry;
		runtime.entryCount = 1;
		runtime.sources[0] = sine();
		runtime.sourceCount = 1;
		return true;
	});
}

} // namespace

class ModulationAudioViewTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase() { Engine::destroy(); }

	void theAudioSnapshotIsPlainBytes()
	{
		using Snapshot = decltype(std::declval<const ModulationLayerPublisher&>().snapshot());
		QVERIFY2(std::is_trivially_copyable_v<Snapshot>,
			"the audio thread's seqlock copy holds a non-trivial type (a QPointer)");
	}

	void aDestroyedTargetIsSkippedByAnOlderView()
	{
		ModulationLayerPublisher publisher;
		auto* model = new FloatModel(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, model);
		const ModulationAudioView view = publisher.snapshot();
		QCOMPARE(view.entryCount, 1);
		QVERIFY(publisher.targetAlive(0, view.entries[0].token));

		applyModulationBlock(publisher, view, 0.25);
		QCOMPARE(valueOf(*model), 0.25f * 200.0f);

		delete model;
		QVERIFY2(!publisher.targetAlive(0, view.entries[0].token),
			"a destroyed target is still reported alive");
		// Must skip the entry: the raw pointer in the view is dangling now.
		applyModulationBlock(publisher, view, 0.75);
	}

	void aRepublishRetiresEveryOlderView()
	{
		ModulationLayerPublisher publisher;
		FloatModel model(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, &model);
		const ModulationAudioView before = publisher.snapshot();
		publishOne(publisher, &model);
		const ModulationAudioView after = publisher.snapshot();
		QVERIFY(!publisher.targetAlive(0, before.entries[0].token));
		QVERIFY(publisher.targetAlive(0, after.entries[0].token));

		model.setValue(7.0f);
		applyModulationBlock(publisher, before, 0.25);
		QCOMPARE(valueOf(model), 7.0f);
	}

	void theViewWritesWhatTheRuntimeWrites()
	{
		ModulationLayerPublisher publisher;
		FloatModel viaView(0.0f, -100.0f, 100.0f, 0.01f);
		FloatModel viaRuntime(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, &viaView);
		const ModulationAudioView view = publisher.snapshot();
		ModulationRuntime runtime = publisher.runtime();
		runtime.entries[0].model = &viaRuntime;
		for (double seconds : {0.0, 0.1, 0.25, 0.5, 0.6, 0.75, 0.9})
		{
			applyModulationBlock(publisher, view, seconds);
			applyModulationBlock(runtime, seconds);
			QCOMPARE(valueOf(viaView), valueOf(viaRuntime));
		}
	}

	void hasLayerFollowsTheAuthoredLayer()
	{
		ModulationLayerPublisher publisher;
		QVERIFY(!publisher.hasLayer());
		publisher.edit([](ModulationLayer& layer, ModulationRuntime&) {
			return layer.addModulator(QStringLiteral("lfo"), sine()) >= 0;
		});
		QVERIFY(publisher.hasLayer());
		publisher.edit([](ModulationLayer& layer, ModulationRuntime&) {
			layer.clear();
			return true;
		});
		QVERIFY(!publisher.hasLayer());
	}

	void aBlockThroughTheViewAllocatesNothing()
	{
		ModulationLayerPublisher publisher;
		FloatModel model(0.0f, -100.0f, 100.0f, 0.01f);
		publishOne(publisher, &model);

		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int block = 0; block < 64; ++block)
		{
			const ModulationAudioView view = publisher.snapshot();
			applyModulationBlock(publisher, view, 0.001 * block);
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QCOMPARE(allocations, std::uint64_t{0});
	}
};

QTEST_GUILESS_MAIN(ModulationAudioViewTest)
#include "ModulationAudioViewTest.moc"
