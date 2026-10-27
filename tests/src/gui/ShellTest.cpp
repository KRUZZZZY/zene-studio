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
#include <QJsonArray>
#include <QLabel>
#include <QPointer>
#include <QSplitter>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include "ConfigManager.h"
#include "MainWindow.h"
#include "Shell.h"
#include "ShellChrome.h"
#include "ShellPanel.h"
#include "ShellTearOff.h"

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

ShellTearOff* tearOffOf(ShellPanel* frame)
{
	return frame->header()->findChild<ShellTearOff*>();
}

//! A point on panel @a frame's header (the handle), and where it is on the screen.
const QPoint kGrip(60, 10);
QPoint gripOf(ShellPanel* frame)
{
	return frame->header()->mapToGlobal(kGrip);
}

} // namespace

class ShellTest : public QObject
{
	Q_OBJECT

private slots:
	void init()
	{
		// Every test starts from the first-run layout: no persisted ui/shell.layout, and no personal layout put
		// aside for a project's (theme 22: restoreConfiguredLayout would prefer it).
		lmms::ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("shell.layout"), QString());
		lmms::ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("shell.personal"), QString());
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

	void everyRegisteredPanelHasAFrame()
	{
		// window.focus_panel (ControlCommandsWorkspace.cpp, focusPanel) passes Shell::panel() a frame only after
		// ShellLayout::isPanel() accepted the id; this pins that both answer from the same register.
		World world;
		for (const ShellPanelSpec& spec : ShellLayout::panels())
		{
			QVERIFY(ShellLayout::isPanel(spec.id));
			QVERIFY2(world.shell.panel(spec.id) != nullptr, qPrintable(spec.id));
		}
		QVERIFY(!ShellLayout::isPanel(QStringLiteral("no-such-panel")));
		QVERIFY(world.shell.panel(QStringLiteral("no-such-panel")) == nullptr);
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

	//! 0.5.0 G (theme 8): a panel floats in a window of its own and docks back with its editor
	//! intact; docking shows it, in its home region, in place of what was there.
	void aFloatingPanelDocksBackIntoItsZone()
	{
		World world;
		QPointer<QWidget> mixer = world.content.value(QStringLiteral("mixer"));
		QVERIFY(world.shell.detachPanel(QStringLiteral("mixer")));
		QVERIFY(world.shell.floatingWindow(QStringLiteral("mixer"))->isVisible());
		QVERIFY(mixer->isVisible());
		QVERIFY(world.shell.panelAction(QStringLiteral("mixer"))->isChecked());  // on screen, in its own window
		QVERIFY(world.shell.attachPanel(QStringLiteral("mixer")));
		QCOMPARE(hostOf(world.shell.panel(QStringLiteral("mixer"))), QStringLiteral("shellBottom"));
		QVERIFY(world.shell.model().isVisible(QStringLiteral("mixer")));
		QCOMPARE(world.shell.panel(QStringLiteral("mixer"))->content(), mixer.data());
		QVERIFY(!world.shell.attachPanel(QStringLiteral("mixer")));  // docked already: nothing to do
	}

	void aFloatingPanelsSwitchBringsItsWindowForward()
	{
		World world;
		world.shell.detachPanel(QStringLiteral("browser"));
		QVERIFY(world.shell.togglePanel(QStringLiteral("browser")));
		QVERIFY(world.shell.isDetached(QStringLiteral("browser")));
		QVERIFY(world.shell.floatingWindow(QStringLiteral("browser"))->isVisible());
	}

	void theCentreSaysWhereItsPanelWentAndTheButtonSaysDock()
	{
		World world;
		auto* placeholder = world.shell.findChild<QWidget*>(QStringLiteral("shellFloatingPlaceholder"));
		auto* button = world.shell.panel(QStringLiteral("arranger"))->findChild<QToolButton*>(QStringLiteral("panelFloat"));
		QVERIFY(placeholder != nullptr && button != nullptr);
		QVERIFY(!placeholder->isVisible());
		QCOMPARE(button->property("controlCommand").toString(), QStringLiteral("window.detach"));
		world.shell.detachPanel(QStringLiteral("arranger"));
		QVERIFY(placeholder->isVisible());
		QVERIFY(placeholder->findChild<QLabel*>()->text().contains(QStringLiteral("Arranger")));
		QCOMPARE(button->text(), Shell::tr("Dock"));
		QCOMPARE(button->property("controlCommand").toString(), QStringLiteral("window.attach"));
		world.shell.attachPanel(QStringLiteral("arranger"));
		QVERIFY(!placeholder->isVisible());
		QCOMPARE(hostOf(world.shell.panel(QStringLiteral("arranger"))), QStringLiteral("shellCenter"));
		QCOMPARE(button->property("controlCommand").toString(), QStringLiteral("window.detach"));
	}

	void draggingAHeaderOutFloatsThePanelWhereItIsDropped()
	{
		World world;
		QVERIFY(QTest::qWaitForWindowExposed(&world.shell));
		ShellPanel* frame = world.shell.panel(QStringLiteral("inspector"));
		ShellTearOff* tear = tearOffOf(frame);
		QVERIFY(tear != nullptr);
		const QPoint start = gripOf(frame);
		tear->press(start, kGrip);
		tear->move(start + QPoint(20, 0));  // past the drag distance, still on the header
		QCOMPARE(tear->drag(), ShellTearOff::Drag::Tearing);
		QVERIFY(tear->previewText().contains(QStringLiteral("Keep dragging")));
		const QPoint drop = frame->mapToGlobal(QPoint(frame->width() + 300, 200));
		tear->move(drop);
		QVERIFY(tear->previewText().contains(QStringLiteral("Release to float")));
		tear->release(drop);
		QCOMPARE(tear->drag(), ShellTearOff::Drag::None);
		QVERIFY(world.shell.isDetached(QStringLiteral("inspector")));
		QCOMPARE(world.shell.floatingWindow(QStringLiteral("inspector"))->geometry().topLeft(), drop - kGrip);
		QCOMPARE(frame->content(), world.content.value(QStringLiteral("inspector")).data());
	}

	void aDragReleasedOnTheHeaderOrCancelledChangesNothing()
	{
		World world;
		QVERIFY(QTest::qWaitForWindowExposed(&world.shell));
		ShellPanel* frame = world.shell.panel(QStringLiteral("inspector"));
		ShellTearOff* tear = tearOffOf(frame);
		const QPoint start = gripOf(frame);
		tear->press(start, kGrip);
		tear->move(start + QPoint(30, 25));  // within a header's height of it
		tear->release(start + QPoint(30, 25));
		QVERIFY(!world.shell.isDetached(QStringLiteral("inspector")));
		tear->press(start, kGrip);
		tear->move(frame->mapToGlobal(QPoint(frame->width() + 300, 200)));
		tear->cancel();  // Esc
		QCOMPARE(tear->drag(), ShellTearOff::Drag::None);
		QVERIFY(tear->previewText().isEmpty());
		QVERIFY(!world.shell.isDetached(QStringLiteral("inspector")));
		QCOMPARE(hostOf(frame), QStringLiteral("shellLeft"));
	}

	void draggingAFloatingHeaderOntoItsZoneDocksItAndElsewhereMovesIt()
	{
		World world;
		QVERIFY(QTest::qWaitForWindowExposed(&world.shell));
		const QString mixer = QStringLiteral("mixer");
		world.shell.setNextFloatGeometry(mixer, QRect(world.shell.mapToGlobal(QPoint(500, 200)), QSize(480, 320)));
		QVERIFY(world.shell.detachPanel(mixer));
		ShellPanel* frame = world.shell.panel(mixer);
		ShellTearOff* tear = tearOffOf(frame);
		auto* hint = world.shell.findChild<ShellDockHint*>();
		QVERIFY(hint != nullptr);
		const QPoint zone = world.shell.mapToGlobal(world.shell.dockZone(mixer).center());
		tear->press(gripOf(frame), kGrip);
		tear->move(zone);
		QCOMPARE(tear->drag(), ShellTearOff::Drag::Moving);
		QVERIFY(hint->isVisible());
		QCOMPARE(hint->mode(), ShellDockHint::Armed);
		// The bottom region shows one panel: the highlight says which one docking replaces.
		QVERIFY(hint->text().contains(QStringLiteral("Release to dock")));
		QVERIFY(hint->text().contains(QStringLiteral("Note Editor")));
		tear->release(zone);
		QVERIFY(!world.shell.isDetached(mixer));
		QVERIFY(!hint->isVisible());
		QCOMPARE(hostOf(frame), QStringLiteral("shellBottom"));

		QVERIFY(world.shell.detachPanel(mixer));
		const QPoint before = world.shell.floatingWindow(mixer)->pos();
		const QPoint start = gripOf(frame);
		const QPoint away = world.shell.mapToGlobal(QPoint(world.shell.width() + 400, 50));
		tear->press(start, kGrip);
		tear->move(away);
		QVERIFY(!hint->isVisible());  // off the main window there is nowhere to dock
		tear->release(away);
		QVERIFY(world.shell.isDetached(mixer));
		QCOMPARE(world.shell.floatingWindow(mixer)->pos(), before + (away - start));
	}

	void theKeyFloatsAndDocksTheFocusedPanel()
	{
		World world;
		QCOMPARE(world.shell.floatAction()->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+F")));
		// The focused panel when it is on screen, else the view's main panel.
		const QString focused = world.shell.model().focused();
		const bool shown = !focused.isEmpty() && world.shell.model().isVisible(focused);
		const QString id = shown ? focused : QStringLiteral("arranger");
		world.shell.floatAction()->trigger();
		QVERIFY(world.shell.isDetached(id));
		world.shell.floatAction()->trigger();
		QVERIFY(!world.shell.isDetached(id));
	}

	void aSavedLayoutRemembersItsFloatingPanels()
	{
		World world;
		const QRect where(100, 100, 500, 400);
		world.shell.setNextFloatGeometry(QStringLiteral("devices"), where);
		QVERIFY(world.shell.detachPanel(QStringLiteral("devices")));
		const QJsonObject saved = world.shell.snapshot();
		const QJsonArray floating = saved.value(QStringLiteral("floating")).toArray();
		QCOMPARE(floating.size(), 1);
		QCOMPARE(floating.at(0).toObject().value(QStringLiteral("panel")).toString(), QStringLiteral("devices"));
		QVERIFY(floating.at(0).toObject().contains(QStringLiteral("screen")));
		world.shell.attachPanel(QStringLiteral("devices"));
		world.shell.restoreSnapshot(saved);
		QVERIFY(world.shell.isDetached(QStringLiteral("devices")));
		QCOMPARE(world.shell.floatingWindow(QStringLiteral("devices"))->geometry(), where);
		// A layout saved before the tear-off (no `floating`) leaves the panels as they are ...
		QJsonObject older = saved;
		older.remove(QStringLiteral("floating"));
		world.shell.restoreSnapshot(older);
		QVERIFY(world.shell.isDetached(QStringLiteral("devices")));
		// ... and one that lists none docks them.
		QJsonObject docked = saved;
		docked.insert(QStringLiteral("floating"), QJsonArray());
		world.shell.restoreSnapshot(docked);
		QVERIFY(!world.shell.isDetached(QStringLiteral("devices")));
	}

	void floatingPanelsSurviveARestart()
	{
		const QRect where(120, 140, 500, 380);
		{
			World world;
			world.shell.restoreConfiguredLayout();
			world.shell.setNextFloatGeometry(QStringLiteral("mixer"), where);
			world.shell.detachPanel(QStringLiteral("mixer"));
		}
		World again;
		again.shell.restoreConfiguredLayout();
		QVERIFY(again.shell.isDetached(QStringLiteral("mixer")));
		QCOMPARE(again.shell.floatingWindow(QStringLiteral("mixer"))->geometry(), where);
	}

	void quittingKeepsAPanelFloatingAndACancelledQuitBringsItBack()
	{
		World world;
		world.shell.detachPanel(QStringLiteral("browser"));
		QWidget* own = world.shell.floatingWindow(QStringLiteral("browser"));
		own->close();  // not from its title bar: what File > Quit's closeAllWindows does
		QVERIFY(world.shell.isDetached(QStringLiteral("browser")));
		QTRY_VERIFY(own->isVisible());  // the main window stayed open, so the window comes back
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
