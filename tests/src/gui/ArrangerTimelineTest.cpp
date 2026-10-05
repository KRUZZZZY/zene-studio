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

#include <QAbstractSlider>
#include <QRegularExpression>
#include <QToolBar>
#include <QWheelEvent>

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
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "ProjectMarkers.h"
#include "Song.h"
#include "SongEditor.h"
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

	private:
	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
	Track* m_track = nullptr;
};

QTEST_MAIN(ArrangerTimelineTest)
#include "ArrangerTimelineTest.moc"
