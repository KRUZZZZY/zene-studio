/*
 * SampleClipWarpEditTest.cpp - warp markers authored over a sample clip's waveform (Q050-063)
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// Everything here drives the REAL song editor: the clip's own SampleClipView takes the pointer events a
// musician makes, the commands it sends are the registered warp.add / warp.move / warp.remove, and the
// marker map it shows is read back against warp.list. The project render is the real export path.

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QMenu>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "../GuiTestApplication.h"
#include "MainWindow.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleClipView.h"
#include "SamplePlayHandle.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "SongEditor.h"

#include "SampleClipWarpEditTestSupport.h"

using namespace lmms;
using namespace lmms::gui;

class SampleClipWarpEditTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		QVERIFY(m_out.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		m_editor = getGUI()->songEditor()->m_editor;
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_track = new SampleTrack(Engine::getSong());
		m_clip = dynamic_cast<SampleClip*>(m_track->createClip(TimePos(0)));
		QVERIFY(m_clip != nullptr);
		m_clip->setSampleBuffer(burstBuffer());
		m_clip->changeLength(TimePos(static_cast<int>(kSeconds * kRate / Engine::framesPerTick(kRate))));
		m_clip->setAutoResize(false);
		Engine::getSong()->updateLength();
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		m_view = nullptr;
		for (SampleClipView* view : m_editor->findChildren<SampleClipView*>())
		{
			if (view->getClip() == m_clip) { m_view = view; }
		}
		QVERIFY2(m_view != nullptr, "the song editor built no view for the sample clip");
		QVERIFY2(m_view->warpOverlayFrame(0).pixelsPerTick > 0.3f, "the clip is too narrow to point at");
	}

	//! warp.add / warp.move from the agent surface: the view shows exactly the map warp.list reads, and each marker
	//! is a line through the waveform at its mapped position.
	void anAgentEditShowsAsTheSameMapWithALineThroughTheWaveform()
	{
		const ClipOverlayFrame frame = m_view->warpOverlayFrame(0);
		QVERIFY(agentMap(m_clip).empty());
		QVERIFY(viewMap(m_view).empty());
		const QImage before = widgetImage(m_view);

		QVERIFY(addByAgent(kBursts[0], 56).ok);
		QVERIFY(addByAgent(kBursts[2], 170).ok);
		QCoreApplication::processEvents();
		const std::vector<std::pair<qint64, qint64>> expected{{kBursts[0], 56}, {kBursts[2], 170}};
		QCOMPARE(agentMap(m_clip), expected);
		QCOMPARE(viewMap(m_view), expected);

		const QImage drawn = widgetImage(m_view);
		const int firstX = static_cast<int>(std::lround(56 * frame.pixelsPerTick));
		const int secondX = static_cast<int>(std::lround(170 * frame.pixelsPerTick));
		const int rows = m_view->height() / 2;
		QVERIFY2(guideRowsAt(drawn, firstX) > rows, "the first marker is not a line through the waveform");
		QVERIFY2(guideRowsAt(drawn, secondX) > rows, "the second marker is not a line through the waveform");
		QCOMPARE(guideRowsAt(before, firstX), 0);  // not there before the markers

		// Moving one from the agent surface moves its line, and the view reads back the new map.
		ControlResult moved = ControlRegistry::instance()->invoke(QStringLiteral("warp.move"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)},
			{QStringLiteral("source_frame"), static_cast<qint64>(kBursts[2])}, {QStringLiteral("offset_ticks"), 200}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		QCoreApplication::processEvents();
		const std::vector<std::pair<qint64, qint64>> after{{kBursts[0], 56}, {kBursts[2], 200}};
		QCOMPARE(agentMap(m_clip), after);
		QCOMPARE(viewMap(m_view), after);
		const QImage redrawn = widgetImage(m_view);
		QVERIFY(guideRowsAt(redrawn, static_cast<int>(std::lround(200 * frame.pixelsPerTick))) > rows);
		QCOMPARE(guideRowsAt(redrawn, secondX), 0);
	}

	//! The menu turns warp editing on; a double-click then adds a marker at the click - the frame heard there,
	//! where it is heard - and a double-click on a marker removes it. Each is one registered command, one undo step.
	void aDoubleClickAddsAMarkerAndOneOnAMarkerRemovesIt()
	{
		QVERIFY(!m_view->warpEditing());
		QVERIFY2(triggerInSubmenu(m_view, QStringLiteral("Warp markers"), QStringLiteral("Edit warp markers")),
			"the clip's context menu has no Warp markers > Edit warp markers");
		QVERIFY(m_view->warpEditing());

		const ClipOverlayFrame frame = m_view->warpOverlayFrame(0);
		const int x = pixelOfTick(100);
		const int y = m_view->height() / 2;
		QTest::mouseDClick(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(x, y));
		QCoreApplication::processEvents();

		QCOMPARE(m_clip->warpMarkers().size(), 1);
		const WarpMarker added = m_clip->warpMarkers()[0];
		const int tolerance = static_cast<int>(std::ceil(1.0f / frame.pixelsPerTick));
		QVERIFY2(std::abs(static_cast<int>(added.offsetTicks) - 100) <= tolerance, "the marker is not where the click was");
		// It pins the frame heard at the click: adding a marker changes no sound.
		QCOMPARE(added.sourceFrame, m_clip->sourceFrameAt(TimePos(static_cast<int>(added.offsetTicks))));
		QCOMPARE(agentMap(m_clip), viewMap(m_view));
		QCOMPARE(static_cast<int>(agentMap(m_clip).size()), 1);

		// One undo step takes it back off.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCoreApplication::processEvents();
		QVERIFY(m_clip->warpMarkers().empty());
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.redo")).ok);
		QCoreApplication::processEvents();
		QCOMPARE(m_clip->warpMarkers().size(), 1);

		// A double-click on the marker itself removes it.
		const int markerX = pixelOfTick(static_cast<int>(added.offsetTicks));
		QTest::mouseDClick(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(markerX, y));
		QCoreApplication::processEvents();
		QVERIFY(m_clip->warpMarkers().empty());
		QVERIFY(agentMap(m_clip).empty());
		QVERIFY(viewMap(m_view).empty());
	}

	//! A drag shows the proposed map and the stretched waveform at once, writes nothing until the release, and the
	//! release is ONE warp.move (one undo step) that leaves the view showing exactly what the drag showed.
	void aDragPreviewsTheWaveformThenCommitsOneUndoableMove()
	{
		enableEditing();
		QVERIFY(addByAgent(kBursts[0], 56).ok);
		QVERIFY(addByAgent(kBursts[2], 168).ok);
		m_view->setWarpSnap(WarpSnap::Off);
		QCoreApplication::processEvents();

		const std::vector<WarpWaveformSegment> segmentsBefore = m_view->shownWarpSegments();
		QVERIFY2(segmentsBefore.size() >= 3, "a two-marker clip draws three stretches");
		const auto mapBefore = agentMap(m_clip);
		const int y = m_view->height() / 2;
		const QImage imageBefore = widgetImage(m_view);

		QTest::mousePress(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(56), y));
		QVERIFY(m_view->warpDragging());
		movePointer(m_view, QPoint(pixelOfTick(96), y));

		// Mid-drag: the proposal is shown, nothing is committed.
		QVERIFY(m_view->warpDragging());
		QCOMPARE(agentMap(m_clip), mapBefore);
		const auto proposed = viewMap(m_view);
		QCOMPARE(proposed.at(0).first, static_cast<qint64>(kBursts[0]));  // the marker keeps its frame
		QVERIFY2(proposed.at(0).second > 80, "the dragged marker does not follow the pointer");
		QCOMPARE(proposed.at(1), mapBefore.at(1));
		const std::vector<WarpWaveformSegment> segmentsDuring = m_view->shownWarpSegments();
		QVERIFY2(segmentsDuring != segmentsBefore, "the waveform preview did not change during the drag");
		QVERIFY2(widgetImage(m_view) != imageBefore, "nothing on screen changed during the drag");
		// Before the first marker the audio keeps its rate and moves with it; the stretch between the two markers
		// is the one that shrank, and it now starts later.
		const auto width = [](const WarpWaveformSegment& segment) { return segment.x1 - segment.x0; };
		QVERIFY(segmentsDuring.at(0).x0 > segmentsBefore.at(0).x0);
		QVERIFY(segmentsDuring.at(1).x0 > segmentsBefore.at(1).x0);
		QVERIFY(width(segmentsDuring.at(1)) < width(segmentsBefore.at(1)));
		QCOMPARE(segmentsDuring.at(2).x1, segmentsBefore.at(2).x1 + (segmentsDuring.at(2).x0 - segmentsBefore.at(2).x0));

		const int depthBefore = undoDepth();
		QTest::mouseRelease(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(96), y));
		QCoreApplication::processEvents();

		// Committed: the agent reads what the drag proposed and the waveform is what the drag showed.
		QVERIFY(!m_view->warpDragging());
		QCOMPARE(agentMap(m_clip), proposed);
		QCOMPARE(viewMap(m_view), proposed);
		QVERIFY2(m_view->shownWarpSegments() == segmentsDuring, "the committed waveform is not the one the drag previewed");
		QCOMPARE(undoDepth(), depthBefore + 1);

		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCoreApplication::processEvents();
		QCOMPARE(agentMap(m_clip), mapBefore);
		QCOMPARE(viewMap(m_view), mapBefore);
		QVERIFY(m_view->shownWarpSegments() == segmentsBefore);
	}

	//! A drag stops at its neighbours: the engine's own strict order, so warp.move would refuse the rest.
	void aDragStaysBetweenItsNeighbours()
	{
		enableEditing();
		QVERIFY(addByAgent(kBursts[0], 56).ok);
		QVERIFY(addByAgent(kBursts[1], 112).ok);
		m_view->setWarpSnap(WarpSnap::Off);
		const int y = m_view->height() / 2;
		QTest::mousePress(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(56), y));
		movePointer(m_view, QPoint(pixelOfTick(300), y));
		QCOMPARE(viewMap(m_view).at(0).second, static_cast<qint64>(111));
		movePointer(m_view, QPoint(-40, y));
		QCOMPARE(viewMap(m_view).size(), std::size_t{2});
		QTest::mouseRelease(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(0, y));
		QCoreApplication::processEvents();
		QCOMPARE(agentMap(m_clip), viewMap(m_view));
		QVERIFY(agentMap(m_clip).at(0).second < agentMap(m_clip).at(1).second);
	}

	//! The grid and the transient snap are chosen from the same menu, and a drag lands on the grid line or on the
	//! transient, not where the pointer happened to be.
	void aDragSnapsToTheGridOrToATransient()
	{
		enableEditing();
		QVERIFY(addByAgent(kBursts[0], 56).ok);
		const int y = m_view->height() / 2;
		const float snapBars = m_editor->getSnapSize();
		const int grid = std::max(1, static_cast<int>(std::lround(snapBars * TimePos::ticksPerBar())));

		QVERIFY(triggerInSubmenu(m_view, QStringLiteral("Warp markers"), QStringLiteral("Snap to grid")));
		QCOMPARE(m_view->warpSnap(), WarpSnap::Grid);
		QTest::mousePress(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(56), y));
		movePointer(m_view, QPoint(pixelOfTick(100), y));
		const qint64 gridded = viewMap(m_view).at(0).second;
		QCOMPARE(static_cast<int>(gridded % grid), 0);
		QVERIFY(std::abs(static_cast<int>(gridded) - 100) <= grid / 2 + 1);
		QVERIFY2(gridded != 100 || grid == 1, "the grid snap left the marker where the pointer was");
		// Alt drags without snapping, and the release commits what was proposed.
		movePointer(m_view, QPoint(pixelOfTick(100), y), Qt::AltModifier);
		const qint64 free = viewMap(m_view).at(0).second;
		QVERIFY(std::abs(static_cast<int>(free) - 100) <= static_cast<int>(std::ceil(1.0f / m_view->warpOverlayFrame(0).pixelsPerTick)));
		QTest::mouseRelease(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(100), y));
		QCoreApplication::processEvents();
		QCOMPARE(agentMap(m_clip).at(0).second, free);
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCoreApplication::processEvents();
		QCOMPARE(agentMap(m_clip).at(0).second, static_cast<qint64>(56));

		// Transients: with the marker pinned to the first burst at tick 56 the second burst is heard at tick 112.
		QVERIFY(triggerInSubmenu(m_view, QStringLiteral("Warp markers"), QStringLiteral("Snap to transients")));
		QCOMPARE(m_view->warpSnap(), WarpSnap::Transient);
		QTest::mousePress(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(56), y));
		movePointer(m_view, QPoint(pixelOfTick(110), y));
		const qint64 snapped = viewMap(m_view).at(0).second;
		QVERIFY2(std::abs(static_cast<int>(snapped) - 112) <= 1, qPrintable(QStringLiteral("snapped to %1, not the transient at 112").arg(snapped)));
		// Far from every transient the marker goes where the pointer is.
		movePointer(m_view, QPoint(pixelOfTick(84), y));
		QVERIFY(std::abs(static_cast<int>(viewMap(m_view).at(0).second) - 84) <= 2);
		QTest::mouseRelease(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(84), y));
	}

	//! The final render follows the committed marker: with the first marker where the audio naturally is, the burst
	//! the second marker pins is heard where that marker lies - before the drag and after it.
	void theRenderFollowsTheCommittedMarker()
	{
		enableEditing();
		m_view->setWarpSnap(WarpSnap::Off);
		const int y = m_view->height() / 2;
		QVERIFY(addByAgent(kBursts[0], 56).ok);
		QVERIFY(addByAgent(kBursts[1], 112).ok);
		const double framesPerTick = Engine::framesPerTick(kRate);
		const std::vector<int> before = burstOnsets(render(false));
		QCOMPARE(static_cast<int>(before.size()), 3);
		QVERIFY(std::abs(before.at(1) - 112 * framesPerTick) < 2 * framesPerTick);

		QTest::mousePress(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(112), y));
		movePointer(m_view, QPoint(pixelOfTick(140), y));
		QTest::mouseRelease(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(140), y));
		QCoreApplication::processEvents();
		const qint64 committed = agentMap(m_clip).at(1).second;
		QVERIFY(committed > 130);

		const std::vector<int> after = burstOnsets(render(true));
		QCOMPARE(static_cast<int>(after.size()), 3);
		QVERIFY2(std::abs(after.at(0) - static_cast<int>(kBursts[0])) < 2 * framesPerTick, "the first burst moved though its marker did not");
		QVERIFY2(std::abs(after.at(1) - committed * framesPerTick) < 2 * framesPerTick,
			qPrintable(QStringLiteral("the second burst starts at frame %1; the committed marker lies at frame %2")
				.arg(after.at(1)).arg(committed * framesPerTick)));
		QVERIFY2(after.at(1) > before.at(1) + 20 * framesPerTick, "the render did not move with the marker");
	}

	//! The FIRST marker is not special: dragging it moves the burst it pins to the committed tick in the export, later
	//! (the audio is held back until the map reaches it) and earlier (the pass starts further into the source).
	void theRenderFollowsAMovedFirstMarker()
	{
		enableEditing();
		m_view->setWarpSnap(WarpSnap::Off);
		const int y = m_view->height() / 2;
		const double framesPerTick = Engine::framesPerTick(kRate);
		QVERIFY(addByAgent(kBursts[0], 56).ok);
		const std::vector<int> natural = burstOnsets(render(false));
		QCOMPARE(static_cast<int>(natural.size()), 3);
		QVERIFY(std::abs(natural.at(0) - static_cast<int>(kBursts[0])) < 2 * framesPerTick);

		int fromTick = 56;
		for (const int toTick : {96, 30})
		{
			QTest::mousePress(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(fromTick), y));
			movePointer(m_view, QPoint(pixelOfTick(toTick), y));
			QTest::mouseRelease(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(toTick), y));
			QCoreApplication::processEvents();
			const qint64 committed = agentMap(m_clip).at(0).second;
			QVERIFY(std::abs(static_cast<int>(committed) - toTick) <= 2);
			fromTick = static_cast<int>(committed);

			const std::vector<int> onsets = burstOnsets(render(toTick == 96));
			QVERIFY2(!onsets.empty(), "the render is silent");
			// The burst the marker pins starts where the marker lies, not where it used to be.
			const double expected = committed * framesPerTick;
			const auto pinned = std::min_element(onsets.begin(), onsets.end(),
				[&](int a, int b) { return std::abs(a - expected) < std::abs(b - expected); });
			QVERIFY2(std::abs(*pinned - expected) < 2 * framesPerTick,
				qPrintable(QStringLiteral("marker at tick %1 (frame %2); the bursts start at frames %3 ...")
					.arg(committed).arg(expected).arg(onsets.front())));
			QVERIFY2(std::abs(*pinned - natural.at(0)) > 20 * framesPerTick, "the render did not move with the first marker");
		}
	}

	/*! The review's race, by handshake: a pass takes its generation and reads what a real one reads (the window's
	 *  mapping, SampleTrack::play's lead, a real SamplePlayHandle). The drag's release, warp.move and an undo then
	 *  commit to completion, and the same pass reads all of it again: nothing moved. The next pass sees the commit. */
	void aPassKeepsOneMapWhileACommitLands()
	{
		enableEditing();
		m_view->setWarpSnap(WarpSnap::Off);
		const int y = m_view->height() / 2;
		QVERIFY(addByAgent(kBursts[0], 56).ok);
		QVERIFY(addByAgent(kBursts[2], 168).ok);

		// The drag's release: warp.move on the UI thread, one undo step. Moving the second marker changes the handle's length.
		QTest::mousePress(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(168), y));
		movePointer(m_view, QPoint(pixelOfTick(240), y));
		const int depthBefore = undoDepth();
		const PassReads idle = passReads(m_clip);
		PassOverlap pass = overlapPass([&] { QTest::mouseRelease(m_view, Qt::LeftButton, Qt::NoModifier, QPoint(pixelOfTick(240), y)); });
		QVERIFY2(pass.finished, "the pass or the commit did not finish");
		QVERIFY2(pass.before == idle, "the pass did not start from the published map");
		QVERIFY2(pass.after == pass.before, "the drag's commit changed what the pass in progress reads");
		QCOMPARE(undoDepth(), depthBefore + 1);
		const PassReads dragged = passReads(m_clip);
		QVERIFY2(dragged != idle, "the next pass does not see the committed map");
		QVERIFY(dragged.handleFrames != idle.handleFrames);

		// The agent's warp.move of the FIRST marker: this is the commit that changes the lead.
		pass = overlapPass([&] {
			QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("warp.move"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)},
				{QStringLiteral("source_frame"), static_cast<qint64>(kBursts[0])}, {QStringLiteral("offset_ticks"), 120}}).ok);
		});
		QVERIFY(pass.finished);
		QVERIFY2(pass.after == pass.before, "warp.move changed what the pass in progress reads");
		QCOMPARE(pass.before.lead, f_cnt_t{0});
		const PassReads moved = passReads(m_clip);
		QVERIFY2(moved.lead > 0, "the next pass does not owe the moved first marker its lead");

		// An undo restores through Clip::loadWarp; a clear publishes no generation at all.
		pass = overlapPass([&] { QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok); });
		QVERIFY(pass.finished);
		QVERIFY2(pass.after == pass.before, "the undo changed what the pass in progress reads");
		QVERIFY(passReads(m_clip) == dragged);
		pass = overlapPass([&] { m_clip->clearWarpMarkers(); });
		QVERIFY(pass.finished);
		QVERIFY2(pass.after == pass.before, "a clear changed what the pass in progress reads");
		QVERIFY(passReads(m_clip).map.empty());
	}

	//! The review's blocking point: a render period holds the engine's change lock, and a commit must not need it,
	//! or the next period could wait for the control thread.
	void aCommitDoesNotTakeTheLockARenderPeriodHolds()
	{
		QVERIFY(addByAgent(kBursts[0], 56).ok);
		std::atomic<bool> holding{false};
		std::atomic<bool> release{false};
		std::atomic<bool> timedOut{false};
		std::unique_ptr<QThread> period(QThread::create([&] {
			const auto lock = Engine::audioEngine()->requestChangesGuard();
			holding = true;
			QElapsedTimer held;
			held.start();
			while (!release) { if (held.elapsed() > 5000) { timedOut = true; return; } QThread::msleep(1); }
		}));
		period->start();
		QVERIFY(waitFor(holding));
		const WarpMarker next[] = {{kBursts[0], 96}, {kBursts[2], 168}};
		const bool taken = m_clip->setWarpMarkers(next);
		release = true;
		period->wait();
		QVERIFY(taken);
		QVERIFY2(!timedOut, "committing a map waited for the lock a render period holds");
		QCOMPARE(agentMap(m_clip).size(), std::size_t{2});
	}

	//! A refused map writes nothing, so it has no reason to wait for the audio thread: it returns while a pass is
	//! still running, and the map and the undo depth are what they were.
	void aRefusedMapChangesNothingAndDoesNotWaitForAPass()
	{
		QVERIFY(addByAgent(kBursts[0], 56).ok);
		QVERIFY(addByAgent(kBursts[2], 168).ok);
		const auto mapBefore = agentMap(m_clip);
		const int depthBefore = undoDepth();

		std::atomic<bool> holding{false};
		std::atomic<bool> release{false};
		std::atomic<bool> timedOut{false};
		std::unique_ptr<QThread> period(QThread::create([&] {
			const auto lock = Engine::audioEngine()->requestChangesGuard();
			holding = true;
			QElapsedTimer held;
			held.start();
			while (!release) { if (held.elapsed() > 5000) { timedOut = true; return; } QThread::msleep(1); }
		}));
		period->start();
		QVERIFY(waitFor(holding));

		const WarpMarker crossing[] = {{kBursts[0], 168}, {kBursts[2], 56}};
		const bool crossingTaken = m_clip->setWarpMarkers(crossing);
		std::vector<WarpMarker> tooMany;
		for (int i = 0; i <= WarpMarkers::MaxMarkers; ++i) { tooMany.push_back({static_cast<f_cnt_t>(100 + i), i}); }
		const bool tooManyTaken = m_clip->setWarpMarkers(tooMany);
		const ControlResult moved = ControlRegistry::instance()->invoke(QStringLiteral("warp.move"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)},
			{QStringLiteral("source_frame"), static_cast<qint64>(kBursts[0])}, {QStringLiteral("offset_ticks"), 500}});
		release = true;
		period->wait();

		QVERIFY2(!timedOut, "a refused map waited for the pass in progress");
		QVERIFY(!crossingTaken);
		QVERIFY(!tooManyTaken);
		QVERIFY(!moved.ok);
		QCOMPARE(agentMap(m_clip), mapBefore);
		QCOMPARE(undoDepth(), depthBefore);
	}

private:
	//! Everything a pass derives from the map.
	struct PassReads
	{
		f_cnt_t lead = 0;
		f_cnt_t sourceFrame = 0;
		int windowTicks = 0;
		f_cnt_t handleFrames = 0;
		WarpMarkers map;
		bool operator==(const PassReads& other) const
		{
			return lead == other.lead && sourceFrame == other.sourceFrame && windowTicks == other.windowTicks
				&& handleFrames == other.handleFrames && map == other.map;
		}
		bool operator!=(const PassReads& other) const { return !(*this == other); }
	};

	//! What a pass saw before and after @a commit ran on this (the control) thread.
	struct PassOverlap
	{
		bool finished = false;
		PassReads before;
		PassReads after;
	};

	//! The reads inside a scope, as SampleTrack::play makes them, ending with a real play handle.
	static PassReads readInPass(SampleClip* clip)
	{
		PassReads reads;
		reads.lead = clip->leadOutputFramesAt(TimePos(0));
		reads.sourceFrame = clip->sourceFrameAt(TimePos(100));
		reads.windowTicks = clip->windowTicksFor(clip->sampleWindow());
		SamplePlayHandle handle(clip, clip->sampleWindow());
		reads.handleFrames = handle.totalFrames();
		reads.map = clip->warpMarkers();
		return reads;
	}

	static PassReads passReads(SampleClip* clip)
	{
		const WarpSnapshot generation = clip->snapshotWarp();
		const Clip::WarpPassScope scope(*clip, generation);
		return readInPass(clip);
	}

	static bool waitFor(const std::atomic<bool>& flag)
	{
		QElapsedTimer waited;
		waited.start();
		while (!flag) { if (waited.elapsed() > 5000) { return false; } QThread::msleep(1); }
		return true;
	}

	PassOverlap overlapPass(const std::function<void()>& commit)
	{
		PassOverlap seen;
		std::atomic<bool> readOnce{false};
		std::atomic<bool> committed{false};
		std::atomic<bool> commitSeen{false};
		SampleClip* clip = m_clip;
		std::unique_ptr<QThread> pass(QThread::create([&, clip] {
			const WarpSnapshot generation = clip->snapshotWarp();
			const Clip::WarpPassScope scope(*clip, generation);
			seen.before = readInPass(clip);
			readOnce = true;
			commitSeen = waitFor(committed);
			seen.after = readInPass(clip);
		}));
		pass->start();
		const bool started = waitFor(readOnce);
		commit();
		committed = true;
		pass->wait();
		QCoreApplication::processEvents();
		seen.finished = started && commitSeen;
		return seen;
	}

	int pixelOfTick(int tick) const
	{
		return static_cast<int>(std::lround(tick * m_view->warpOverlayFrame(0).pixelsPerTick));
	}

	ControlResult addByAgent(f_cnt_t sourceFrame, int offsetTicks)
	{
		ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("warp.add"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)},
			{QStringLiteral("source_frame"), static_cast<qint64>(sourceFrame)}, {QStringLiteral("offset_ticks"), offsetTicks}});
		QCoreApplication::processEvents();
		return result;
	}

	void enableEditing()
	{
		QVERIFY(triggerInSubmenu(m_view, QStringLiteral("Warp markers"), QStringLiteral("Edit warp markers")));
		QVERIFY(m_view->warpEditing());
	}

	int undoDepth() const
	{
		return ControlRegistry::instance()->invoke(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
	}

	//! Renders the song through the real export path and returns the WAV's bytes.
	QByteArray render(bool second)
	{
		Engine::audioEngine()->audioDev()->stopProcessing();
		const QString path = m_out.filePath(second ? QStringLiteral("warped.wav") : QStringLiteral("unwarped.wav"));
		QFile::remove(path);
		const OutputSettings settings(kRate, 192, OutputSettings::BitDepth::Depth16Bit, OutputSettings::StereoMode::Stereo);
		ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
		if (!renderer.isReady()) { return QByteArray(); }
		renderer.startProcessing();
		renderer.wait();
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) { return QByteArray(); }
		return file.readAll();
	}

	QTemporaryDir m_home;
	QTemporaryDir m_out;
	SongEditor* m_editor = nullptr;
	SampleTrack* m_track = nullptr;
	SampleClip* m_clip = nullptr;
	SampleClipView* m_view = nullptr;
};

QTEST_MAIN(SampleClipWarpEditTest)
#include "SampleClipWarpEditTest.moc"
