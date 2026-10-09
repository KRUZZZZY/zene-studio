/*
 * AutomationClipSnapshotTest.cpp - immutable automation reader snapshots
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
 */

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <QMutexLocker>

#include "AllocationProbe.h"
#include "AutomationClip.h"
#include "AutomationRamp.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

struct OldNode
{
	int tick;
	float inValue;
	float outValue;
	float inTangent;
	float outTangent;
	float bend;
	AutomationNode::Curve curve;
};

struct OldCurve
{
	std::vector<OldNode> nodes;
	AutomationClip::ProgressionType progression;
	float tension;
	int startPosition;
	int offset;
	int length;
	bool inPattern;
};

OldCurve captureOldCurve(const AutomationClip& clip)
{
	OldCurve curve{{}, clip.progressionType(), clip.getTension(), clip.startPosition().getTicks(),
		clip.startTimeOffset().getTicks(), clip.length().getTicks(), clip.isInPattern()};
	for (auto it = clip.getTimeMap().cbegin(); it != clip.getTimeMap().cend(); ++it)
	{
		const auto& node = it.value();
		curve.nodes.push_back({it.key(), node.getInValue(), node.getOutValue(), node.getInTangent(),
			node.getOutTangent(), node.bend(), node.curve()});
	}
	return curve;
}

float oldSegmentValue(const OldCurve& curve, std::size_t index, int offset)
{
	const OldNode& node = curve.nodes[index];
	if (offset == 0) { return node.inValue; }
	const OldNode& next = curve.nodes[index + 1];
	if (node.curve == AutomationNode::Curve::Bend)
	{
		const double t = std::clamp(static_cast<double>(offset) / std::max(1, next.tick - node.tick), 0.0, 1.0);
		const double shaped = node.bend >= 0.0 ? std::pow(t, 1.0 + 4.0 * node.bend)
			: 1.0 - std::pow(1.0 - t, 1.0 - 4.0 * node.bend);
		return static_cast<float>(node.outValue + (next.inValue - node.outValue) * shaped);
	}
	const auto type = node.curve == AutomationNode::Curve::Clip ? curve.progression
		: node.curve == AutomationNode::Curve::Hold ? AutomationClip::ProgressionType::Discrete
		: node.curve == AutomationNode::Curve::Linear ? AutomationClip::ProgressionType::Linear
		: AutomationClip::ProgressionType::CubicHermite;
	if (type == AutomationClip::ProgressionType::Discrete) { return node.outValue; }
	if (type == AutomationClip::ProgressionType::Linear)
	{
		const float slope = (next.inValue - node.outValue) / (next.tick - node.tick);
		return node.outValue + offset * slope;
	}
	const int numValues = next.tick - node.tick;
	const float t = static_cast<float>(offset) / static_cast<float>(numValues);
	const float m1 = node.outTangent * numValues * curve.tension;
	const float m2 = next.inTangent * numValues * curve.tension;
	const auto t2 = t * t, t3 = t2 * t;
	return (2 * t3 - 3 * t2 + 1) * node.outValue
		+ (t3 - 2 * t2 + t) * m1 + (-2 * t3 + 3 * t2) * next.inValue + (t3 - t2) * m2;
}

float oldValueAt(const OldCurve& curve, int tick)
{
	if (curve.nodes.empty()) { return 0.0f; }
	const auto next = std::lower_bound(curve.nodes.begin(), curve.nodes.end(), tick,
		[](const OldNode& node, int pos) { return node.tick < pos; });
	if (next != curve.nodes.end() && next->tick == tick) { return next->inValue; }
	if (next == curve.nodes.begin()) { return 0.0f; }
	const auto previous = std::prev(next);
	if (next == curve.nodes.end()) { return previous->outValue; }
	return oldSegmentValue(curve, static_cast<std::size_t>(previous - curve.nodes.begin()), tick - previous->tick);
}

float oldRampValueAt(const OldCurve& curve, int tick, float fraction)
{
	if (curve.nodes.empty() || tick < 0) { return 0.0f; }
	const auto last = std::prev(curve.nodes.end());
	if (tick >= last->tick) { return last->outValue; }
	const auto next = std::upper_bound(curve.nodes.begin(), curve.nodes.end(), tick,
		[](int pos, const OldNode& node) { return pos < node.tick; });
	if (next == curve.nodes.begin()) { return 0.0f; }
	const auto node = std::prev(next);
	const auto index = static_cast<std::size_t>(node - curve.nodes.begin());
	const int offset = tick - node->tick;
	const float start = offset == 0 ? node->outValue : oldSegmentValue(curve, index, offset);
	const float end = oldSegmentValue(curve, index, offset + 1);
	return start + fraction * (end - start);
}

void oldAddKnot(const OldCurve& curve, AutomationRamp& ramp, int relStart, int frame,
	int frameOffsetInTick, double framesPerTick)
{
	const double position = (static_cast<double>(frameOffsetInTick) + frame) / framesPerTick;
	const int tickOffset = static_cast<int>(std::floor(position));
	const float fraction = static_cast<float>(position - tickOffset);
	int rel = relStart + tickOffset;
	if (!curve.inPattern) { rel = std::min(rel, curve.length - curve.offset); }
	ramp.addKnot(static_cast<f_cnt_t>(frame), oldRampValueAt(curve, rel, fraction));
}

void oldAddBoundaryKnots(const OldCurve& curve, AutomationRamp& ramp, int relStart, f_cnt_t frames,
	int frameOffsetInTick, double framesPerTick)
{
	for (int tick = 1;; ++tick)
	{
		const double knotFrame = tick * framesPerTick - frameOffsetInTick;
		if (knotFrame > static_cast<double>(frames)) { break; }
		int before = static_cast<int>(std::floor(knotFrame));
		const int after = static_cast<int>(std::ceil(knotFrame));
		if (before == after) { before = after - 1; }
		if (before > 0) { oldAddKnot(curve, ramp, relStart, before, frameOffsetInTick, framesPerTick); }
		if (after > 0 && after < static_cast<int>(frames))
		{
			oldAddKnot(curve, ramp, relStart, after, frameOffsetInTick, framesPerTick);
		}
	}
}

AutomationRamp oldWriteRamp(const OldCurve& curve, const TimePos& blockStart, f_cnt_t frames,
	double framesPerTick, int frameOffsetInTick)
{
	AutomationRamp ramp;
	ramp.reset(frames);
	if (curve.nodes.empty() || frames == 0 || framesPerTick <= 0.0) { return ramp; }
	const int relStart = blockStart.getTicks() - curve.startPosition - curve.offset;
	if (relStart < 0) { return ramp; }
	oldAddKnot(curve, ramp, relStart, 0, frameOffsetInTick, framesPerTick);
	oldAddBoundaryKnots(curve, ramp, relStart, frames, frameOffsetInTick, framesPerTick);
	oldAddKnot(curve, ramp, relStart, static_cast<int>(frames), frameOffsetInTick, framesPerTick);
	return ramp;
}

std::uint32_t bits(float value)
{
	return std::bit_cast<std::uint32_t>(value);
}

void fillCurve(AutomationClip& clip, AutomationClip::ProgressionType progression,
	AutomationNode::Curve firstCurve)
{
	clip.clear();
	clip.setProgressionType(progression);
	clip.setTension(QStringLiteral("0.67"));
	clip.putValues(TimePos(0), 0.125f, 0.25f, false, true);
	clip.putValues(TimePos(12), 0.625f, 0.5f, false, true);
	clip.putValues(TimePos(27), 0.375f, 0.4375f, false, true);
	clip.putValues(TimePos(42), 0.875f, 0.8125f, false, true);
	clip.setNodeCurve(TimePos(0), firstCurve, 0.43f);
	clip.setNodeTangents(TimePos(0), 0.1f, -0.3f, true);
	clip.setNodeTangents(TimePos(12), 0.2f, 0.4f, true);
	clip.movePosition(TimePos(8));
	clip.setStartTimeOffset(TimePos(3));
	clip.changeLength(TimePos(60));
	clip.setRampSetting(AutomationClip::RampSetting::On);
}

bool sameRamp(const AutomationRamp& first, const AutomationRamp& second)
{
	if (first.knotCount() != second.knotCount() || first.frames() != second.frames()) { return false; }
	for (int i = 0; i < first.knotCount(); ++i)
	{
		if (first.knot(i).frame != second.knot(i).frame || bits(first.knot(i).value) != bits(second.knot(i).value))
		{
			return false;
		}
	}
	for (f_cnt_t frame = 0; frame <= first.frames(); ++frame)
	{
		if (bits(first.valueAt(frame)) != bits(second.valueAt(frame))) { return false; }
	}
	return true;
}

} // namespace

class AutomationClipSnapshotTest : public QObject
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

	void valuesAndRampsMatchTheOldEvaluatorBitForBit()
	{
		AutomationClip clip(nullptr);
		const std::vector<std::pair<AutomationClip::ProgressionType, AutomationNode::Curve>> cases{
			{AutomationClip::ProgressionType::Discrete, AutomationNode::Curve::Clip},
			{AutomationClip::ProgressionType::Linear, AutomationNode::Curve::Clip},
			{AutomationClip::ProgressionType::CubicHermite, AutomationNode::Curve::Clip},
			{AutomationClip::ProgressionType::Discrete, AutomationNode::Curve::Bend},
			{AutomationClip::ProgressionType::Linear, AutomationNode::Curve::Hold},
			{AutomationClip::ProgressionType::CubicHermite, AutomationNode::Curve::Linear},
			{AutomationClip::ProgressionType::Discrete, AutomationNode::Curve::Smooth}};
		for (const auto& [progression, firstCurve] : cases)
		{
			fillCurve(clip, progression, firstCurve);
			const OldCurve old = captureOldCurve(clip);
			QVERIFY(clip.sampleAccurate());
			for (int tick = -2; tick <= 48; ++tick)
			{
				QVERIFY2(bits(clip.valueAt(TimePos(tick))) == bits(oldValueAt(old, tick)),
					qPrintable(QStringLiteral("valueAt differs at tick %1 (curve case %2)").arg(tick).arg(static_cast<int>(firstCurve))));
			}
			const auto expected = oldWriteRamp(old, TimePos(16), 64, 2.35, 1);
			AutomationRamp actual;
			clip.writeBlockRamp(actual, TimePos(16), 64, 2.35, 1);
			QVERIFY2(sameRamp(actual, expected), "generated ramp knots or frame samples differ from the old evaluator");
			clip.setRampSetting(AutomationClip::RampSetting::Off);
			QVERIFY(!clip.sampleAccurate());
			clip.setRampSetting(AutomationClip::RampSetting::Default);
			QCOMPARE(clip.sampleAccurate(), progression != AutomationClip::ProgressionType::Discrete);
		}
	}

	void audioReadersAllocateNothingAndIgnoreTheEditMutex()
	{
		AutomationClip clip(nullptr);
		fillCurve(clip, AutomationClip::ProgressionType::Linear, AutomationNode::Curve::Clip);
		std::atomic<bool> done{false};
		std::atomic<std::uint64_t> allocations{0};
		std::atomic<std::uint32_t> valueBits{0};
		QMutexLocker held(&clip.m_clipMutex);
		std::thread reader([&] {
			test::resetAllocationCount();
			test::tlCountAllocations = true;
			valueBits.store(bits(clip.valueAt(TimePos(20))), std::memory_order_relaxed);
			AutomationRamp ramp;
			clip.writeBlockRamp(ramp, TimePos(16), 64, 2.35, 1);
			allocations.store(test::tlAllocationCount, std::memory_order_relaxed);
			test::tlCountAllocations = false;
			done.store(true, std::memory_order_release);
		});
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		while (!done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		const bool finishedWhileMutexHeld = done.load(std::memory_order_acquire);
		held.unlock();
		reader.join();
		QVERIFY2(finishedWhileMutexHeld, "valueAt/writeBlockRamp waited for m_clipMutex");
		QCOMPARE(allocations.load(std::memory_order_relaxed), std::uint64_t{0});
		QCOMPARE(valueBits.load(std::memory_order_relaxed), bits(clip.valueAt(TimePos(20))));
	}

	void concurrentEditsPublishWholeCurves()
	{
		AutomationClip clip(nullptr);
		clip.setProgressionType(AutomationClip::ProgressionType::Linear);
		clip.putValue(TimePos(0), 0.25f, false, true);
		clip.putValue(TimePos(96), 0.75f, false, true);
		clip.changeLength(TimePos(96));
		const auto oldA = captureOldCurve(clip);
		const auto rampA = oldWriteRamp(oldA, TimePos(0), 64, 1.5, 0);
		clip.flipY(0, 1);
		const auto oldB = captureOldCurve(clip);
		const auto rampB = oldWriteRamp(oldB, TimePos(0), 64, 1.5, 0);
		clip.flipY(0, 1);

		std::atomic<bool> go{false};
		std::atomic<bool> done{false};
		std::atomic<bool> failed{false};
		std::thread renderer([&] {
			go.store(true, std::memory_order_release);
			while (!done.load(std::memory_order_acquire))
			{
				const auto value = bits(clip.valueAt(TimePos(24)));
				if (value != bits(oldValueAt(oldA, 24)) && value != bits(oldValueAt(oldB, 24)))
				{
					failed.store(true, std::memory_order_relaxed);
				}
				AutomationRamp ramp;
				clip.writeBlockRamp(ramp, TimePos(0), 64, 1.5, 0);
				if (!sameRamp(ramp, rampA) && !sameRamp(ramp, rampB))
				{
					failed.store(true, std::memory_order_relaxed);
				}
			}
		});
		while (!go.load(std::memory_order_acquire)) { std::this_thread::yield(); }
		for (int i = 0; i < 1000; ++i) { clip.flipY(0, 1); }
		done.store(true, std::memory_order_release);
		renderer.join();
		QVERIFY2(!failed.load(std::memory_order_relaxed), "reader observed a torn or partially edited curve");
	}

	void draggingPublishesOnlyTheCompletedEdit()
	{
		AutomationClip clip(nullptr);
		clip.setProgressionType(AutomationClip::ProgressionType::Linear);
		clip.putValue(TimePos(0), 0.1f, false, true);
		clip.putValue(TimePos(24), 0.4f, false, true);
		clip.putValue(TimePos(48), 0.8f, false, true);
		const auto before = clip.m_readSnapshotPublicationCount;

		clip.setDragValue(TimePos(32), 0.6f, false, true);

		QCOMPARE(clip.m_readSnapshotPublicationCount, before + 1);
		QCOMPARE(bits(clip.valueAt(TimePos(32))), bits(0.6f));
		clip.applyDragValue();
	}

	void concurrentCompoundDragsNeverExposeAMissingPoint()
	{
		auto makeClip = [] {
			auto* clip = new AutomationClip(nullptr);
			clip->setProgressionType(AutomationClip::ProgressionType::Linear);
			clip->putValue(TimePos(0), 0.25f, false, true);
			clip->putValue(TimePos(24), 0.5f, false, true);
			clip->putValue(TimePos(96), 0.75f, false, true);
			clip->changeLength(TimePos(96));
			return clip;
		};
		auto clip = std::unique_ptr<AutomationClip>(makeClip());
		auto expectedB = std::unique_ptr<AutomationClip>(makeClip());
		auto missing = std::unique_ptr<AutomationClip>(makeClip());
		missing->removeNode(TimePos(24));

		const OldCurve curveA = captureOldCurve(*clip);
		const auto rampA = oldWriteRamp(curveA, TimePos(0), 64, 1.5, 0);
		expectedB->setDragValue(TimePos(24), 0.5f, false, true);
		expectedB->setDragValue(TimePos(32), 0.5f, false, true);
		expectedB->applyDragValue();
		const OldCurve curveB = captureOldCurve(*expectedB);
		const auto rampB = oldWriteRamp(curveB, TimePos(0), 64, 1.5, 0);
		const auto missingBits = bits(oldValueAt(captureOldCurve(*missing), 24));

		std::atomic<bool> go{false};
		std::atomic<bool> done{false};
		std::atomic<bool> failed{false};
		std::thread renderer([&] {
			go.store(true, std::memory_order_release);
			while (!done.load(std::memory_order_acquire))
			{
				const auto atDraggedPoint = bits(clip->valueAt(TimePos(24)));
				if (atDraggedPoint == missingBits
					|| (atDraggedPoint != bits(oldValueAt(curveA, 24))
						&& atDraggedPoint != bits(oldValueAt(curveB, 24))))
				{
					failed.store(true, std::memory_order_relaxed);
				}
				AutomationRamp ramp;
				clip->writeBlockRamp(ramp, TimePos(0), 64, 1.5, 0);
				if (!sameRamp(ramp, rampA) && !sameRamp(ramp, rampB))
				{
					failed.store(true, std::memory_order_relaxed);
				}
			}
		});
		while (!go.load(std::memory_order_acquire)) { std::this_thread::yield(); }
		for (int i = 0; i < 500; ++i)
		{
			clip->setDragValue(TimePos(24), 0.5f, false, true);
			clip->setDragValue(TimePos(32), 0.5f, false, true);
			clip->applyDragValue();
			clip->setDragValue(TimePos(32), 0.5f, false, true);
			clip->setDragValue(TimePos(24), 0.5f, false, true);
			clip->applyDragValue();
		}
		done.store(true, std::memory_order_release);
		renderer.join();
		QVERIFY2(!failed.load(std::memory_order_relaxed), "reader observed the dragged point missing from the published curve");
	}
};

QTEST_GUILESS_MAIN(AutomationClipSnapshotTest)

#include "AutomationClipSnapshotTest.moc"
