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
#include <QTemporaryDir>

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
#include "Song.h"
#include "TextFloat.h"

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
};

QTEST_MAIN(GrooveMenuTest)
#include "GrooveMenuTest.moc"
