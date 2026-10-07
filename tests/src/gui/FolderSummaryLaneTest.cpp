/*
 * FolderSummaryLaneTest.cpp - a folder's row draws and edits its children's clips as summary clips
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

/*! 0.5.0 ED-035, in the real application: a folder track's row carries one summary clip per stretch of
 *  its children's clips; dragging one moves only the clips inside it, Ctrl+dragging copies them, and its
 *  Delete removes them - each one undo step, through track.folder_move and the range commands - and the
 *  arranger's own range is left as the user had it. */

#include <QtTest>

#include <QJsonArray>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "FolderSummaryLane.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "SampleClip.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackContainerView.h"
#include "TrackContentWidget.h"
#include "TrackFolder.h"
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

Clip* clipAt(Track* track, tick_t start, tick_t length)
{
	Clip* clip = track->createClip(TimePos(start));
	clip->changeLength(TimePos(length));
	clip->movePosition(TimePos(start));
	return clip;
}

std::vector<tick_t> startsOf(const Track* track)
{
	std::vector<tick_t> out;
	for (const Clip* clip : track->getClips()) { out.push_back(clip->startPosition().getTicks()); }
	std::sort(out.begin(), out.end());
	return out;
}

} // namespace

class FolderSummaryLaneTest : public QObject
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
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! One folder for every case; each case undoes what it did, so the next starts from the same clips.
	void init()
	{
		if (m_folder == nullptr) { makeFolder(); }
		QVERIFY2(m_lane != nullptr, "the folder's row has no summary lane");
		QCOMPARE(startsOf(m_a), (std::vector<tick_t>{0, 768}));
		QCOMPARE(startsOf(m_b), (std::vector<tick_t>{96}));
		run(QStringLiteral("range.clear"));
	}

	void theRowDrawsOneSummaryClipPerStretch()
	{
		const std::vector<FolderSpan> spans = m_lane->spans();
		QCOMPARE(spans.size(), std::size_t{2});
		QCOMPARE(spans[0].start, tick_t{0});
		QCOMPARE(spans[0].end, tick_t{288});
		QCOMPARE(spans[0].clips, 2);
		QCOMPARE(spans[1].start, tick_t{768});
		QCOMPARE(m_lane->geometry(), m_lane->parentWidget()->rect());
		QVERIFY(m_lane->isVisible());
		const QRect first = m_lane->spanRect(spans[0]);
		QVERIFY(first.width() > 0);
		QCOMPARE(m_lane->spanAt(first.center()), 0);
		QCOMPARE(m_lane->spanAt(m_lane->spanRect(spans[1]).center()), 1);
		// Between them is the folder's own row, untouched.
		const QPoint gap((first.right() + m_lane->spanRect(spans[1]).left()) / 2, first.center().y());
		QCOMPARE(m_lane->spanAt(gap), -1);
		// The summary clip is painted: its pixels are not the row's background beside it.
		const QImage drawn = viewOf(m_folder)->getTrackContentWidget()->grab().toImage();
		QVERIFY(drawn.pixelColor(first.center() + QPoint(0, first.height() / 2 - 2)) != drawn.pixelColor(gap));
	}

	void dragMovesTheClipsInsideOneSummaryClip()
	{
		const std::vector<FolderSpan> spans = m_lane->spans();
		const QRect first = m_lane->spanRect(spans[0]);
		const auto* tcv = viewOf(m_folder)->trackContainerView();
		const int oneBar = static_cast<int>(tcv->pixelsPerBar());
		const QPoint from = first.center();
		const QPoint to = from + QPoint(oneBar, 0);
		QTest::mousePress(m_lane, Qt::LeftButton, Qt::NoModifier, from);
		QTest::mouseMove(m_lane, to);
		QTest::mouseRelease(m_lane, Qt::LeftButton, Qt::NoModifier, to);
		const tick_t bar = TimePos::ticksPerBar();
		QCOMPARE(startsOf(m_a), (std::vector<tick_t>{bar, 768}));  // the clip in the second span stays
		QCOMPARE(startsOf(m_b), (std::vector<tick_t>{96 + bar}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step puts both back
		QCOMPARE(startsOf(m_a), (std::vector<tick_t>{0, 768}));
		QCOMPARE(startsOf(m_b), (std::vector<tick_t>{96}));
	}

	void copyAndDeleteAreOneStepEachAndKeepTheRange()
	{
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 1920}, {QStringLiteral("end"), 2112}}).ok);
		const FolderSpan first = m_lane->spans()[0];
		QVERIFY(m_lane->copySpan(first, 1536));
		QCOMPARE(startsOf(m_a), (std::vector<tick_t>{0, 768, 1536}));
		QCOMPARE(startsOf(m_b), (std::vector<tick_t>{96, 1632}));
		QCOMPARE(m_lane->spans().size(), std::size_t{3});
		const QJsonObject range = run(QStringLiteral("range.get")).result;
		QCOMPARE(range.value(QStringLiteral("start")).toInt(), 1920);  // the user's range, back as it was
		QCOMPARE(range.value(QStringLiteral("end")).toInt(), 2112);
		QVERIFY(range.value(QStringLiteral("all_tracks")).toBool());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(startsOf(m_a), (std::vector<tick_t>{0, 768}));
		QCOMPARE(startsOf(m_b), (std::vector<tick_t>{96}));

		run(QStringLiteral("range.clear"));
		QVERIFY(m_lane->deleteSpan(first));
		QCOMPARE(startsOf(m_a), (std::vector<tick_t>{768}));
		QVERIFY(m_b->getClips().empty());
		QVERIFY(!run(QStringLiteral("range.get")).result.value(QStringLiteral("active")).toBool());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(startsOf(m_a), (std::vector<tick_t>{0, 768}));
		QCOMPARE(startsOf(m_b), (std::vector<tick_t>{96}));
	}

	void ctrlDragCopies()
	{
		const QRect second = m_lane->spanRect(m_lane->spans()[1]);
		const int oneBar = static_cast<int>(viewOf(m_folder)->trackContainerView()->pixelsPerBar());
		const QPoint from = second.center();
		const QPoint to = from + QPoint(2 * oneBar, 0);
		QTest::mousePress(m_lane, Qt::LeftButton, Qt::ControlModifier, from);
		QTest::mouseMove(m_lane, to);
		QTest::mouseRelease(m_lane, Qt::LeftButton, Qt::ControlModifier, to);
		QCOMPARE(startsOf(m_a), (std::vector<tick_t>{0, 768, 768 + 2 * TimePos::ticksPerBar()}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(startsOf(m_a), (std::vector<tick_t>{0, 768}));
	}

private:
	void makeFolder()
	{
		Song* song = Engine::getSong();
		m_folder = dynamic_cast<TrackFolder*>(Track::create(Track::Type::Folder, song));
		m_a = Track::create(Track::Type::Sample, song);
		m_b = Track::create(Track::Type::Sample, song);
		for (Track* child : {m_a, m_b})
		{
			QVERIFY(run(QStringLiteral("track.set_folder"), {{QStringLiteral("track"), control::trackIdOf(child)},
				{QStringLiteral("folder"), control::trackIdOf(m_folder)}}).ok);
		}
		clipAt(m_a, 0, 192);
		clipAt(m_b, 96, 192);      // overlaps: the first summary clip is 0..288
		clipAt(m_a, 768, 192);     // a gap: the second is 768..960
		QCoreApplication::processEvents();
		TrackView* view = viewOf(m_folder);
		QVERIFY(view != nullptr);
		m_lane = view->getTrackContentWidget()->findChild<FolderSummaryLane*>(QStringLiteral("folderSummaryLane"));
		QVERIFY2(m_lane != nullptr, "the folder's row has no summary lane");
	}

	QTemporaryDir m_home;
	TrackFolder* m_folder = nullptr;
	Track* m_a = nullptr;
	Track* m_b = nullptr;
	FolderSummaryLane* m_lane = nullptr;
};

QTEST_MAIN(FolderSummaryLaneTest)
#include "FolderSummaryLaneTest.moc"
