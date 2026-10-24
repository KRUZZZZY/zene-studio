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

#include <vector>

#include "Engine.h"
#include "LfoController.h"

using namespace lmms;

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
		LfoController lfo(nullptr);
		auto original = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>{}, 44100);
		std::weak_ptr<const SampleBuffer> originalLifetime = original;
		lfo.setUserDefSampleBuffer(std::move(original));

		QVERIFY(lfo.acquireUserDefSampleBufferForRender() != nullptr);
		auto replacement = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>{}, 44100);
		lfo.setUserDefSampleBuffer(replacement);

		QVERIFY2(!originalLifetime.expired(),
			"replacing the waveform freed a buffer while a render lease still used it");
		lfo.reclaimRetiredUserDefSampleBuffers();
		QVERIFY(!originalLifetime.expired());

		lfo.releaseUserDefSampleBufferForRender();
		lfo.reclaimRetiredUserDefSampleBuffers();
		QVERIFY2(originalLifetime.expired(), "the released waveform was not reclaimed on the control thread");

		QCOMPARE(lfo.acquireUserDefSampleBufferForRender(), replacement.get());
		lfo.releaseUserDefSampleBufferForRender();
	}
};

QTEST_GUILESS_MAIN(LfoControllerUserWaveTest)
#include "LfoControllerUserWaveTest.moc"
