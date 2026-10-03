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
 *  - ED-191 automation.last_touched names the parameter a setValue last changed. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include "AutomationClip.h"
#include "AutomationNode.h"
#include "AutomationTrack.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
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

} // namespace

class AutomationLanesTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
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
		trim->putValue(TimePos(0), trim->getMax(), false, true);
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
		const QJsonArray parameters = run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), id}}).result
			.value(QStringLiteral("tracks")).toArray().first().toObject().value(QStringLiteral("parameters")).toArray();
		QVERIFY(!parameters.isEmpty());
		const QString parameter = parameters.first().toObject().value(QStringLiteral("id")).toString();
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

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement saved = lane->saveState(doc, root);
		QCOMPARE(saved.attribute(QStringLiteral("follows")).toInt(), m_track->id());
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
	}

private:
	InstrumentTrack* m_track = nullptr;
};

QTEST_MAIN(AutomationLanesTest)
#include "AutomationLanesTest.moc"
