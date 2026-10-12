#pragma once

#include <QtTest>

#include <limits>

#include "AutomationClip.h"
#include "AutomationNode.h"

namespace automationClipTransformTangentTest
{
	using namespace lmms;

inline AutomationClip* handLockedSegment(FloatModel* model, AutomationClip* (*makeClip)(FloatModel*))
{
	AutomationClip* clip = makeClip(model);
	clip->setProgressionType(AutomationClip::ProgressionType::CubicHermite);
	clip->putValue(TimePos(0), 0.f, false, true);
	clip->putValue(TimePos(100), 100.f, false, true);
	clip->getTimeMap()[0].setInTangent(0.5f);
	clip->getTimeMap()[0].setOutTangent(0.5f);
	clip->getTimeMap()[100].setInTangent(-0.25f);
	clip->getTimeMap()[100].setOutTangent(-0.25f);
	clip->getTimeMap()[0].setLockedTangents(true);
	clip->getTimeMap()[100].setLockedTangents(true);
	return clip;
}

inline void refusesInfiniteLockedTangentScale(AutomationClip* clip)
{
	const float pivot = (clip->getMin() + clip->getMax()) / 2.f;
	clip->putValue(TimePos(96), pivot, false, true);
	auto& node = clip->getTimeMap()[96];
	node.setInTangent(0.25f);
	node.setOutTangent(-0.5f);
	node.setLockedTangents(true);
	const int pointCount = static_cast<int>(clip->getTimeMap().size());

	QCOMPARE(clip->transformRange(96, 96, std::numeric_limits<double>::min(), 0,
		std::numeric_limits<double>::max(), 0.0), 0);
	QCOMPARE(static_cast<int>(clip->getTimeMap().size()), pointCount);
	QVERIFY(clip->getTimeMap().contains(96));
	QVERIFY(clip->getTimeMap().value(96).lockedTangents());
	QCOMPARE(clip->getTimeMap().value(96).getInValue(), pivot);
	QCOMPARE(clip->getTimeMap().value(96).getOutValue(), pivot);
	QCOMPARE(clip->getTimeMap().value(96).getInTangent(), 0.25f);
	QCOMPARE(clip->getTimeMap().value(96).getOutTangent(), -0.5f);
}

} // namespace automationClipTransformTangentTest
