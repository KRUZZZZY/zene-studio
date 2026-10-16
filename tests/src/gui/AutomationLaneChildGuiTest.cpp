/*
 * AutomationLaneChildGuiTest.cpp - automation lanes as track children, in the arranger (Q050-022)
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

/*! 0.5.0 L06, Q050-022 (R-workflow-197), in the real application: a lane created
 *  by automation.lane_add is DRAWN as a child of its owning track in the arranger
 *  - the row sits directly beneath the owner and is indented - and folding or
 *  hiding the owner hides it. The fold the AGENT command performs - and the undo
 *  and redo of it - move the rows on their own, with no realignTracks() call in
 *  the test. A downward MOVE of the owner past a peer re-draws the lane directly
 *  beneath it on its own, too. The child lane's clip is selectable where it is drawn, a point is
 *  edited through the lane's own UI (a double-click opens its automation editor),
 *  and the agent surface (automation.get_state, automation.lanes_set_collapsed)
 *  reports the same owner and lane state. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include "AutomationClip.h"
#include "AutomationClipView.h"
#include "AutomationEditor.h"
#include "AutomationTrack.h"
#include "ClipView.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "KeyboardShortcuts.h"
#include "MainWindow.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackContentWidget.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

TrackView* viewOf(const Track* track)
{
	for (TrackView* view : getGUI()->songEditor()->m_editor->trackViews())
	{
		if (view->getTrack() == track) { return view; }
	}
	return nullptr;
}

QString firstParameter(const QString& track)
{
	const QJsonArray parameters = run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), track}})
		.result.value(QStringLiteral("tracks")).toArray().first().toObject()
		.value(QStringLiteral("parameters")).toArray();
	return parameters.isEmpty() ? QString() : parameters.first().toObject().value(QStringLiteral("id")).toString();
}

QJsonObject stateOf(const QString& track)
{
	return run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), track}})
		.result.value(QStringLiteral("tracks")).toArray().first().toObject();
}

ClipView* laneClipView(TrackView* laneView, const Clip* clip)
{
	for (ClipView* view : laneView->getTrackContentWidget()->findChildren<ClipView*>())
	{
		if (view->getClip() == clip) { return view; }
	}
	return nullptr;
}

} // namespace

class AutomationLaneChildGuiTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // a real instrument, whose parameters a lane can take
		#endif
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
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
		// The owner's row is built on the next event-loop turn (createTrackView is queued).
		QCoreApplication::processEvents();
		const ControlResult added = run(QStringLiteral("automation.lane_add"),
			{{QStringLiteral("track"), control::trackIdOf(m_track)},
				{QStringLiteral("parameter"), firstParameter(control::trackIdOf(m_track))}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		m_laneId = added.result.value(QStringLiteral("lane")).toString();
		QCoreApplication::processEvents();
		m_ownerView = viewOf(m_track);
		m_lane = dynamic_cast<AutomationTrack*>(Engine::getSong()->findTrackById(m_laneId.mid(4).toInt()));
		QVERIFY(m_lane != nullptr);
		m_laneView = viewOf(m_lane);
		QVERIFY2(m_ownerView != nullptr && m_laneView != nullptr, "a lane row is missing from the arranger");
	}

	//! the lane row sits directly beneath its owner and is indented like a child.
	void theLaneIsDrawnDirectlyUnderItsOwner()
	{
		const QList<TrackView*>& rows = getGUI()->songEditor()->m_editor->trackViews();
		const int ownerIndex = rows.indexOf(m_ownerView);
		QVERIFY(ownerIndex >= 0);
		QCOMPARE(rows.indexOf(m_laneView), ownerIndex + 1);
		QVERIFY(m_laneView->isVisible());
		QVERIFY2(m_laneView->getTrackSettingsWidget()->contentsMargins().left()
			> m_ownerView->getTrackSettingsWidget()->contentsMargins().left(),
			"the lane row is not indented under its owner");
	}

	//! the child lane's clip is selectable where the row is drawn, and a POINT EDIT
	//! made through the lane's own UI (the automation editor it opens) shows up in
	//! the agent's state - no automation.add_point anywhere.
	void theChildLaneIsSelectableAndEditable()
	{
		QVERIFY(!m_lane->getClips().empty());
		Clip* clip = m_lane->getClips().front();
		ClipView* clipView = laneClipView(m_laneView, clip);
		QVERIFY2(clipView != nullptr, "the lane's clip is not drawn on the lane's row");

		getGUI()->songEditor()->m_editor->selectAllClips(false);
		// A plain arranger click starts a MOVE (ClipView::mousePressEvent); selection
		// on a click is the copy-modifier's toggle, the same gesture the product uses.
		const QPoint centre(clipView->width() / 2, clipView->height() / 2);
		QTest::mousePress(clipView, Qt::LeftButton, KBD_COPY_MODIFIER, centre);
		QTest::mouseRelease(clipView, Qt::LeftButton, KBD_COPY_MODIFIER, centre);
		QVERIFY2(clipView->isSelected(), "a click on the child lane's clip did not select it");

		const QString owner = control::trackIdOf(m_track);
		const QString parameter = firstParameter(owner);
		QVERIFY2(!parameter.isEmpty(), "the owner has no parameter to automate");

		// A GUI gesture on the lane: double-clicking the child lane's clip opens it
		// in the automation editor (AutomationClipView::mouseDoubleClickEvent).
		QTest::mouseDClick(clipView, Qt::LeftButton, Qt::NoModifier, centre);
		QCoreApplication::processEvents();
		AutomationEditor* editor = getGUI()->automationEditor()->m_editor;
		QVERIFY2(editor->validClip() && editor->currentClip() == clip,
			"double-clicking the child lane's clip did not open it for editing");
		editor->setFocus();

		// A real point edit through that UI: Ctrl+Right chooses the point lane_add
		// seeded at tick 0, Right moves it one grid step (AutomationPointEdit - the
		// same keyboard gesture AutomationEditorEditingTest drives), one undo step.
		const int step = AutomationClip::quantization();
		QVERIFY(step > 0);
		QTest::keyClick(editor, Qt::Key_Right, Qt::ControlModifier);
		QTest::keyClick(editor, Qt::Key_Right);
		QCoreApplication::processEvents();

		// The edit the GUI made is what the agent surface now reports for that lane.
		const QJsonArray points = stateOf(owner).value(QStringLiteral("parameters")).toArray()
			.first().toObject().value(QStringLiteral("automation")).toObject()
			.value(QStringLiteral("points")).toArray();
		bool moved = false;
		for (const QJsonValue& point : points)
		{
			moved = moved || point.toObject().value(QStringLiteral("ticks")).toInt() == step;
		}
		QVERIFY2(moved, "the point edited through the lane's UI is not in automation.get_state");

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one GUI edit, one step
	}

	//! folding the owner hides its lane row; unfolding brings it back. The command
	//! alone must move the view: no realignTracks() is called here.
	void foldingTheOwnerHidesItsChildLane()
	{
		const QString owner = control::trackIdOf(m_track);
		QVERIFY(run(QStringLiteral("automation.lanes_set_collapsed"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("collapsed"), true}}).ok);
		QCoreApplication::processEvents();  // the fold signal reaches the arranger
		QVERIFY(m_ownerView->isVisible());
		QVERIFY2(!m_laneView->isVisible(), "a folded owner left its child lane row shown");
		QCOMPARE(stateOf(owner).value(QStringLiteral("lanes_collapsed")).toBool(), true);

		QVERIFY(run(QStringLiteral("automation.lanes_set_collapsed"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("collapsed"), false}}).ok);
		QCoreApplication::processEvents();
		QVERIFY2(m_laneView->isVisible(), "the lane row did not come back after unfolding");
	}

	//! hiding the owner through the command hides its child lane row. The command
	//! alone must move the view: no realignTracks() is called here.
	void hidingTheOwnerHidesItsChildLane()
	{
		const QString owner = control::trackIdOf(m_track);
		QVERIFY(run(QStringLiteral("track.set_visible"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("visible"), false}}).ok);
		QCoreApplication::processEvents();
		QVERIFY(!m_ownerView->isVisible());
		QVERIFY2(!m_laneView->isVisible(), "a hidden owner left its child lane row shown");
		// trackViewAt skips both hidden rows: the hit test agrees with what is drawn.
		QVERIFY(getGUI()->songEditor()->m_editor->trackViewAt(0) != m_laneView);

		QVERIFY(run(QStringLiteral("track.set_visible"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("visible"), true}}).ok);
		QCoreApplication::processEvents();
		QVERIFY(m_laneView->isVisible());
	}

	//! A DOWNWARD owner move (the acceptance case the round-5 review exposed):
	//! the flat list ends up with the lane BEFORE its owner, and only the
	//! regrouping inside realignTracks() - which moveTrackView() runs on its own
	//! - puts the lane back directly beneath the moved owner while the peer keeps
	//! its row. The test calls neither realignTracks() nor any fold command.
	void movingTheOwnerDownKeepsItsLaneBeneathIt()
	{
		Track* peer = Track::create(Track::Type::Sample, Engine::getSong());
		QCoreApplication::processEvents();  // the peer's row is queued
		TrackView* peerView = viewOf(peer);
		QVERIFY2(peerView != nullptr, "the peer's row is missing from the arranger");

		const QList<TrackView*>& rows = getGUI()->songEditor()->m_editor->trackViews();
		QCOMPARE(rows.indexOf(m_ownerView), 0);
		QCOMPARE(rows.indexOf(m_laneView), 1);
		QCOMPARE(rows.indexOf(peerView), 2);

		// The owner crosses both its own lane's drawn row and the peer's,
		// landing directly below the peer.
		getGUI()->songEditor()->m_editor->moveTrackView(m_ownerView, 2);
		QCoreApplication::processEvents();
		QCOMPARE(rows.indexOf(peerView), 0);
		QCOMPARE(rows.indexOf(m_ownerView), 1);
		QVERIFY2(rows.indexOf(m_laneView) == rows.indexOf(m_ownerView) + 1,
			"the redraw after a downward owner move detached its lane");
		QVERIFY(m_laneView->isVisible());
	}

	//! the agent read and the drawn state are the SAME state - on the command, and
	//! on the undo and redo of it, with no realignTracks() anywhere.
	void theAgentReportsTheDrawnOwnerAndLaneState()
	{
		const QString owner = control::trackIdOf(m_track);
		QCOMPARE(stateOf(owner).value(QStringLiteral("automation_lane_count")).toInt(), 1);
		QCOMPARE(stateOf(m_laneId).value(QStringLiteral("follows")).toString(), owner);

		QVERIFY(run(QStringLiteral("automation.lanes_set_collapsed"),
			{{QStringLiteral("track"), owner}, {QStringLiteral("collapsed"), true}}).ok);
		QCoreApplication::processEvents();
		QCOMPARE(stateOf(owner).value(QStringLiteral("lanes_collapsed")).toBool(), true);
		QVERIFY2(!m_laneView->isVisible(), "the agent folded the lanes but the arranger still draws them");

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCoreApplication::processEvents();
		QCOMPARE(stateOf(owner).value(QStringLiteral("lanes_collapsed")).toBool(), false);
		QVERIFY2(m_laneView->isVisible(), "undoing the fold left the lane hidden");

		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCoreApplication::processEvents();
		QCOMPARE(stateOf(owner).value(QStringLiteral("lanes_collapsed")).toBool(), true);
		QVERIFY2(!m_laneView->isVisible(), "redoing the fold left the lane drawn");

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCoreApplication::processEvents();
		QVERIFY(m_laneView->isVisible());
	}

private:
	QTemporaryDir m_home;
	InstrumentTrack* m_track = nullptr;
	AutomationTrack* m_lane = nullptr;
	QString m_laneId;
	TrackView* m_ownerView = nullptr;
	TrackView* m_laneView = nullptr;
};

QTEST_MAIN(AutomationLaneChildGuiTest)
#include "AutomationLaneChildGuiTest.moc"
