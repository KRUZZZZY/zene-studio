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
 *  - the fold flag and the follow relation both survive a save/load round trip;
 *  - a LEGACY owner (a project saved before lanes could fold: no `autolanes` attribute) loads
 *    EXPANDED - missing attribute means shown, on a fresh load and on a restore over a folded
 *    track - and an owner that was never folded saves no attribute at all. */

#include <QtTest>

#include <cmath>
#include <limits>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationPointEdit.h"
#include "AutomationTrack.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
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

//! The registry's most recent recorded transaction for @a command (the
//! `__transaction` payload is a side channel the result does not expose).
QJsonObject lastTransaction(const QString& command)
{
	const QJsonArray records = ControlRegistry::instance()->transactions();
	for (int i = records.size() - 1; i >= 0; --i)
	{
		const QJsonObject record = records.at(i).toObject();
		if (record.value(QStringLiteral("command")).toString() == command) { return record; }
	}
	return {};
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

//! A fresh clip starting at song tick 0 on a new automation track, carrying @a model.
AutomationClip* freshClip(AutomatableModel* model)
{
	auto* track = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
	auto* clip = dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
	clip->addObject(model);
	clip->changeLength(TimePos(4 * 192));
	return clip;
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

		// The transaction must say what the inverse captures: the owner AND its
		// child lanes, as one all-or-nothing group.
		const QJsonObject tx = lastTransaction(QStringLiteral("track.remove"));
		QCOMPARE(tx.value(QStringLiteral("reversible")).toBool(), true);
		QVERIFY2(tx.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString()
			.contains(QStringLiteral("child automation lanes")),
			"track.remove's inverse does not name the lanes it captures");
		QVERIFY2(tx.value(QStringLiteral("mechanism")).toString().contains(QStringLiteral("automation lanes")),
			"track.remove's mechanism does not describe capturing the lanes");

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

	/*! The inverse is all-or-nothing: when a child lane cannot be captured (its own
	 *  XML is over the structural cap) NO inverse is recorded, and track.remove's
	 *  transaction says so - naming the lanes, not pretending the owner alone was
	 *  captured. The delete itself still happens. */
	void anUncapturableLaneRefusesTheInverseHonestly()
	{
		const QString owner = control::trackIdOf(m_track);
		const ControlResult added = run(QStringLiteral("automation.lane_add"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("parameter"), firstParameter(owner)}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString laneId = added.result.value(QStringLiteral("lane")).toString();
		auto* lane = dynamic_cast<AutomationTrack*>(Engine::getSong()->findTrackById(laneId.mid(4).toInt()));
		QVERIFY(lane != nullptr);
		auto* clip = dynamic_cast<AutomationClip*>(lane->getClips().front());
		QVERIFY(clip != nullptr);
		// Only the LANE is over the cap; the owner alone would fit. 5000 nodes
		// serialize to well over the 64 KB StructuralSnapshotLimit.
		for (int tick = 0; tick < 5000; ++tick)
		{
			clip->getTimeMap()[tick] = AutomationNode(clip, 0.5f, tick);
		}

		const int before = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult removed = run(QStringLiteral("track.remove"), {{QStringLiteral("track"), owner}});
		QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
		// The delete still happens with its lanes; only the undo is refused.
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), before - 2);
		QCOMPARE(removed.result.value(QStringLiteral("lanes_removed")).toInt(), 1);

		const QJsonObject tx = lastTransaction(QStringLiteral("track.remove"));
		QCOMPARE(tx.value(QStringLiteral("reversible")).toBool(), false);
		QVERIFY2(tx.value(QStringLiteral("inverse")).toObject().value(QStringLiteral("op")).toString()
			.contains(QStringLiteral("could not all be captured")),
			"the refused inverse does not say the group could not all be captured");
		QVERIFY2(tx.value(QStringLiteral("mechanism")).toString().contains(QStringLiteral("child automation lanes")),
			"the refused inverse's mechanism does not name a failed child lane capture");
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

	//! Luna r6 rework (2): the ABSENT-attribute case. An owner that was never folded writes no
	//! `autolanes` (the bytes an older build wrote), a project file without it - saved by an older
	//! build - loads with the lane visible (owner expanded, lane still following it), a restore of
	//! such an element over a FOLDED live track unfolds it, and the same file WITH the attribute
	//! loads folded (so the assertions above can tell the two apart).
	void aLegacyOwnerWithoutTheFoldAttributeLoadsExpanded()
	{
		const QString owner = control::trackIdOf(m_track);
		QVERIFY(run(QStringLiteral("automation.lane_add"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("parameter"), firstParameter(owner)}}).ok);

		// Never folded: the owner's own element carries no attribute.
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		const QDomElement neverFolded = m_track->saveState(doc, root);
		QVERIFY2(!neverFolded.hasAttribute(QStringLiteral("autolanes")), "a never-folded owner wrote the fold attribute");

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString folded = QDir(dir.path()).filePath(QStringLiteral("folded.mmp"));
		const QString legacy = QDir(dir.path()).filePath(QStringLiteral("legacy.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(legacy));
		QVERIFY(!fileText(legacy).contains(QStringLiteral("autolanes")));  // the bytes of a project older than the fold
		QVERIFY(run(QStringLiteral("automation.lanes_set_collapsed"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("collapsed"), true}}).ok);
		QVERIFY(Engine::getSong()->saveProjectFile(folded));
		QVERIFY(fileText(folded).contains(QStringLiteral("autolanes=\"1\"")));

		// A restore of the legacy element over the FOLDED live track unfolds it (reset on absence).
		QVERIFY(m_track->automationLanesCollapsed());
		m_track->restoreState(neverFolded);
		QCOMPARE(m_track->automationLanesCollapsed(), false);
		QVERIFY(m_track->hasAutomationLaneChildren());

		// The legacy project file loads with its lane visible: expanded owner, the lane still its child.
		Engine::getSong()->loadProject(legacy);
		QJsonObject loaded;
		QVERIFY2(loadedOwnerState(&loaded), "the legacy project lost its owner");
		QCOMPARE(loaded.value(QStringLiteral("lanes_collapsed")).toBool(), false);
		QCOMPARE(loaded.value(QStringLiteral("automation_lane_count")).toInt(), 1);
		Track* loadedOwner = loadedInstrument();
		QVERIFY(loadedOwner != nullptr);
		QCOMPARE(loadedOwner->automationLanesCollapsed(), false);
		QCOMPARE(static_cast<int>(loadedOwner->automationLaneChildren().size()), 1);

		// The same project saved folded loads folded.
		Engine::getSong()->loadProject(folded);
		QVERIFY(loadedOwnerState(&loaded));
		QCOMPARE(loaded.value(QStringLiteral("lanes_collapsed")).toBool(), true);
		QCOMPARE(loaded.value(QStringLiteral("automation_lane_count")).toInt(), 1);
	}

	/*! A NaN is no automation value: std::clamp hands it back unchanged (every comparison with NaN is false), so the
	 *  in-place path wrote it into the time map, where it poisons the curve and the audio it drives. The track's volume
	 *  is a control no command can name, so movePoint takes that in-place path: the NaN move is refused with both
	 *  points as they were, and a finite move onto the same tick still works. */
	void movePointRefusesANonFiniteValueOnAnInPlaceClip()
	{
		AutomationClip* clip = freshClip(m_track->volumeModel());
		clip->putValue(TimePos(10), 0.5f, false, true);
		QString error;
		QVERIFY(!gui::automationedit::movePoint(clip, 10, 20, std::numeric_limits<float>::quiet_NaN(), &error));
		QVERIFY(!error.isEmpty());
		QVERIFY(clip->getTimeMap().contains(10));  // the source survived the refusal
		QVERIFY(!clip->getTimeMap().contains(20));  // and nothing was written at the destination
		QCOMPARE(clip->getTimeMap().value(10).getOutValue(), 0.5f);
		QVERIFY2(gui::automationedit::movePoint(clip, 10, 20, 0.25f, &error), qPrintable(error));
		QCOMPARE(clip->getTimeMap().value(20).getOutValue(), 0.25f);
	}

	/*! The command's own range check is two comparisons, and NaN passes both, so an in-process caller's NaN new_value
	 *  (a script's 0/0, say) was stored as the moved point's value. The command refuses it with the points as they were. */
	void movePointCommandRefusesANonFiniteValue()
	{
		const QString id = control::trackIdOf(m_track);
		const QString parameter = firstParameter(id);
		QVERIFY(!parameter.isEmpty());
		AutomatableModel* model = modelOf(id, parameter);
		QVERIFY(model != nullptr);
		AutomationClip* clip = freshClip(model);
		clip->putValue(TimePos(10), model->minValue<float>(), false, true);
		const ControlResult moved = run(QStringLiteral("automation.move_point"), addressed(id, parameter,
			{{QStringLiteral("ticks"), 10}, {QStringLiteral("new_ticks"), 20},
				{QStringLiteral("new_value"), std::numeric_limits<double>::quiet_NaN()}}));
		QVERIFY(!moved.ok);  // refused, not stored
		QVERIFY(clip->getTimeMap().contains(10));  // the source survived the refusal
		QVERIFY(!clip->getTimeMap().contains(20));
		QVERIFY(!std::isnan(clip->getTimeMap().value(10).getOutValue()));
	}

private:
	static QString fileText(const QString& path)
	{
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
	}

	//! The instrument track of the (just loaded) song, nullptr when there is none.
	static Track* loadedInstrument()
	{
		for (Track* track : Engine::getSong()->tracks())
		{
			if (dynamic_cast<InstrumentTrack*>(track) != nullptr) { return track; }
		}
		return nullptr;
	}

	//! automation.get_state of the loaded song's instrument track; false when there is none.
	static bool loadedOwnerState(QJsonObject* out)
	{
		Track* owner = loadedInstrument();
		if (owner == nullptr) { return false; }
		*out = stateOf(control::trackIdOf(owner));
		return true;
	}

	InstrumentTrack* m_track = nullptr;
};

QTEST_MAIN(AutomationLaneChildrenTest)
#include "AutomationLaneChildrenTest.moc"
