/*
 * FocusDeskPaneTest.cpp - the Focus Desk's wiring to the product's windows.
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
#include <QBoxLayout>
#include <QDir>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QtTest>

#include "ConfigManager.h"
#include "FocusDesk.h"
#include "FocusDeskPane.h"
#include "SubWindow.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! The smallest stand-in for MainWindow the pane can be real against: a
//! vertical layout holding a workspace page, the page holding a workspace, and
//! editors each inside their own subwindow. That is the shape the pane is
//! written for, and the only part of MainWindow it touches.
struct World
{
	QWidget window;
	QVBoxLayout* layout = new QVBoxLayout(&window);
	QWidget page;
	QMdiArea workspace;

	World()
	{
		layout->addWidget(&page);
		page.setLayout(new QVBoxLayout);
		page.layout()->addWidget(&workspace);
		window.show();
	}

	//! A live editor inside its own subwindow, wrapped the way the product
	//! wraps them. `shown` is the state the pane has to give back.
	QWidget* editor(const QString& name, bool shown = false)
	{
		auto* w = new QWidget;
		auto* sub = workspace.addSubWindow(w);
		sub->setObjectName(name);
		sub->setVisible(shown);
		return w;
	}

	//! The subwindow an editor is in - read BEFORE the desk claims it, since
	//! claiming re-parents the editor onto the desk.
	static QMdiSubWindow* windowOf(QWidget* editor)
	{
		return qobject_cast<QMdiSubWindow*>(editor->parentWidget());
	}
};

} // namespace

//! The Focus Desk's page: which editors it takes out of which window, what it
//! gives back, and the two ways the mode is entered. The desk's own rules are
//! FocusDeskTest's; this file is about the wiring, so every test here goes
//! through a real workspace and real subwindows.
class FocusDeskPaneTest : public QObject
{
	Q_OBJECT

private slots:
	//! The pane writes its state to the config on every switch, so the file it
	//! writes is redirected to one of this process's own: a test that saved
	//! over the settings of the person running it would be a bug, not a detail.
	void initTestCase()
	{
		m_configPath = QDir::tempPath()
			+ QStringLiteral("/focusdesk-pane-test-%1.xml")
				.arg(QCoreApplication::applicationPid());
		ConfigManager::inst()->loadConfigFile(m_configPath);
	}

	//! Each test starts from no saved desk, so the persistence test's writes
	//! cannot become another test's starting conditions.
	void init()
	{
		auto* conf = ConfigManager::inst();
		conf->setValue("ui", QStringLiteral("focusdesk"), QStringLiteral("0"));
		conf->deleteValue("ui", QStringLiteral("focusdesk.focus"));
		conf->deleteValue("ui", QStringLiteral("focusdesk.density"));
	}

	//! Activating with nothing to claim is refused. An empty shell over the
	//! workspace would hide every editor and show nothing in their place, which
	//! is worse than not switching at all.
	void activatingWithNothingToClaimLeavesTheWorkspaceAlone()
	{
		World world;
		FocusDeskPane pane(&world.workspace, &world.page, world.layout,
			FocusDeskPane::Mountables{}, &world.window);

		QVERIFY(!pane.deskActive());
		QVERIFY(!pane.setDeskActive(true));
		QVERIFY(!pane.deskActive());
		QVERIFY(pane.mountedIds().isEmpty());
		QVERIFY(world.page.isVisible());
		QVERIFY(!pane.isVisible());
	}

	void activatingClaimsEachEditorOutOfItsOwnSubwindow()
	{
		World world;
		QWidget* arrangement = world.editor(QStringLiteral("arrangement"), true);
		QWidget* mixer = world.editor(QStringLiteral("mixer"));
		QMdiSubWindow* arrangementWindow = World::windowOf(arrangement);
		QMdiSubWindow* mixerWindow = World::windowOf(mixer);
		QVERIFY(arrangementWindow != nullptr);
		QVERIFY(mixerWindow != nullptr);

		FocusDeskPane pane(&world.workspace, &world.page, world.layout,
			{{QStringLiteral("arrangement"), arrangement}, {QStringLiteral("mixer"), mixer}},
			&world.window);

		QVERIFY(pane.setDeskActive(true));
		QCOMPARE(pane.mountedIds(),
			QStringList({QStringLiteral("arrangement"), QStringLiteral("mixer")}));
		// The desk shows the editor itself, not a window drawn around it.
		QVERIFY(pane.desk()->mountedWidget(QStringLiteral("arrangement")) == arrangement);
		QCOMPARE(pane.sourceWindow(QStringLiteral("mixer")), mixerWindow);
		// Each subwindow is emptied - but only after the desk took the widget,
		// so a refusal would have left the product's own window intact.
		QVERIFY(arrangementWindow->widget() == nullptr);
		QVERIFY(mixerWindow->widget() == nullptr);
		// And the workspace page is out of the way while the desk is up.
		QVERIFY(!world.page.isVisible());
		QVERIFY(pane.isVisible());
	}

	void deactivatingHandsEachEditorBackWhereItCameFrom()
	{
		World world;
		QWidget* arrangement = world.editor(QStringLiteral("arrangement"), true);
		QWidget* mixer = world.editor(QStringLiteral("mixer"));
		QMdiSubWindow* arrangementWindow = World::windowOf(arrangement);
		QMdiSubWindow* mixerWindow = World::windowOf(mixer);

		FocusDeskPane pane(&world.workspace, &world.page, world.layout,
			{{QStringLiteral("arrangement"), arrangement}, {QStringLiteral("mixer"), mixer}},
			&world.window);
		QVERIFY(pane.setDeskActive(true));
		QVERIFY(!pane.setDeskActive(false));

		QCOMPARE(arrangementWindow->widget(), arrangement);
		QCOMPARE(mixerWindow->widget(), mixer);
		// A window the product had on screen comes back on screen; one it had
		// hidden comes back hidden - the state it was taken from, not a state
		// the desk invented.
		QVERIFY(arrangementWindow->isVisible());
		QVERIFY(!mixerWindow->isVisible());
		QVERIFY(pane.mountedIds().isEmpty());
		QVERIFY(world.page.isVisible());
	}

	//! The editors do not live in bare QMdiSubWindows: MainWindow wraps each in
	//! the product's own SubWindow, whose show/hide and detach check read the
	//! widget it holds. Emptying one is exactly what claiming does to every
	//! editor, so the emptied window has to stay safe to hide and to show -
	//! the workspace page is hidden for as long as the desk is up, and a window
	//! whose hide() dereferences the widget it no longer has takes the process
	//! down instead of the mode.
	void claimingOutOfAProductSubWindowLeavesItSafeToHide()
	{
		World world;
		QWidget* content = new QWidget;
		auto* window = new SubWindow(world.workspace.viewport());
		window->setWidget(content);
		world.workspace.addSubWindow(window);
		window->show();
		QVERIFY(!window->isDetached());

		FocusDeskPane pane(&world.workspace, &world.page, world.layout,
			{{QStringLiteral("mixer"), content}}, &world.window);
		QVERIFY(pane.setDeskActive(true));
		QCOMPARE(pane.sourceWindow(QStringLiteral("mixer")), window);
		QVERIFY(window->widget() == nullptr);

		// The two calls that used to segfault on the emptied window: this is
		// the regression, and it is a crash rather than a wrong answer.
		window->hide();
		QVERIFY(!window->isDetached());
		window->show();
		// The desk has the workspace page hidden, so the window is not on
		// screen here; what the call has to prove is that show() ran at all
		// without a widget to read.
		QVERIFY(!window->isHidden());

		// And the editor still comes back with its window intact.
		QVERIFY(!pane.setDeskActive(false));
		QCOMPARE(window->widget(), content);
	}

	//! A borrowed editor can be destroyed before the desk that borrowed it.
	//! `MainWindow::~MainWindow` deletes `automationEditor`, `pianoRoll` and
	//! `songEditor` outright - it has to, Song holds references to them - and
	//! the pane holding the desk is destroyed *after* that, so the desk is asked
	//! about three widgets that no longer exist. It must answer "gone": a desk
	//! that answers with the freed pointer makes every later read of it a
	//! use-after-free, which is what the product did on shutdown (SIGSEGV in
	//! FocusDesk::releaseAll's `content->setParent(m_park)`).
	void aBorrowedEditorDestroyedBeforeTheDeskIsNotHandedBack()
	{
		World world;
		QWidget* mixer = world.editor(QStringLiteral("mixer"));
		FocusDeskPane pane(&world.workspace, &world.page, world.layout,
			{{QStringLiteral("mixer"), mixer}}, &world.window);
		QVERIFY(pane.setDeskActive(true));
		QVERIFY(pane.desk()->mountedWidget(QStringLiteral("mixer")) == mixer);

		delete mixer;   // exactly what ~MainWindow does to three editors

		QVERIFY(pane.desk()->mountedWidget(QStringLiteral("mixer")) == nullptr);
		QVERIFY(pane.desk()->releaseAll().isEmpty());
	}

	//! The same destruction order driven through the pane, which is the half
	//! that puts a widget back: after the editor is deleted there is nothing to
	//! hand back, and `setWidget()` must not be given the freed pointer - the
	//! subwindow would be left holding it, and the write into it is what the
	//! desk's own guard cannot reach.
	void aClaimedEditorDeletedBeforeThePaneLeavesNothingToHandBack()
	{
		World world;
		QWidget* mixer = world.editor(QStringLiteral("mixer"));
		QMdiSubWindow* window = World::windowOf(mixer);
		QVERIFY(window != nullptr);

		auto* pane = new FocusDeskPane(&world.workspace, &world.page, world.layout,
			{{QStringLiteral("mixer"), mixer}}, &world.window);
		QVERIFY(pane->setDeskActive(true));
		QVERIFY(window->widget() == nullptr);   // emptied by the claim

		delete mixer;   // the editor dies first...
		delete pane;    // ...and the pane is destroyed while it believes it holds it

		// Nothing was put back: the subwindow is still (correctly) empty, rather
		// than holding a widget that does not exist.
		QVERIFY(window->widget() == nullptr);
	}

	//! An editor belonging to a different workspace is left alone: adopting it
	//! would put it on the desk with no window of ours to give it back to.
	void anEditorFromAnotherWorkspaceIsNotClaimed()
	{
		World mine;
		World other;
		QWidget* stranger = other.editor(QStringLiteral("mixer"));
		QMdiSubWindow* strangerWindow = World::windowOf(stranger);

		FocusDeskPane pane(&mine.workspace, &mine.page, mine.layout,
			{{QStringLiteral("mixer"), stranger}}, &mine.window);
		QVERIFY(!pane.setDeskActive(true));
		QVERIFY(World::windowOf(stranger) == strangerWindow);
		QVERIFY(strangerWindow->widget() == stranger);
	}

	//! The menu entry is the only way into the mode, so it has to describe the
	//! window it belongs to: no pane greys it, and a refusal takes the tick
	//! back rather than leaving the menu describing a mode the window is not in.
	//!
	//! Each scenario gets its own pane, and the panes do not overlap. A pane
	//! follows the `ui/focusdesk` setting, and that key is global: two live panes
	//! are two readers and two writers of one value, so each one's write is an
	//! instruction to the other. The product never creates that state - a
	//! MainWindow owns exactly one FocusDeskPane - and arbitration between panes
	//! is not what this test is about.
	void theViewMenuToggleReflectsTheDeskAndRevertsARefusal()
	{
		World world;

		// No pane: the entry says the mode cannot be entered at all.
		{
			QMenu withoutPane;
			addFocusDeskToggle(&withoutPane, nullptr);
			QCOMPARE(withoutPane.actions().size(), 1);
			QVERIFY(!withoutPane.actions().first()->isEnabled());
		}

		// A pane with nothing to claim refuses, and the tick is taken back.
		{
			FocusDeskPane empty(&world.workspace, &world.page, world.layout,
				FocusDeskPane::Mountables{}, &world.window);
			QMenu refusing;
			addFocusDeskToggle(&refusing, &empty);
			QAction* refused = refusing.actions().last();
			QVERIFY(refused->isCheckable());
			QVERIFY(!refused->isChecked());

			refused->setChecked(true);        // the user asks for the desk
			QVERIFY(!empty.deskActive());     // nothing could be claimed...
			QVERIFY(!refused->isChecked());   // ...so the tick is taken back
		}

		// A pane that can claim enters on the tick, and leaves on it.
		{
			QWidget* mixer = world.editor(QStringLiteral("mixer"));
			FocusDeskPane full(&world.workspace, &world.page, world.layout,
				{{QStringLiteral("mixer"), mixer}}, &world.window);
			QMenu entering;
			addFocusDeskToggle(&entering, &full);
			QAction* live = entering.actions().last();
			QVERIFY(!live->isChecked());
			live->setChecked(true);
			QVERIFY(full.deskActive());
			QVERIFY(live->isChecked());
			live->setChecked(false);
			QVERIFY(!full.deskActive());
		}
	}

	//! The setting is the agent's way in as well as the menu's: `settings.set`
	//! writes the same key this pane follows, so entering the mode through the
	//! config alone has to work. And what it persists has to be the state it
	//! arrives at, not the state it is leaving.
	//!
	//! The two are one property because the second is what the first costs.
	//! Claiming the stage module moves the focus, the desk reports that move, and
	//! the handler for it writes the config - at which point the pane has not yet
	//! marked itself active, so it would persist "off" in the middle of switching
	//! "on", then "on" again. The setting would be a value the window was in for
	//! the duration of one function call, and anything else reading the config
	//! during the switch would be told the wrong thing.
	void enteringTheModeByTheSettingAnnouncesTheFinishedStateOnly()
	{
		World world;
		QWidget* stage = world.editor(QStringLiteral("arrangement"));
		FocusDeskPane pane(&world.workspace, &world.page, world.layout,
			{{QStringLiteral("arrangement"), stage}}, &world.window);

		// What the config file would receive, in order.
		QSignalSpy writes(ConfigManager::inst(), &ConfigManager::valueChanged);
		QVERIFY(writes.isValid());

		ConfigManager::inst()->setValue("ui", QStringLiteral("focusdesk"),
			QStringLiteral("1"));

		QVERIFY(pane.deskActive());
		QCOMPARE(pane.mountedIds(), QStringList({QStringLiteral("arrangement")}));
		QCOMPARE(pane.desk()->focused(), QStringLiteral("arrangement"));

		QStringList seen;
		for (const QList<QVariant>& write : writes)
		{
			if (write.at(0).toString() == QStringLiteral("ui")
				&& write.at(1).toString() == QStringLiteral("focusdesk"))
			{
				seen.append(write.at(2).toString());
			}
		}
		// The write that asked for the mode, and no other: no "0" from the focus
		// the claim moved, and no second "1" from the switch publishing itself.
		QCOMPARE(seen, QStringList({QStringLiteral("1")}));
	}

	//! The mode and the focus module come back on the next run, and the mode is
	//! applied by the seam - not by the constructor, which runs before
	//! GuiApplication has built a single editor.
	void theModeAndFocusSurviveARestart()
	{
		World world;
		QWidget* mixer = world.editor(QStringLiteral("mixer"));
		{
			FocusDeskPane first(&world.workspace, &world.page, world.layout,
				{{QStringLiteral("mixer"), mixer}}, &world.window);
			QVERIFY(first.setDeskActive(true));
			QVERIFY(first.desk()->focusModule(QStringLiteral("mixer")));
		}
		QCOMPARE(ConfigManager::inst()->value("ui", QStringLiteral("focusdesk")),
			QStringLiteral("1"));

		FocusDeskPane second(&world.workspace, &world.page, world.layout,
			{{QStringLiteral("mixer"), mixer}}, &world.window);
		QVERIFY(!second.deskActive());          // asked for, not yet applied
		QVERIFY(second.applyConfiguredState());
		QCOMPARE(second.desk()->focused(), QStringLiteral("mixer"));
		QCOMPARE(second.mountedIds(), QStringList({QStringLiteral("mixer")}));
		// Calling the seam again claims nothing a second time.
		QVERIFY(second.applyConfiguredState());
		QCOMPARE(second.mountedIds(), QStringList({QStringLiteral("mixer")}));
		// And the hand-back still works after a restore rather than a click.
		QVERIFY(!second.setDeskActive(false));
		QVERIFY(!ConfigManager::inst()->value("ui", QStringLiteral("focusdesk")).toInt());
	}

	//! The saved mode is applied by the seam and by no other turn. A
	//! constructor that defer-claims onto the next event-loop turn is exactly
	//! the defect that crashed the product: inside GuiApplication the first
	//! turn arrives before a single editor has been built, and the accessors
	//! hand back pointers that were never assigned - there is nothing to test
	//! for, only not to read. So the deferral is not merely early or late; it
	//! must not exist.
	void theSavedModeIsAppliedByTheSeamAndNoOtherTurn()
	{
		World world;
		QWidget* mixer = world.editor(QStringLiteral("mixer"));
		{
			FocusDeskPane first(&world.workspace, &world.page, world.layout,
				{{QStringLiteral("mixer"), mixer}}, &world.window);
			QVERIFY(first.setDeskActive(true));
		}

		// The config says "on" and mixer is back in its subwindow, so a
		// deferred claim would have something to claim and would succeed.
		FocusDeskPane second(&world.workspace, &world.page, world.layout,
			{{QStringLiteral("mixer"), mixer}}, &world.window);
		QVERIFY(!second.deskActive());
		QTest::qWait(50);
		QCoreApplication::processEvents();
		QVERIFY(!second.deskActive());     // no turn applies it...
		QVERIFY(second.applyConfiguredState());   // ...only the seam does
		QVERIFY(second.deskActive());
	}

private:
	QString m_configPath;
};

// The platform plugin has to be chosen before the QApplication exists; the gate
// runner exports QT_QPA_PLATFORM=offscreen and CI does not, so default to
// offscreen when nothing else is configured and no display is attached.
int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
		&& qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}

	QApplication app(argc, argv);
	FocusDeskPaneTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "FocusDeskPaneTest.moc"
