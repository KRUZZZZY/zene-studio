/*
 * ArrangerTimelineTest.cpp - the arranger's ruler lanes, anchored zoom, drag preview and lock badges
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

/*! 0.5.0 themes 2 and 9, in the real application:
 *  - the ruler lanes: sections, markers and tempo each have a lane under the bar ruler, a marker is drawn
 *    in its lane, and a lane hidden over the control surface (arrangement.ruler_lanes) closes up;
 *  - a keyboard zoom (arrangement.zoom step) keeps the playhead where it is on screen;
 *  - the drag preview shows the ghost, what the drop trims and the snap line, and marks the drop refused
 *    when the clip under it is locked;
 *  - a locked clip wears a padlock (EditLocks' change hook), and a click still selects it without moving it.
 *  Wave 3 (theme 9 finished):
 *  - the wheel zooms by the turn's size around the pointer, one history entry a gesture;
 *  - a crowded lane names what it could not label in its tooltip, and the tempo lane names the tempo;
 *  - a double-click on a section zooms to it; Fit Selection fits the time range;
 *  - the landmarks are found and named, and the navigator is a readable slider a screen reader can set. */

#include <QtTest>

#include <cstdlib>
#include <limits>

#include <QAbstractSlider>
#include <QApplication>
#include <QMenu>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QImage>
#include <QPainter>
#include <QScrollBar>
#include <QToolBar>
#include <QWheelEvent>

#include "../core/AudioBurstSupport.h"
#include "ArrangementEdits.h"
#include "ArrangerEditPreview.h"
#include "ArrangerLandmarks.h"
#include "ArrangerNavigator.h"
#include "ArrangerOverlays.h"
#include "ArrangerRulerLanes.h"
#include "ArrangerZoom.h"
#include "Clip.h"
#include "ClipLockFeedback.h"
#include "ClipView.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ProjectJournal.h"
#include "ShellSelection.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "EditLocks.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternClip.h"
#include "PatternClipView.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ProjectMarkers.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleClipView.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackLabelButton.h"
#include "TrackView.h"
#include "TimeLineWidget.h"
#include "Track.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! Drains the per-process zoom walk (the review round 1: an unbounded drain spins against a walk that
//! never runs dry). The walk's back stack holds at most its cap of 32 entries, so twice over is ample;
//! false when it did not drain within the bound, so the case fails here instead of hanging.
bool drainZoomWalk()
{
	constexpr int WalkBound = 64;  // twice the walk's 32-entry cap, plus change
	for (int i = 0; i < WalkBound; ++i)
	{
		if (!run(QStringLiteral("arrangement.zoom_back")).ok) { return true; }
	}
	return false;
}

Clip* clipAt(Track* track, tick_t start, tick_t length)
{
	Clip* clip = track->createClip(TimePos(start));
	clip->changeLength(TimePos(length));
	clip->setAutoResize(false);
	return clip;
}

ClipView* viewOf(const Clip* clip)
{
	for (ClipView* view : getGUI()->songEditor()->m_editor->findChildren<ClipView*>())
	{
		if (view->getClip() == clip) { return view; }
	}
	return nullptr;
}

} // namespace

class TestTrackLabel final : public TrackLabelButton
{
public:
	using TrackLabelButton::TrackLabelButton;
	using TrackLabelButton::mousePressEvent;
};

class ArrangerTimelineTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		m_editor = getGUI()->songEditor()->m_editor;
		installArrangerOverlays(m_editor);  // what the shell does once it is shown; installs once
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();  // and with it the locks and the markers (project sections)
		ArrangerRange::instance().clear();  // the range is a singleton: a case must not inherit the last one's
		m_track = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);
		QCoreApplication::processEvents();
	}

	//! Q050-322: a focused track header and the ruler both open their real menu with Menu and
	//! Shift+F10, with the same actions the mouse path offers.
	void trackHeaderAndRulerOpenMenusFromTheKeyboard()
	{
		const auto popup = [] {
			for (QWidget* widget : QApplication::topLevelWidgets())
			{
				auto* menu = qobject_cast<QMenu*>(widget);
				if (menu != nullptr && menu->isVisible()) { return menu; }
			}
			return static_cast<QMenu*>(nullptr);
		};
		const auto actionTexts = [](QMenu* menu) {
			QStringList texts;
			for (QAction* action : menu->actions()) { texts << action->text(); }
			return texts;
		};
		TrackView* view = nullptr;
		for (TrackView* candidate : m_editor->trackViews())
		{
			if (candidate->getTrack() == m_track) { view = candidate; }
		}
		QVERIFY(view != nullptr);
		auto* label = view->findChild<TrackLabelButton*>();
		QVERIFY(label != nullptr);
		QVERIFY((label->focusPolicy() & Qt::TabFocus) != 0);
		QVERIFY(!label->accessibleName().isEmpty());
		QVERIFY(label->isVisible());
		QVERIFY(TrackLabelButton::isMenuKey(Qt::Key_Menu, Qt::NoModifier));
		QVERIFY(TrackLabelButton::isMenuKey(Qt::Key_F10, Qt::ShiftModifier));
		QVERIFY(!TrackLabelButton::isMenuKey(Qt::Key_F10, Qt::NoModifier));
		label->openActionsMenu();
		QTRY_VERIFY(popup() != nullptr);
		QStringList keyboardActions = actionTexts(popup());
		QVERIFY2(keyboardActions.contains(QStringLiteral("Clone this track")), qPrintable(keyboardActions.join(QStringLiteral(" | "))));
		QVERIFY(keyboardActions.contains(QStringLiteral("Remove this track")));
		popup()->close();
		QTRY_VERIFY(popup() == nullptr);  // let a popup's queued hide finish before reopening

		label->openActionsMenu();
		QTRY_VERIFY(popup() != nullptr);
		QCOMPARE(actionTexts(popup()), keyboardActions);
		popup()->close();
		QTRY_VERIFY(popup() == nullptr);  // let a popup's queued hide finish before reopening

		// The mouse route opens that same menu now, rather than the old right-click rename.
		TestTrackLabel probe(view, view->getTrackSettingsWidget());
		probe.show();
		QMouseEvent rightPress(QEvent::MouseButtonPress, QPointF(1, 1), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
		probe.mousePressEvent(&rightPress);
		QTRY_VERIFY(popup() != nullptr);
		QCOMPARE(actionTexts(popup()), keyboardActions);
		popup()->close();
		QTRY_VERIFY(popup() == nullptr);  // let a popup's queued hide finish before reopening

		TimeLineWidget* ruler = m_editor->timeLine();
		QVERIFY((ruler->focusPolicy() & Qt::TabFocus) != 0);
		QCOMPARE(ruler->accessibleName(), QStringLiteral("Time ruler"));
		Engine::getSong()->setPlayPos(2 * Bar, Song::PlayMode::Song);
		QVERIFY(TimeLineWidget::isMenuKey(Qt::Key_Menu, Qt::NoModifier));
		QVERIFY(TimeLineWidget::isMenuKey(Qt::Key_F10, Qt::ShiftModifier));
		QVERIFY(!TimeLineWidget::isMenuKey(Qt::Key_F10, Qt::NoModifier));
		ruler->openRulerMenu();
		QTRY_VERIFY(popup() != nullptr);
		const QStringList rulerActions = actionTexts(popup());
		QVERIFY(rulerActions.contains(QStringLiteral("Set loop begin here")));
		QVERIFY(rulerActions.contains(QStringLiteral("Set loop end here")));
		// The shared builder keeps the punch range's own actions (Q050-147), so the keyboard menu has them too.
		QVERIFY2(rulerActions.contains(QStringLiteral("Set punch in here")), qPrintable(rulerActions.join(QStringLiteral(" | "))));
		QVERIFY(rulerActions.contains(QStringLiteral("Clear the punch range")));
		popup()->close();
		QTRY_VERIFY(popup() == nullptr);  // let a popup's queued hide finish before reopening
		ruler->openRulerMenu();
		QTRY_VERIFY(popup() != nullptr);
		QCOMPARE(actionTexts(popup()), rulerActions);
		popup()->close();
	}

	//! A canceled pinch that has already zoomed still leaves its starting zoom for arrangement.zoom_back.
	void aCanceledPinchKeepsItsStartingZoomInHistory()
	{
		const int start = m_editor->zoomingModel()->value();
		const int changed = start + 2;
		arrangerzoom::remember(start + 1);  // make the history result unambiguous if cancellation is ignored
		arrangerzoom::zoomAround(m_editor, changed, 0);
		QCOMPARE(m_editor->zoomingModel()->value(), changed);
#ifndef QT_NO_GESTURES
		arrangerzoom::finishPinch(start, changed, Qt::GestureCanceled);
#else
		arrangerzoom::finishPinch(start, changed, true);  // a Qt without gestures has no Qt::GestureState
#endif
		const ControlResult back = run(QStringLiteral("arrangement.zoom_back"));
		QVERIFY2(back.ok, qPrintable(back.errorMessage));
		QCOMPARE(m_editor->zoomingModel()->value(), start);
	}

	//! 0.5.0 L13 (WF-094): back and forward step over the several prior extents, each restored exactly,
	//! forward refused where nothing was walked past, and a new zoom cuts the forward walk off.
	void backAndForwardWalkSeveralPriorZooms()
	{
		// The walk is a per-process static: drain it, so the assertions below are exact however many
		// prior extents the cases before this one left in it.
		QVERIFY2(drainZoomWalk(), "the back walk did not drain within its bound");
		const int first = m_editor->zoomingModel()->value();
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -6}}).ok);
		const int second = m_editor->zoomingModel()->value();
		QCOMPARE(second, first - 6);
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -6}}).ok);
		const int third = m_editor->zoomingModel()->value();
		QCOMPARE(third, first - 12);

		// Back takes each prior extent in turn: the walk's last entry first, refused when it runs out.
		QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), second);
		QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), first);
		const ControlResult noEarlier = run(QStringLiteral("arrangement.zoom_back"));
		QVERIFY2(!noEarlier.ok, qPrintable(noEarlier.errorMessage));
		QCOMPARE(m_editor->zoomingModel()->value(), first);

		// Forward returns over them in order, one entry a step.
		QVERIFY(run(QStringLiteral("arrangement.zoom_forward")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), second);
		QVERIFY(run(QStringLiteral("arrangement.zoom_forward")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), third);
		QVERIFY2(!run(QStringLiteral("arrangement.zoom_forward")).ok, "nothing further was walked past");
		// ... and the walk back over the same two is exact again.
		QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), second);
		QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), first);
		QVERIFY2(!run(QStringLiteral("arrangement.zoom_back")).ok, "the walk back is drained to its bottom");

		// Sorry! A zoom resets the forward walk: what you walked past is unreachable now.
		QVERIFY(run(QStringLiteral("arrangement.zoom_forward")).ok);  // to second, so back works below
		QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);  // and back to the bottom
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -3}}).ok);
		QVERIFY2(!run(QStringLiteral("arrangement.zoom_forward")).ok, "a new zoom cut the forward walk off");
	}

	//! 0.5.0 L13 (WF-094): the toggle swaps the current extent with the one it last left, and swaps back.
	void zoomToggleSwapsTheCurrentAndPriorExtent()
	{
		// The toggle is a per-process static: drain the walk, then re-remember the walk's bottom (the
		// zoom the view is at), so the toggle has exactly nothing to swap with however the cases before
		// this one left it.
		QVERIFY2(drainZoomWalk(), "the back walk did not drain within its bound");
		const int first = m_editor->zoomingModel()->value();
		arrangerzoom::remember(first);
		QVERIFY2(!run(QStringLiteral("arrangement.zoom_toggle")).ok,
			"before a zoom is left behind there is nothing to swap with");
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -6}}).ok);
		const int after = m_editor->zoomingModel()->value();
		QCOMPARE(m_editor->zoomingModel()->value(), first - 6);
		// The zoom left the first extent behind: the toggle goes there, and back again.
		QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), first);
		QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), after);

		// A back step leaves its extent behind too: from here the toggle swaps with it.
		QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), first);
		QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), after);
		QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), first);
	}

	//! 0.5.0 L13 (WF-094, the round-1 review): the toggle's swap reconciles the walk, so Back or
	//! Forward straight after it either is refused or really changes the zoom - never a successful no-op.
	void backAndForwardAfterTheToggleNeverNoOp()
	{
		QVERIFY2(drainZoomWalk(), "the back walk did not drain within its bound");
		const int bottom = m_editor->zoomingModel()->value();
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -6}}).ok);
		const int middle = m_editor->zoomingModel()->value();
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -6}}).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), bottom - 12);

		// The review's repro, one level along: toggle from the deep extent to the middle, then Back.
		// The swap took the walk's entry for the middle (the view is at it now), so Back goes on to the
		// bottom - with the unreconciled walk it succeeded and left the zoom where it was.
		QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), middle);
		const int beforeBack = m_editor->zoomingModel()->value();
		const ControlResult back = run(QStringLiteral("arrangement.zoom_back"));
		QVERIFY2(!back.ok || m_editor->zoomingModel()->value() != beforeBack,
			qPrintable(back.ok ? QStringLiteral("Back after the toggle succeeded without changing the zoom")
							   : back.errorMessage));
		const int beforeForward = m_editor->zoomingModel()->value();
		const ControlResult forward = run(QStringLiteral("arrangement.zoom_forward"));
		QVERIFY2(!forward.ok || m_editor->zoomingModel()->value() != beforeForward,
			qPrintable(forward.ok ? QStringLiteral("Forward after the toggle succeeded without changing the zoom")
								  : forward.errorMessage));

		// Toggled all the way to the walk's bottom, Back has nothing earlier and must say so.
		QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
		QCOMPARE(m_editor->zoomingModel()->value(), bottom);
		QVERIFY2(!run(QStringLiteral("arrangement.zoom_back")).ok,
			"Back after the toggle to the walk's bottom succeeded, where it would repeat the zoom");
	}

	void theRulerHasALaneEachUnderTheBarRuler()
	{
		auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
		QVERIFY2(lanes != nullptr, "no ruler lanes on the song editor");
		using Lane = ArrangerRulerLanes::Lane;
		const QRect sections = lanes->laneRect(Lane::Sections);
		const QRect markers = lanes->laneRect(Lane::Markers);
		const QRect tempo = lanes->laneRect(Lane::Tempo);
		QVERIFY(!sections.isEmpty() && !markers.isEmpty() && !tempo.isEmpty());
		QVERIFY(sections.bottom() < markers.top());
		QVERIFY(markers.bottom() < tempo.top());
		QTRY_VERIFY(lanes->geometry().top() > m_editor->timeLine()->geometry().bottom() - 1);  // under the ruler, not over it

		ProjectMarkers::instance().add(2 * Bar, QStringLiteral("Drop"), QString());
		lanes->update();
		QTRY_COMPARE(lanes->drawnMarkers().size(), 1);
		QCOMPARE(lanes->drawnMarkers().first(), m_editor->timeLine()->markerX(TimePos(2 * Bar)));

		QVERIFY(run(QStringLiteral("arrangement.ruler_lanes"), {{QStringLiteral("tempo"), false}}).ok);
		QVERIFY(lanes->laneRect(Lane::Tempo).isEmpty());
		QCOMPARE(lanes->height(), sections.height() + markers.height());
		QCOMPARE(run(QStringLiteral("arrangement.ruler_lanes")).result.value(QStringLiteral("tempo")).toBool(), false);
		QVERIFY(run(QStringLiteral("arrangement.ruler_lanes"), {{QStringLiteral("tempo"), true}}).ok);
		QCOMPARE(lanes->height(), sections.height() + markers.height() + tempo.height());
	}

	//! 0.5.0 E23: the chord track has a lane under Tempo - it takes room only while the chord track holds a
	//! chord, follows a chord set and an undo without a signal, and a click on a chord seeks there.
	void theChordTrackIsDrawnInItsLane()
	{
		auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
		QVERIFY(lanes != nullptr);
		using Lane = ArrangerRulerLanes::Lane;
		QVERIFY2(lanes->laneRect(Lane::Chords).isEmpty(), "an empty chord track took room");
		const int without = lanes->height();

		QVERIFY(run(QStringLiteral("chord.set"), {{QStringLiteral("pos"), 2 * Bar}, {QStringLiteral("chord"), QStringLiteral("Major")},
			{QStringLiteral("root"), 7}, {QStringLiteral("length"), 2 * Bar}}).ok);
		QTRY_VERIFY(!lanes->laneRect(Lane::Chords).isEmpty());
		QTRY_COMPARE(lanes->height(), without + lanes->laneRect(Lane::Chords).height());
		QVERIFY(lanes->laneRect(Lane::Tempo).bottom() < lanes->laneRect(Lane::Chords).top());

		// A click on the chord moves the play position to it.
		const QRect lane = lanes->laneRect(Lane::Chords);
		const int x = m_editor->timeLine()->markerX(TimePos(3 * Bar));
		QTest::mouseClick(lanes, Qt::LeftButton, Qt::NoModifier, QPoint(x, lane.center().y()));
		QCOMPARE(Engine::getSong()->getPlayPos().getTicks(), 2 * Bar);

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the seek
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the chord
		QTRY_VERIFY(lanes->laneRect(Lane::Chords).isEmpty());
		QTRY_COMPARE(lanes->height(), without);
	}

	//! Hiding every lane keeps the ruler on screen as a strip: its context menu is the only in-UI way to show a lane
	//! again, and a hidden ruler takes no right-click.
	void aRulerWithEveryLaneHiddenKeepsItsContextMenuReachable()
	{
		auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
		QVERIFY(lanes != nullptr);
		using Lane = ArrangerRulerLanes::Lane;
		const QJsonObject allLanesOn{{QStringLiteral("sections"), true}, {QStringLiteral("markers"), true},
			{QStringLiteral("tempo"), true}, {QStringLiteral("chords"), true}};
		// However the case ends, every lane shows again: the cases after it read the lanes.
		struct LanesShownAgain
		{
			const QJsonObject& lanesOn;
			~LanesShownAgain() { run(QStringLiteral("arrangement.ruler_lanes"), lanesOn); }
		} shownAgain{allLanesOn};
		const QJsonObject everyLaneOff{{QStringLiteral("sections"), false}, {QStringLiteral("markers"), false},
			{QStringLiteral("tempo"), false}, {QStringLiteral("chords"), false}};
		QVERIFY(run(QStringLiteral("arrangement.ruler_lanes"), everyLaneOff).ok);
		QVERIFY(lanes->laneRect(Lane::Sections).isEmpty());
		QVERIFY2(!lanes->isHidden(), "hiding every lane hid the ruler, which took its context menu with it");
		QVERIFY2(lanes->height() > 0, "no room is left under the ruler to right-click");

		QVERIFY(run(QStringLiteral("arrangement.ruler_lanes"), {{QStringLiteral("markers"), true}}).ok);
		QVERIFY(!lanes->laneRect(Lane::Markers).isEmpty());
		QVERIFY(lanes->laneRect(Lane::Sections).isEmpty());
		QVERIFY(run(QStringLiteral("arrangement.ruler_lanes"), allLanesOn).ok);
		QVERIFY(!lanes->isHidden());
		QVERIFY(!lanes->laneRect(Lane::Sections).isEmpty());
	}

	void aKeyboardZoomKeepsThePlayheadInPlace()
	{
		clipAt(m_track, 40 * Bar, Bar);  // a song long enough to scroll
		m_editor->selectAllClips(false);
		ArrangerRange::instance().clear();
		Engine::getSong()->setPlayPos(6 * Bar, Song::PlayMode::Song);
		const int before = m_editor->timeLine()->markerX(TimePos(6 * Bar));
		const ControlResult zoomed = run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), 20}});
		QVERIFY2(zoomed.ok, qPrintable(zoomed.errorMessage));
		QCOMPARE(zoomed.result.value(QStringLiteral("anchor")).toInt(), 6 * Bar);
		QVERIFY(m_editor->pixelsPerBar() > 40);
		QVERIFY2(std::abs(m_editor->timeLine()->markerX(TimePos(6 * Bar)) - before) <= 2,
			qPrintable(QStringLiteral("the playhead moved on screen from x=%1 to x=%2").arg(before).arg(m_editor->timeLine()->markerX(TimePos(6 * Bar)))));
	}

	void anOversizedZoomStepClampsToMaximum()
	{
		IntModel* model = m_editor->zoomingModel();
		const ControlResult zoomed = run(QStringLiteral("arrangement.zoom"),
			{{QStringLiteral("step"), std::numeric_limits<int>::max()}});
		QVERIFY2(zoomed.ok, qPrintable(zoomed.errorMessage));
		QCOMPARE(zoomed.result.value(QStringLiteral("zoom")).toInt(), model->maxValue());
		QCOMPARE(model->value(), model->maxValue());
	}

	void aPatternPreviewCombinesOffsetAndNoteBeforeRasterizing()
	{
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 4},
			{QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::patternStore()));
		QVERIFY(instrumentTrack != nullptr);
		QCoreApplication::processEvents();

		PatternTrack* patternTrack = nullptr;
		for (Track* track : Engine::getSong()->tracks())
		{
			patternTrack = dynamic_cast<PatternTrack*>(track);
			if (patternTrack != nullptr) { break; }
		}
		QVERIFY(patternTrack != nullptr);

		auto* midiClip = dynamic_cast<MidiClip*>(instrumentTrack->getClip(patternTrack->patternIndex()));
		QVERIFY(midiClip != nullptr);
		midiClip->addNote(Note(TimePos(48), TimePos(36), 60, 100), false);
		midiClip->rearrangeAllNotes();
		QVERIFY(!midiClip->notes().empty());

		auto* patternClip = dynamic_cast<PatternClip*>(patternTrack->createClip(TimePos(0)));
		QVERIFY(patternClip != nullptr);
		patternClip->setStartTimeOffset(TimePos(36));
		QCoreApplication::processEvents();

		PatternClipView* view = nullptr;
		for (PatternClipView* candidate : m_editor->findChildren<PatternClipView*>())
		{
			if (candidate->getClip() == patternClip) { view = candidate; break; }
		}
		QVERIFY(view != nullptr);
		view->setGradient(false);
		view->setProperty("noteColor", QColor(255, 0, 255));
		view->setFixedWidth(40); // Show later repetitions past the clip's left border.
		view->setFixedHeight(40);
		view->setNeedsUpdate(true);

		QImage image(view->size(), QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		view->render(&painter);
		painter.end();

		int notePixelX = -1;
		for (int y = 3; y < image.height() - 3 && notePixelX < 0; ++y)
		{
			for (int x = 0; x < image.width(); ++x)
			{
				if (image.pixelColor(x, y) == QColor(255, 0, 255)) { notePixelX = x; break; }
			}
		}
		QCOMPARE(notePixelX - 4, 1); // x=1 repeats at x=5; x=1 itself lies under the clip border.
	}

	void theDragPreviewShowsWhatTheDropWillDo()
	{
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("overlap"), QStringLiteral("trim")}}).ok);
		Clip* under = clipAt(m_track, 0, 2 * Bar);
		Clip* moved = clipAt(m_track, 4 * Bar, 2 * Bar);
		QCoreApplication::processEvents();
		moved->movePosition(TimePos(Bar));  // where the drag has it now
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		ClipView* view = viewOf(moved);
		QVERIFY(view != nullptr);
		auto* preview = m_editor->findChild<ArrangerEditPreview*>();
		QVERIFY2(preview != nullptr, "no drag preview on the song editor");

		arrangement::SnapHit snap;
		snap.start = Bar;
		snap.line = 2 * Bar;
		snap.kind = arrangement::SnapHit::Kind::ClipEdge;
		ArrangerEditPreview::showDrag(view, 4 * Bar, snap);
		QVERIFY(preview->isVisible());
		QVERIFY2(!preview->ghostRect().isEmpty(), "no ghost where the clip started");
		QCOMPARE(preview->effectRects().size(), 1);  // the second bar of 'under' will be trimmed away
		QVERIFY(std::abs(preview->effectRects().first().width() - m_editor->pixelsPerBar()) <= 2);  // one bar wide
		QVERIFY(preview->snapLineX() >= 0);
		QVERIFY(!preview->refused());

		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(under)}}).ok);
		ArrangerEditPreview::showDrag(view, 4 * Bar, snap);
		QVERIFY2(preview->refused(), "a locked clip under the drop was not shown as a refusal");
		ArrangerEditPreview::endDrag(view);
		QVERIFY(!preview->isVisible());
	}

	void theDragPreviewDoesNotWrapAnEndNearTheTickLimit()
	{
		constexpr tick_t InitialStart = 1'610'612'641;
		Clip* clip = clipAt(m_track, InitialStart, 447);
		QCoreApplication::processEvents();
		ClipView* view = viewOf(clip);
		QVERIFY(view != nullptr);
		auto* preview = m_editor->findChild<ArrangerEditPreview*>();
		QVERIFY(preview != nullptr);

		const arrangement::Modes previousModes = arrangement::modes();
		arrangement::Modes previewModes = previousModes;
		previewModes.overlap = arrangement::Overlap::Free;
		previewModes.snapToEdges = false;
		arrangement::setModes(previewModes);

		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 4}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		QCoreApplication::processEvents();
		view->setSelected(false);
		const QPoint pressPos(view->width() / 2, view->height() / 2);
		const QPoint pressGlobal = view->mapToGlobal(pressPos);
		QMouseEvent press(QEvent::MouseButtonPress, QPointF(pressPos), QPointF(pressGlobal), Qt::LeftButton,
			Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(view, &press);

		// At four pixels per bar this moves it by 11,184,810 * 48 ticks, to max(tick_t) - 126.
		constexpr int DragPixels = 11'184'810;
		const QPoint dragPos = pressPos + QPoint(DragPixels, 0);
		const QPoint dragGlobal = view->mapToGlobal(dragPos);
		QMouseEvent move(QEvent::MouseMove, QPointF(dragPos), QPointF(dragGlobal), Qt::NoButton,
			Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(view, &move);

		const tick_t movedStart = clip->startPosition().getTicks();
		const int snapLine = preview->snapLineX();
		const int expectedSnapLine = preview->mapFromGlobal(view->mapToGlobal(QPoint(0, 0))).x();
		clip->movePosition(TimePos(InitialStart));  // keep the release and the next test away from the boundary
		const QPoint releaseGlobal = view->mapToGlobal(pressPos);
		QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pressPos), QPointF(releaseGlobal), Qt::LeftButton,
			Qt::NoButton, Qt::NoModifier);
		QCoreApplication::sendEvent(view, &release);
		arrangement::setModes(previousModes);

		QCOMPARE(movedStart, 2'147'483'521);
		QCOMPARE(snapLine, expectedSnapLine);
	}

	void aLockedClipWearsAPadlockAndStillSelects()
	{
		Clip* clip = clipAt(m_track, 0, 2 * Bar);
		QCoreApplication::processEvents();
		ClipView* view = viewOf(clip);
		QVERIFY(view != nullptr);
		QVERIFY(!cliplocks::badgeShown(view));
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
		QVERIFY2(cliplocks::badgeShown(view), "a locked clip shows no padlock");

		m_editor->selectAllClips(false);
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(view->width() / 2, view->height() * 3 / 4));
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(view->width() / 2, view->height() * 3 / 4));
		QVERIFY2(view->isSelected(), "a click did not select the locked clip");
		QCOMPARE(clip->startPosition().getTicks(), 0);

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the lock's own undo step
		QVERIFY2(!cliplocks::badgeShown(view), "the padlock stayed after the lock was undone");
	}

	//! Hidden rows keep QWidget height, but must not claim hit-test space in the arranger.
	void hiddenTrackRowsDoNotClaimHitTestSpace()
	{
		Track* second = Track::create(Track::Type::Instrument, Engine::getSong());
		QCoreApplication::processEvents();
		TrackView* firstView = nullptr;
		TrackView* secondView = nullptr;
		for (TrackView* view : m_editor->trackViews())
		{
			if (view->getTrack() == m_track) { firstView = view; }
			if (view->getTrack() == second) { secondView = view; }
		}
		QVERIFY(firstView != nullptr && secondView != nullptr);
		m_track->setVisible(false);
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		QVERIFY(!firstView->isVisible());
		QCOMPARE(m_editor->trackViewAt(0), secondView);
	}

	//! One Delete gesture over a selection is one undo step, even though each clip removal journals its track.
	void deletingAClipSelectionCreatesOneUndoStep()
	{
		Clip* first = clipAt(m_track, 0, Bar);
		Clip* second = clipAt(m_track, 2 * Bar, Bar);
		QCoreApplication::processEvents();
		ClipView* firstView = viewOf(first);
		ClipView* secondView = viewOf(second);
		QVERIFY(firstView != nullptr && secondView != nullptr);
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const int depthBefore = journal->undoDepth();

		ClipView::remove(QVector<ClipView*>{firstView, secondView});
		QCoreApplication::processEvents();
		QCOMPARE(m_track->numOfClips(), 0);
		QCOMPARE(journal->undoDepth(), depthBefore + 1);
		journal->undo();
		QCOMPARE(m_track->numOfClips(), 2);
	}

	//! The cap can evict steps under a selection delete; its merge window is still one undo.
	void deletingAClipSelectionAtUndoCapStillCreatesOneStep()
	{
		Clip* first = clipAt(m_track, 0, Bar);
		Clip* second = clipAt(m_track, 2 * Bar, Bar);
		QCoreApplication::processEvents();
		ClipView* firstView = viewOf(first);
		ClipView* secondView = viewOf(second);
		QVERIFY(firstView != nullptr && secondView != nullptr);
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		journal->clearJournal();
		QVERIFY(journal->setMaxUndoStates(ProjectJournal::MAX_UNDO_STATES));
		for (int i = 0; i < ProjectJournal::MAX_UNDO_STATES; ++i)
		{
			journal->addJournalAction([] {}, [] {});
		}
		QCOMPARE(journal->undoDepth(), ProjectJournal::MAX_UNDO_STATES);

		ClipView::remove(QVector<ClipView*>{firstView, secondView});
		QCoreApplication::processEvents();
		QCOMPARE(m_track->numOfClips(), 0);
		journal->undo();
		QCOMPARE(m_track->numOfClips(), 2);
		QCOMPARE(journal->undoDepth(), ProjectJournal::MAX_UNDO_STATES - 2);
	}

	//! Luna cluster 4674: the badge stays locked either way, so the tooltip must be refreshed when the
	//! lock holder moves from the track to the clip - not only when the locked state itself flips.
	void theLockTooltipFollowsTheLockHolder()
	{
		Clip* clip = clipAt(m_track, 0, 2 * Bar);
		QCoreApplication::processEvents();
		ClipView* view = viewOf(clip);
		QVERIFY(view != nullptr);

		QVERIFY(run(QStringLiteral("track.set_locked"), {{QStringLiteral("track"), control::trackIdOf(m_track)}}).ok);
		QVERIFY(cliplocks::badgeShown(view));
		auto* badge = view->findChild<QWidget*>(QStringLiteral("clipLockBadge"));
		QVERIFY(badge != nullptr);
		QVERIFY2(badge->toolTip().contains(QLatin1String("track")), qPrintable(badge->toolTip()));

		// The clip is locked too: still locked, but now it is this clip that holds the lock.
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
		QVERIFY2(cliplocks::badgeShown(view), "the padlock went when the clip took over the lock");
		QVERIFY2(badge->toolTip().contains(QLatin1String("This clip is locked")), qPrintable(badge->toolTip()));

		// And back again: the track takes the lock back when the clip is unlocked.
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("locked"), false}}).ok);
		QVERIFY2(badge->toolTip().contains(QLatin1String("track")), qPrintable(badge->toolTip()));
	}

	//! Luna cluster 5578: the lock check hides Delete/Cut and disables Paste, but every other mutating
	//! context-menu action went through anyway - so a right-clicked locked clip could be muted, recolored or
	//! toggled to auto-resize, and the mouse's own Ctrl+middle-click mute was refused where the menu's was not.
	void aLockedClipKeepsEveryMutatingMenuAction()
	{
		Clip* clip = clipAt(m_track, 0, 2 * Bar);
		QCoreApplication::processEvents();
		ClipView* view = viewOf(clip);
		QVERIFY(view != nullptr);
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
		QVERIFY(cliplocks::badgeShown(view));

		// Auto-resize: the menu item that changes the clicked clips without asking their locks.
		QCOMPARE(clip->getAutoResize(), false);
		view->toggleSelectedAutoResize();
		QCOMPARE(clip->getAutoResize(), false);

		// Mute: the same edit the Ctrl+middle-click gesture already refuses at the mouse press.
		QCOMPARE(clip->isMuted(), false);
		view->toggleMute(QVector<ClipView*>{view});
		QCOMPARE(clip->isMuted(), false);

		// And the color menu, which rewrites every clicked clip too (randomizeColor -> setColor).
		view->randomizeColor();
		QVERIFY2(!clip->color().has_value(), "a locked clip was recolored");

		// An unlocked clip still takes all three, so the lock is what refuses them and not the fixture.
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("locked"), false}}).ok);
		view->toggleSelectedAutoResize();
		QCOMPARE(clip->getAutoResize(), true);
		view->toggleMute(QVector<ClipView*>{view});
		QCOMPARE(clip->isMuted(), true);
		view->randomizeColor();
		QVERIFY(clip->color().has_value());
	}

	//! Cluster new-p11-8 (ported): the Ctrl+middle-click mute is refused for a locked clip, and the
	//! menu path refuses it too - the edit must not depend on how it was asked for (Luna cluster 5578).
	//! The mouse gesture and the menu are one answer, and an unlocked clip takes the very same gesture.
	void aLockedClipIsNotMutedByCtrlMiddleClick()
	{
		Clip* clip = clipAt(m_track, 0, 2 * Bar);
		QCoreApplication::processEvents();
		ClipView* view = viewOf(clip);
		QVERIFY(view != nullptr);
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
		QVERIFY(EditLocks::instance().locks(clip));

		// The mouse gesture on a locked clip: refused, and the clip stays where it is.
		const QPoint center(view->width() / 2, view->height() * 3 / 4);
		QTest::mouseClick(view, Qt::MiddleButton, Qt::ControlModifier, center);
		QVERIFY2(!clip->isMuted(), "a locked clip was muted by the Ctrl+middle-click gesture");
		QCOMPARE(m_track->getClips().size(), std::size_t{1});
		QCOMPARE(clip->startPosition().getTicks(), 0);

		// The menu path on the same locked clip: the same answer, so the finding's premise (that the
		// context menu mutes what the mouse refuses) does not hold here.
		view->toggleMute(QVector<ClipView*>{view});
		QVERIFY2(!clip->isMuted(), "a locked clip was muted by the menu path");

		// An unlocked clip takes the very same gesture, so the lock is what refuses it and not the fixture.
		QVERIFY(run(QStringLiteral("clip.set_locked"),
			{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("locked"), false}}).ok);
		QTest::mouseClick(view, Qt::MiddleButton, Qt::ControlModifier, center);
		QVERIFY2(clip->isMuted(), "an unlocked clip was not muted by the Ctrl+middle-click gesture");
	}

	//! Luna cluster 5578: a lock in a SELECTION keeps the locked clip as it is and still changes the
	//! unlocked ones, exactly like Delete (WF-044) - the whole selection is not refused just because one
	//! member is locked.
	void aLockedClipInASelectionStaysWhileTheOthersChange()
	{
		Clip* locked = clipAt(m_track, 0, Bar);
		Clip* free_ = clipAt(m_track, 2 * Bar, Bar);
		QCoreApplication::processEvents();
		ClipView* lockedView = viewOf(locked);
		ClipView* freeView = viewOf(free_);
		QVERIFY(lockedView != nullptr && freeView != nullptr);
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(locked)}}).ok);

		m_editor->selectAllClips(false);
		lockedView->setSelected(true);
		freeView->setSelected(true);

		freeView->toggleSelectedAutoResize();
		QCOMPARE(locked->getAutoResize(), false);
		QCOMPARE(free_->getAutoResize(), true);

		lockedView->toggleMute(QVector<ClipView*>{lockedView, freeView});
		QCOMPARE(locked->isMuted(), false);
		QCOMPARE(free_->isMuted(), true);
	}

	void recoloringOnlyLockedClipsKeepsTheSongClean()
	{
		Clip* clip = clipAt(m_track, 0, 2 * Bar);
		QCoreApplication::processEvents();
		ClipView* view = viewOf(clip);
		QVERIFY(view != nullptr);
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
		QVERIFY(EditLocks::instance().locks(clip));

		m_editor->selectAllClips(false);
		QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(view->width() / 2, view->height() * 3 / 4));
		QVERIFY(view->isSelected());

		const QString projectPath = m_home.filePath(QStringLiteral("locked-color.mmp"));
		Song* song = Engine::getSong();
		QVERIFY(song->saveProjectFile(projectPath));
		song->noteProjectSaved(projectPath);
		QVERIFY(!song->isModified());

		const auto colorBefore = clip->color();
		view->randomizeColor();
		QVERIFY2(!song->isModified(), "recoloring only locked clips marked the song modified");
		QVERIFY(clip->color() == colorBefore);
	}

	//! 0.5.0 theme 9 (wave 3): a wheel turn zooms by its size - a touchpad's tenth-of-a-notch deltas add up to a
	//! notch's steps - around the tick under the pointer, and one gesture joins the zoom history once.
	void aWheelZoomIsContinuousAroundThePointer()
	{
		clipAt(m_track, 40 * Bar, Bar);
		const tick_t anchor = 4 * Bar;
		const int x = m_editor->timeLine()->markerX(TimePos(anchor));
		const int value = m_editor->zoomingModel()->value();
		const int history = arrangerzoom::historySize();
		arrangerzoom::wheelZoom(m_editor, 12, false, anchor);  // a tenth of a notch: half a step, nothing yet
		QCOMPARE(m_editor->zoomingModel()->value(), value);
		for (int i = 0; i < 19; ++i) { arrangerzoom::wheelZoom(m_editor, 12, false, anchor); }  // twenty tenths: two notches
		QCOMPARE(m_editor->zoomingModel()->value(), value + 2 * arrangerzoom::WheelNotchSteps);
		QVERIFY2(arrangerzoom::historySize() - history <= 1, "one wheel gesture left more than one zoom to go back to");
		QVERIFY2(std::abs(m_editor->timeLine()->markerX(TimePos(anchor)) - x) <= 2, "the tick under the pointer moved");

		// The real wheel over the arranger: Ctrl and one notch, a new gesture, zooms in by a notch's steps.
		QTest::qWait(arrangerzoom::WheelBurstMs + 100);
		const int before = m_editor->zoomingModel()->value();
		const QPoint at = m_editor->timeLine()->mapTo(m_editor, QPoint(m_editor->timeLine()->markerX(TimePos(anchor)), 0))
			+ QPoint(0, m_editor->height() / 2);
		QWheelEvent wheel(at, m_editor->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
			Qt::NoScrollPhase, false);
		QCoreApplication::sendEvent(m_editor, &wheel);
		QCOMPARE(m_editor->zoomingModel()->value(), before + arrangerzoom::WheelNotchSteps);
	}

	//! Wave 3: no label collides - what a crowded lane leaves out, its tooltip names.
	void aCrowdedLaneNamesWhatItLeftOutInItsTooltip()
	{
		auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
		QVERIFY(lanes != nullptr);
		ProjectMarkers::instance().add(2 * Bar, QStringLiteral("Verse"), QString());
		ProjectMarkers::instance().add(2 * Bar + 4, QStringLiteral("Drop"), QString());  // under a pixel apart at 40 px a bar
		ProjectMarkers::instance().add(8 * Bar, QStringLiteral("Outro"), QString());
		QCOMPARE(lanes->markerCluster(0), (QStringList{QStringLiteral("Verse"), QStringLiteral("Drop")}));
		QCOMPARE(lanes->markerCluster(2), QStringList{QStringLiteral("Outro")});

		const int x = m_editor->timeLine()->markerX(TimePos(3 * Bar));
		const QString tip = lanes->tempoTip(QPoint(x, lanes->laneRect(ArrangerRulerLanes::Lane::Tempo).center().y()));
		QVERIFY2(tip.contains(QString::number(Engine::getSong()->getTempo())), qPrintable(tip));
		QVERIFY(lanes->tempoTip(QPoint(x, lanes->laneRect(ArrangerRulerLanes::Lane::Markers).center().y())).isEmpty());
	}

	//! Wave 3 (Bitwig's Alt-click on a cue): a double-click on a section fills the arranger with it, and Fit Selection
	//! fits the time range when there is one.
	void aDoubleClickZoomsToASectionAndFitSelectionFitsTheRange()
	{
		clipAt(m_track, 40 * Bar, Bar);
		auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
		QVERIFY(lanes != nullptr);
		QVERIFY(run(QStringLiteral("section.add"), {{QStringLiteral("start"), 4 * Bar}, {QStringLiteral("end"), 8 * Bar},
			{QStringLiteral("name"), QStringLiteral("Chorus")}}).ok);
		QCoreApplication::processEvents();
		const int y = lanes->laneRect(ArrangerRulerLanes::Lane::Sections).center().y();
		QTest::mouseDClick(lanes, Qt::LeftButton, Qt::NoModifier, QPoint(m_editor->timeLine()->markerX(TimePos(5 * Bar)), y));
		QTRY_COMPARE(m_editor->currentPosition().getTicks(), 4 * Bar);
		const tick_t shown = arrangerzoom::visibleTicks(m_editor);
		QVERIFY2(std::abs(shown - 4 * Bar) < Bar / 2, qPrintable(QStringLiteral("the section's 4 bars show as %1 ticks").arg(shown)));

		m_editor->selectAllClips(false);
		ArrangerRange::instance().set(10 * Bar, 12 * Bar, {}, true);
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("fit"), QStringLiteral("selection")}}).ok);
		QTRY_COMPARE(m_editor->currentPosition().getTicks(), 10 * Bar);
		ArrangerRange::instance().clear();
	}

	void theLandmarksAreFoundAndNamed()
	{
		using landmarks::Landmark;
		QVERIFY(run(QStringLiteral("section.add"), {{QStringLiteral("start"), 4 * Bar}, {QStringLiteral("end"), 8 * Bar},
			{QStringLiteral("name"), QStringLiteral("Chorus")}}).ok);
		ProjectMarkers::instance().add(12 * Bar, QStringLiteral("Drop"), QString());
		ProjectMarkers::instance().add(16 * Bar, QStringLiteral("Outro"), QString());
		Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
		timeline.setLoopPoints(TimePos(20 * Bar), TimePos(24 * Bar));
		timeline.setLoopEnabled(false);

		const Landmark marker = landmarks::at(12 * Bar + 10, 20);
		QCOMPARE(marker.kind, Landmark::Kind::Marker);
		QCOMPARE(marker.name, QStringLiteral("Drop"));
		QCOMPARE(marker.end, 16 * Bar);  // up to the next marker
		const Landmark section = landmarks::at(5 * Bar, 0);
		QCOMPARE(section.kind, Landmark::Kind::Section);
		QCOMPARE(section.start, 4 * Bar);
		QVERIFY(landmarks::describe(section).contains(QStringLiteral("Chorus")));
		QCOMPARE(landmarks::at(21 * Bar, 0).kind, Landmark::Kind::None);
		timeline.setLoopEnabled(true);
		QCOMPARE(landmarks::at(21 * Bar, 0).kind, Landmark::Kind::Loop);
		timeline.setLoopEnabled(false);
	}

	//! Wave 3: the navigator keeps a readable size from the design tokens, rings its keyboard focus, and is a
	//! slider - the arranger's start - that follows the arranger and that a screen reader can set.
	void theNavigatorIsAReadableSliderOverTheSong()
	{
		installArrangerNavigator(getGUI()->songEditor());
		auto* bar = getGUI()->songEditor()->findChild<QToolBar*>(QStringLiteral("arrangerNavigator"));
		QVERIFY(bar != nullptr);
		auto* navigator = bar->findChild<QAbstractSlider*>();
		QVERIFY2(navigator != nullptr, "the navigator is not a slider");
		QVERIFY(navigator->minimumHeight() >= tokens::targetSize());
		QVERIFY(navigator->property("zeneFocusRing").toBool());

		clipAt(m_track, 40 * Bar, Bar);
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 8 * Bar}}).ok);
		QTRY_COMPARE(navigator->value(), static_cast<int>(m_editor->currentPosition().getTicks()));
		navigator->setValue(2 * Bar);  // what a screen reader's value setting does
		QTRY_COMPARE(m_editor->currentPosition().getTicks(), 2 * Bar);
		QVERIFY2(navigator->accessibleDescription().startsWith(QStringLiteral("Showing bars 3 ")), qPrintable(navigator->accessibleDescription()));
	}

	//! The navigator names the bars it SHOWS. The view's end is exclusive, so a view that ends exactly on a
	//! bar line shows one bar fewer than its length in bars - naming the end tick's own bar adds a bar that is
	//! not on screen.
	void theNavigatorNamesTheLastBarItShows()
	{
		installArrangerNavigator(getGUI()->songEditor());
		auto* bar = getGUI()->songEditor()->findChild<QToolBar*>(QStringLiteral("arrangerNavigator"));
		QVERIFY(bar != nullptr);
		auto* navigator = bar->findChild<QAbstractSlider*>();
		QVERIFY2(navigator != nullptr, "the navigator is not a slider");
		clipAt(m_track, 40 * Bar, Bar);
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);

		// A zoom whose view ends exactly on a bar line: the boundary the text has to treat as exclusive.
		tick_t view = 0;
		for (int ppb = 4; ppb <= 400; ++ppb)
		{
			QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), ppb},
				{QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
			view = arrangerzoom::visibleTicks(m_editor);
			if (view > 0 && view % Bar == 0) { break; }
		}
		QVERIFY2(view > 0 && view % Bar == 0, "no zoom ended the view exactly on a bar line");
		QCOMPARE(m_editor->currentPosition().getTicks(), 0);

		static const QRegularExpression said(QStringLiteral("Showing bars (\\d+) to (\\d+) of (\\d+)"));
		const QRegularExpressionMatch found = said.match(navigator->accessibleDescription());
		QVERIFY2(found.hasMatch(), qPrintable(navigator->accessibleDescription()));
		QCOMPARE(found.captured(1).toInt(), 1);
		QCOMPARE(found.captured(2).toInt(), static_cast<int>(view / Bar));
	}

	//! A range whose BOTH ends fall off the track rows resolves each to the nearest boundary track, so the
	//! gesture reaches the tracks it was dragged across instead of selecting none.
	void aRangeWithBothEndsOffTheRowsReachesTheBoundaryTrack()
	{
		auto* overlay = m_editor->findChild<ArrangerRangeOverlay*>();
		QVERIFY2(overlay != nullptr, "no range overlay on the song editor");
		Track::create(Track::Type::Instrument, Engine::getSong());
		QCoreApplication::processEvents();
		const QList<TrackView*> rows = m_editor->trackViews();
		QVERIFY(rows.size() >= 2);
		TrackView* last = rows.last();
		const int below = last->mapTo(overlay->parentWidget(), QPoint(0, 0)).y() + last->height();
		QVERIFY2(below + 8 < overlay->height(), "no space below the last track to drag off the rows");

		QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("range")}}).ok);
		const int x = overlay->width() / 4;
		// Both presses land below the last row, where trackAt() finds no track either way.
		QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(x, below + 2));
		QTest::mouseMove(overlay, QPoint(x + overlay->width() / 2, below + 8));
		QVERIFY2(ArrangerRange::instance().active(), "a range dragged off the rows dispatched no range at all");
		QCOMPARE(ArrangerRange::instance().tracks(), QVector<Track*>{last->getTrack()});
		QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(x + overlay->width() / 2, below + 8));
		ArrangerRange::instance().clear();
		QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("select")}}).ok);
	}

	//! A drag cut short - the grab taken away by a popup, the overlay hidden - does not go on dispatching:
	//! the gesture belongs to the press that began it.
	void aRangeDragCutShortByTheGrabBeingTakenStops()
	{
		auto* overlay = m_editor->findChild<ArrangerRangeOverlay*>();
		QVERIFY2(overlay != nullptr, "no range overlay on the song editor");
		const TrackView* row = m_editor->trackViews().first();
		const int onRow = row->mapTo(overlay->parentWidget(), QPoint(0, 0)).y() + row->height() / 2;
		QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("range")}}).ok);

		const int x = overlay->width() / 4;
		QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(x, onRow));
		QEvent grabLost(QEvent::UngrabMouse);  // a popup took the mouse grab away
		QCoreApplication::sendEvent(overlay, &grabLost);
		QTest::mouseMove(overlay, QPoint(x + overlay->width() / 2, onRow));
		QVERIFY2(!ArrangerRange::instance().active(), "a drag that lost its grab still dispatched a range");
		QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(x + overlay->width() / 2, onRow));
		ArrangerRange::instance().clear();
		QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("select")}}).ok);
	}

	//! A tool chosen while an audition is held ends it: the gesture belongs to the tool that began it, so the
	//! release finishes the audition instead of dispatching a range with the tool that replaced it.
	void switchingToolsDuringAHeldAuditionStopsIt()
	{
		auto* overlay = m_editor->findChild<ArrangerRangeOverlay*>();
		QVERIFY2(overlay != nullptr, "no range overlay on the song editor");
		Track* other = Track::create(Track::Type::Instrument, Engine::getSong());
		QCoreApplication::processEvents();
		clipAt(m_track, 40 * Bar, Bar);
		// The press lands on the row the audition silences every other track for.
		const TrackView* row = m_editor->trackViews().first();
		const QPoint press(overlay->width() / 2, row->mapTo(overlay->parentWidget(), QPoint(0, 0)).y() + row->height() / 2);

		QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("audition")}}).ok);
		QCOMPARE(overlay->tool(), ArrangerRangeOverlay::Tool::Audition);
		QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, press);
		QVERIFY2(other->isMuted(), "the held audition did not silence the other tracks");
		QVERIFY(run(QStringLiteral("transport.audition_stop")).ok);  // tidy, should this case fail halfway

		QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, press);
		QVERIFY2(other->isMuted(), "the second held audition did not silence the other tracks");
		QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("range")}}).ok);
		QCOMPARE(overlay->tool(), ArrangerRangeOverlay::Tool::Range);
		QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(press.x() + 40, press.y()));

		QVERIFY2(!other->isMuted(), "the audition was still sounding after the release that ended the gesture");
		QVERIFY2(!ArrangerRange::instance().active(), "a tool change mid-audition still dispatched a range");
		QVERIFY(run(QStringLiteral("transport.audition_stop")).ok);  // tidy whatever happened
		QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("select")}}).ok);
	}

	//! QtTest runs only the private slots. A test method declared under `private:` is never run, so this asks the
	//! meta-object for each of the timeline tests that must run.
	void everyTimelineTestIsAPrivateSlot()
	{
		for (const char* name : {"aBurstOfRealNudgesIsOneUndoStep()", "theNavigatorRangeIncludesSongsNearTheTickLimit()",
			"oversizedScrollPositionsReachTheEndOfTheArranger()"})
		{
			QVERIFY2(staticMetaObject.indexOfSlot(name) >= 0, name);
		}
	}

	//! The REAL arrangement.nudge (its production declaration, through the arranger): three presses on one clip are
	//! one undo step and one record, and control.undo puts the clip back where it started. A press on another clip
	//! starts its own run.
	void aBurstOfRealNudgesIsOneUndoStep()
	{
		Clip* clip = clipAt(m_track, 2 * Bar, Bar);
		Clip* other = clipAt(m_track, 8 * Bar, Bar);
		QCoreApplication::processEvents();
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const QJsonObject press{{QStringLiteral("direction"), QStringLiteral("right")}, {QStringLiteral("amount"), 48}};
		const int start = clip->startPosition().getTicks();
		ShellSelection::instance()->setClip(clip);
		const int depthBefore = journal->undoDepth();
		const int recordsBefore = ControlRegistry::instance()->transactions().size();

		for (int i = 0; i < 3; ++i) { QVERIFY2(run(QStringLiteral("arrangement.nudge"), press).ok, "a press did not nudge"); }
		QCOMPARE(clip->startPosition().getTicks(), start + 3 * 48);
		QCOMPARE(journal->undoDepth() - depthBefore, 1);
		QCOMPARE(ControlRegistry::instance()->transactions().size() - recordsBefore, 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clip->startPosition().getTicks(), start);
		QCOMPARE(journal->undoDepth(), depthBefore);

		ShellSelection::instance()->setClip(other);
		QVERIFY2(run(QStringLiteral("arrangement.nudge"), press).ok, "the other clip did not nudge");
		QCOMPARE(journal->undoDepth() - depthBefore, 1);
		QCOMPARE(ControlRegistry::instance()->transactions().size() - recordsBefore, 2);
		ShellSelection::instance()->setClip(nullptr);
	}

	void theNavigatorRangeIncludesSongsNearTheTickLimit()
	{
		installArrangerNavigator(getGUI()->songEditor());
		auto* bar = getGUI()->songEditor()->findChild<QToolBar*>(QStringLiteral("arrangerNavigator"));
		QVERIFY(bar != nullptr);
		auto* navigator = bar->findChild<QAbstractSlider*>();
		QVERIFY(navigator != nullptr);

		const tick_t maxTick = std::numeric_limits<tick_t>::max();
		clipAt(m_track, maxTick - Bar, Bar);
		QCOMPARE(Engine::getSong()->length(), maxTick / Bar);
		QTRY_VERIFY(navigator->maximum() > 1'000'000'000);
	}

	void oversizedScrollPositionsReachTheEndOfTheArranger()
	{
		clipAt(m_track, 40 * Bar, Bar);
		auto* scrollbar = m_editor->horizontalScroll();
		QVERIFY(scrollbar->maximum() > 0);

		const ControlResult result = run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 2147483648LL}});
		QVERIFY(result.ok);
		QCOMPARE(scrollbar->value(), scrollbar->maximum());
	}

	/*! ED-150: the transient overlay of a sample clip shows the transients of the audio the clip
	 *  PLAYS. A reversed clip sounds the MIRROR of its window (Sample::render reverses the whole
	 *  buffer), so the overlay's analysed range moves with the direction - and the overlay's cache
	 *  (analysed once per audio and per range) has to move with it: the placement alone only
	 *  re-maps the frames the cache has, and those are the frames of the range the clip played
	 *  BEFORE the reversal.
	 *
	 *  A trimmed clip makes the difference visible: its window covers the first half of the
	 *  source, so before the reversal the overlay shows the two bursts of that half and after it
	 *  the two bursts of the other half, heard backwards. */
	void aReversedTrimmedClipDrawsTheTransientsOfTheAudioItPlays()
	{
		constexpr int BurstTicks[] = {10, 58, 106, 154};
		const auto data = test::burstAudio(200, {BurstTicks[0], BurstTicks[1], BurstTicks[2], BurstTicks[3]});
		auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
		// the window covers the first half of the source only
		const auto framesPerTick = static_cast<f_cnt_t>(Engine::framesPerTick(rate));
		clip->setSamplePlayLength(100 * framesPerTick);
		QCoreApplication::processEvents();

		SampleClipView* view = nullptr;
		for (SampleClipView* candidate : m_editor->findChildren<SampleClipView*>())
		{
			if (candidate->getClip() == clip) { view = candidate; }
		}
		QVERIFY2(view != nullptr, "no clip view for the sample clip");

		// the overlay analyses the range the clip plays: the window, the first half
		const std::vector<int> forward = view->transientTicks();
		QCOMPARE(forward.size(), std::size_t{2});
		QVERIFY2(std::abs(forward[0] - BurstTicks[0]) <= 2, qPrintable(QStringLiteral("the first transient sits at %1").arg(forward[0])));
		QVERIFY2(std::abs(forward[1] - BurstTicks[1]) <= 2, qPrintable(QStringLiteral("the second transient sits at %1").arg(forward[1])));

		// the user's own reverse: the clip now plays the MIRROR of its window
		view->reverseSample();
		QCoreApplication::processEvents();
		QVERIFY(clip->sample().reversed());

		// ... so the overlay has to show the transients of the second half, heard backwards: the
		// burst at 154 ticks is played at 199 - 154, the one at 106 ticks at 199 - 106. The ticks
		// come in the order the frames were found (the overlay draws one mark per tick, so the
		// order does not matter - the set does).
		const std::vector<int> reversed = view->transientTicks();
		QCOMPARE(reversed.size(), std::size_t{2});
		const int expected[] = {199 - BurstTicks[3], 199 - BurstTicks[2]};
		const bool first = std::abs(reversed[0] - expected[0]) <= 2 && std::abs(reversed[1] - expected[1]) <= 2;
		const bool swapped = std::abs(reversed[0] - expected[1]) <= 2 && std::abs(reversed[1] - expected[0]) <= 2;
		QVERIFY2(first || swapped, qPrintable(QStringLiteral("the transients sit at %1 and %2, expected %3 and %4")
			.arg(reversed[0]).arg(reversed[1]).arg(expected[0]).arg(expected[1])));
	}

private:
	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
	Track* m_track = nullptr;
};

QTEST_MAIN(ArrangerTimelineTest)
#include "ArrangerTimelineTest.moc"
