/*
 * LfoControllerUserWaveTest.cpp - user-wave buffer ownership across render updates
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <memory>
#include <utility>
#include <vector>

#include "Engine.h"
#include "AutomatableModel.h"
#include "Controller.h"
#include "ControllerDialog.h"
#include "SampleBuffer.h"
#include "TempoSyncKnobModel.h"

// No `#define private public`: LfoController befriends this test, and MSVC mangles the access level into
// member names, so a redefined `private` links against symbols the product does not have.
#include "LfoController.h"

using namespace lmms;

template<typename T>
concept HasRenderSafeUserWaveHandoff = requires(T& lfo, std::shared_ptr<const SampleBuffer> buffer)
{
	lfo.setUserDefSampleBuffer(std::move(buffer));
	lfo.acquireUserDefSampleBufferForRender();
	lfo.releaseUserDefSampleBufferForRender();
	lfo.reclaimRetiredUserDefSampleBuffers();
};

class LfoControllerUserWaveTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void replacedWaveRemainsAliveWhileRenderLeaseIsHeld()
	{
		verifyReplacementLifetime<LfoController>();
	}

private:
	template<typename T>
	static void verifyReplacementLifetime()
	{
		T lfo(nullptr);
		auto original = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>{}, 44100);
		std::weak_ptr<const SampleBuffer> originalLifetime = original;
		const auto* originalBuffer = original.get();
		auto replacement = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>{}, 44100);

		if constexpr (HasRenderSafeUserWaveHandoff<T>)
		{
			lfo.setUserDefSampleBuffer(std::move(original));
			QVERIFY(lfo.acquireUserDefSampleBufferForRender() == originalBuffer);
			lfo.setUserDefSampleBuffer(replacement);
			QVERIFY2(!originalLifetime.expired(),
				"replacing the waveform freed a buffer while rendering held its lease");
			lfo.reclaimRetiredUserDefSampleBuffers();
			QVERIFY(!originalLifetime.expired());
			lfo.releaseUserDefSampleBufferForRender();
			lfo.reclaimRetiredUserDefSampleBuffers();
			QVERIFY2(originalLifetime.expired(), "released waveform was not reclaimed on the control thread");
			QCOMPARE(lfo.acquireUserDefSampleBufferForRender(), replacement.get());
			lfo.releaseUserDefSampleBufferForRender();
		}
		else
		{
			// Model the old render-side raw pointer and the dialog's direct shared_ptr assignment.
			lfo.m_userDefSampleBuffer = std::move(original);
			const auto* renderBuffer = lfo.m_userDefSampleBuffer.get();
			QVERIFY(renderBuffer == originalBuffer);
			lfo.m_userDefSampleBuffer = replacement;
			QVERIFY2(!originalLifetime.expired(),
				"replacing the waveform freed a buffer while rendering held its raw pointer");
		}
	}
};

QTEST_GUILESS_MAIN(LfoControllerUserWaveTest)
#include "LfoControllerUserWaveTest.moc"
