/*
 * AutomationLaneChildrenTest.cpp - automation lanes as track children (Q050-022)
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

/*! 0.5.0 L06, Q050-022 (R-workflow-197): the model half of "automation lanes as
 *  track children".
 *
 *  - automation.get_state reports each lane's OWNER (`follows`) and each owner's
 *    child-lane count and fold flag, from the same fields the arranger renders;
 *  - automation.lanes_set_collapsed folds/unfolds the owner's lanes as ONE undo
 *    step (a live Track checkpoint - the owner's own `autolanes` attribute);
 *  - deleting an owner deletes its child lanes as ONE undo step, and the undo
 *    brings them back with their `follows` intact (a lane cannot outlive its
 *    owner pointing at a track that is gone);
 *  - an owner reorder (track.move) keeps each lane's `follows`;
 *  - the fold flag and the follow relation both survive a save/load round trip. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The first parameter automation.get_state names for @a track (the instrument's own).
QString firstParameter(const QString& track)
{
	const QJsonArray parameters = run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), track}})
		.result.value(QStringLiteral("tracks")).toArray().first().toObject()
		.value(QStringLiteral("parameters")).toArray();
	return parameters.isEmpty() ? QString() : parameters.first().toObject().value(QStringLiteral("id")).toString();
}

//! The single get_state entry for @a track.
QJsonObject stateOf(const QString& track)
{
	return run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), track}})
		.result.value(QStringLiteral("tracks")).toArray().first().toObject();
}

} // namespace

class AutomationLaneChildrenTest : public QObject
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

	//! ownership + child-lane count are readable from the surface, both ways round.
	void theSurfaceReportsTheOwnerAndTheChildCount()
	{
		const QString owner = control::trackIdOf(m_track);
		const ControlResult added = run(QStringLiteral("automation.lane_add"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("parameter"), firstParameter(owner)}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString laneId = added.result.value(QStringLiteral("lane")).toString();

		const QJsonObject ownerState = stateOf(owner);
		QCOMPARE(ownerState.value(QStringLiteral("automation_lane_count")).toInt(), 1);
		QCOMPARE(ownerState.value(QStringLiteral("lanes_collapsed")).toBool(), false);
		QVERIFY(ownerState.value(QStringLiteral("follows")).toString().isEmpty());

		const QJsonObject laneState = stateOf(laneId);
		QCOMPARE(laneState.value(QStringLiteral("follows")).toString(), owner);
		QCOMPARE(laneState.value(QStringLiteral("automation_lane_count")).toInt(), 0);
	}

	//! one fold is one undo step, and the read agrees before and after both ways.
	void foldingIsOneUndoStepAndRoundTrips()
	{
		const QString owner = control::trackIdOf(m_track);
		QVERIFY(run(QStringLiteral("automation.lane_add"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("parameter"), firstParameter(owner)}}).ok);
		QCOMPARE(stateOf(owner).value(QStringLiteral("lanes_collapsed")).toBool(), false);

		const ControlResult folded = run(QStringLiteral("automation.lanes_set_collapsed"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("collapsed"), true}});
		QVERIFY2(folded.ok, qPrintable(folded.errorMessage));
		QCOMPARE(folded.result.value(QStringLiteral("collapsed")).toBool(), true);
		QCOMPARE(folded.result.value(QStringLiteral("lane_count")).toInt(), 1);
		QCOMPARE(stateOf(owner).value(QStringLiteral("lanes_collapsed")).toBool(), true);
		QCOMPARE(m_track->automationLanesCollapsed(), true);
		QCOMPARE(m_track->hasAutomationLaneChildren(), true);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(m_track->automationLanesCollapsed(), false);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(m_track->automationLanesCollapsed(), true);

		// A fold with no lanes is still an honest, reversible flag change.
		const ControlResult noLanes = run(QStringLiteral("automation.lanes_set_collapsed"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("collapsed"), false}});
		QVERIFY(noLanes.ok);
	}

	/*! Deleting the owner takes its child lanes with it as ONE undo step: after
	 *  the undo the lane is back and still FOLLOWS the owner, and the point it held
	 *  came back with it. */
	void deletingTheOwnerRemovesLanesAndUndoRestoresThem()
	{
		const QString owner = control::trackIdOf(m_track);
		const ControlResult added = run(QStringLiteral("automation.lane_add"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("parameter"), firstParameter(owner)}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString laneId = added.result.value(QStringLiteral("lane")).toString();
		auto* lane = dynamic_cast<AutomationTrack*>(Engine::getSong()->findTrackById(
			laneId.mid(4).toInt()));
		QVERIFY(lane != nullptr);
		auto* laneClip = dynamic_cast<AutomationClip*>(lane->getClips().front());
		QVERIFY(laneClip != nullptr);
		laneClip->putValue(TimePos(96), 20.f, false, true);

		const int before = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult removed = run(QStringLiteral("track.remove"), {{QStringLiteral("track"), owner}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		QCOMPARE(removed.result.value(QStringLiteral("lanes_removed")).toInt(), 1);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before - 2);

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // ONE step brings both back
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before);
		auto* restoredLane = dynamic_cast<AutomationTrack*>(Engine::getSong()->findTrackById(laneId.mid(4).toInt()));
		QVERIFY(restoredLane != nullptr);
		QCOMPARE(control::trackIdOf(Engine::getSong()->findTrackById(restoredLane->followsTrack())), owner);
		QCOMPARE(stateOf(control::trackIdOf(restoredLane)).value(QStringLiteral("follows")).toString(), owner);
		auto* restoredClip = dynamic_cast<AutomationClip*>(restoredLane->getClips().front());
		QVERIFY(restoredClip != nullptr);
		QVERIFY2(restoredClip->getTimeMap().contains(96), "the lane's point did not come back with it");

		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before - 2);
	}

	//! a reorder keeps every lane pointing at its owner.
	void reorderingTheOwnerKeepsLaneOwnership()
	{
		const QString owner = control::trackIdOf(m_track);
		const ControlResult added = run(QStringLiteral("automation.lane_add"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("parameter"), firstParameter(owner)}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString laneId = added.result.value(QStringLiteral("lane")).toString();
		Track* other = Track::create(Track::Type::Sample, Engine::getSong());

		QVERIFY(run(QStringLiteral("track.move"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("index"), 2}}).ok);
		QCOMPARE(control::trackIdOf(m_track), owner);  // same stable id, moved row
		QCOMPARE(stateOf(laneId).value(QStringLiteral("follows")).toString(), owner);
		QCOMPARE(m_track->automationLaneChildren().size(), std::size_t{1});
		QVERIFY(other != nullptr);
	}

	//! both the follow relation and the fold flag survive a save/load round trip.
	void followAndFoldSurviveSaveAndLoad()
	{
		const QString owner = control::trackIdOf(m_track);
		const ControlResult added = run(QStringLiteral("automation.lane_add"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("parameter"), firstParameter(owner)}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString laneId = added.result.value(QStringLiteral("lane")).toString();
		QVERIFY(run(QStringLiteral("automation.lanes_set_collapsed"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("collapsed"), true}}).ok);

		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));

		const QDomElement savedOwner = m_track->saveState(doc, root);
		QCOMPARE(savedOwner.attribute(QStringLiteral("autolanes")).toInt(), 1);
		Track* restoredOwner = Track::create(savedOwner, Engine::getSong());
		QVERIFY(restoredOwner != nullptr);
		QCOMPARE(restoredOwner->automationLanesCollapsed(), true);

		Track* lane = Engine::getSong()->findTrackById(laneId.mid(4).toInt());
		QVERIFY(lane != nullptr);
		const QDomElement savedLane = lane->saveState(doc, root);
		QCOMPARE(savedLane.firstChildElement(QStringLiteral("automationtrack"))
			.attribute(QStringLiteral("follows")).toInt(), m_track->id());
		Track* restoredLane = Track::create(savedLane, Engine::getSong());
		QVERIFY(restoredLane != nullptr);
		auto* restoredAutomation = dynamic_cast<AutomationTrack*>(restoredLane);
		QVERIFY(restoredAutomation != nullptr);
		QCOMPARE(restoredAutomation->followsTrack(), m_track->id());
	}

private:
	InstrumentTrack* m_track = nullptr;
};

QTEST_MAIN(AutomationLaneChildrenTest)
#include "AutomationLaneChildrenTest.moc"
