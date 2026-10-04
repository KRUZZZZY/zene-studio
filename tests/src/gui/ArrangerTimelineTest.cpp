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
 *  - a locked clip wears a padlock (EditLocks' change hook), and a click still selects it without moving it. */

#include <QtTest>

#include <cstdlib>

#include "ArrangementEdits.h"
#include "ArrangerEditPreview.h"
#include "ArrangerOverlays.h"
#include "ArrangerRulerLanes.h"
#include "Clip.h"
#include "ClipLockFeedback.h"
#include "ClipView.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "ProjectMarkers.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"
#include "Track.h"

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

private:
	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
	Track* m_track = nullptr;
};

QTEST_MAIN(ArrangerTimelineTest)
#include "ArrangerTimelineTest.moc"
