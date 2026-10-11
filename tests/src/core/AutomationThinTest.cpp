/*
 * AutomationThinTest.cpp - regression tests for automation point thinning
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

#include "AudioEngine.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "Engine.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

class AutomationThinTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void thinningKeepsTheEndpointOfACustomSegment()
	{
		auto* track = new AutomationTrack(Engine::getSong());
		auto* clip = static_cast<AutomationClip*>(track->createClip(TimePos(0)));
		clip->setProgressionType(AutomationClip::ProgressionType::Linear);
		for (const int tick : {0, 25, 50, 75, 100})
		{
			clip->putValue(TimePos(tick), static_cast<float>(tick), false, true);
		}
		clip->getTimeMap()[25].setCurve(AutomationNode::Curve::Hold);
		QCOMPARE(clip->valueAt(TimePos(75)), 75.f);

		const int removed = clip->thin(0.f);
		QCOMPARE(clip->valueAt(TimePos(75)), 75.f);
		QCOMPARE(removed, 1);
		QVERIFY(clip->getTimeMap().contains(50));
		QVERIFY(!clip->getTimeMap().contains(75));
	}
};

QTEST_MAIN(AutomationThinTest)

#include "AutomationThinTest.moc"
