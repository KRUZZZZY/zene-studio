/*
 * TouchLayoutTest.cpp - accessible mode is the touch-first layout, and a finger operates it
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

/*! 0.5.0 Q050-141, in the real application started in accessible mode:
 *  - the touch minimum is the documented 44 x 44 logical pixels with 8 between adjacent targets,
 *    and the rendered transport, arranger header and note-editor toolbar hold it;
 *  - a finger's touch event selects an arranger clip, selects a note, and plays and stops the
 *    transport. The events are delivered to the widget as a touchscreen's platform plugin delivers
 *    them to a widget that accepted touch; without the single-touch filter they are ignored (or the
 *    arranger's parent swallows them), so each case is a real product behaviour, not a mouse click.
 *
 *  Accessible mode is read once at startup, so this executable configures it before GuiApplication
 *  is made and every slot runs in it (the same shape as KnobAccessibleTargetSizingTest). */

#include <QtTest>

#include <algorithm>
#include <limits>
#include <memory>

#include <QAbstractButton>
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLayout>
#include <QMenu>
#include <QToolButton>

#include "Appearance.h"
#include "ArrangerAids.h"
#include "ClipView.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NotePlayHandle.h"
#include "ShellChrome.h"
#include "ShellSelection.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackOperationsWidget.h"
#include "TrackView.h"
#include "WorkspaceMenus.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

ClipView* viewOf(const Clip* clip)
{
	for (ClipView* view : getGUI()->songEditor()->m_editor->findChildren<ClipView*>())
	{
		if (view->getClip() == clip) { return view; }
	}
	return nullptr;
}

TrackView* trackViewOf(const Track* track)
{
	for (TrackView* view : getGUI()->songEditor()->m_editor->trackViews())
	{
		if (view->getTrack() == track) { return view; }
	}
	return nullptr;
}

//! Widgets a row lays out, left to right, in the order their geometry puts them.
QList<QWidget*> visibleRow(QWidget* parent)
{
	QList<QWidget*> row;
	for (QWidget* widget : parent->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
	{
		if (widget->isVisibleTo(parent)) { row << widget; }
	}
	std::sort(row.begin(), row.end(), [](QWidget* a, QWidget* b) { return a->x() < b->x(); });
	return row;
}

//! The least horizontal gap between neighbouring widgets of @a row, in @a ancestor's coordinates.
int leastGapWithin(QWidget* ancestor, const QList<QWidget*>& row)
{
	int least = std::numeric_limits<int>::max();
	for (int i = 1; i < row.size(); ++i)
	{
		const int left = row.at(i - 1)->mapTo(ancestor, QPoint(row.at(i - 1)->width(), 0)).x();
		const int right = row.at(i)->mapTo(ancestor, QPoint()).x();
		least = std::min(least, right - left);
	}
	return least;
}

int leastGap(const QList<QWidget*>& row)
{
	return row.isEmpty() ? std::numeric_limits<int>::max() : leastGapWithin(row.first()->parentWidget(), row);
}

} // namespace

class TouchLayoutTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		// Accessible mode is captured by the design tokens the first time they are read, which is
		// while GuiApplication is built: configure it first.
		ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("density"), QStringLiteral("accessible"));
		#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
		#endif
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
		NotePlayHandleManager::init();
		m_device = QTest::createTouchDevice();
		getGUI()->mainWindow()->resize(2000, 1000);
		getGUI()->mainWindow()->show();
		QVERIFY(QTest::qWaitForWindowExposed(getGUI()->mainWindow()));
		getGUI()->songEditor()->parentWidget()->show();
		m_editor = getGUI()->songEditor()->m_editor;
		installArrangerAids(m_editor);  // the product installs it with the shell's menus
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		if (Engine::getSong()->isPlaying()) { Engine::getSong()->stop(); }
		Engine::getSong()->clearProject();
	}

	//! The GUI entry the touch layout is switched on from - Project tools > Customise > Theme >
	//! Accessible Mode - runs the same window.appearance command the agent surface uses.
	void theAppearanceMenuRunsTheSameCommandAsAgents()
	{
		QMenu customise;
		populateCustomiseExtras(&customise);
		QMenu* theme = nullptr;
		for (QMenu* menu : customise.findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly))
		{
			if (menu->title() == QStringLiteral("Theme")) { theme = menu; }
		}
		QVERIFY2(theme != nullptr, "Customise > Theme is missing");
		QMenu* accessible = nullptr;
		for (QMenu* menu : theme->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly))
		{
			if (menu->title() == QStringLiteral("Accessible Mode")) { accessible = menu; }
		}
		QVERIFY2(accessible != nullptr, "Theme > Accessible Mode is missing");
		QAction* off = nullptr;
		QAction* on = nullptr;
		for (QAction* action : accessible->actions())
		{
			QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("window.appearance"));
			if (action->text() == QStringLiteral("Off")) { off = action; }
			if (action->text() == QStringLiteral("On")) { on = action; }
		}
		QVERIFY(off != nullptr && on != nullptr);
		QVERIFY(appearance::options().accessible);  // this executable started in accessible mode
		off->trigger();
		QVERIFY2(!appearance::options().accessible, "the menu did not write window.appearance's accessible field");
		on->trigger();
		QVERIFY2(appearance::options().accessible, "the menu did not turn accessible mode back on");
	}

	//! The documented minimum: 44 x 44 logical pixels with 8 between adjacent targets.
	void theTouchMinimumIsDocumentedAndHeld()
	{
		QVERIFY(tokens::accessibleMode());
		QCOMPARE(tokens::targetSize(), 44);
		QVERIFY(tokens::targetSize() >= 44);
	}

	//! The rendered transport and arranger header: every shell button is 44 x 44 or larger, and
	//! neighbouring buttons keep the gap.
	void theTransportAndArrangerHeaderHoldTheTouchMinimum()
	{
		auto* header = getGUI()->mainWindow()->findChild<ShellHeader*>();
		QVERIFY2(header != nullptr, "no shell header");
		QWidget* strip = header->strip();
		QVERIFY(strip != nullptr);
		auto* layout = qobject_cast<QHBoxLayout*>(strip->layout());
		QVERIFY(layout != nullptr);
		QVERIFY2(layout->spacing() >= 8,
			qPrintable(QStringLiteral("strip spacing is %1").arg(layout->spacing())));

		bool sawPlay = false;
		bool sawStop = false;
		QList<QWidget*> buttons;
		for (ShellButton* button : strip->findChildren<ShellButton*>(QString(), Qt::FindDirectChildrenOnly))
		{
			if (!button->isVisibleTo(strip)) { continue; }
			const QString command = button->property("controlCommand").toString();
			sawPlay = sawPlay || command == QLatin1String("transport.play");
			sawStop = sawStop || command == QLatin1String("transport.stop");
			QVERIFY2(button->width() >= 44,
				qPrintable(QStringLiteral("%1 is %2 px wide").arg(command).arg(button->width())));
			QVERIFY2(button->height() >= 44,
				qPrintable(QStringLiteral("%1 is %2 px high").arg(command).arg(button->height())));
			buttons << button;
		}
		QVERIFY2(sawPlay && sawStop, "the transport's Play and Stop are not in the header");
		QVERIFY2(leastGap(visibleRow(strip)) >= 8,
			qPrintable(QStringLiteral("the header's gaps are smaller than %1 px").arg(8)));
	}

	//! The rendered note-editor toolbar: its buttons and the grid target hold the minimum, and the
	//! toolbar keeps the targets apart (the repeated addSpacing after tool groups only widens gaps).
	void theNoteEditorToolbarHoldsTheTouchMinimum()
	{
		NoteEditor editor;
		editor.resize(1300, 700);
		editor.show();
		QVERIFY(QTest::qWaitForWindowExposed(&editor));
		auto* bar = editor.findChild<QWidget*>(QStringLiteral("noteEditorToolbar"));
		QVERIFY2(bar != nullptr, "no note editor toolbar");
		auto* layout = qobject_cast<QHBoxLayout*>(bar->layout());
		QVERIFY(layout != nullptr);
		QVERIFY2(layout->spacing() >= 8,
			qPrintable(QStringLiteral("toolbar spacing is %1").arg(layout->spacing())));

		int buttons = 0;
		for (QAbstractButton* button : bar->findChildren<QAbstractButton*>())
		{
			if (!button->isVisibleTo(bar)) { continue; }
			++buttons;
			QVERIFY2(button->width() >= 44,
				qPrintable(QStringLiteral("%1 is %2 px wide").arg(button->metaObject()->className()).arg(button->width())));
			QVERIFY2(button->height() >= 44,
				qPrintable(QStringLiteral("%1 is %2 px high").arg(button->metaObject()->className()).arg(button->height())));
		}
		QVERIFY2(buttons >= 10, "the note editor toolbar has no buttons to measure");
		QComboBox* grid = nullptr;
		for (QComboBox* box : bar->findChildren<QComboBox*>())
		{
			if (box->isVisibleTo(bar) && box->height() > 0) { grid = box; break; }
		}
		QVERIFY2(grid != nullptr, "no grid field");
		QVERIFY2(grid->width() >= 44 && grid->height() >= 44,
			qPrintable(QStringLiteral("the grid field is %1x%2").arg(grid->width()).arg(grid->height())));
		QVERIFY2(leastGap(visibleRow(bar)) >= 8,
			qPrintable(QStringLiteral("the toolbar's gaps are smaller than %1 px").arg(8)));
	}

	//! A finger on a clip selects it through the arranger (which accepts touch for pinch to zoom,
	//! so it must dispatch the tap to the clip under it).
	void aTouchTapSelectsAnArrangerClip()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(track != nullptr);
		Clip* clip = track->createClip(TimePos(0));
		QVERIFY(clip != nullptr);
		clip->changeLength(TimePos(4 * Bar));
		clip->setAutoResize(false);
		QTRY_VERIFY(viewOf(clip) != nullptr);
		ClipView* view = viewOf(clip);
		view->show();
		QTest::qWait(30);

		// The rendered arranger target: a clip is at least 44 x 44, the lane leaves the 8 px
		// touch gap to the next track's clip, and the track's operation buttons are 44 px
		// targets a touch gap apart.
		TrackView* trackView = view->getTrackView();
		QVERIFY(trackView != nullptr);
		QVERIFY2(trackView->getTrackContentWidget()->height() >= 44 + 8,
			qPrintable(QStringLiteral("the lane is %1 px high").arg(trackView->getTrackContentWidget()->height())));
		QVERIFY2(view->width() >= 44 && view->height() >= 44,
			qPrintable(QStringLiteral("the clip is %1x%2").arg(view->width()).arg(view->height())));
		QList<QWidget*> operations;
		QWidget* operationsParent = nullptr;
		for (QAbstractButton* button : trackView->getTrackOperationsWidget()->findChildren<QAbstractButton*>())
		{
			if (!button->isVisibleTo(trackView)) { continue; }
			QVERIFY2(button->width() >= 44 && button->height() >= 44,
				qPrintable(QStringLiteral("track operation %1 is %2x%3").arg(button->objectName()).arg(button->width()).arg(button->height())));
			operationsParent = button->parentWidget();
			operations << button;
		}
		QVERIFY2(operations.size() >= 3, "the track has no operation targets to measure");
		std::sort(operations.begin(), operations.end(), [operationsParent](QWidget* a, QWidget* b) {
			return a->mapTo(operationsParent, QPoint()).x() < b->mapTo(operationsParent, QPoint()).x();
		});
		QVERIFY2(leastGapWithin(operationsParent, operations) >= 8,
			qPrintable(QStringLiteral("track operation gaps are %1 px").arg(leastGapWithin(operationsParent, operations))));

		QVERIFY(ShellSelection::instance()->clip() != clip);
		// A point 22 px into the clip: its centre can be under a side panel on a wide touch header.
		QWidget* window = getGUI()->mainWindow();
		const QPoint at = view->mapTo(window, QPoint(22, view->height() / 2));
		tap(window, at);
		QVERIFY2(ShellSelection::instance()->clip() == clip, "the touch tap did not select the clip");
		QCOMPARE(ShellSelection::instance()->track(), clip->getTrack());

		// Two tracks: the clips keep the touch gap between their rows.
		auto* below = Track::create(Track::Type::Instrument, Engine::getSong());
		Clip* lower = below->createClip(TimePos(0));
		QVERIFY(lower != nullptr);
		lower->changeLength(TimePos(4 * Bar));
		QTRY_VERIFY(viewOf(lower) != nullptr);
		ClipView* lowerView = viewOf(lower);
		const int upperBottom = view->mapTo(m_editor, QPoint(0, view->height())).y();
		const int lowerTop = lowerView->mapTo(m_editor, QPoint()).y();
		QVERIFY2(lowerTop - upperBottom >= 8,
			qPrintable(QStringLiteral("the clips' rows are %1 px apart").arg(lowerTop - upperBottom)));
	}

	//! A finger on a note selects it.
	void aTouchTapSelectsANote()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		clip->addNote(Note(TimePos(48), TimePos(192), 64, 100), false);
		clip->rearrangeAllNotes();
		auto editor = std::make_unique<NoteEditor>();
		editor->resize(1200, 640);
		editor->show();
		QVERIFY(QTest::qWaitForWindowExposed(editor.get()));
		editor->setClip(clip);
		editor->setZoom(1.0);
		NoteGrid& grid = editor->state().grid;
		grid.setScrollTicks(0);
		grid.setScrollY(grid.rowOfKey(66) * grid.rowHeight());
		emit editor->stateChanged();
		editor->setTool(NoteTool::Select);

		Note* note = clip->notes().at(0);
		QVERIFY(!note->selected());
		const QRect noteTarget = editor->canvas()->touchRect(*note);
		QVERIFY2(noteTarget.width() >= 44 && noteTarget.height() >= 44,
			qPrintable(QStringLiteral("the note target is %1x%2").arg(noteTarget.width()).arg(noteTarget.height())));
		const QPoint at = noteTarget.center();
		tap(editor->canvas(), at);
		QVERIFY2(note->selected(), "the touch tap did not select the note");
		editor->setClip(nullptr);
		editor.reset();
	}

	//! Two adjacent 12-tick notes overlap as 44 px-wide targets; a tap on each selects the one tapped.
	void touchTapsOnAdjacentShortNotesSelectTheIntendedNote()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->addNote(Note(TimePos(12), TimePos(0), 60, 100), false);
		clip->addNote(Note(TimePos(12), TimePos(12), 60, 100), false);
		clip->rearrangeAllNotes();
		auto editor = std::make_unique<NoteEditor>();
		editor->resize(1200, 640);
		editor->show();
		QVERIFY(QTest::qWaitForWindowExposed(editor.get()));
		editor->setClip(clip);
		editor->setZoom(1.0);
		NoteGrid& grid = editor->state().grid;
		grid.setScrollTicks(0);
		grid.setScrollY(grid.rowOfKey(66) * grid.rowHeight());
		emit editor->stateChanged();
		editor->setTool(NoteTool::Select);

		NoteCanvas* canvas = editor->canvas();
		Note* first = clip->notes().at(0);
		Note* second = clip->notes().at(1);
		QVERIFY(first->pos().getTicks() < second->pos().getTicks());
		const QRect firstTarget = canvas->touchRect(*first);
		const QRect secondTarget = canvas->touchRect(*second);
		QVERIFY2(firstTarget.width() >= 44 && secondTarget.width() >= 44 && firstTarget.intersects(secondTarget),
			"the two short notes' touch targets should be 44 px wide and overlap");
		QVERIFY2(canvas->noteRect(*first).right() < canvas->noteRect(*second).left(), "the drawn notes should not overlap");

		tap(canvas, canvas->noteRect(*second).center());
		QVERIFY2(second->selected() && !first->selected(), "a tap on the second note did not select it alone");
		tap(canvas, canvas->noteRect(*first).center());
		QVERIFY2(first->selected() && !second->selected(), "a tap on the first note did not select it alone");
		// Beside the pair, inside the first note's wider target: the nearest note still takes it.
		const QPoint beside(canvas->noteRect(*first).left() - 8, canvas->noteRect(*first).center().y());
		QVERIFY(canvas->noteAt(beside) == first);
		editor->setClip(nullptr);
		editor.reset();
	}

	//! Adjacent pitch rows hold 44 px high note targets 8 px apart, and vertical zoom cannot shrink them.
	void noteRowsKeepTheTouchMinimumAndTheZoomFloor()
	{
		auto* track = Track::create(Track::Type::Instrument, Engine::getSong());
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		clip->addNote(Note(TimePos(48), TimePos(0), 61, 100), false);
		clip->rearrangeAllNotes();
		auto editor = std::make_unique<NoteEditor>();
		editor->resize(1200, 640);
		editor->show();
		QVERIFY(QTest::qWaitForWindowExposed(editor.get()));
		editor->setClip(clip);
		NoteGrid& grid = editor->state().grid;
		QCOMPARE(grid.minRowHeight(), 52);

		// Squeeze the rows as hard as the vertical zoom (Alt+wheel, the key-strip drag) can.
		for (int i = 0; i < 100; ++i) { grid.setRowHeight(grid.rowHeight() - 1); }
		grid.setRowHeight(1);
		QVERIFY2(grid.rowHeight() >= 52, qPrintable(QStringLiteral("rows shrank to %1 px").arg(grid.rowHeight())));

		grid.setScrollY(grid.rowOfKey(61) * grid.rowHeight());
		emit editor->stateChanged();
		QRect upper;  // key 61 sits above key 60
		QRect lower;
		for (const Note* note : clip->notes())
		{
			(note->key() == 61 ? upper : lower) = editor->canvas()->noteRect(*note);
		}
		QVERIFY2(upper.height() >= 44 && lower.height() >= 44,
			qPrintable(QStringLiteral("note heights are %1 and %2").arg(upper.height()).arg(lower.height())));
		QVERIFY2(lower.top() - upper.bottom() - 1 >= 8,
			qPrintable(QStringLiteral("adjacent pitch rows are %1 px apart").arg(lower.top() - upper.bottom() - 1)));
		editor->setClip(nullptr);
		editor.reset();
	}

	//! A finger on Play starts the transport; on Stop it stops.
	void aTouchTapPlaysAndStopsTheTransport()
	{
		auto* header = getGUI()->mainWindow()->findChild<ShellHeader*>();
		QVERIFY(header != nullptr);
		ShellButton* play = nullptr;
		ShellButton* stop = nullptr;
		for (ShellButton* button : header->findChildren<ShellButton*>())
		{
			const QString command = button->property("controlCommand").toString();
			if (command == QLatin1String("transport.play")) { play = button; }
			if (command == QLatin1String("transport.stop")) { stop = button; }
		}
		QVERIFY(play != nullptr && stop != nullptr);
		QVERIFY(!Engine::getSong()->isPlaying());
		tap(play, play->rect().center());
		QTRY_VERIFY_WITH_TIMEOUT(Engine::getSong()->isPlaying(), 3000);
		tap(stop, stop->rect().center());
		QTRY_VERIFY_WITH_TIMEOUT(!Engine::getSong()->isPlaying(), 3000);
	}

	//! An automation owner adds a fifth operation target; it must fit inside the touch-sized column.
	void anAutomationLaneTargetFitsInsideItsTrackOperationsColumn()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		track->loadInstrument(QStringLiteral("tripleoscillator"));
		QTRY_VERIFY(trackViewOf(track) != nullptr);

		const QString trackId = control::trackIdOf(track);
		const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("automation.get_state"),
			{{QStringLiteral("track"), trackId}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		const QJsonArray parameters = state.result.value(QStringLiteral("tracks")).toArray().first().toObject()
			.value(QStringLiteral("parameters")).toArray();
		QVERIFY(!parameters.isEmpty());
		const QString parameter = parameters.first().toObject().value(QStringLiteral("id")).toString();
		const ControlResult added = ControlRegistry::instance()->invoke(QStringLiteral("automation.lane_add"),
			{{QStringLiteral("track"), trackId}, {QStringLiteral("parameter"), parameter}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));

		TrackView* owner = trackViewOf(track);
		QVERIFY(owner != nullptr);
		TrackOperationsWidget* operations = owner->getTrackOperationsWidget();
		operations->repaint();
		auto* fold = operations->findChild<QToolButton*>(QStringLiteral("btn-automation-lanes"));
		QTRY_VERIFY(fold != nullptr && fold->isVisibleTo(operations));
		QVERIFY(fold->width() >= 44 && fold->height() >= 44);
		QVERIFY(leastGap(visibleRow(fold->parentWidget())) >= 8);
		const QRect target(fold->mapTo(operations, QPoint()), fold->size());
		QVERIFY2(operations->rect().contains(target),
			qPrintable(QStringLiteral("the %1x%2 automation target at %3..%4 is clipped by a %5 px column")
				.arg(target.width()).arg(target.height()).arg(target.left()).arg(target.right()).arg(operations->width())));
	}

private:
	//! One finger's tap, as the platform plugin delivers it: a touch begin and end at @a localPos,
	//! in the widget's own coordinates. The pair is split so each turn reaches the event loop, as a
	//! real finger's press and lift do.
	void tap(QWidget* target, const QPoint& localPos)
	{
		QTest::touchEvent(target, m_device).press(0, localPos, target);
		QTest::qWait(20);
		QTest::touchEvent(target, m_device).release(0, localPos, target);
		QTest::qWait(20);
	}

	QTemporaryDir m_home;
	decltype(QTest::createTouchDevice()) m_device = nullptr;
	SongEditor* m_editor = nullptr;
};

int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}
	// This executable proves the product's own touch handling, not Qt's fallback that turns an
	// unhandled touch into a mouse click for widgets that never accepted touch.
	QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTouchEvents, false);
	QApplication app(argc, argv);
	TouchLayoutTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "TouchLayoutTest.moc"
