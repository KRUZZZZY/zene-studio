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

#include <limits>

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

	void aPointsOwnCurveDecidesTheSegmentAfterIt()
	{
		AutomationClip* clip = freshClip(m_track->volumeModel());
		clip->setProgressionType(AutomationClip::ProgressionType::Linear);
		clip->putValue(TimePos(0), 0.f, false, true);
		clip->putValue(TimePos(100), 100.f, false, true);
		QCOMPARE(clip->valueAt(TimePos(50)), 50.f);
		clip->getTimeMap()[0].setCurve(AutomationNode::Curve::Hold);
		QCOMPARE(clip->valueAt(TimePos(50)), 0.f);
		clip->getTimeMap()[0].setCurve(AutomationNode::Curve::Bend, 0.5f);
		QVERIFY(clip->valueAt(TimePos(50)) < 50.f);  // a positive bend starts slow

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = clip->saveState(doc, root);
		AutomationClip* loaded = freshClip(m_track->volumeModel());
		loaded->restoreState(saved);
		QCOMPARE(loaded->getTimeMap().value(0).curve(), AutomationNode::Curve::Bend);
		QCOMPARE(loaded->getTimeMap().value(0).bend(), 0.5f);
	}

	/*! A flipped range must keep each point's OWN curve. The replacement nodes
	 *  a mirror builds carry the values but nothing else, so a Hold or a Bend
	 *  drawn inside the range came back as the default Clip progression - the
	 *  curve survived the flip on paper and was gone in the sound. */
	void aFlippedRangeKeepsEachPointsOwnCurve()
	{
		AutomationClip* clip = freshClip(m_track->volumeModel());
		clip->setProgressionType(AutomationClip::ProgressionType::Linear);
		for (const int tick : {0, 48, 96, 192}) { clip->putValue(TimePos(tick), tick / 2.f, false, true); }
		clip->getTimeMap()[48].setCurve(AutomationNode::Curve::Hold);
		clip->getTimeMap()[96].setCurve(AutomationNode::Curve::Bend, 0.5f);
		clip->getTimeMap()[0].setCurve(AutomationNode::Curve::Linear);

		// [0, 192] mirrored about its middle: 48 <-> 144, 96 stays put.
		clip->flipX(0, 192);
		QVERIFY(clip->getTimeMap().contains(144));
		QCOMPARE(clip->getTimeMap().value(144).curve(), AutomationNode::Curve::Hold);
		QCOMPARE(clip->getTimeMap().value(96).curve(), AutomationNode::Curve::Bend);
		QCOMPARE(clip->getTimeMap().value(96).bend(), 0.5f);
		QCOMPARE(clip->getTimeMap().value(0).curve(), AutomationNode::Curve::Linear);

		// And the curve still decides the curve: the Hold point at 144 holds its
		// own value through the segment that follows it.
		clip->putValue(TimePos(192), 200.f, false, true);
		clip->getTimeMap()[144].setCurve(AutomationNode::Curve::Hold);
		QCOMPARE(clip->valueAt(TimePos(170)), clip->getTimeMap().value(144).getOutValue());
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

private:
	InstrumentTrack* m_track = nullptr;
};

QTEST_MAIN(AutomationLanesTest)
#include "AutomationLanesTest.moc"
