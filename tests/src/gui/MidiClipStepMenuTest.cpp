/*
 * MidiClipStepMenuTest.cpp - the beat step menu names the step under the cursor on a clip with many steps
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

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDomDocument>
#include <QMenu>
#include <QTemporaryDir>
#include <QTimer>

#include "ClipView.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "MidiClipControllers.h"
#include "MidiClipView.h"
#include "Note.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

MidiClipView* viewOf(const Clip* clip)
{
	for (MidiClipView* view : getGUI()->songEditor()->m_editor->findChildren<MidiClipView*>())
	{
		if (view->getClip() == clip) { return view; }
	}
	return nullptr;
}

//! Right-clicks at `at` and triggers `itemText` in the submenu titled `submenuTitle`.
bool triggerSubmenuItemOfContextMenu(QWidget* target, const QPoint& at, const QString& submenuTitle,
	const QString& itemText)
{
	bool found = false;
	QTimer::singleShot(0, [&]() {
		auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
		if (menu == nullptr) { return; }
		for (QAction* action : menu->actions())
		{
			if (action->menu() == nullptr || action->text() != submenuTitle) { continue; }
			for (QAction* item : action->menu()->actions())
			{
				if (item->text() == itemText)
				{
					found = true;
					item->trigger();
				}
			}
		}
		menu->close();
	});
	QContextMenuEvent context(QContextMenuEvent::Mouse, at, target->mapToGlobal(at), Qt::NoModifier);
	QCoreApplication::sendEvent(target, &context);
	return found;
}

} // namespace

class MidiClipStepMenuTest : public QObject
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
		m_track = Track::create(Track::Type::Instrument, Engine::getSong());
		QCoreApplication::processEvents();
	}

	//! The paint loop may visit steps only when their button image has at least one horizontal pixel.
	//! This bounds a narrow view's work without a timing assertion, even for the maximum loaded count.
	void beatPaintingSkipsStepsWithoutAWholePixel()
	{
		QCOMPARE(MidiClipView::beatStepCountToPaint(16, 64), 16);
		QCOMPARE(MidiClipView::beatStepCountToPaint(16, 8), 0);
		QCOMPARE(MidiClipView::beatStepCountToPaint(64, 32), 0);
		QCOMPARE(MidiClipView::beatStepCountToPaint(64, 128), 64);
		QCOMPARE(MidiClipView::beatStepCountToPaint(5'000'000, 1000), 0);
		QCOMPARE(MidiClipView::beatStepCountToPaint(64, 0), 0);
	}

	//! Merge preserves each lane kind and clips/rebases a nonzero source window; later input wins at a shared tick.
	void mergingClipsKeepsAndRebasesControllerLanes()
	{
		auto* a = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		QVERIFY(a != nullptr);
		a->changeLength(TimePos(Bar));
		a->setAutoResize(false);
		a->setControllerPoints(ControllerKind::ControlChange, 74, {{0, 20}, {96, 90}, {192, 100}});
		a->setControllerPoints(ControllerKind::PitchBend, 0, {{0, 8192}, {192, 10000}});

		auto* b = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(Bar / 2)));
		QVERIFY(b != nullptr);
		b->changeLength(TimePos(Bar));
		b->setAutoResize(false);
		b->setStartTimeOffset(TimePos(-24));
		b->setControllerPoints(ControllerKind::ControlChange, 74, {{24, 40}, {72, 120}, {216, 80}});
		b->setControllerPoints(ControllerKind::ChannelPressure, 0, {{0, 10}, {192, 70}});
		b->setControllerPoints(ControllerKind::ProgramChange, 0, {{0, 5}, {48, 11}, {192, 13}});

		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* aView = viewOf(a);
		MidiClipView* bView = viewOf(b);
		QVERIFY(aView != nullptr && bView != nullptr);

		MidiClipView::mergeClips(QVector<ClipView*>{aView, bView});
		QCoreApplication::processEvents();

		QCOMPARE(m_track->numOfClips(), 1);
		const auto* merged = static_cast<const MidiClip*>(m_track->getClip(0));
		const ControllerLane* cc = merged->controllers().lane(ControllerKind::ControlChange, 74);
		QVERIFY2(cc != nullptr, "the merge dropped the controller lane");
		QCOMPARE(cc->points.size(), std::size_t{5});
		QCOMPARE(cc->points[0].tick, tick_t{0});
		QCOMPARE(cc->points[0].value, 20);
		QCOMPARE(cc->points[1].tick, tick_t{96});
		QCOMPARE(cc->points[1].value, 40); // later clip wins the overlapping tick
		QCOMPARE(cc->points[2].tick, tick_t{144});
		QCOMPARE(cc->points[2].value, 120);
		QCOMPARE(cc->points[3].tick, tick_t{192});
		QCOMPARE(cc->points[3].value, 100);
		QCOMPARE(cc->points[4].tick, tick_t{288});
		QCOMPARE(cc->points[4].value, 80);

		const ControllerLane* pitch = merged->controllers().lane(ControllerKind::PitchBend, 0);
		QVERIFY(pitch != nullptr);
		QCOMPARE(pitch->points.front().tick, tick_t{0});
		QCOMPARE(pitch->points.front().value, 8192);
		QCOMPARE(pitch->points.back().tick, tick_t{192});
		QCOMPARE(pitch->points.back().value, 10000);
		const ControllerLane* pressure = merged->controllers().lane(ControllerKind::ChannelPressure, 0);
		QVERIFY(pressure != nullptr);
		QCOMPARE(pressure->points.front().tick, tick_t{96});
		const ControllerLane* program = merged->controllers().lane(ControllerKind::ProgramChange, 0);
		QVERIFY(program != nullptr);
		QCOMPARE(program->points.front().tick, tick_t{96});
		QCOMPARE(program->points.front().value, 5);
		QCOMPARE(program->points[1].tick, tick_t{120});
		QCOMPARE(program->points[1].value, 11);
	}

	//! A beat clip with the most steps a project may load, and a cursor far across it: the step menu's
	//! (x - border) * steps overflows int at this scale. The menu must still name the step under the
	//! cursor and edit that step's note.
	void aStepMenuFarAcrossAHugeBeatClipEditsTheStepUnderTheCursor()
	{
		constexpr int steps = 5000000;
		constexpr int cursorX = 1000;
		// Sixteen bars wide, so the view is wide enough for the cursor to be over it.
		auto* clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->changeLength(TimePos(16 * Bar));
		clip->setAutoResize(false);

		QDomDocument doc;
		QDomElement element = doc.createElement("midiclip");
		element.setAttribute("steps", steps);
		element.setAttribute("autoresize", 0);  // keep the sixteen bars the clip was made with
		clip->loadSettings(element);
		QDomElement saved = doc.createElement("midiclip");
		clip->saveSettings(doc, saved);
		QCOMPARE(saved.attribute("steps").toInt(), steps);

		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(clip);
		QVERIFY2(view != nullptr, "the song editor built no view for the beat clip");
		const int width = view->width();
		QVERIFY2(width > cursorX, qPrintable(QStringLiteral("the view is %1 px wide, too narrow for the cursor").arg(width)));

		// The step under the cursor, in 64-bit arithmetic: the view's width less its border, over the steps.
		const int expected = static_cast<int>(static_cast<qint64>(cursorX - ClipView::BORDER_WIDTH) * steps
			/ (width - 2 * ClipView::BORDER_WIDTH));
		QVERIFY(clip->addStepNote(expected) != nullptr);
		QCoreApplication::processEvents();
		QCOMPARE(view->width(), width);

		// The step menu reads the cursor position, not the event's, so the cursor is put over the step too.
		const QPoint at(cursorX, 5);
		QCursor::setPos(view->mapToGlobal(at));
		QCoreApplication::processEvents();
		QCOMPARE(view->mapFromGlobal(QCursor::pos()), at);
		QVERIFY(triggerSubmenuItemOfContextMenu(view, at, QStringLiteral("Step %1 Chance").arg(expected + 1),
			QStringLiteral("50 %")));
		QCoreApplication::processEvents();

		const Note* note = clip->noteAtStep(expected);
		QVERIFY(note != nullptr);
		QCOMPARE(note->probability(), 0.5f);
	}

private:
	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
	Track* m_track = nullptr;
};

QTEST_MAIN(MidiClipStepMenuTest)
#include "MidiClipStepMenuTest.moc"
