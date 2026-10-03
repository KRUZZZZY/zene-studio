/*
 * HelpAccessTest.cpp - the manual and command reference, hover help, guided tours, appearance, the library search view
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
/*! 0.5.0 L14 / L11: the command reference is the registry and filters as asked (WF-136); a control
 *  that runs a command shows that command's description as its help (WF-137); the tours are found
 *  and one steps through a window (WF-140); app.help and app.tour open the help centre and a tour
 *  over the REAL main window (GuiApplication: app.tour has no window to show a tour in without
 *  it); window.appearance stores the scale where main() reads it and one undo puts it back
 *  (WF-131); window.pin keeps a side panel open across views (MX-007); the library search view
 *  runs a query and lists its columns (WF-003 / WF-011). */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

#include "Appearance.h"
#include "BrowserSearchView.h"
#include "ContextHelp.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "HelpCenter.h"
#include "MainWindow.h"
#include "Shell.h"

using namespace lmms;
using namespace lmms::gui;

class HelpAccessTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		qputenv("ZENE_UI_SCALE_FILE", m_dir.filePath(QStringLiteral("scale")).toLocal8Bit());
		guitest::startGui(m_dir);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theReferenceIsTheRegistry()
	{
		HelpCenter* help = HelpCenter::showHelp(QString(), true);
		QVERIFY(help != nullptr);
		QVERIFY(help->referenceText().contains(QStringLiteral("app.help")));
		help->filterReference(QStringLiteral("collection_save"));
		QVERIFY(help->referenceText().contains(QStringLiteral("browser.collection_save")));
		QVERIFY(!help->referenceText().contains(QStringLiteral("transport.play")));
		help->close();
	}

	void aControlShowsItsCommandsHelp()
	{
		QToolButton button;
		button.setProperty("controlCommand", QStringLiteral("transport.record_options"));
		QVERIFY(ContextHelp::helpFor(&button, false).startsWith(QStringLiteral("How a recording pass starts")));
		QVERIFY(ContextHelp::helpFor(&button, true).contains(QStringLiteral("transport.record_options")));
		QToolButton plain;
		QVERIFY(ContextHelp::helpFor(&plain, false).isEmpty());
	}

	void aTourStepsThroughTheWindow()
	{
		const QJsonArray tours = GuidedTour::tours();
		QVERIFY2(tours.size() >= 3, "the shipped tours (data/tours) were not found");
		QWidget window;
		window.resize(800, 600);
		auto* target = new QWidget(&window);
		target->setObjectName(QStringLiteral("shellPanel_browser"));
		target->setGeometry(10, 10, 100, 100);
		window.show();
		QString error;
		GuidedTour* tour = GuidedTour::start(QStringLiteral("first-beat"), &window, &error);
		QVERIFY2(tour != nullptr, qPrintable(error));
		QVERIFY(tour->stepCount() >= 3);
		tour->showStep(1);
		QCOMPARE(tour->step(), 1);
		QVERIFY(GuidedTour::start(QStringLiteral("no-such-tour"), &window, &error) == nullptr);
	}

	void theHelpAndATourOpenFromTheirCommands()
	{
		MainWindow* shellWindow = getGUI()->mainWindow();
		shellWindow->show();  // a tour step toggles its panel when the target is not on screen
		ControlRegistry* registry = ControlRegistry::instance();
		const bool headless = registry->isHeadless();
		registry->setHeadless(false);  // the offscreen platform is a display (both declare requires: display)
		const ControlResult help = registry->invoke(QStringLiteral("app.help"), {{QStringLiteral("reference"), true}});
		const ControlResult tour = registry->invoke(QStringLiteral("app.tour"), {{QStringLiteral("name"), QStringLiteral("first-beat")}});
		const ControlResult missing = registry->invoke(QStringLiteral("app.tour"), {{QStringLiteral("name"), QStringLiteral("no-such-tour")}});
		registry->setHeadless(headless);
		QVERIFY2(help.ok, qPrintable(help.errorMessage));
		QVERIFY(help.result.value(QStringLiteral("open")).toBool());
		HelpCenter* centre = shellWindow->findChild<HelpCenter*>();
		QVERIFY(centre != nullptr && centre->isVisible());
		centre->close();
		QVERIFY2(tour.ok, qPrintable(tour.errorMessage));
		QCOMPARE(tour.result.value(QStringLiteral("name")).toString(), QStringLiteral("first-beat"));
		auto* shown = shellWindow->findChild<GuidedTour*>(QStringLiteral("guidedTour"));
		QVERIFY(shown != nullptr && shown->isVisible());
		QCOMPARE(tour.result.value(QStringLiteral("steps")).toInt(), shown->stepCount());
		QCOMPARE(shown->step(), 0);
		delete shown;
		QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);
	}

	void appearanceIsStoredWhereMainReadsIt()
	{
		const ControlResult set = ControlRegistry::instance()->invoke(QStringLiteral("window.appearance"),
			{{QStringLiteral("scale"), 1.5}, {QStringLiteral("palette"), QStringLiteral("high_contrast")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(appearance::storedScale(), 1.5);
		QCOMPARE(appearance::options().palette, QStringLiteral("high_contrast"));
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(appearance::options().palette, QStringLiteral("dark"));
		QCOMPARE(appearance::storedScale(), 1.0);
	}

	void aPinnedPanelStaysOpenInEveryView()
	{
		Shell* shell = getGUI()->mainWindow()->shell();
		shell->setView(ShellView::Arrange);
		QVERIFY(!shell->model().isVisible(QStringLiteral("mixer")));  // Arrange shows the note editor below
		const ControlResult pinned = ControlRegistry::instance()->invoke(QStringLiteral("window.pin"), {{QStringLiteral("panel"), QStringLiteral("mixer")}});
		QVERIFY2(pinned.ok, qPrintable(pinned.errorMessage));
		QCOMPARE(pinned.result.value(QStringLiteral("pinned")).toBool(), true);
		QVERIFY(shell->model().isPinned(QStringLiteral("mixer")));
		QVERIFY(shell->model().isVisible(QStringLiteral("mixer")));
		shell->setView(ShellView::Edit);  // the Edit view opens its device chain below - the pin wins
		QVERIFY(shell->model().isVisible(QStringLiteral("mixer")));
		const ControlResult unpinned = ControlRegistry::instance()->invoke(QStringLiteral("window.pin"),
			{{QStringLiteral("panel"), QStringLiteral("mixer")}, {QStringLiteral("pinned"), false}});
		QVERIFY2(unpinned.ok, qPrintable(unpinned.errorMessage));
		QVERIFY(!shell->model().isPinned(QStringLiteral("mixer")));
		QCOMPARE(ControlRegistry::instance()->invoke(QStringLiteral("window.pin"), {{QStringLiteral("panel"), QStringLiteral("arranger")}}).errorKind,
			ControlErrorKind::InvalidArgs);  // a view's main panel cannot be pinned
		shell->setView(ShellView::Arrange);
	}

	void theSearchViewListsColumns()
	{
		BrowserSearchView view(nullptr);
		QCOMPARE(view.results()->columnCount(), 5);
		QVERIFY(view.results()->isSortingEnabled());
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(HelpAccessTest)
#include "HelpAccessTest.moc"
