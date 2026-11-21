/*
 * ArrangerOverlaysVideoLaneTest.cpp - the arranger's range / audition / zoom tools and the video lane under its ruler
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

/*! The arranger's tools beyond the song editor's: RANGE (press, drag across time and tracks, release: range.set,
 *  snapped, the rows between the two ends), AUDITION (the other tracks are silenced while the button is held and come
 *  back on release - or when the tool is changed, the grab is taken away, the window loses focus), ZOOM (the dragged span
 *  fills the width) and SMART (a clip hands its upper half's press over). The overlay is transparent to the mouse with no
 *  tool of its own. The video lane takes room only while a video is attached, tiles thumbnails from the tick the video
 *  starts at to where it ends, and a click on the strip moves the play position there. */

#include <QtTest>

#include <QJsonObject>
#include <QLabel>
#include <QTemporaryDir>

#include "ArrangementEdits.h"
#include "ArrangerOverlays.h"
#include "ArrangerRulerLanes.h"
#include "ArrangerZoom.h"
#include "ClipView.h"
#include "DesignTokens.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Clip.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimeLineWidget.h"
#include "Track.h"
#include "TrackView.h"
#include "VideoLane.h"
#include "VideoReference.h"
#include "../core/VideoTestClip.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! Counts the paint events a widget is sent: a repaint loop shows as a count that keeps growing.
class PaintCounter : public QObject
{
protected:
	bool eventFilter(QObject*, QEvent* event) override
	{
		if (event->type() == QEvent::Paint) { ++paints; }
		return false;
	}

public:
	int paints = 0;
};

//! Runs @a passes rounds of the event loop, with no timers waited on: each round delivers what is queued.
void pumpEvents(int passes)
{
	for (int i = 0; i < passes; ++i) { QCoreApplication::processEvents(); }
}

} // namespace

class ArrangerOverlaysVideoLaneTest : public QObject
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
		installArrangerOverlays(m_editor);
		QCoreApplication::processEvents();
		m_overlay = m_editor->findChild<ArrangerRangeOverlay*>();
		QVERIFY(m_overlay != nullptr);
	}

	void cleanupTestCase()
	{
		VideoReference::instance().set({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		m_overlay->setTool(ArrangerRangeOverlay::Tool::None);
		Engine::getSong()->clearProject();
		ArrangerRange::instance().clear();
		VideoReference::instance().set({});
		m_tracks.clear();
		for (int i = 0; i < 3; ++i)
		{
			m_tracks.push_back(Track::create(Track::Type::Instrument, Engine::getSong()));
			QCoreApplication::processEvents();
		}
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);
		QCoreApplication::processEvents();
	}

	void theToolsHaveNamesAndTheOverlayIsTransparentUntilOneIsChosen()
	{
		QCOMPARE(arrangerToolNames(), (QStringList{QStringLiteral("draw"), QStringLiteral("knife"), QStringLiteral("select"),
			QStringLiteral("range"), QStringLiteral("audition"), QStringLiteral("zoom"), QStringLiteral("smart")}));
		QVERIFY(m_overlay->testAttribute(Qt::WA_TransparentForMouseEvents));
		QVERIFY(!m_overlay->toolActive());
		QCOMPARE(currentArrangerTool(m_editor), arrangerToolNames().at(static_cast<int>(m_editor->editMode())));
		using Tool = ArrangerRangeOverlay::Tool;
		for (const auto& [tool, name, active] : {std::tuple{Tool::Range, "range", true}, std::tuple{Tool::Audition, "audition", true},
				 std::tuple{Tool::Zoom, "zoom", true}, std::tuple{Tool::Smart, "smart", false}})
		{
			m_overlay->setTool(tool);
			QCOMPARE(m_overlay->tool(), tool);
			QCOMPARE(m_overlay->toolActive(), active);
			QCOMPARE(m_overlay->testAttribute(Qt::WA_TransparentForMouseEvents), !active);
			QCOMPARE(currentArrangerTool(m_editor), QLatin1String(name));
		}
		QCOMPARE(m_overlay->cursor().shape(), Qt::IBeamCursor);  // smart
		m_overlay->setTool(Tool::Zoom);
		QCOMPARE(m_overlay->cursor().shape(), Qt::CrossCursor);
		m_overlay->setTool(Tool::Audition);
		QCOMPARE(m_overlay->cursor().shape(), Qt::PointingHandCursor);
	}

	void theRangeToolDragsOutATimeRangeOverTheTracksBetween()
	{
		m_overlay->setTool(ArrangerRangeOverlay::Tool::Range);
		const QPoint first(xAt(2 * Bar), rowY(0));
		const QPoint last(xAt(5 * Bar), rowY(2));
		QCOMPARE(m_overlay->trackAt(rowY(1)), m_tracks.at(1));
		QCOMPARE(m_overlay->tickAt(xAt(2 * Bar)), 2 * Bar);
		QTest::mousePress(m_overlay, Qt::LeftButton, Qt::NoModifier, first);
		QTest::mouseMove(m_overlay, last);
		QTest::mouseRelease(m_overlay, Qt::LeftButton, Qt::NoModifier, last);
		const ArrangerRange& range = ArrangerRange::instance();
		QVERIFY(range.active());
		QCOMPARE(range.start(), 2 * Bar);
		QCOMPARE(range.end(), 5 * Bar);
		QCOMPARE(range.tracks().size(), 3);  // the two ends and the one between
		QVERIFY(!m_overlay->rangeRect().isEmpty());
		QVERIFY(m_overlay->rangeRect().contains(QPoint(xAt(3 * Bar), rowY(1))));
		// The covered rows are painted (a translucent accent); outside the range the overlay paints nothing.
		const QImage painted = m_overlay->grab().toImage().convertToFormat(QImage::Format_ARGB32);
		const QRect covered = m_overlay->rangeRect();
		QVERIFY2(painted.pixelColor(covered.center()).alpha() > 0, "the range's rows were not painted");
		QCOMPARE(painted.pixelColor(QPoint(xAt(8 * Bar), covered.center().y())).alpha(), 0);

		// Dragging off the rows (above the first, below the last) takes the nearest row, and both ends off reach every track.
		ArrangerRange::instance().clear();
		QTest::mousePress(m_overlay, Qt::LeftButton, Qt::NoModifier, QPoint(xAt(Bar), 0));
		QTest::mouseRelease(m_overlay, Qt::LeftButton, Qt::NoModifier, QPoint(xAt(3 * Bar), m_overlay->height() - 1));
		QVERIFY(range.active());
		QCOMPARE(range.tracks().size(), static_cast<int>(m_tracks.size()));

		// A right-button press is not the tool's.
		ArrangerRange::instance().clear();
		QTest::mouseClick(m_overlay, Qt::RightButton, Qt::NoModifier, first);
		QVERIFY(!range.active());
	}

	void anIdleRangeOverlayStopsRepaintingItself()
	{
		// The overlay is a transparent child of the viewport it watches, and repainting a transparent child
		// paints that viewport again. A viewport Paint must schedule the overlay only when the rows it draws moved;
		// an unconditional update() there feeds itself, and the main thread never sleeps (BUGS stress-5).
		QVERIFY(!ArrangerRange::instance().active());
		PaintCounter counter;
		m_overlay->installEventFilter(&counter);
		m_overlay->update();
		pumpEvents(30);
		m_overlay->removeEventFilter(&counter);
		QVERIFY2(counter.paints <= 2, qPrintable(QStringLiteral("an idle overlay was painted %1 times in 30 event passes")
			.arg(counter.paints)));
	}

	void theRangeOverlayRepaintsWhenItsRowsMove()
	{
		// The guard for the fix: with a range shown, scrolling moves the rows under the overlay, and it must repaint.
		// An empty project has no horizontal scroll range, so the rows are moved by zooming (the rows are placed by
		// pixels per bar); the overlay must repaint once they have moved.
		ArrangerRange::instance().set(2 * Bar, 5 * Bar, {}, true);
		pumpEvents(3);
		const QRect before = m_overlay->rangeRect();
		QVERIFY(!before.isEmpty());
		PaintCounter counter;
		m_overlay->installEventFilter(&counter);
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 200}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		pumpEvents(10);
		m_overlay->removeEventFilter(&counter);
		QVERIFY(m_overlay->rangeRect() != before);
		QVERIFY2(counter.paints >= 1, "the range overlay did not repaint after its rows moved");
		ArrangerRange::instance().clear();
	}

	void theAuditionToolSilencesTheOtherTracksWhileTheButtonIsHeld()
	{
		m_overlay->setTool(ArrangerRangeOverlay::Tool::Audition);
		const QPoint at(xAt(Bar), rowY(1));
		QTest::mousePress(m_overlay, Qt::LeftButton, Qt::NoModifier, at);
		QVERIFY(m_tracks.at(0)->isMuted());
		QVERIFY(!m_tracks.at(1)->isMuted());  // the pressed track alone is heard
		QVERIFY(m_tracks.at(2)->isMuted());
		QTest::mouseRelease(m_overlay, Qt::LeftButton, Qt::NoModifier, at);
		for (Track* track : m_tracks) { QVERIFY(!track->isMuted()); }

		// Choosing another tool while it is held ends it: the release then runs nothing of the new tool's.
		QTest::mousePress(m_overlay, Qt::LeftButton, Qt::NoModifier, at);
		QVERIFY(m_tracks.at(0)->isMuted());
		m_overlay->setTool(ArrangerRangeOverlay::Tool::Range);
		QVERIFY(!m_tracks.at(0)->isMuted());
		QTest::mouseRelease(m_overlay, Qt::LeftButton, Qt::NoModifier, at);
		QVERIFY(!ArrangerRange::instance().active());

		// A window losing focus, or the overlay hidden, ends it as well.
		m_overlay->setTool(ArrangerRangeOverlay::Tool::Audition);
		QTest::mousePress(m_overlay, Qt::LeftButton, Qt::NoModifier, at);
		QVERIFY(m_tracks.at(0)->isMuted());
		QEvent deactivate(QEvent::WindowDeactivate);
		QApplication::sendEvent(m_overlay, &deactivate);
		QVERIFY(!m_tracks.at(0)->isMuted());
		QTest::mouseRelease(m_overlay, Qt::LeftButton, Qt::NoModifier, at);  // nothing left to end
		QVERIFY(!m_tracks.at(0)->isMuted());

		QTest::mousePress(m_overlay, Qt::LeftButton, Qt::NoModifier, at);
		QVERIFY(m_tracks.at(0)->isMuted());
		QEvent ungrab(QEvent::UngrabMouse);
		QApplication::sendEvent(m_overlay, &ungrab);
		QVERIFY(!m_tracks.at(0)->isMuted());
		QTest::mousePress(m_overlay, Qt::LeftButton, Qt::NoModifier, at);
		QEvent hide(QEvent::Hide);
		QApplication::sendEvent(m_overlay, &hide);
		QVERIFY(!m_tracks.at(0)->isMuted());
		QTest::mouseRelease(m_overlay, Qt::LeftButton, Qt::NoModifier, at);
	}

	void theZoomToolFillsTheWidthWithTheDraggedSpanAndIgnoresAJitter()
	{
		// A song long enough to scroll, or the arranger has nowhere to scroll to.
		Clip* far = m_tracks.at(0)->createClip(TimePos(60 * Bar));
		far->changeLength(TimePos(2 * Bar));
		QCoreApplication::processEvents();
		m_overlay->setTool(ArrangerRangeOverlay::Tool::Zoom);
		const int before = static_cast<int>(visibleTicks());
		QTest::mousePress(m_overlay, Qt::LeftButton, Qt::NoModifier, QPoint(xAt(4 * Bar), rowY(0)));
		QTest::mouseRelease(m_overlay, Qt::LeftButton, Qt::NoModifier, QPoint(xAt(4 * Bar) + 3, rowY(0)));  // a jitter
		QCOMPARE(static_cast<int>(visibleTicks()), before);
		QTest::mousePress(m_overlay, Qt::LeftButton, Qt::NoModifier, QPoint(xAt(8 * Bar), rowY(0)));
		QTest::mouseRelease(m_overlay, Qt::LeftButton, Qt::NoModifier, QPoint(xAt(4 * Bar), rowY(0)));  // right to left is the same span
		QVERIFY2(std::abs(visibleTicks() - 4 * Bar) < Bar / 2, qPrintable(QString::number(visibleTicks())));
		QVERIFY(std::abs(static_cast<int>(m_editor->currentPosition().getTicks()) - 4 * Bar) < Bar / 2);
	}

	void theSmartToolTakesTheDragFromAClipsUpperHalf()
	{
		Clip* clip = m_tracks.at(0)->createClip(TimePos(Bar));
		clip->changeLength(TimePos(4 * Bar));
		QCoreApplication::processEvents();
		ClipView* view = nullptr;
		for (ClipView* candidate : m_editor->findChildren<ClipView*>())
		{
			if (candidate->getClip() == clip) { view = candidate; }
		}
		QVERIFY2(view != nullptr, "the clip has no view");
		m_overlay->setTool(ArrangerRangeOverlay::Tool::Smart);
		QVERIFY(!m_overlay->toolActive());  // the overlay lets every press through to the clip

		// A press in the clip's LOWER half is the clip's own (it selects and moves): no range begins.
		const QPoint lower(10, view->height() * 3 / 4);
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, lower);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, lower);
		QVERIFY(!ArrangerRange::instance().active());

		// A press in the UPPER half hands the gesture to the overlay, which takes the grab and draws the range.
		const QPoint upper(2, view->height() / 4);
		QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, upper);
		QCOMPARE(QWidget::mouseGrabber(), m_overlay);
		const QPoint inOverlay = m_overlay->mapFromGlobal(view->mapToGlobal(upper));
		const QPoint to(xAt(4 * Bar), rowY(1));
		QTest::mouseMove(m_overlay, to);
		QTest::mouseRelease(m_overlay, Qt::LeftButton, Qt::NoModifier, to);
		const ArrangerRange& range = ArrangerRange::instance();
		QVERIFY(range.active());
		QVERIFY2(std::abs(range.start() - Bar) <= 24, qPrintable(QString::number(range.start())));  // where the clip starts, snapped
		QCOMPARE(range.start(), m_overlay->tickAt(inOverlay.x()));
		QCOMPARE(range.end(), 4 * Bar);
		QCOMPARE(range.tracks().size(), 2);  // the clip's track and the one below
		QVERIFY(QWidget::mouseGrabber() != m_overlay);
	}

	void shiftClickingALockedClipKeepsTheExistingSelection()
	{
		Clip* selected = m_tracks.at(0)->createClip(TimePos(Bar));
		Clip* locked = m_tracks.at(1)->createClip(TimePos(2 * Bar));
		QCoreApplication::processEvents();
		ClipView* selectedView = nullptr;
		ClipView* lockedView = nullptr;
		for (ClipView* candidate : m_editor->findChildren<ClipView*>())
		{
			if (candidate->getClip() == selected) { selectedView = candidate; }
			if (candidate->getClip() == locked) { lockedView = candidate; }
		}
		QVERIFY(selectedView != nullptr && lockedView != nullptr);
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(locked)}}).ok);

		m_editor->selectAllClips(false);
		selectedView->setSelected(true);
		QTest::mouseClick(lockedView, Qt::LeftButton, Qt::ShiftModifier,
			QPoint(lockedView->width() / 2, lockedView->height() * 3 / 4));

		QVERIFY2(selectedView->isSelected(), "Shift-clicking a locked clip cleared the existing selection");
		QVERIFY(lockedView->isSelected());
	}

	void theVideoLaneTakesRoomOnlyWhileAVideoIsAttached()
	{
		auto* lane = m_editor->findChild<VideoLane*>();
		QVERIFY(lane != nullptr);
		QCOMPARE(lane->objectName(), QStringLiteral("arrangerVideoLane"));
		QVERIFY(!lane->isVisible());
		QCOMPARE(lane->height(), 0);
		QCOMPARE(lane->thumbnailsReady(), 0);

		// A video the decoder cannot read (no such file) still lays the strip out, with plain blocks.
		VideoAttachment attachment;
		attachment.path = QStringLiteral("/nowhere/clip.mkv");
		attachment.offset = 2 * Bar;
		attachment.fps = 25.0;
		attachment.duration = 8.0;
		attachment.width = 160;
		attachment.height = 90;
		VideoReference::instance().set(attachment);
		QTRY_VERIFY(lane->isVisible());
		QCOMPARE(lane->height(), VideoLane::LaneHeight);
		const tokens::Palette& pal = tokens::palette();
		const QImage plain = lane->grab().toImage();  // paints the strip: plain blocks, no thumbnail has arrived
		QVERIFY2(lane->drawnSpan().second > lane->drawnSpan().first, "the video's span was not drawn");
		QCOMPARE(lane->drawnSpan().first, m_editor->timeLine()->markerX(TimePos(2 * Bar)));
		const int spanWidth = lane->drawnSpan().second - lane->drawnSpan().first;
		QCOMPARE(plain.pixelColor(lane->drawnSpan().first + 3, 15), pal.control);   // a block in the span
		QCOMPARE(plain.pixelColor(lane->drawnSpan().first - 3, 15), pal.panel);     // beside the video: the lane's own ground

		// A click on the strip moves the play position to that tick; beside it does nothing.
		const int inside = (lane->drawnSpan().first + lane->drawnSpan().second) / 2;
		Engine::getSong()->setPlayPos(0);
		QTest::mouseClick(lane, Qt::LeftButton, Qt::NoModifier, QPoint(inside, 10));
		QVERIFY2(Engine::getSong()->getPlayPos().getTicks() > 2 * Bar, "the click did not seek");
		Engine::getSong()->setPlayPos(0);
		QTest::mouseClick(lane, Qt::LeftButton, Qt::NoModifier, QPoint(lane->drawnSpan().first - 5, 10));
		QCOMPARE(Engine::getSong()->getPlayPos().getTicks(), 0);
		QTest::mouseClick(lane, Qt::RightButton, Qt::NoModifier, QPoint(inside, 10));
		QCOMPARE(Engine::getSong()->getPlayPos().getTicks(), 0);

		// It follows the ruler's zoom and scroll, and goes away with the video.
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 20}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		lane->grab();  // repaint at the new zoom, which records the span it drew
		const int zoomedWidth = lane->drawnSpan().second - lane->drawnSpan().first;
		QVERIFY2(std::abs(zoomedWidth * 2 - spanWidth) <= 4, qPrintable(QStringLiteral("%1 -> %2").arg(spanWidth).arg(zoomedWidth)));
		VideoReference::instance().set({});
		QTRY_VERIFY(!lane->isVisible());
		QCOMPARE(lane->height(), 0);
	}

#ifdef ZENE_HAVE_FFMPEG
	void theThumbnailsArriveOneAtATimeFromARealVideo()
	{
		const QString clip = m_home.filePath(QStringLiteral("clip.mkv"));
		if (!videotest::makeClip(clip)) { QSKIP("the test clip could not be made"); }
		auto* lane = m_editor->findChild<VideoLane*>();
		// Zoomed in until the two-second clip is several tiles wide (at 40 px a bar it is under one).
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 400}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		const ControlResult attached = run(QStringLiteral("video.attach"), {{QStringLiteral("path"), clip}});
		QVERIFY2(attached.ok, qPrintable(attached.errorMessage));
		QTRY_VERIFY_WITH_TIMEOUT(lane->thumbnailsReady() >= 3, 15000);
		QTRY_COMPARE_WITH_TIMEOUT(lane->thumbnailsReady(), VideoLane::Thumbnails, 30000);
		// The clip is one second red, one second blue: the tiles at the two ends of the strip show it.
		const QImage strip = lane->grab().toImage();
		QVERIFY(lane->drawnSpan().second > lane->drawnSpan().first);
		const QColor start = strip.pixelColor(lane->drawnSpan().first + 6, 15);
		const QColor end = strip.pixelColor(lane->drawnSpan().second - 8, 15);
		QVERIFY2(start.red() > start.blue() + 60, qPrintable(start.name()));
		QVERIFY2(end.blue() > end.red() + 60, qPrintable(end.name()));
		// Attaching again starts over: an answer from before the change is dropped.
		VideoReference::instance().set({});
		QCOMPARE(lane->thumbnailsReady(), 0);
	}
#endif

private:
	int xAt(tick_t tick) const { return m_editor->timeLine()->markerX(TimePos(tick)); }

	//! The y, in the overlay, of the middle of track @a index's row.
	int rowY(int index) const
	{
		TrackView* view = m_editor->trackViews().at(index);
		return view->mapTo(m_overlay->parentWidget(), QPoint(0, view->height() / 2)).y();
	}

	tick_t visibleTicks() const
	{
		return arrangerzoom::visibleTicks(m_editor);
	}

	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
	ArrangerRangeOverlay* m_overlay = nullptr;
	std::vector<Track*> m_tracks;
};

QTEST_MAIN(ArrangerOverlaysVideoLaneTest)
#include "ArrangerOverlaysVideoLaneTest.moc"
