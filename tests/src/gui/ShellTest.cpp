/*
 * ShellTest.cpp - the 0.5.0 shell renders its layout, moves editors without losing them, persists
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

#include <QAction>
#include <QApplication>
#include <QDomDocument>
#include <QPointer>
#include <QSplitter>
#include <QTemporaryDir>
#include <QtTest>

#include "ConfigManager.h"
#include "MainWindow.h"
#include "Shell.h"
#include "ShellChrome.h"
#include "ShellPanel.h"

using namespace lmms::gui;

namespace
{

//! The region host a frame sits in, by the host's object name ("shellCenter", "shellBottom"...).
QString hostOf(ShellPanel* frame)
{
	return frame->parentWidget() != nullptr ? frame->parentWidget()->objectName() : QString();
}

//! A shell with a plain widget mounted in every panel, shown at a desktop size.
struct World
{
	Shell shell;
	QHash<QString, QPointer<QWidget>> content;

	World()
	{
		for (const ShellPanelSpec& spec : ShellLayout::panels())
		{
			auto* w = new QWidget;
			w->setObjectName(QStringLiteral("content_") + spec.id);
			content.insert(spec.id, w);
			shell.mount(spec.id, w);
		}
		shell.resize(1600, 1000);
		shell.show();
	}
};

} // namespace

class ShellTest : public QObject
{
	Q_OBJECT

private slots:
	void init()
	{
		// Every test starts from the first-run layout: no persisted ui/shell.layout.
		lmms::ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("shell.layout"), QString());
	}

	void eachViewPutsItsMainPanelInTheCentre()
	{
		World world;
		QVERIFY(QTest::qWaitForWindowExposed(&world.shell));
		for (ShellView v : {ShellView::Arrange, ShellView::Mix, ShellView::Edit})
		{
			world.shell.setView(v);
			ShellPanel* centre = world.shell.panel(ShellLayout::centerOf(v));
			QCOMPARE(hostOf(centre), QStringLiteral("shellCenter"));
			QVERIFY(centre->isVisible());
			QVERIFY(world.content.value(ShellLayout::centerOf(v))->isVisible());
		}
	}

	void aViewSwitchMovesAnEditorWithoutLosingIt()
	{
		World world;
		QPointer<QWidget> noteEditor = world.content.value(QStringLiteral("detail"));
		QCOMPARE(hostOf(world.shell.panel(QStringLiteral("detail"))), QStringLiteral("shellBottom"));
		world.shell.setView(ShellView::Edit);
		QCOMPARE(hostOf(world.shell.panel(QStringLiteral("detail"))), QStringLiteral("shellCenter"));
		world.shell.setView(ShellView::Arrange);
		QCOMPARE(hostOf(world.shell.panel(QStringLiteral("detail"))), QStringLiteral("shellBottom"));
		// The same widget, still alive, still in its own frame: a move is a re-parent, never a delete.
		QVERIFY(!noteEditor.isNull());
		QCOMPARE(world.shell.panel(QStringLiteral("detail"))->content(), noteEditor.data());
	}

	void aRegionShowsOnePanelAndTheRestAreParked()
	{
		World world;
		world.shell.togglePanel(QStringLiteral("mixer"));
		QVERIFY(world.content.value(QStringLiteral("mixer"))->isVisible());
		QVERIFY(!world.content.value(QStringLiteral("detail"))->isVisible());
		QCOMPARE(hostOf(world.shell.panel(QStringLiteral("detail"))), QStringLiteral("shellParking"));
		// Closing the region's panel collapses the region.
		QVERIFY(!world.shell.togglePanel(QStringLiteral("mixer")));
		QVERIFY(!world.shell.findChild<QWidget*>(QStringLiteral("shellBottom"))->isVisible());
	}

	void aPanelTheViewLacksIsRevealedBySwitchingView()
	{
		World world;
		world.shell.setView(ShellView::Edit);
		QVERIFY(world.shell.togglePanel(QStringLiteral("arranger")));
		QCOMPARE(world.shell.model().view(), ShellView::Arrange);
		world.shell.setView(ShellView::Edit);
		QVERIFY(world.shell.togglePanel(QStringLiteral("launcher")));
		QCOMPARE(world.shell.model().view(), ShellView::Arrange);
		QVERIFY(world.content.value(QStringLiteral("launcher"))->isVisible());
	}

	void theActionsAndTheFooterFollowTheLayout()
	{
		World world;
		QAction* mixer = world.shell.panelAction(QStringLiteral("mixer"));
		QVERIFY(mixer != nullptr);
		QVERIFY(!mixer->isChecked());
		world.shell.togglePanel(QStringLiteral("mixer"));
		QVERIFY(mixer->isChecked());
		QVERIFY(!world.shell.panelAction(QStringLiteral("detail"))->isChecked());
		ShellButton* footerSwitch = nullptr;
		for (ShellButton* b : world.shell.findChildren<ShellButton*>())
		{
			if (b->defaultAction() == mixer) { footerSwitch = b; }
		}
		QVERIFY(footerSwitch != nullptr);
		QVERIFY(footerSwitch->isChecked());
		world.shell.setView(ShellView::Mix);
		QVERIFY(world.shell.viewActions().at(1)->isChecked());
	}

	void everyTargetMeetsTheMinimumSize()
	{
		World world;
		for (ShellButton* b : world.shell.findChildren<ShellButton*>())
		{
			if (!b->isVisible()) { continue; }
			QVERIFY2(b->width() >= 24 && b->height() >= 24,
				qPrintable(QStringLiteral("%1 is %2x%3").arg(b->accessibleName()).arg(b->width()).arg(b->height())));
		}
	}

	void theLayoutSurvivesARestart()
	{
		{
			World world;
			world.shell.restoreConfiguredLayout();
			world.shell.setView(ShellView::Mix);
			world.shell.togglePanel(QStringLiteral("inspector"));  // close it
			world.shell.togglePanel(QStringLiteral("launcher"));
		}
		World again;
		again.shell.restoreConfiguredLayout();
		QCOMPARE(again.shell.model().view(), ShellView::Mix);
		QVERIFY(!again.shell.model().isVisible(QStringLiteral("inspector")));
		QVERIFY(again.shell.model().launcherBesideCenter());
	}

	void aCorruptPersistedLayoutFallsBackToTheDefault()
	{
		lmms::ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("shell.layout"),
			QStringLiteral("{not json"));
		World world;
		world.shell.restoreConfiguredLayout();
		QCOMPARE(world.shell.model().view(), ShellView::Arrange);
		QVERIFY(world.shell.model().isVisible(QStringLiteral("inspector")));
		// ... and the corrupt value is replaced by the layout actually on screen.
		const QString now = lmms::ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("shell.layout"));
		QVERIFY(now.startsWith(QLatin1Char('{')) && now.contains(QStringLiteral("\"view\":\"arrange\"")));
	}

	void mdiEraGeometryInAProjectIsIgnoredSafely()
	{
		// An old project's <pianoroll visible="0" x="-5000" .../>: the editor is a panel's
		// content now, so restoring that state must change nothing and crash nothing.
		World world;
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("pianoroll"));
		element.setAttribute(QStringLiteral("visible"), 0);
		element.setAttribute(QStringLiteral("maximized"), 1);
		element.setAttribute(QStringLiteral("x"), -5000);
		element.setAttribute(QStringLiteral("y"), 9000);
		element.setAttribute(QStringLiteral("width"), 10);
		element.setAttribute(QStringLiteral("height"), 10);
		QWidget* noteEditor = world.content.value(QStringLiteral("detail"));
		MainWindow::restoreWidgetState(noteEditor, element);
		QVERIFY(noteEditor->isVisible());
		QCOMPARE(world.shell.panel(QStringLiteral("detail"))->content(), noteEditor);
		QDomElement saved = doc.createElement(QStringLiteral("pianoroll"));
		MainWindow::saveWidgetState(noteEditor, saved);
		QVERIFY(!saved.hasAttribute(QStringLiteral("x")));  // a panel has no window geometry to save
	}

	//! BUGS_FOUND 11.30, carried over from FocusDeskPaneTest when the Focus Desk was retired: a
	//! restored window is never clamped into an area too small to hold the grab sliver - the
	//! never-shown workspace is exactly such an area now.
	void aRestoredWindowIsNotClampedIntoAHiddenWorkspace()
	{
		const QRect saved(300, 200, 400, 300);
		QCOMPARE(MainWindow::clampToVisibleArea(saved, QRect(0, 0, 70, 22)), saved);
		QCOMPARE(MainWindow::clampToVisibleArea(saved, QRect(0, 0, 48, 600)), saved);
		QCOMPARE(MainWindow::clampToVisibleArea(saved, QRect(0, 0, 600, 48)), saved);

		const QRect area(0, 0, 800, 600);
		QCOMPARE(MainWindow::clampToVisibleArea(saved, area), saved);
		const QRect pastRight = MainWindow::clampToVisibleArea(QRect(790, 100, 400, 300), area);
		QCOMPARE(pastRight.left(), area.right() - 48);
		const QRect lost = MainWindow::clampToVisibleArea(QRect(5000, 5000, 400, 300), area);
		QCOMPARE(lost.topLeft(), QPoint(24, 24));
	}
};

int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
		&& qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}
	QApplication app(argc, argv);
	ShellTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "ShellTest.moc"
