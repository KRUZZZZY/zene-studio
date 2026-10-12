/*
 * AutomationLanesTest.cpp - automation lanes, trim, shapes, transform, thinning, curves
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

/*! 0.5.0 L06 over a real song:
 *  - ED-184 drawShape writes the shape's values at the steps; ED-186 transformRange moves and
 *    rescales a range; ED-187 thin removes collinear points and keeps the corners;
 *  - ED-185 a point's own curve (hold, linear, bend) decides the segment after it, and survives
 *    a save/load;
 *  - ED-199 a relative clip offsets the absolute clip's value (automatedValuesFromTracks);
 *  - ED-181 automation.lane_add puts a following lane right under the track, one undo removes
 *    it; ED-190 clip.move carries the lane's points; the follow survives a save/load;
 *  - ED-191 automation.last_touched names the parameter a setValue last changed;
 *  - the same edits through the surface: automation.draw_shape, thin, transform and set_curve on
 *    the parameter's own clip (set_curve one undo step), and automation.add_trim's relative lane. */

#include <QtTest>

#include <cmath>
#include <limits>
#include <utility>

#include <QDomDocument>
#include <QJsonArray>

#include "AutomationClip.h"
#include "AutomationNode.h"
#include "AutomationTrack.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "ReversibilityTestSupport.h"
#include "Song.h"
#include "TrackContainer.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

AutomationClip* freshClip(FloatModel* model)
{
	auto* track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
	auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
	clip->addObject(model);
	clip->changeLength(TimePos(4 * 192));
	return clip;
}

//! The points the mirrored-segment test draws on both clips, so a flipped clip and the clip it was
//! flipped from can be read against each other tick for tick.
const std::pair<int, float> MirrorPoints[] = { {0, 0.f}, {48, 0.25f}, {96, 0.75f}, {192, 0.1f} };

//! A fresh clip carrying MirrorPoints, with @a draw setting the curves over them (never read back).
template <typename Draw>
AutomationClip* clipWithCurves(FloatModel* model, Draw draw)
{
	AutomationClip* clip = freshClip(model);
	clip->setProgressionType(AutomationClip::ProgressionType::Linear);
	for (const auto& point : MirrorPoints) { clip->putValue(TimePos(point.first), point.second, false, true); }
	draw(*clip);
	return clip;
}

//! The first parameter automation.get_state names for @a track (the instrument's own), or empty.
QString firstParameter(const QString& track)
{
	const QJsonArray parameters = run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), track}}).result
		.value(QStringLiteral("tracks")).toArray().first().toObject().value(QStringLiteral("parameters")).toArray();
	return parameters.isEmpty() ? QString() : parameters.first().toObject().value(QStringLiteral("id")).toString();
}

//! @a extra plus the track and parameter an automation command addresses.
QJsonObject addressed(const QString& track, const QString& parameter, QJsonObject extra = {})
{
	extra.insert(QStringLiteral("track"), track);
	extra.insert(QStringLiteral("parameter"), parameter);
	return extra;
}

//! The model behind @a parameter of @a track, through the commands' own resolver.
AutomatableModel* modelOf(const QString& track, const QString& parameter)
{
	ControlTarget target;
	control::AutomationParameter found;
	ControlResult error;
	return control::resolveAutomationParameter(addressed(track, parameter), &target, &found, &error) ? found.model : nullptr;
}

QJsonObject automationOf(const ControlResult& reply)
{
	return reply.result.value(QStringLiteral("automation")).toObject();
}

} // namespace

class AutomationLanesTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // a real instrument, whose parameters a lane can take
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		m_track->loadInstrument(QStringLiteral("tripleoscillator"));
	}

	void shapesTransformAndThinning()
	{
		AutomationClip* clip = freshClip(m_track->volumeModel());
		clip->drawShape(0, 192, AutomationClip::Shape::Square, 1, 0.f, 100.f, 48);
		QCOMPARE(clip->getTimeMap().size(), 5);
		QCOMPARE(clip->getTimeMap().value(0).getOutValue(), 100.f);  // the square's first half is high
		QCOMPARE(clip->getTimeMap().value(144).getOutValue(), 0.f);
		clip->drawShape(0, 192, AutomationClip::Shape::Saw, 1, 0.f, 100.f, 96);
		QCOMPARE(clip->getTimeMap().value(96).getOutValue(), 50.f);

		QCOMPARE(clip->transformRange(0, 192, 2.0, 0, 1.0, 10.0), 3);
		QVERIFY(clip->getTimeMap().contains(384));  // 192 scaled by 2 about 0
		QCOMPARE(clip->getTimeMap().value(192).getOutValue(), 60.f);  // 50 + 10, moved from 96

		clip->clear();
		for (const int tick : {0, 48, 96, 144, 192}) { clip->putValue(TimePos(tick), tick / 2.f, false, true); }  // a straight line
		clip->putValue(TimePos(240), 0.f, false, true);  // and a corner
		QCOMPARE(clip->thin(0.5f), 3);
		QVERIFY(clip->getTimeMap().contains(0) && clip->getTimeMap().contains(192) && clip->getTimeMap().contains(240));
	}

	/*! ED-184 with a range and a step that name an impractical number of points (the same
	 *  shape as 0..INT_MAX by 1, scaled down so the unfixed loop terminates): the draw
	 *  must be refused outright, and it must leave the clip's own points alone rather
	 *  than clearing the range first and then inserting 200000 nodes into it. */
	void aDrawThatWouldNameBillionsOfPointsIsRefused()
	{
		AutomationClip* clip = freshClip(m_track->volumeModel());
		clip->putValue(TimePos(96), 42.f, false, true);  // inside the refused range: a refusal must not clear it
		const int before = static_cast<int>(clip->getTimeMap().size());

		clip->drawShape(0, 200000, AutomationClip::Shape::Saw, 1, 0.f, 100.f, 1);

		QCOMPARE(static_cast<int>(clip->getTimeMap().size()), before);
		QVERIFY(clip->getTimeMap().contains(96));
		QCOMPARE(clip->getTimeMap().value(96).getOutValue(), 42.f);

		// A draw inside the limit still draws: the cap is a ceiling, not a refusal of everything.
		clip->drawShape(384, 576, AutomationClip::Shape::Saw, 1, 0.f, 100.f, 48);
		QCOMPARE(static_cast<int>(clip->getTimeMap().size()), before + 5);
	}

	/*! ED-186 with inputs the tick arithmetic cannot represent: a time scale and an
	 *  offset past the end of int, and a scale that is not a number at all. Every
	 *  resulting point must still be a real tick in [0, INT_MAX] - the unfixed code
	 *  lround()s a double far outside long's range and casts the result to int. */
	void aTransformWithUnrepresentableInputsStaysInTickRange()
	{
		AutomationClip* clip = freshClip(m_track->volumeModel());
		clip->putValue(TimePos(0), 0.f, false, true);
		clip->putValue(TimePos(96), 100.f, false, true);
		clip->putValue(TimePos(192), 0.f, false, true);

		// A scale that keeps every destination distinct but sends them a very long way: each
		// point must still be a real tick, and the point count must be unchanged.
		QCOMPARE(clip->transformRange(0, 192, 1.0e6, 0, 1.0, 0.0), 3);
		QCOMPARE(static_cast<int>(clip->getTimeMap().size()), 3);
		QVERIFY(clip->getTimeMap().contains(192000000));
		for (auto it = clip->getTimeMap().begin(); it != clip->getTimeMap().end(); ++it)
		{
			QVERIFY(it.key() >= 0);
			QVERIFY2(it.value().getOutValue() >= clip->getMin() && it.value().getOutValue() <= clip->getMax(),
				"a transformed point's value left the clip's range");
		}

		// A scale so large that every destination clamps onto the same tick is refused as
		// a collision, and leaves the clip exactly as it was.
		AutomationClip* collapsed = freshClip(m_track->volumeModel());
		for (const int tick : {0, 96, 192}) { collapsed->putValue(TimePos(tick), tick / 2.f, false, true); }
		QCOMPARE(collapsed->transformRange(0, 192, 1.0e18, 0, 1.0, 0.0), 0);
		QCOMPARE(static_cast<int>(collapsed->getTimeMap().size()), 3);
		QVERIFY(collapsed->getTimeMap().contains(192));
		QCOMPARE(collapsed->getTimeMap().value(96).getOutValue(), 48.f);

		// The same for a scale that is not finite: nothing may be written at all.
		AutomationClip* other = freshClip(m_track->volumeModel());
		other->putValue(TimePos(0), 10.f, false, true);
		other->putValue(TimePos(96), 90.f, false, true);
		QCOMPARE(other->transformRange(0, 96, std::numeric_limits<double>::infinity(), 0, 1.0, 0.0), 0);
		QCOMPARE(static_cast<int>(other->getTimeMap().size()), 2);
		QCOMPARE(other->getTimeMap().value(0).getOutValue(), 10.f);

		// An ordinary transform is unaffected: the guards are for the extremes.
		QCOMPARE(other->transformRange(0, 96, 2.0, 0, 1.0, 0.0), 2);
		QVERIFY(other->getTimeMap().contains(192));
	}

	/*! ED-186 where two source points round onto one tick: the transform must refuse
	 *  rather than let the later point silently replace the earlier one while still
	 *  reporting every source point as moved. Automation was lost and the count lied. */
	void aTransformThatWouldLandTwoPointsOnOneTickIsRefused()
	{
		AutomationClip* clip = freshClip(m_track->volumeModel());
		clip->putValue(TimePos(0), 0.f, false, true);
		clip->putValue(TimePos(1), 100.f, false, true);
		clip->putValue(TimePos(2), 0.f, false, true);

		// Halving the range rounds ticks 1 and 2 both onto 1.
		QCOMPARE(clip->transformRange(0, 2, 0.5, 0, 1.0, 0.0), 0);
		QCOMPARE(static_cast<int>(clip->getTimeMap().size()), 3);
		QCOMPARE(clip->getTimeMap().value(1).getOutValue(), 100.f);  // neither point was overwritten
		QCOMPARE(clip->getTimeMap().value(2).getOutValue(), 0.f);

		// And a moved point must not land on an unmoved one outside the range either.
		AutomationClip* onto = freshClip(m_track->volumeModel());
		onto->putValue(TimePos(0), 10.f, false, true);
		onto->putValue(TimePos(192), 90.f, false, true);  // outside [0, 96]
		QCOMPARE(onto->transformRange(0, 96, 0.0, 192, 1.0, 0.0), 0);
		QCOMPARE(static_cast<int>(onto->getTimeMap().size()), 2);
		QCOMPARE(onto->getTimeMap().value(192).getOutValue(), 90.f);

		// A transform whose destinations are all distinct still moves every point.
		QCOMPARE(onto->transformRange(0, 96, 2.0, 0, 1.0, 0.0), 1);
		QVERIFY(onto->getTimeMap().contains(0));  // the in-range point stayed, the out-of-range one was untouched
		QCOMPARE(onto->getTimeMap().value(192).getOutValue(), 90.f);
	}

	/*! ED-186 with a point whose tangents were locked by hand: the transform's
	 *  replacement node carries the values, the curve and the bend, but not the tangent
	 *  values or the lock flag, so generateTangents() overwrote a hand-drawn slope and
	 *  an identity transform changed the curve. */
	void aRangeTransformKeepsManuallyLockedTangents()
	{
		AutomationClip* clip = freshClip(m_track->volumeModel());
		clip->setProgressionType(AutomationClip::ProgressionType::Linear);
		clip->putValue(TimePos(0), 0.f, false, true);
		clip->putValue(TimePos(100), 100.f, false, true);
		clip->setNodeTangents(TimePos(0), 0.f, 0.25f, true);

		// An identity transform: every destination is where its point already was.
		QCOMPARE(clip->transformRange(0, 100, 1.0, 0, 1.0, 0.0), 2);
		QVERIFY(clip->getTimeMap().value(0).lockedTangents());
		QCOMPARE(clip->getTimeMap().value(0).getOutTangent(), 0.25f);
		QCOMPARE(clip->getTimeMap().value(0).getInTangent(), 0.f);
	}

	/*! A one-segment cubic clip whose ENDPOINT tangents were locked by hand, so the segment's
	 *  shape comes from those locks instead of from generateTangents(). Built twice, so one copy
	 *  can be transformed and read back against the untouched other. */
	AutomationClip* handLockedSegment(FloatModel* model)
	{
		AutomationClip* clip = freshClip(model);
		clip->setProgressionType(AutomationClip::ProgressionType::CubicHermite);
		clip->putValue(TimePos(0), 0.f, false, true);
		clip->putValue(TimePos(100), 100.f, false, true);
		clip->setNodeTangents(TimePos(0), 0.5f, 0.5f, true);
		clip->setNodeTangents(TimePos(100), -0.25f, -0.25f, true);
		return clip;
	}

	/*! Completes review of b76118bc8: that commit kept a locked point's tangent VALUE across a
	 *  range transform. A tangent is a slope - a value per tick - because generateTangents()
	 *  divides a value difference by a tick difference (src/core/AutomationClip.cpp:1292) and
	 *  valueAt()'s cubic branch multiplies it back by the segment's tick span
	 *  (src/core/AutomationClip.cpp:651). So a tangent copied unchanged through a rescaling
	 *  transform draws the wrong curve: right only for the identity and for a pure offset.
	 *
	 *  Checked as sound: the transformed clip must read, at every stretched tick, the original's
	 *  value at the source tick mapped through the value transform. The value transform is affine
	 *  about the clip's midpoint, so a correctly rescaled tangent reproduces it exactly. */
	void aRangeTransformRescalesManuallyLockedTangent()
	{
		AutomationClip* original = handLockedSegment(m_track->volumeModel());
		AutomationClip* stretched = handLockedSegment(m_track->volumeModel());
		const float pivot = (original->getMin() + original->getMax()) / 2.f;

		// Time doubled, value halved about the midpoint: 0 and 100 become 25 and 75.
		QCOMPARE(stretched->transformRange(0, 100, 2.0, 0, 0.5, 0.0), 2);
		QCOMPARE(static_cast<int>(stretched->getTimeMap().size()), 2);
		QVERIFY(stretched->getTimeMap().contains(200));
		QVERIFY(stretched->getTimeMap().value(0).lockedTangents());
		// The value per tick halves while a tick costs twice as much, so the slope is a quarter.
		QCOMPARE(stretched->getTimeMap().value(0).getOutTangent(), 0.125f);
		QCOMPARE(stretched->getTimeMap().value(0).getInTangent(), 0.125f);

		for (const int tick : {10, 25, 50, 75, 90})
		{
			const float expected = (original->valueAt(TimePos(tick)) - pivot) * 0.5f + pivot;
			const float actual = stretched->valueAt(TimePos(2 * tick));
			QVERIFY2(std::abs(actual - expected) < 0.001f,
				qPrintable(QStringLiteral("tick %1: the stretched curve is not the original's shape").arg(2 * tick)));
		}

		// Time reversed about tick 100 (time_scale is an unbounded number, so it can be negative):
		// the same curve read the other way round, so each point's two tangents trade sides and
		// negate - the segment that arrived at a point now leaves it.
		AutomationClip* reversed = handLockedSegment(m_track->volumeModel());
		QCOMPARE(reversed->transformRange(0, 100, -1.0, 200, 1.0, 0.0), 2);
		QVERIFY(reversed->getTimeMap().contains(100) && reversed->getTimeMap().contains(200));
		QCOMPARE(reversed->getTimeMap().value(200).getInTangent(), -original->getTimeMap().value(0).getOutTangent());
		QCOMPARE(reversed->getTimeMap().value(100).getOutTangent(), -original->getTimeMap().value(100).getInTangent());

		for (const int tick : {10, 25, 50, 75, 90})
		{
			QVERIFY2(std::abs(reversed->valueAt(TimePos(200 - tick)) - original->valueAt(TimePos(tick))) < 0.001f,
				qPrintable(QStringLiteral("tick %1: the reversed curve is not the original's shape").arg(200 - tick)));
		}

		// A time_scale of 0 collapses the range onto one tick, where no slope survives: the
		// tangents are carried across as they were, not turned into an infinity. Two points in the
		// range would land on that one tick, so the refusal in aTransformThatWouldLandTwoPointsOnOneTickIsRefused
		// covers that case; a lone point is the one that gets through.
		AutomationClip* lone = handLockedSegment(m_track->volumeModel());
		lone->removeNode(TimePos(0));
		QCOMPARE(lone->transformRange(100, 100, 0.0, 200, 0.5, 0.0), 1);
		QVERIFY(lone->getTimeMap().value(300).lockedTangents());
		QVERIFY(std::isfinite(lone->getTimeMap().value(300).getOutTangent()));
		QCOMPARE(lone->getTimeMap().value(300).getOutTangent(), -0.25f);
	}

	/*! ED-187 in a spline clip: a point on the straight line between its neighbours
	 *  but with its tangents locked by hand. Its OUT value lies on the line, so the
	 *  simplifier measured nothing worth keeping and removed it - and the hand-drawn
	 *  curve went with it. A locked point's shape is the point's own, so it stays. */
	void thinningKeepsPointsWhoseTangentsWereLockedByHand()
	{
		AutomationClip* clip = freshClip(m_track->volumeModel());
		clip->setProgressionType(AutomationClip::ProgressionType::CubicHermite);
		for (const int tick : {0, 96, 192, 288}) { clip->putValue(TimePos(tick), tick / 3.f, false, true); }
		// 96 and 192 both sit exactly on the line 0..288: only the lock distinguishes them.
		clip->setNodeTangents(TimePos(96), 0.f, 2.f, true);

		clip->thin(0.5f);
		QVERIFY2(clip->getTimeMap().contains(96), "a point whose tangents were locked by hand was thinned away");
		QVERIFY(clip->getTimeMap().value(96).lockedTangents());
		QCOMPARE(clip->getTimeMap().value(96).getOutTangent(), 2.f);
	}

	void aPointsOwnCurveDecidesTheSegmentAfterIt()
	{
		AutomationClip* clip = freshClip(m_track->volumeModel());
		clip->setProgressionType(AutomationClip::ProgressionType::Linear);
		clip->putValue(TimePos(0), 0.f, false, true);
		clip->putValue(TimePos(100), 100.f, false, true);
		QCOMPARE(clip->valueAt(TimePos(50)), 50.f);
		clip->setNodeCurve(TimePos(0), AutomationNode::Curve::Hold);
		QCOMPARE(clip->valueAt(TimePos(50)), 0.f);
		clip->setNodeCurve(TimePos(0), AutomationNode::Curve::Bend, 0.5f);
		QVERIFY(clip->valueAt(TimePos(50)) < 50.f);  // a positive bend starts slow

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = clip->saveState(doc, root);
		AutomationClip* loaded = freshClip(m_track->volumeModel());
		loaded->restoreState(saved);
		QCOMPARE(loaded->getTimeMap().value(0).curve(), AutomationNode::Curve::Bend);
		QCOMPARE(loaded->getTimeMap().value(0).bend(), 0.5f);
	}

	/*! A flipped range must keep each MIRRORED SEGMENT's own curve. A point's curve belongs to
	 *  the segment that STARTS at it (include/AutomationNode.h:156), and mirroring [start, end]
	 *  reverses time, so the segment A -> B inside the range comes back as B' -> A': it now starts
	 *  at the mirror of B, which is the point that has to take A's curve. Copying each point's own
	 *  curve onto its mirror moved every shape one segment along - the Hold drawn on 48 governed
	 *  48 -> 96 and came back governing the mirror of 0 -> 48 instead, so the sound changed and
	 *  not just the attributes.
	 *
	 *  Checked as sound: the same clip, flipped, must read the original's value at every mirrored
	 *  tick. A Bend's amount is negated with its segment, because bendValueAt() (in
	 *  src/core/AutomationClipTransforms.cpp) shapes the segment from the point that starts it, so
	 *  reading that segment backwards is t -> 1 - t. No step below sets a curve it then reads. */
	void aFlippedRangeKeepsEachSegmentOwnCurve()
	{
		const auto withCurves = [](AutomationClip& clip) {
			clip.setNodeCurve(TimePos(0), AutomationNode::Curve::Linear);   // 0 -> 48
			clip.setNodeCurve(TimePos(48), AutomationNode::Curve::Hold);   // 48 -> 96
			clip.setNodeCurve(TimePos(96), AutomationNode::Curve::Bend, 0.5f);  // 96 -> 192
		};
		AutomationClip* original = clipWithCurves(m_track->volumeModel(), withCurves);
		AutomationClip* flipped = clipWithCurves(m_track->volumeModel(), withCurves);

		// [0, 192] mirrored about its middle: 0 <-> 192, 48 <-> 144, 96 stays put.
		flipped->flipX(0, 192);
		QCOMPARE(static_cast<int>(flipped->getTimeMap().size()), 4);
		QVERIFY(flipped->getTimeMap().contains(144));

		// The Linear segment 0 -> 48 comes back as 144 -> 192 and the Bend 96 -> 192 as 0 -> 96:
		// read backwards, they must be the same curve at the same value.
		for (const int tick : {12, 24, 36, 120, 144, 168})
		{
			const float mirrored = original->valueAt(TimePos(tick));
			QVERIFY2(qFuzzyCompare(flipped->valueAt(TimePos(192 - tick)), mirrored),
				qPrintable(QStringLiteral("tick %1: a mirrored segment is not the original's shape").arg(192 - tick)));
		}

		// The Hold 48 -> 96 comes back as 96 -> 144, so THAT is the segment that holds - at the
		// level of the point which now starts it. (The held level itself is re-anchored by every
		// mirror: a hold is the value of its own point, and reversal carries the point's own value
		// across. What is compared is which segment holds, and it must not hold any earlier.)
		QCOMPARE(flipped->valueAt(TimePos(120)), flipped->getTimeMap().value(96).getOutValue());
		QCOMPARE(flipped->valueAt(TimePos(140)), flipped->getTimeMap().value(96).getOutValue());
	}

	void aRelativeClipOffsetsTheAbsoluteOne()
	{
		FloatModel* volume = m_track->volumeModel();
		AutomationClip* absolute = freshClip(volume);
		absolute->putValue(TimePos(0), 80.f, false, true);
		AutomationClip* trim = freshClip(volume);
		trim->setRelative(true);
		const float middle = (trim->getMin() + trim->getMax()) / 2.f;
		trim->putValue(TimePos(0), middle + 15.f, false, true);
		const AutomatedValueMap values = Engine::getSong()->automatedValuesAt(TimePos(10), -1);
		QCOMPARE(values.value(volume), 95.f);
		absolute->putValue(TimePos(0), 150.f, false, true);
		trim->putValue(TimePos(0), trim->getMax(), false, true);  // +100 on 150
		QCOMPARE(Engine::getSong()->automatedValuesAt(TimePos(10), -1).value(volume), volume->maxValue());  // clamped

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = trim->saveState(doc, root);
		QCOMPARE(saved.attribute(QStringLiteral("relative")), QStringLiteral("1"));
		QVERIFY(!absolute->saveState(doc, root).hasAttribute(QStringLiteral("relative")));
	}

	void aLaneFollowsTheTrackAndMovesWithItsClips()
	{
		const QString id = control::trackIdOf(m_track);
		const QString parameter = firstParameter(id);
		QVERIFY(!parameter.isEmpty());
		const int before = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult added = run(QStringLiteral("automation.lane_add"), {{QStringLiteral("track"), id}, {QStringLiteral("parameter"), parameter}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const auto& tracks = Engine::getSong()->tracks();
		QCOMPARE(static_cast<int>(tracks.size()), before + 1);
		auto* lane = dynamic_cast<AutomationTrack*>(tracks.at(1));  // right under the instrument track
		QVERIFY(lane != nullptr);
		QCOMPARE(lane->followsTrack(), m_track->id());

		auto* laneClip = dynamic_cast<AutomationClip*>(lane->getClips().front());
		laneClip->putValue(TimePos(48), 10.f, false, true);
		Clip* clip = m_track->createClip(TimePos(0));
		clip->changeLength(TimePos(192));
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("automation_follows_clips"), true}}).ok);
		QVERIFY(run(QStringLiteral("clip.move"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("position"), 384}}).ok);
		QVERIFY2(laneClip->getTimeMap().contains(432), "the lane's point did not move with the clip");
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(laneClip->getTimeMap().contains(48));
		QCOMPARE(clip->startPosition().getTicks(), 0);  // ONE undo takes back the whole move

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = lane->saveState(doc, root);
		// The automation track's own attribute (saveTrackSpecificSettings writes the <automationtrack> child).
		QCOMPARE(saved.firstChildElement(QStringLiteral("automationtrack")).attribute(QStringLiteral("follows")).toInt(), m_track->id());
	}

	//! ED-191: a person's (or a command's) change records the model; automation does not.
	void lastTouchedFollowsUserChangesOnly()
	{
		FloatModel* volume = m_track->volumeModel();
		volume->setValue(42.f);
		QCOMPARE(AutomatableModel::lastTouched(), volume->id());
		FloatModel* pan = m_track->panningModel();
		pan->setValue(10.f, true);  // an automated write
		QCOMPARE(AutomatableModel::lastTouched(), volume->id());
		// The track's own volume is not a device parameter the surface addresses: refused, typed.
		QCOMPARE(run(QStringLiteral("automation.last_touched")).errorKind, ControlErrorKind::NotFound);

		// An instrument parameter is one: named by the ids automation.lane_add takes back.
		const QString id = control::trackIdOf(m_track);
		const QString parameter = firstParameter(id);
		AutomatableModel* model = modelOf(id, parameter);
		QVERIFY(model != nullptr);
		model->setValue(model->value<float>() == model->maxValue<float>() ? model->minValue<float>() : model->maxValue<float>());
		const ControlResult touched = run(QStringLiteral("automation.last_touched"));
		QVERIFY2(touched.ok, qPrintable(touched.errorMessage));
		QCOMPARE(touched.result.value(QStringLiteral("track")).toString(), id);
		QCOMPARE(touched.result.value(QStringLiteral("parameter")).toString(), parameter);
	}

	//! ED-184/187/186/185 through the surface: each edit is a command on the parameter's own clip.
	void shapesThinTransformAndCurvesAreCommands()
	{
		const QString id = control::trackIdOf(m_track);
		const QString parameter = firstParameter(id);
		QVERIFY(!parameter.isEmpty());
		const ControlResult drawn = run(QStringLiteral("automation.draw_shape"), addressed(id, parameter,
			{{QStringLiteral("start"), 0}, {QStringLiteral("end"), 192}, {QStringLiteral("shape"), QStringLiteral("triangle")},
				{QStringLiteral("step"), 48}}));
		QVERIFY2(drawn.ok, qPrintable(drawn.errorMessage));
		const QJsonArray points = automationOf(drawn).value(QStringLiteral("points")).toArray();
		QCOMPARE(points.size(), 5);  // 0, 48, 96, 144, 192
		QVERIFY(points.at(2).toObject().value(QStringLiteral("value")).toDouble()
			> points.at(0).toObject().value(QStringLiteral("value")).toDouble());  // the triangle's peak

		const ControlResult thinned = run(QStringLiteral("automation.thin"), addressed(id, parameter));
		QVERIFY2(thinned.ok, qPrintable(thinned.errorMessage));
		QCOMPARE(thinned.result.value(QStringLiteral("removed")).toInt(), 2);  // the two mid-slope points
		QCOMPARE(automationOf(thinned).value(QStringLiteral("point_count")).toInt(), 3);

		const ControlResult stretched = run(QStringLiteral("automation.transform"), addressed(id, parameter,
			{{QStringLiteral("start"), 0}, {QStringLiteral("end"), 192}, {QStringLiteral("time_scale"), 2.0}}));
		QVERIFY2(stretched.ok, qPrintable(stretched.errorMessage));
		QCOMPARE(stretched.result.value(QStringLiteral("moved")).toInt(), 3);
		QCOMPARE(automationOf(stretched).value(QStringLiteral("points")).toArray().last().toObject()
			.value(QStringLiteral("ticks")).toInt(), 384);  // 192 scaled by 2 about 0

		const ControlResult held = run(QStringLiteral("automation.set_curve"), addressed(id, parameter,
			{{QStringLiteral("ticks"), 0}, {QStringLiteral("curve"), QStringLiteral("hold")}}));
		QVERIFY2(held.ok, qPrintable(held.errorMessage));
		AutomationClip* clip = control::existingAutomationClip(modelOf(id, parameter));
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->getTimeMap().value(0).curve(), AutomationNode::Curve::Hold);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one undo step
		QCOMPARE(clip->getTimeMap().value(0).curve(), AutomationNode::Curve::Clip);
	}

	//! ED-199 through the surface: automation.add_trim is a RELATIVE lane right under the track.
	void aTrimLaneIsARelativeLaneUnderTheTrack()
	{
		const QString id = control::trackIdOf(m_track);
		const QString parameter = firstParameter(id);
		QVERIFY(!parameter.isEmpty());
		const int before = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult trim = run(QStringLiteral("automation.add_trim"), addressed(id, parameter));
		QVERIFY2(trim.ok, qPrintable(trim.errorMessage));
		QVERIFY(trim.result.value(QStringLiteral("relative")).toBool());
		auto* lane = dynamic_cast<AutomationTrack*>(Engine::getSong()->tracks().at(1));  // right under the instrument track
		QVERIFY(lane != nullptr);
		QCOMPARE(control::trackIdOf(lane), trim.result.value(QStringLiteral("lane")).toString());
		QCOMPARE(lane->followsTrack(), m_track->id());
		QVERIFY(dynamic_cast<AutomationClip*>(lane->getClips().front())->isRelative());
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one undo step removes the lane
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before);
	}

	//! automation.lane_add's undo, and the creating automation.add_point's, find the track they made by
	//! its id: track.remove and its undo rebuild it as a new object, which a kept Track* does not name.
	void aMadeLaneIsFoundAgainAfterItsRemovalIsUndone()
	{
		const QString id = control::trackIdOf(m_track);
		const QString parameter = firstParameter(id);
		QVERIFY(!parameter.isEmpty());
		const ControlResult lane = run(QStringLiteral("automation.lane_add"), addressed(id, parameter));
		QVERIFY2(lane.ok, qPrintable(lane.errorMessage));
		QVERIFY(revtest::makerUndoFindsTrackAfterRemovalUndone(lane.result.value(QStringLiteral("lane")).toString()));

		AutomatableModel* model = modelOf(id, parameter);
		QVERIFY(model != nullptr);
		const ControlResult point = run(QStringLiteral("automation.add_point"), addressed(id, parameter,
			{{QStringLiteral("ticks"), 0}, {QStringLiteral("value"), static_cast<double>(model->minValue<float>())}}));
		QVERIFY2(point.ok, qPrintable(point.errorMessage));
		QVERIFY(point.result.value(QStringLiteral("created_automation_track")).toBool());
		AutomationClip* clip = control::existingAutomationClip(model);
		QVERIFY(clip != nullptr);
		QVERIFY(revtest::makerUndoFindsTrackAfterRemovalUndone(control::trackIdOf(clip->getTrack())));
	}

	/*! ED-186: a time map is keyed by CLIP-LOCAL ticks, so on a clip starting at song tick 384
	 *  the point at song tick 400 sits at local tick 16. move_point used the supplied ticks for
	 *  the lookup, the removal and the insertion alike, while its siblings (automation.set_curve
	 *  at src/core/ControlCommandsAutomationLanes.cpp:209, automation.draw_shape at :158 and
	 *  automation.transform at :172) all subtract the clip's own origin first. */
	void movePointTakesSongTicksOnAClipThatStartsLater()
	{
		const QString id = control::trackIdOf(m_track);
		const QString parameter = firstParameter(id);
		QVERIFY(!parameter.isEmpty());
		AutomatableModel* model = modelOf(id, parameter);
		QVERIFY(model != nullptr);
		const double high = model->maxValue<double>();

		// A lane whose clip starts at song tick 384, with a point 16 ticks into it (song tick 400).
		auto* track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(384)));
		clip->addObject(model);
		clip->changeLength(TimePos(4 * 192));
		clip->putValue(TimePos(16), 0.f, false, true);
		clip->putValue(TimePos(112), 0.f, false, true);
		const ControlResult moved = run(QStringLiteral("automation.move_point"), addressed(id, parameter,
			{{QStringLiteral("ticks"), 400}, {QStringLiteral("new_ticks"), 448}, {QStringLiteral("new_value"), high}}));
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCOMPARE(moved.result.value(QStringLiteral("ticks")).toInt(), 448);  // the reply reports song ticks
		QVERIFY(clip->getTimeMap().contains(64));  // 448 - 384: local, not the song tick
		QVERIFY(!clip->getTimeMap().contains(448));
		QCOMPARE(clip->getTimeMap().value(64).getOutValue(), model->inverseScaledValue(static_cast<float>(high)));
	}

	/*! ED-186 for automation.move_point's curve handling. The handler saved only the point's
	 *  value, removed the node and inserted a fresh one, so a node carrying a custom curve
	 *  (and, for Bend, a bend amount) came back with the insertion default - Hold for
	 *  AutomationClip::putValue's plain insert path, bend 0. The point's own curve is its
	 *  progression to the next point, so losing it silently rewrites the shape of the segment.
	 *
	 *  Checked as sound: the curve type AND the bend amount survive a move, on a clip that does
	 *  not start at song tick 0 so the move really relocates the node, and the curve on a
	 *  neighbouring point that the move does not touch is left alone. */
	void movePointKeepsTheCurveAndBendOfThePointItMoves()
	{
		const QString id = control::trackIdOf(m_track);
		const QString parameter = firstParameter(id);
		QVERIFY(!parameter.isEmpty());
		AutomatableModel* model = modelOf(id, parameter);
		QVERIFY(model != nullptr);
		const double high = model->maxValue<double>();

		auto* track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(384)));
		clip->addObject(model);
		clip->changeLength(TimePos(4 * 192));
		clip->putValue(TimePos(16), 0.f, false, true);   // song tick 400: the moved point
		clip->putValue(TimePos(112), 0.f, false, true);  // song tick 496: not touched
		clip->setNodeCurve(TimePos(16), AutomationNode::Curve::Bend, 0.6f);
		clip->setNodeCurve(TimePos(112), AutomationNode::Curve::Hold);
		const ControlResult moved = run(QStringLiteral("automation.move_point"), addressed(id, parameter,
			{{QStringLiteral("ticks"), 400}, {QStringLiteral("new_ticks"), 448}, {QStringLiteral("new_value"), high}}));
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QVERIFY(clip->getTimeMap().contains(64));  // 448 - 384
		const AutomationNode& node = clip->getTimeMap()[TimePos(64)];
		QCOMPARE(int(node.curve()), int(AutomationNode::Curve::Bend));
		QVERIFY(qAbs(node.bend() - 0.6f) < 1e-6f);
		QCOMPARE(int(clip->getTimeMap()[TimePos(112)].curve()), int(AutomationNode::Curve::Hold));
	}

	/*! ED-186: a time map is keyed by tick, so moving a point onto an occupied tick silently
	 *  overwrote that point. The request must be refused with both points intact; a move onto a
	 *  point's OWN tick is a plain value change and must still succeed. */
	void movePointRefusesADestinationAnotherPointAlreadyHolds()
	{
		const QString id = control::trackIdOf(m_track);
		const QString parameter = firstParameter(id);
		QVERIFY(!parameter.isEmpty());
		AutomatableModel* model = modelOf(id, parameter);
		QVERIFY(model != nullptr);
		const auto low = model->minValue<float>();
		const auto high = model->maxValue<float>();

		auto* track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
		clip->addObject(model);
		clip->changeLength(TimePos(4 * 192));
		clip->putValue(TimePos(10), low, false, true);
		clip->putValue(TimePos(20), high, false, true);
		const ControlResult moved = run(QStringLiteral("automation.move_point"), addressed(id, parameter,
			{{QStringLiteral("ticks"), 10}, {QStringLiteral("new_ticks"), 20}, {QStringLiteral("new_value"), static_cast<double>(low)}}));
		QVERIFY(!moved.ok);  // refused, not a silent overwrite
		QVERIFY(clip->getTimeMap().contains(10));  // the source survived the refusal
		QVERIFY(clip->getTimeMap().contains(20));  // and so did the point it tried to move onto
		QCOMPARE(clip->getTimeMap().value(20).getOutValue(), high);
		// Moving a point onto its own tick is a plain value change, and must still work.
		const ControlResult inPlace = run(QStringLiteral("automation.move_point"), addressed(id, parameter,
			{{QStringLiteral("ticks"), 20}, {QStringLiteral("new_ticks"), 20}, {QStringLiteral("new_value"), 0.5}}));
		QVERIFY2(inPlace.ok, qPrintable(inPlace.errorMessage));
		QVERIFY(clip->getTimeMap().contains(10));  // its neighbour is not in the way
		QVERIFY(qAbs(clip->getTimeMap().value(20).getOutValue()
				- model->scaledValue(model->inverseScaledValue(0.5f))) < 1e-6f);
	}

private:
	InstrumentTrack* m_track = nullptr;
};

QTEST_MAIN(AutomationLanesTest)
#include "AutomationLanesTest.moc"
