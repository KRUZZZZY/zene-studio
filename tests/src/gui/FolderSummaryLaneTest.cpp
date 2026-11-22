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
 *  arranger's own range is left as the user had it.
 *
 *  Q050-255: a summary clip is also resized by its edges and split from its menu, over its child clips on every track
 *  (nested ones too); clicks select several, which the keyboard route and the edits take with the arranger's own selected
 *  clips - a clip reached twice changing once - and each edit is one undo step. */

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <QtTest>

#include <QContextMenuEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMenu>
#include <QTimer>

#include "ArrangerClipKeys.h"
#include "ClipView.h"
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
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackContentWidget.h"
#include "TrackFolder.h"
#include "TrackLabelButton.h"
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

using Spans = std::vector<std::pair<tick_t, tick_t>>;

//! Every clip of @a track as (start, end), earliest first.
Spans layoutOf(const Track* track)
{
	Spans out;
	for (const Clip* clip : track->getClips()) { out.emplace_back(clip->startPosition().getTicks(), clip->endPosition().getTicks()); }
	std::sort(out.begin(), out.end());
	return out;
}

void hover(QWidget* widget, const QPoint& pos)
{
	QMouseEvent event(QEvent::MouseMove, pos, widget->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(widget, &event);
}

bool press(int key, Qt::KeyboardModifiers mods)
{
	QKeyEvent event(QEvent::KeyPress, key, mods);
	return arrangerkeys::handle(getGUI()->songEditor()->m_editor, &event);
}

const Qt::KeyboardModifiers Chord = Qt::ControlModifier | Qt::AltModifier;

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

	void settingsControlsIndentWhenTrackIsInFolder()
	{
		Track* pattern = Track::create(Track::Type::Pattern, Engine::getSong());
		QVERIFY(pattern != nullptr);
		QVERIFY(run(QStringLiteral("track.set_folder"), {{QStringLiteral("track"), control::trackIdOf(pattern)},
			{QStringLiteral("folder"), control::trackIdOf(m_folder)}}).ok);
		QCoreApplication::processEvents();

		TrackView* view = viewOf(pattern);
		QVERIFY(view != nullptr);
		auto* label = view->getTrackSettingsWidget()->findChild<TrackLabelButton*>(QString(), Qt::FindDirectChildrenOnly);
		QVERIFY(label != nullptr);
		QCOMPARE(label->x(), 13); // its original x is 3, plus one folder level of indentation
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

	void anEdgeDragLengthensTheClipsAtThatEdgeInOneStep()
	{
		const Spans a = layoutOf(m_a);
		const Spans b = layoutOf(m_b);
		dragEdge(m_lane->spanRect(m_lane->spans()[0]).right() - 1, bar());
		// The first summary clip now ends a bar later; only the clip that ended there moved out, and the next span stayed.
		QCOMPARE(layoutOf(m_b), (Spans{{96, 288 + bar()}}));
		QCOMPARE(layoutOf(m_a), a);
		QCOMPARE(m_lane->spans()[0].end, 288 + bar());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layoutOf(m_a), a);
		QCOMPARE(layoutOf(m_b), b);
	}

	void anEdgeDragInwardsCutsTheChildClipsTogether()
	{
		const Spans a = layoutOf(m_a);
		const Spans b = layoutOf(m_b);
		// Both clips cross the new end: both are cut there together.
		dragEdge(m_lane->spanRect(m_lane->spans()[0]).right() - 1, -96);
		QCOMPARE(layoutOf(m_a), (Spans{{0, 192}, {768, 960}}));
		QCOMPARE(layoutOf(m_b), (Spans{{96, 192}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		// Past every start but the first: the clip wholly past it goes, and one undo brings it back.
		dragEdge(m_lane->spanRect(m_lane->spans()[0]).right() - 1, -192);
		QCOMPARE(layoutOf(m_a), (Spans{{0, 96}, {768, 960}}));
		QVERIFY(m_b->getClips().empty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layoutOf(m_a), a);
		QCOMPARE(layoutOf(m_b), b);
	}

	void theStartEdgeTrimsAndKeepsTheContentInPlace()
	{
		const Spans a = layoutOf(m_a);
		Clip* first = m_a->getClips().front();
		const tick_t offset = first->startTimeOffset().getTicks();
		dragEdge(m_lane->spanRect(m_lane->spans()[0]).left() + 1, 96);
		QCOMPARE(layoutOf(m_a), (Spans{{96, 192}, {768, 960}}));
		QCOMPARE(layoutOf(m_b), (Spans{{96, 288}}));
		QCOMPARE(first->startTimeOffset().getTicks(), offset - 96);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layoutOf(m_a), a);
		// Out to the left: the second summary clip's start, not past the first summary clip's end.
		dragEdge(m_lane->spanRect(m_lane->spans()[1]).left() + 1, -bar());
		QCOMPARE(layoutOf(m_a), (Spans{{0, 192}, {768 - bar(), 960}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layoutOf(m_a), a);
		const QJsonObject one{{QStringLiteral("folder"), control::trackIdOf(m_folder)}, {QStringLiteral("start"), 768}, {QStringLiteral("end"), 960}};
		const ControlResult tooFar = run(QStringLiteral("track.folder_span_resize"), {{QStringLiteral("spans"), QJsonArray{one}},
			{QStringLiteral("edge"), QStringLiteral("start")}, {QStringLiteral("delta"), -600}});
		QCOMPARE(tooFar.errorKind, ControlErrorKind::Refused);
		QCOMPARE(layoutOf(m_a), a);
	}

	void theCursorShowsAnEdgeAndAClickSelects()
	{
		const QRect first = m_lane->spanRect(m_lane->spans()[0]);
		hover(m_lane, QPoint(first.right() - 1, first.center().y()));
		QCOMPARE(m_lane->cursor().shape(), Qt::SizeHorCursor);
		hover(m_lane, first.center());
		QVERIFY(m_lane->cursor().shape() != Qt::SizeHorCursor);
		QTest::mouseClick(m_lane, Qt::LeftButton, Qt::NoModifier, first.center());
		QCOMPARE(selectedCount(), 1);
		QTest::mouseClick(m_lane, Qt::LeftButton, Qt::ShiftModifier, m_lane->spanRect(m_lane->spans()[1]).center());
		QCOMPARE(selectedCount(), 2);
		QTest::mouseClick(m_lane, Qt::LeftButton, Qt::ShiftModifier, first.center());  // Shift takes it out again
		QCOMPARE(selectedCount(), 1);
		QVERIFY(run(QStringLiteral("track.folder_span_select")).ok);
		QCOMPARE(selectedCount(), 0);
	}

	void theMenuSplitsEveryCrossingClipIncludingNestedOnes()
	{
		// A sub-folder's track inside the first summary clip: the split reaches it too.
		auto* sub = dynamic_cast<TrackFolder*>(Track::create(Track::Type::Folder, Engine::getSong()));
		Track* nested = Track::create(Track::Type::Sample, Engine::getSong());
		QVERIFY(run(QStringLiteral("track.set_folder"), {{QStringLiteral("track"), control::trackIdOf(sub)}, {QStringLiteral("folder"), control::trackIdOf(m_folder)}}).ok);
		QVERIFY(run(QStringLiteral("track.set_folder"), {{QStringLiteral("track"), control::trackIdOf(nested)}, {QStringLiteral("folder"), control::trackIdOf(sub)}}).ok);
		clipAt(nested, 48, 192);
		const Spans a = layoutOf(m_a);
		const Spans b = layoutOf(m_b);
		const FolderSpan span = m_lane->spans()[0];
		const int x = m_lane->spanRect(span).left() + (bar() / 2) * m_lane->spanRect(span).width() / (span.end - span.start);
		menuAction(QPoint(x, m_lane->spanRect(span).center().y()), QStringLiteral("track.folder_span_split"));
		QCOMPARE(layoutOf(m_a), (Spans{{0, 96}, {96, 192}, {768, 960}}));
		QCOMPARE(layoutOf(m_b), (Spans{{96, 288}}));  // it starts at the cut: not crossed
		QCOMPARE(layoutOf(nested), (Spans{{48, 96}, {96, 240}}));
		QCOMPARE(m_lane->spans().size(), std::size_t{2});  // the pieces touch: still one summary clip
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layoutOf(m_a), a);
		QCOMPARE(layoutOf(nested), (Spans{{48, 240}}));
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), control::trackIdOf(nested)}}).ok);
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), control::trackIdOf(sub)}}).ok);
		QCOMPARE(layoutOf(m_b), b);
	}

	void aSelectionOfSpansAndClipsResizesOnceInOneStepFromTheKeyboard()
	{
		const Spans a = layoutOf(m_a);
		const Spans b = layoutOf(m_b);
		Track* outside = Track::create(Track::Type::Sample, Engine::getSong());
		clipAt(outside, 0, 192);
		QCoreApplication::processEvents();
		QTest::mouseClick(m_lane, Qt::LeftButton, Qt::NoModifier, m_lane->spanRect(m_lane->spans()[0]).center());
		QTest::mouseClick(m_lane, Qt::LeftButton, Qt::ShiftModifier, m_lane->spanRect(m_lane->spans()[1]).center());
		// In the arranger's selection too: an ordinary clip, and a child of a selected summary clip that must change once.
		SongEditor* editor = getGUI()->songEditor()->m_editor;
		editor->selectAllClips(false);
		for (ClipView* view : editor->findChildren<ClipView*>())
		{
			if (view->getClip()->getTrack() == outside || view->getClip()->getTrack() == m_b) { view->setSelected(true); }
		}
		const tick_t snap = std::max<tick_t>(1, std::lround(editor->getSnapSize() * TimePos::ticksPerBar()));
		QVERIFY(press(Qt::Key_Right, Chord));
		QCOMPARE(layoutOf(m_a), (Spans{{0, 192}, {768, 960 + snap}}));
		QCOMPARE(layoutOf(m_b), (Spans{{96, 288 + snap}}));  // once, not twice
		QCOMPARE(layoutOf(outside), (Spans{{0, 192 + snap}}));
		QCOMPARE(selectedCount(), 2);  // the selection stays on them
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the one step
		QCOMPARE(layoutOf(m_a), a);
		QCOMPARE(layoutOf(m_b), b);
		QCOMPARE(layoutOf(outside), (Spans{{0, 192}}));
		QCOMPARE(selectedCount(), 2);  // an undo rebuilt the clips: the selection found them again
		editor->selectAllClips(false);
		QVERIFY(run(QStringLiteral("track.remove"), {{QStringLiteral("track"), control::trackIdOf(outside)}}).ok);
		QVERIFY(run(QStringLiteral("track.folder_span_select")).ok);
		QCoreApplication::processEvents();
	}

	void theKeysChooseSplitMoveAndDelete()
	{
		const Spans a = layoutOf(m_a);
		const Spans b = layoutOf(m_b);
		Engine::getSong()->setPlayPos(144, Song::PlayMode::Song);
		// Nothing selected: the edit keys are left alone.
		QVERIFY(!press(Qt::Key_K, Chord));
		QVERIFY(press(Qt::Key_G, Chord));
		QCOMPARE(selectedCount(), 1);

		QVERIFY(press(Qt::Key_K, Chord));
		QCOMPARE(layoutOf(m_a), (Spans{{0, 144}, {144, 192}, {768, 960}}));
		QCOMPARE(layoutOf(m_b), (Spans{{96, 144}, {144, 288}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layoutOf(m_a), a);

		const tick_t snap = std::max<tick_t>(1, std::lround(getGUI()->songEditor()->m_editor->getSnapSize() * TimePos::ticksPerBar()));
		QVERIFY(press(Qt::Key_PageDown, Chord));
		QCOMPARE(layoutOf(m_b), (Spans{{96 + snap, 288 + snap}}));
		QCOMPARE(selectedCount(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layoutOf(m_b), b);

		QVERIFY(press(Qt::Key_Right, Chord | Qt::ShiftModifier));  // the start edge, a snap step inwards
		QCOMPARE(layoutOf(m_a).front(), (std::pair<tick_t, tick_t>{snap, 192}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);

		QVERIFY(press(Qt::Key_Delete, Qt::NoModifier));
		QCOMPARE(layoutOf(m_a), (Spans{{768, 960}}));
		QVERIFY(m_b->getClips().empty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layoutOf(m_a), a);
		QCOMPARE(layoutOf(m_b), b);

		QVERIFY(!press(Qt::Key_Escape, Qt::NoModifier));  // the arranger's own Escape still runs
		QCOMPARE(selectedCount(), 0);
		Engine::getSong()->setPlayPos(0, Song::PlayMode::Song);
	}

	void aDragOfASelectedSpanMovesTheWholeSelection()
	{
		const Spans a = layoutOf(m_a);
		const Spans b = layoutOf(m_b);
		QTest::mouseClick(m_lane, Qt::LeftButton, Qt::NoModifier, m_lane->spanRect(m_lane->spans()[0]).center());
		QTest::mouseClick(m_lane, Qt::LeftButton, Qt::ShiftModifier, m_lane->spanRect(m_lane->spans()[1]).center());
		const QPoint from = m_lane->spanRect(m_lane->spans()[1]).center();
		const QPoint to = from + QPoint(static_cast<int>(viewOf(m_folder)->trackContainerView()->pixelsPerBar()), 0);
		QTest::mousePress(m_lane, Qt::LeftButton, Qt::NoModifier, from);
		QTest::mouseRelease(m_lane, Qt::LeftButton, Qt::NoModifier, to);
		QCOMPARE(layoutOf(m_a), (Spans{{bar(), 192 + bar()}, {768 + bar(), 960 + bar()}}));
		QCOMPARE(layoutOf(m_b), (Spans{{96 + bar(), 288 + bar()}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(layoutOf(m_a), a);
		QCOMPARE(layoutOf(m_b), b);
		QVERIFY(run(QStringLiteral("track.folder_span_select")).ok);
	}

	void aLockedChildRefusesTheGroupEdit()
	{
		const Spans a = layoutOf(m_a);
		const QString clip = control::clipIdOf(m_a->getClips().front());
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), clip}, {QStringLiteral("locked"), true}}).ok);
		const QJsonObject one{{QStringLiteral("folder"), control::trackIdOf(m_folder)}, {QStringLiteral("start"), 0}, {QStringLiteral("end"), 288}};
		QCOMPARE(run(QStringLiteral("track.folder_span_split"), {{QStringLiteral("spans"), QJsonArray{one}}, {QStringLiteral("at"), 144}}).errorKind, ControlErrorKind::Refused);
		QCOMPARE(layoutOf(m_a), a);
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), clip}, {QStringLiteral("locked"), false}}).ok);
	}

private:
	static tick_t bar() { return TimePos::ticksPerBar(); }

	int selectedCount() const
	{
		return run(QStringLiteral("track.folder_spans"), {{QStringLiteral("folder"), control::trackIdOf(m_folder)}}).result.value(QStringLiteral("selected")).toArray().size();
	}

	//! Press at @a x on the lane and drag by @a ticks.
	void dragEdge(int x, tick_t ticks)
	{
		const int y = m_lane->spanRect(m_lane->spans()[0]).center().y();
		const int dx = static_cast<int>(std::lround(ticks * viewOf(m_folder)->trackContainerView()->pixelsPerBar() / bar()));
		QTest::mousePress(m_lane, Qt::LeftButton, Qt::NoModifier, QPoint(x, y));
		QTest::mouseRelease(m_lane, Qt::LeftButton, Qt::NoModifier, QPoint(x + dx, y));
	}

	//! Open the lane's menu at @a pos and trigger the action that runs @a command.
	void menuAction(const QPoint& pos, const QString& command)
	{
		bool triggered = false;
		QTimer::singleShot(0, this, [&] {
			auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
			if (menu == nullptr) { return; }
			for (QAction* action : menu->actions())
			{
				if (action->data().toString() != command) { continue; }
				triggered = true;
				action->trigger();
			}
			menu->close();
		});
		QContextMenuEvent event(QContextMenuEvent::Mouse, pos, m_lane->mapToGlobal(pos));
		QApplication::sendEvent(m_lane, &event);
		QVERIFY2(triggered, qPrintable(QStringLiteral("no menu action runs ") + command));
	}

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
