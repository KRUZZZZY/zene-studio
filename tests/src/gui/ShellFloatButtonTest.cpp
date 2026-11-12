/*
 * ShellFloatButtonTest.cpp - the "Pop out" button and the View > Float or Dock Panel action float a panel
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

// GUI sweep 050/m0-train-24 (cluster gui-1030-*): five reports said a pop-out click "left the panel
// embedded". A real click on a live instance floats the panel every time (window.get_state `floating`
// true, the button reads "Dock"); the window opens exactly over the place the panel had, and with no
// window manager (the sweep's Xvnc) it has no title bar, so a screenshot looks unchanged. One slot per
// reported control keeps what the click really does on the record.

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QPointer>
#include <QToolButton>
#include <QtTest>

#include "ConfigManager.h"
#include "Shell.h"
#include "ShellPanel.h"

using namespace lmms::gui;

namespace
{

//! A shell with a plain widget mounted in every panel, shown at a desktop size.
struct World
{
	Shell shell;

	World()
	{
		for (const ShellPanelSpec& spec : ShellLayout::panels())
		{
			auto* w = new QWidget;
			w->setObjectName(QStringLiteral("content_") + spec.id);
			w->setFocusPolicy(Qt::StrongFocus);
			shell.mount(spec.id, w);
		}
		shell.resize(1600, 1000);
		shell.show();
	}

	QToolButton* floatButton(const QString& id) const
	{
		return shell.panel(id)->findChild<QToolButton*>(QStringLiteral("panelFloat"));
	}

	//! A left click on the panel's "Pop out" / "Dock" button, as the pointer delivers it.
	bool click(const QString& id) const
	{
		QToolButton* button = floatButton(id);
		if (button == nullptr || !button->isVisible()) { return false; }
		QTest::mouseClick(button, Qt::LeftButton);
		return true;
	}

	//! Real keyboard focus into @a id's editor (what Shell::onFocusChanged records).
	bool focus(const QString& id)
	{
		if (!QTest::qWaitForWindowActive(&shell)) { return false; }
		shell.panel(id)->content()->setFocus();
		return shell.model().focused() == id;
	}
};

} // namespace

class ShellFloatButtonTest : public QObject
{
	Q_OBJECT

private:
	//! What every reported pop-out has to show: the panel floats, in a visible window titled "<Panel> - Zene
	//! Studio", with its frame and editor inside, and its button turned into "Dock".
	static void verifyFloating(World& world, const QString& id, const QString& title)
	{
		QVERIFY2(world.shell.isDetached(id), qPrintable(id + " is still docked after the click"));
		QWidget* own = world.shell.floatingWindow(id);
		QVERIFY(own != nullptr);
		QVERIFY(own->isVisible());
		QVERIFY(own->isWindow());
		QCOMPARE(own->windowTitle(), title);
		QCOMPARE(world.shell.panel(id)->window(), own);
		QVERIFY(world.shell.panel(id)->content()->isVisible());
		QCOMPARE(world.floatButton(id)->text(), Shell::tr("Dock"));
		QCOMPARE(world.floatButton(id)->property("controlCommand").toString(), QStringLiteral("window.attach"));
	}

private slots:
	void init()
	{
		// Every test starts from the first-run layout: no persisted ui/shell.layout and no personal layout.
		lmms::ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("shell.layout"), QString());
		lmms::ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("shell.personal"), QString());
	}

	//! gui-1030-main-pop-out-the-note-editor-panel: the Note Editor sits in the bottom region of a fresh
	//! project; one click on its Pop out button takes it out of there into a window of its own.
	void theNoteEditorPopOutButtonFloatsItOutOfTheBottomRegion()
	{
		World world;
		const QString id = QStringLiteral("detail");
		QVERIFY(world.shell.model().isVisible(id));
		QCOMPARE(world.shell.panel(id)->parentWidget()->objectName(), QStringLiteral("shellBottom"));
		QCOMPARE(world.floatButton(id)->accessibleName(), Shell::tr("Pop out the %1 panel").arg(QStringLiteral("Note Editor")));
		QVERIFY(world.click(id));
		verifyFloating(world, id, QStringLiteral("Note Editor - Zene Studio"));
		QVERIFY(world.shell.panel(id)->parentWidget()->objectName() != QStringLiteral("shellBottom"));
		QVERIFY(world.click(id));  // the same button now docks it again
		QVERIFY(!world.shell.isDetached(id));
		QCOMPARE(world.shell.panel(id)->parentWidget()->objectName(), QStringLiteral("shellBottom"));
	}

	//! The window opens exactly over the place the panel had ("a pop-out never sends the panel far from the
	//! eye", Shell::detachPanel), so a window-manager-less screenshot shows the same pixels - except for the
	//! button, which says "Dock". That is why the sweep read the Note Editor and the others as unmoved.
	void aPopOutWindowOpensWhereThePanelWas()
	{
		World world;
		const QString id = QStringLiteral("detail");
		const QPoint was = world.shell.panel(id)->mapToGlobal(QPoint(0, 0));
		QVERIFY(world.click(id));
		QVERIFY(world.shell.isDetached(id));
		QCOMPARE(world.shell.floatingWindow(id)->geometry().topLeft(), was);
	}

	//! gui-1030-main-pop-out-the-arranger-panel: the Arranger floats and the centre says where it went.
	void theArrangerPopOutButtonFloatsItAndTheCentreSaysSo()
	{
		World world;
		const QString id = QStringLiteral("arranger");
		auto* placeholder = world.shell.findChild<QWidget*>(QStringLiteral("shellFloatingPlaceholder"));
		QVERIFY(placeholder != nullptr);
		QVERIFY(!placeholder->isVisible());
		QVERIFY(world.click(id));
		verifyFloating(world, id, QStringLiteral("Arranger - Zene Studio"));
		QVERIFY(placeholder->isVisible());
		QVERIFY(placeholder->findChild<QLabel*>()->text().contains(QStringLiteral("Arranger")));
	}

	//! gui-1030-tab-devices-pop-out-the-devices-panel: Devices, opened from the bottom region's tabs.
	void theDevicesPopOutButtonFloatsThePanelTheTabOpened()
	{
		World world;
		const QString id = QStringLiteral("devices");
		QVERIFY(world.shell.togglePanel(id));
		QVERIFY(world.shell.model().isVisible(id));
		QCOMPARE(world.shell.panel(id)->parentWidget()->objectName(), QStringLiteral("shellBottom"));
		QVERIFY(world.click(id));
		verifyFloating(world, id, QStringLiteral("Devices - Zene Studio"));
	}

	//! gui-1030-tab-automation-pop-out-the-automation-pa: Automation, opened from the bottom region's tabs.
	void theAutomationPopOutButtonFloatsThePanelTheTabOpened()
	{
		World world;
		const QString id = QStringLiteral("automation");
		QVERIFY(world.shell.togglePanel(id));
		QVERIFY(world.shell.model().isVisible(id));
		QCOMPARE(world.shell.panel(id)->parentWidget()->objectName(), QStringLiteral("shellBottom"));
		QVERIFY(world.click(id));
		verifyFloating(world, id, QStringLiteral("Automation - Zene Studio"));
	}

	//! gui-1030-menu-view-float-or-dock-panel: the action floats the panel that has the keyboard focus when it
	//! is on screen - here the Pattern Editor in the lower region.
	void floatOrDockFloatsTheFocusedPanelThatIsOnScreen()
	{
		World world;
		const QString id = QStringLiteral("pattern");
		QVERIFY(world.shell.togglePanel(id));
		QVERIFY2(world.focus(id), "the Pattern Editor did not take the keyboard focus");
		world.shell.floatAction()->trigger();
		QVERIFY(world.shell.isDetached(id));
		QVERIFY(!world.shell.isDetached(QStringLiteral("arranger")));
		QVERIFY(world.shell.floatingWindow(id)->isVisible());
		QCOMPARE(world.shell.floatingWindow(id)->windowTitle(), QStringLiteral("Pattern Editor - Zene Studio"));
	}

	//! The documented rest of that contract (the action's tooltip): a visible panel that does NOT hold the
	//! focus stays where it is, and the view's main panel floats instead, leaving the centre's placeholder.
	void floatOrDockFloatsTheViewsMainPanelWhenTheVisibleOneIsNotFocused()
	{
		World world;
		QVERIFY(world.shell.togglePanel(QStringLiteral("pattern")));
		QVERIFY2(world.focus(QStringLiteral("arranger")), "the Arranger did not take the keyboard focus");
		world.shell.floatAction()->trigger();
		QVERIFY(world.shell.isDetached(QStringLiteral("arranger")));
		QVERIFY(!world.shell.isDetached(QStringLiteral("pattern")));
		QVERIFY(world.shell.model().isVisible(QStringLiteral("pattern")));
		auto* placeholder = world.shell.findChild<QWidget*>(QStringLiteral("shellFloatingPlaceholder"));
		QVERIFY(placeholder != nullptr && placeholder->isVisible());
	}

	//! A focus that went stale - its panel was replaced in the region and is off screen - is not a target:
	//! the view's main panel floats.
	void floatOrDockIgnoresAFocusedPanelThatIsOffScreen()
	{
		World world;
		QVERIFY(world.shell.togglePanel(QStringLiteral("devices")));
		QVERIFY2(world.focus(QStringLiteral("devices")), "Devices did not take the keyboard focus");
		QVERIFY(world.shell.togglePanel(QStringLiteral("pattern")));  // a region shows one panel: Devices goes off screen
		QVERIFY(!world.shell.model().isVisible(QStringLiteral("devices")));
		world.shell.floatAction()->trigger();
		QVERIFY(world.shell.isDetached(QStringLiteral("arranger")));
		QVERIFY(!world.shell.isDetached(QStringLiteral("devices")));
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
	ShellFloatButtonTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "ShellFloatButtonTest.moc"
