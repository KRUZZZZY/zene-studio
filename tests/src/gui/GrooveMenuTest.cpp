/*
 * GrooveMenuTest.cpp - the piano roll's Groove menu extracts, applies and quantises
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! Over a real clip of swung notes: Extract groove... puts a named groove in the pool
 *  (groove.list lists it), the rebuilt menu offers it under Apply groove and applying it to a
 *  straight clip at 100 % moves notes, and Quantize with strength... at 100 % puts a late note back
 *  on the grid - each through its groove.* command. A command that REFUSES (an empty clip carries
 *  no note a groove can be measured from) says so: the refusal is shown to the person who asked
 *  for it, not swallowed. The test runs the REAL application, because the refusal is shown the
 *  way the interface shows it - as a floating message over the main window. */

#include <QtTest>

#include <algorithm>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QMenuBar>
#include <QScreen>
#include <QMenuBar>
#include <QScreen>
#include <QMenuBar>
#include <QScreen>
#include <QScreen>
#include <QScreen>
#include <QMenuBar>
#include <QRect>
#include <QMenuBar>
#include <QMenuBar>
#include <QMenuBar>
#include <QTemporaryDir>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GrooveMenu.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MenuBarExtras.h"
#include "MidiClip.h"
#include "Note.h"
#include "ShellSelection.h"
#include "Song.h"
#include "TextFloat.h"
#include "WorkspaceMenus.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* named(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
		if (action->menu() != nullptr)
		{
			if (QAction* inner = named(action->menu(), text)) { return inner; }
		}
	}
	return nullptr;
}

MidiClip* clipWith(const std::vector<int>& positions)
{
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
	for (int position : positions) { clip->addNote(Note(TimePos(6), TimePos(position), 60, 100), false); }
	return clip;
}

std::vector<int> positionsOf(const MidiClip* clip)
{
	std::vector<int> out;
	for (const Note* note : clip->notes()) { out.push_back(note->pos().getTicks()); }
	return out;
}

/*! What the floating messages over the main window say - one label's text per line.
 *  They are how the interface tells a person what a command answered, so they are
 *  what the test reads to know the refusal was not swallowed. */
QString messagesShown()
{
	QString text;
	for (TextFloat* message : getGUI()->mainWindow()->findChildren<TextFloat*>())
	{
		for (QLabel* label : message->findChildren<QLabel*>()) { text += label->text() + QLatin1Char('\n'); }
	}
	return text;
}

//! Where the window a floating message floats over sits (the message's own place is its frameGeometry()).
QRect globalRect(const QWidget* widget) { return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()); }

//! The first floating message over the main window (they are children of it).
TextFloat* firstMessage() { return getGUI()->mainWindow()->findChild<TextFloat*>(); }

//! The product's Window menu, installed the way the product installs it (WorkspaceMenus.cpp installWorkspaceMenus,
//! which the shell runs when the main window is first shown). The Channel Load entry's host is then the main window -
//! the same host the person's click resolves to - so triggering the entry is the person's path, not a direct call.
QMenu* windowMenu(MainWindow* window)
{
	installWorkspaceMenus(window->menuBar());
	for (QAction* top : window->menuBar()->actions())
	{
		if (top->menu() != nullptr && top->menu()->objectName() == QStringLiteral("workspaceMenu")) { return top->menu(); }
	}
	return nullptr;
}

} // namespace

class GrooveMenuTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_home;

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		// The offscreen platform IS a display here (the platform plugin answers "offscreen",
		// which is a display a person cannot click on but a message can be shown over), so
		// the registry is told there is somebody to show a refusal to.
		ControlRegistry::instance()->setHeadless(false);
	}

	void cleanupTestCase()
	{
		setGroovePrompts({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void extractApplyAndQuantize()
	{
		// Swung sixteenths: every second one late by 4 ticks.
		MidiClip* swung = clipWith({0, 16, 24, 40, 48, 64, 72, 88});
		setGroovePrompts({[] { return std::optional<QString>(QStringLiteral("Swing")); },
			[](const QString&) { return std::optional<double>(100.0); }});
		const MidiClip* current = swung;
		QMenu* menu = makeGrooveMenu([&current] { return current; }, [] { return 12; }, nullptr);
		named(menu, QMenu::tr("Extract groove from this clip..."))->trigger();
		const QJsonArray pool = ControlRegistry::instance()->invoke(QStringLiteral("groove.list"), QJsonObject{})
			.result.value(QStringLiteral("templates")).toArray();
		QCOMPARE(pool.size(), 1);

		MidiClip* straight = clipWith({0, 12, 24, 36, 48, 60, 72, 84});
		current = straight;
		emit menu->aboutToShow();
		QAction* swing = named(menu, QStringLiteral("Swing"));
		QVERIFY2(swing != nullptr, "the pooled groove is not under Apply groove");
		const auto before = positionsOf(straight);
		swing->trigger();
		QVERIFY2(positionsOf(straight) != before, "applying the groove moved no note");

		MidiClip* late = clipWith({0, 15, 24});
		current = late;
		emit menu->aboutToShow();
		named(menu, QMenu::tr("Quantize with strength..."))->trigger();
		QCOMPARE(positionsOf(late), (std::vector<int>{0, 12, 24}));
		delete menu;
	}

	//! A notice floated over a top-level window (the main window a menu command passes as host) must land
	//! inside that window: beside it (width + 2) it is off the right edge, where nobody sees it.
	void aNoticeOverTheMainWindowLandsInsideIt()
	{
		QWidget* window = getGUI()->mainWindow();
		const QRect hostRect(window->mapToGlobal(QPoint(0, 0)), window->size());
		TextFloat* notice = TextFloat::displayMessage(QStringLiteral("Next Clip"),
			QStringLiteral("no clip is selected"), QPixmap(), 60000, window);
		const QRect noticeRect(notice->pos(), notice->size());
		notice->close();
		QVERIFY2(hostRect.contains(noticeRect),
			qPrintable(QStringLiteral("notice %1,%2 %3x%4 is outside the window %5,%6 %7x%8")
				.arg(noticeRect.x()).arg(noticeRect.y()).arg(noticeRect.width()).arg(noticeRect.height())
				.arg(hostRect.x()).arg(hostRect.y()).arg(hostRect.width()).arg(hostRect.height())));
	}

	//! A plain child widget keeps the old placement: just right of it, at its top.
	void aNoticeBesideAWidgetStaysBesideIt()
	{
		QWidget child(getGUI()->mainWindow());
		child.setGeometry(10, 10, 40, 20);
		TextFloat* notice = TextFloat::displayMessage(QStringLiteral("t"), QStringLiteral("m"), QPixmap(), 60000, &child);
		const QPoint expected = child.mapToGlobal(QPoint(0, 0)) + QPoint(child.width() + 2, 0);
		const QPoint got = notice->pos();
		notice->close();
		QCOMPARE(got, expected);
	}

	void aRefusedCommandIsShownToTheUser()
	{
		// An empty clip carries no note a groove can be measured from, so groove.extract
		// refuses. The refusal must be shown: the menu closing on a silence is the bug.
		MidiClip* empty = clipWith({});
		setGroovePrompts({[] { return std::optional<QString>(QStringLiteral("Empty")); },
			[](const QString&) { return std::optional<double>(100.0); }});
		const MidiClip* current = empty;
		QMenu* menu = makeGrooveMenu([&current] { return current; }, [] { return 12; }, nullptr);
		named(menu, QMenu::tr("Extract groove from this clip..."))->trigger();
		const QString shown = messagesShown();
		QVERIFY2(!shown.isEmpty(), "the refused groove.extract was shown to nobody");
		QVERIFY2(shown.contains(QStringLiteral("groove")), qPrintable(shown));
		delete menu;
	}

	void aMessageOverAMaximizedWindowIsOnScreen()
	{
		// gui-1030: Help > Accessibility Check floats its report beside its host - the main
		// window, whose right edge is the screen's own when it fills the screen. The report
		// must still land where it can be read, over the window, not past the screen's edge.
		MainWindow* window = getGUI()->mainWindow();
		QScreen* screen = window->screen();
		QVERIFY(screen != nullptr);
		const QRect area = screen->availableGeometry();
		window->setGeometry(area);
		window->show();
		// The shell adds its menu entries (Help > Accessibility Check among them) once the
		// window's first queued events run; a test that started the GUI runs them here.
		QCoreApplication::processEvents();
		QAction* check = named(window->menuBar()->actions().last()->menu(), QObject::tr("Accessibility Check"));
		QVERIFY2(check != nullptr, "Help has no Accessibility Check entry");
		const QList<TextFloat*> before = window->findChildren<TextFloat*>();
		check->trigger();
		TextFloat* report = nullptr;
		for (TextFloat* shown : window->findChildren<TextFloat*>())
		{
			if (!before.contains(shown)) { report = shown; }
		}
		QVERIFY2(report != nullptr, "Accessibility Check showed no report");
		QVERIFY(report->isVisible());
		QVERIFY2(area.contains(report->frameGeometry()), qPrintable(QStringLiteral("report at %1,%2 %3x%4 is not inside "
			"the screen %5,%6 %7x%8").arg(report->x()).arg(report->y()).arg(report->width()).arg(report->height())
			.arg(area.x()).arg(area.y()).arg(area.width()).arg(area.height())));
		report->close();
	}

	void aMessageOverTheMainWindowLandsInsideIt()
	{
		// gui-1105: a Navigate menu command is announced with the main window as the anchor, and
		// TextFloat put an anchored message at the anchor's right edge plus two pixels - outside the
		// window, so Fit Selection's "nothing selected" refusal was never seen.
		MainWindow* window = getGUI()->mainWindow();
		QMenu* navigate = installNavigateMenu(window->menuBar());  // what Shell.cpp does at start-up
		for (TextFloat* old : window->findChildren<TextFloat*>()) { delete old; }
		QAction* fit = named(navigate, QMenu::tr("Fit Selection"));
		QVERIFY(fit != nullptr);
		fit->trigger();
		const QList<TextFloat*> shown = window->findChildren<TextFloat*>();
		QVERIFY2(shown.size() == 1, "Fit Selection with nothing selected showed no refusal");
		const QRect area(window->mapToGlobal(QPoint(0, 0)), window->size());
		const QRect message = shown.first()->geometry();
		QVERIFY2(area.contains(message.topLeft()), qPrintable(QStringLiteral("message at (%1,%2) is outside the window %3x%4+%5+%6")
			.arg(message.x()).arg(message.y()).arg(area.width()).arg(area.height()).arg(area.x()).arg(area.y())));

		// A small anchor (a knob, a clip) keeps the message beside it, as before.
		QWidget child(window);
		child.setGeometry(10, 10, 40, 20);
		TextFloat* beside = TextFloat::displayMessage(QStringLiteral("t"), QStringLiteral("m"), QPixmap(), 0, &child);
		QCOMPARE(beside->pos(), child.mapToGlobal(QPoint(child.width() + 2, 0)));
		delete beside;
	}

	void aMessageOverTheMainWindowIsOnScreen()
	{
		// A message anchored to a window that fills its screen (the main window, maximised - what every
		// menu refusal is anchored to) used to be placed at the window's right edge + 2, i.e. off the screen.
		MainWindow* window = getGUI()->mainWindow();
		window->setGeometry(window->screen()->availableGeometry());
		const TextFloat* message = TextFloat::displayMessage(QStringLiteral("Title"), QStringLiteral("No clip is selected"),
			QPixmap(), 0, window);
		const QRect screen = window->screen()->availableGeometry();
		const QRect shown(message->mapToGlobal(QPoint(0, 0)), message->size());
		QVERIFY2(screen.contains(shown), qPrintable(QStringLiteral("message %1,%2 %3x%4 is outside the screen %5,%6 %7x%8")
			.arg(shown.x()).arg(shown.y()).arg(shown.width()).arg(shown.height())
			.arg(screen.x()).arg(screen.y()).arg(screen.width()).arg(screen.height())));
	}

	void aMenuRefusalOverAFullScreenWindowStaysOnScreen()
	{
		// gui-1105: Navigate > Nudge Clip Right with no clip selected is refused, and the menu
		// reports it over the main window (CommandRun's parent). With that window covering the
		// screen, the message was placed past the window's right edge - off the screen.
		MainWindow* window = getGUI()->mainWindow();
		const QRect screen = window->screen()->availableGeometry();
		window->setGeometry(screen);
		window->show();
		// The window reaches the screen's right edge, as a maximized one does (its minimum width may
		// even exceed a small screen).
		QVERIFY(window->geometry().right() >= screen.right());
		for (TextFloat* old : window->findChildren<TextFloat*>()) { delete old; }
		QMenu* navigate = installNavigateMenu(window->menuBar()); // the shell installs it in the app
		QVERIFY2(navigate != nullptr, "the main window has no Navigate menu");
		QAction* nudge = named(navigate, QObject::tr("Nudge Clip Right"));
		QVERIFY2(nudge != nullptr, "Navigate has no Nudge Clip Right");
		nudge->trigger();
		const QList<TextFloat*> shown = window->findChildren<TextFloat*>();
		QCOMPARE(shown.size(), 1);
		const QRect placed = shown.first()->frameGeometry();
		QVERIFY2(screen.contains(placed), qPrintable(QStringLiteral("refusal placed at %1,%2 %3x%4, screen %5,%6 %7x%8")
			.arg(placed.x()).arg(placed.y()).arg(placed.width()).arg(placed.height())
			.arg(screen.x()).arg(screen.y()).arg(screen.width()).arg(screen.height())));
	}

	void aMessageOverAWindowFillingTheScreenStaysOnScreen()
	{
		// gui-1105: a menu command's refusal floats "just right of" its parent, and the parent is often
		// the main window; maximised, that is beyond the screen's right edge and nobody sees it.
		MainWindow* window = getGUI()->mainWindow();
		const QRect area = window->screen()->availableVirtualGeometry();
		window->setGeometry(area);
		window->show();
		TextFloat* message = TextFloat::displayMessage(QStringLiteral("Next Clip"), QStringLiteral("No clip is selected."),
			QPixmap(), 0, window);
		const QRect placed = message->frameGeometry();
		const auto screens = window->screen()->virtualSiblings();
		const bool fitsOnAScreen = std::any_of(screens.cbegin(), screens.cend(), [&placed](const QScreen* screen) {
			return screen->availableGeometry().contains(placed);
		});
		delete message;
		QVERIFY2(fitsOnAScreen, "message does not fit within any screen's available geometry");
	}

	void aMessageOverAMaximisedWindowStaysOnScreen()
	{
		// CommandRun floats a refusal beside its parent window (just past its right edge). A window
		// that fills the screen has nothing past that edge, so the message was placed off-screen and
		// the menu closing on a silence was the report (gui-1105: Navigate > Previous Clip).
		QWidget* window = getGUI()->mainWindow();
		const QRect screen = window->screen()->availableGeometry();
		window->setGeometry(screen);
		TextFloat* message = TextFloat::displayMessage(QStringLiteral("Previous Clip"),
			QStringLiteral("No clip is selected."), QPixmap(), 0, window);
		const QRect shown = message->frameGeometry();
		QVERIFY2(screen.contains(shown),
			qPrintable(QStringLiteral("message %1,%2 %3x%4 is outside the screen %5,%6 %7x%8")
				.arg(shown.x()).arg(shown.y()).arg(shown.width()).arg(shown.height())
				.arg(screen.x()).arg(screen.y()).arg(screen.width()).arg(screen.height())));
		delete message;
	}
	void aRefusalFromTheMainWindowIsShownInsideIt()
	{
		// gui-1105: Navigate > Next Marker with no marker refuses through CommandRun, which hands the
		// main window to TextFloat::displayMessage as the float's parent. Placed just past that window's
		// right edge, the message lands off-screen once the window is maximized - so it must sit inside.
		MainWindow* main = getGUI()->mainWindow();
		main->setGeometry(0, 0, 800, 600);
		for (TextFloat* message : main->findChildren<TextFloat*>()) { delete message; }
		QMenu* navigate = installNavigateMenu(main->menuBar());
		QAction* next = named(navigate, QObject::tr("Next Marker"));
		QVERIFY(next != nullptr);
		QVERIFY2(next->isEnabled(), "marker.jump is not registered in this binary");
		next->trigger();
		const QList<TextFloat*> shown = main->findChildren<TextFloat*>();
		QVERIFY2(!shown.isEmpty(), "the refused marker.jump was shown to nobody");
		const QRect window(main->mapToGlobal(QPoint(0, 0)), main->size());
		for (TextFloat* message : shown)
		{
			QVERIFY2(window.contains(message->geometry()), qPrintable(QStringLiteral("float at %1,%2 %3x%4 is outside "
				"the main window %5x%6").arg(message->x()).arg(message->y()).arg(message->width())
				.arg(message->height()).arg(window.width()).arg(window.height())));
		}
	}

	void aMessageOverTheMainWindowIsShownInsideIt()
	{
		// Navigate > Previous Marker with no marker answers "no marker to jump to" through a float
		// parented on the main window. Placed "right of the parent" it sat 2 px outside the window,
		// where nobody looking at the project could see it.
		MainWindow* window = getGUI()->mainWindow();
		TextFloat* message = TextFloat::displayMessage(QStringLiteral("Previous Marker"),
			QStringLiteral("no marker to jump to"), QPixmap(), 0, window);
		const QRect frame = window->frameGeometry();
		QVERIFY2(frame.contains(message->geometry()),
			qPrintable(QStringLiteral("message %1,%2 %3x%4 is outside the window %5,%6 %7x%8")
				.arg(message->x()).arg(message->y()).arg(message->width()).arg(message->height())
				.arg(frame.x()).arg(frame.y()).arg(frame.width()).arg(frame.height())));
		delete message;
	}

	void aMessageOverAChildWidgetStaysBesideIt()
	{
		// Knobs, faders and clips float their value to the right of themselves: unchanged.
		QWidget child(getGUI()->mainWindow());
		child.resize(40, 20);
		TextFloat* message = TextFloat::displayMessage(QStringLiteral("Value"), QStringLiteral("1"), QPixmap(), 0, &child);
		QCOMPARE(message->pos(), child.mapToGlobal(QPoint(child.width() + 2, 0)));
		delete message;
	}

	//! gui-1105: a command's result floated over the main window (Window > Channel Load) was put at the window's
	//! right edge + 2 px - outside the app, so nobody saw it. It must lie over the window it floats over.
	void aResultFloatedOverTheMainWindowLiesOverIt()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->resize(900, 600);
		const QRect app = globalRect(window);
		const TextFloat* message = TextFloat::displayMessage(QStringLiteral("Channel Load"), QStringLiteral("CPU 3 %"),
			QPixmap(), 8000, window);
		QVERIFY(message != nullptr);
		QVERIFY2(app.contains(message->frameGeometry()), qPrintable(QStringLiteral("float %1,%2 %3x%4 is not inside the window %5,%6 %7x%8")
			.arg(message->frameGeometry().x()).arg(message->frameGeometry().y()).arg(message->frameGeometry().width()).arg(message->frameGeometry().height())
			.arg(app.x()).arg(app.y()).arg(app.width()).arg(app.height())));
		delete message;
	}

	//! The same for a message beside a widget that is flush with the window's right edge, and the beside-a-widget
	//! placement is kept where it already lies inside the window.
	void aMessageBesideAWidgetStaysOverTheWindow()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->resize(900, 600);
		const QRect app = globalRect(window);
		QWidget flush(window);
		flush.setGeometry(window->width() - 100, 40, 100, 50);
		const TextFloat* edge = TextFloat::displayMessage(QStringLiteral("t"), QStringLiteral("m"), QPixmap(), 8000, &flush);
		QVERIFY2(app.contains(edge->frameGeometry()), "a message beside a widget at the right edge is outside the window");
		delete edge;

		QWidget inner(window);
		inner.setGeometry(50, 40, 100, 50);
		const TextFloat* beside = TextFloat::displayMessage(QStringLiteral("t"), QStringLiteral("m"), QPixmap(), 8000, &inner);
		QCOMPARE(beside->pos(), inner.mapToGlobal(QPoint(inner.width() + 2, 0)));
		delete beside;
	}

	//! The user's path: Main menu > Window > Channel Load. The entry the product builds (WorkspaceMenus.cpp
	//! fillWindowMenu) runs mixer.cpu_load through the menu's host (the main window) with its result to be shown;
	//! triggering that entry - not calling runCommand directly - is what the person does, and the readout it
	//! floats must lie inside the app.
	void channelLoadFromTheWindowMenuShowsInsideTheApp()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->resize(900, 600);
		qDeleteAll(window->findChildren<TextFloat*>());
		QMenu* menu = windowMenu(window);
		QVERIFY2(menu != nullptr, "the Window menu is not installed");
		QAction* action = named(menu, QObject::tr("Channel Load"));
		QVERIFY2(action != nullptr, "the Window menu has no Channel Load entry");
		QVERIFY2(action->isEnabled(), "the Channel Load entry is disabled");
		QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("mixer.cpu_load"));
		action->trigger();
		const TextFloat* message = firstMessage();
		QVERIFY2(message != nullptr, "Channel Load showed no readout");
		QVERIFY2(globalRect(window).contains(message->frameGeometry()), "the Channel Load readout lies outside the app");
	}

	//! gui-1105: Help > Accessibility Check floats its report. The Help menu belongs to the menu bar, so the
	//! message was placed beside the bar's right edge - past the main window, often off-screen. It must sit
	//! over the main window, where the person who asked can read it.
	void aMenuBarCommandsAnswerIsShownOverTheMainWindow()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->resize(1000, 700);
		window->show();
		QVERIFY(QTest::qWaitForWindowExposed(window));
		// The entry the Help menu has (WorkspaceMenus fillHelpMenu): a show-result run of the command, on a menu
		// of the main window's menu bar.
		const QString id = QStringLiteral("window.accessibility_report");
		QMenu* help = window->menuBar()->addMenu(QStringLiteral("Help (gui-1105)"));
		QAction* check = addCommandAction(help, QStringLiteral("Accessibility Check"), id,
			[id] { return CommandCall{id, {}, {}, QString(), false, true}; });
		QVERIFY2(check->isEnabled(), "window.accessibility_report is not registered");
		const QList<TextFloat*> before = window->findChildren<TextFloat*>();
		check->trigger();
		TextFloat* shown = nullptr;
		for (TextFloat* message : window->findChildren<TextFloat*>())
		{
			if (!before.contains(message)) { shown = message; }
		}
		QVERIFY2(shown != nullptr, "the Accessibility Check answer was shown to nobody");
		const QRect over = window->geometry();
		QVERIFY2(over.contains(shown->geometry()), qPrintable(QStringLiteral("the report at (%1,%2 %3x%4) is not over "
			"the main window (%5,%6 %7x%8)").arg(shown->x()).arg(shown->y()).arg(shown->width()).arg(shown->height())
			.arg(over.x()).arg(over.y()).arg(over.width()).arg(over.height())));
		shown->close();
		delete help;
	}

	/*! The nudge refusal ("no clip is selected") floats over the main window it was run from, so the
	 *  float must be INSIDE that window: placed at the window's width + 2 it lands beyond its right
	 *  edge and no screenshot or person looking at the app ever sees it. */
	void aFloatOverTheMainWindowStaysInsideIt()
	{
		QWidget* window = getGUI()->mainWindow();
		const QRect bounds(window->mapToGlobal(QPoint(0, 0)), window->size());
		QVERIFY2(bounds.width() > 400 && bounds.height() > 200, qPrintable(QString::number(bounds.width())));
		TextFloat* message = TextFloat::displayMessage(QStringLiteral("Nudge"), QStringLiteral("no clip is selected"),
			QPixmap(), 0, window);
		const QRect placed(message->pos(), message->size());
		QVERIFY2(bounds.contains(placed), qPrintable(QStringLiteral("float %1,%2 %3x%4 outside window %5,%6 %7x%8")
			.arg(placed.x()).arg(placed.y()).arg(placed.width()).arg(placed.height())
			.arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())));
		delete message;
	}

	//! A small widget well inside the window keeps the float beside it, as before.
	void aFloatBesideASmallWidgetStaysBesideIt()
	{
		QWidget* window = getGUI()->mainWindow();
		auto* anchor = new QWidget(window);
		anchor->setGeometry(20, 40, 50, 30);
		TextFloat* message = TextFloat::displayMessage(QStringLiteral("Hint"), QStringLiteral("beside"), QPixmap(), 0, anchor);
		QCOMPARE(message->pos(), anchor->mapToGlobal(QPoint(anchor->width() + 2, 0)));
		delete message;
		delete anchor;
	}

	void aRefusalFromAMainWindowMenuIsShownInsideTheWindow()
	{
		// gui-1106: Navigate > Nudge Clip Right with no clip selected refuses, and the menu
		// (parented to the main window) hands the main window to the floating message. The
		// float was placed at the window's width + 2, beyond its right edge, where nobody saw it.
		MainWindow* window = getGUI()->mainWindow();
		window->resize(1000, 700);
		ShellSelection::instance()->setClip(nullptr);
		QMenu* navigate = installNavigateMenu(window->menuBar());
		const QList<TextFloat*> before = window->findChildren<TextFloat*>();
		QAction* nudge = named(navigate, QObject::tr("Nudge Clip Right"));
		QVERIFY2(nudge != nullptr, "Navigate has no Nudge Clip Right");
		nudge->trigger();
		TextFloat* shown = nullptr;
		for (TextFloat* message : window->findChildren<TextFloat*>())
		{
			if (!before.contains(message)) { shown = message; }
		}
		QVERIFY2(shown != nullptr, "the refused arrangement.nudge was shown to nobody");
		const QRect inside(window->mapToGlobal(QPoint(0, 0)), window->size());
		const QRect placed(shown->pos(), shown->size());
		QVERIFY2(inside.contains(placed), qPrintable(QStringLiteral("float at %1,%2 %3x%4 is outside the window %5,%6 %7x%8")
			.arg(placed.x()).arg(placed.y()).arg(placed.width()).arg(placed.height())
			.arg(inside.x()).arg(inside.y()).arg(inside.width()).arg(inside.height())));
	}

	void aRefusalFromAMainMenuIsShownInsideTheWindow()
	{
		// gui-1106: Main menu > Navigate > Previous Clip with no clip selected refuses. The menu passes
		// the main window as the float's parent, and a float placed beside a window (at its width + 2)
		// is outside it, where nobody sees it. A window parent must get its float inside the window.
		MainWindow* window = getGUI()->mainWindow();
		window->resize(900, 600);
		window->show();
		qDeleteAll(window->findChildren<TextFloat*>());
		QMenu* navigate = installNavigateMenu(window->menuBar());
		QAction* previous = named(navigate, QObject::tr("Previous Clip"));
		QVERIFY(previous != nullptr && previous->isEnabled());
		previous->trigger();
		const QList<TextFloat*> floats = window->findChildren<TextFloat*>();
		QCOMPARE(floats.size(), 1);
		const QRect inside(window->mapToGlobal(QPoint(0, 0)), window->size());
		const QRect shown = floats.front()->geometry();
		QVERIFY2(inside.contains(shown), qPrintable(QStringLiteral("float at %1,%2 %3x%4 is outside the window %5,%6 %7x%8")
			.arg(shown.x()).arg(shown.y()).arg(shown.width()).arg(shown.height())
			.arg(inside.x()).arg(inside.y()).arg(inside.width()).arg(inside.height())));
		qDeleteAll(floats);
	}

	void aFloatForAWidgetStillOpensBesideIt()
	{
		// The sibling path keeps its contract: a widget inside a window gets its float to its right.
		MainWindow* window = getGUI()->mainWindow();
		auto* anchor = new QWidget(window);
		anchor->setGeometry(20, 40, 100, 30);
		anchor->show();
		TextFloat* tf = TextFloat::displayMessage(QStringLiteral("Hint"), QStringLiteral("beside"), QPixmap(), 4000, anchor);
		QCOMPARE(tf->pos(), anchor->mapToGlobal(QPoint(0, 0)) + QPoint(anchor->width() + 2, 0));
		delete tf;
		delete anchor;
	}
};

QTEST_MAIN(GrooveMenuTest)
#include "GrooveMenuTest.moc"
