/*
 * ShellHomesTest.cpp - design themes 8 and 22 in the 0.5.0 shell: a home for each tool, layouts that behave
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

/*! Theme 8: a side region names its panels as tabs (a tab opens its panel, the hide button closes the region, a
 *  floating panel leaves the tabs, one panel shows no strip); a row of the chrome labels its buttons while they fit,
 *  shows icons only when narrower and moves the rest into More - the least kept last, a user-hidden button never;
 *  the header does the same with its right-hand buttons; the footer's Tools home lists every tool window; and
 *  window.get_state's shell part reports the regions and the footer's fit.
 *  Theme 22: a project's layout puts the personal one aside once and gives it back, also at the next start;
 *  replacing a saved layout asks first and a No changes nothing; a tool window's remembered place is fitted inside
 *  the screen, and a window that is no tool has none. */

#include <QAction>
#include <QApplication>
#include <QFontMetrics>
#include <QJsonArray>
#include <QLayout>
#include <QMdiArea>
#include <QMenu>
#include <QPointer>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "LayoutMenu.h"
#include "Shell.h"
#include "ShellChrome.h"
#include "ShellIcons.h"
#include "ShellOverflow.h"
#include "ShellRegionTabs.h"
#include "ShellToolsMenu.h"
#include "SubWindow.h"
#include "ToolWindowMemory.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

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

QJsonObject headerEntry(const ShellHeader* header, const QString& name)
{
	for (const QJsonValue& value : header->buttons())
	{
		if (value.toObject().value(QStringLiteral("name")).toString() == name) { return value.toObject(); }
	}
	return {};
}

QAction* item(const QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

} // namespace

class ShellHomesTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		ConfigManager::inst()->setWorkingDir(m_dir.path());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		init();  // nothing this run wrote is left for the next one
		LayoutMenu::setQuestion({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		// Every test starts from the first-run layout, with no personal layout put aside.
		for (const char* key : {"shell.layout", "shell.personal", "shell.layouts", "tools.geometry"})
		{
			ConfigManager::inst()->setValue(QStringLiteral("ui"), QLatin1String(key), QString());
		}
	}

	void theBottomRegionNamesItsPanelsAsTabs()
	{
		World world;
		QVERIFY(QTest::qWaitForWindowExposed(&world.shell));
		ShellRegionTabs* tabs = world.shell.regionTabs(ShellRegion::Bottom);
		QVERIFY(tabs != nullptr);
		QVERIFY(tabs->isVisible());
		const QStringList bottom{QStringLiteral("mixer"), QStringLiteral("detail"), QStringLiteral("devices"),
			QStringLiteral("automation"), QStringLiteral("pattern")};
		QCOMPARE(tabs->tabIds(), bottom);
		QCOMPARE(tabs->currentId(), QStringLiteral("detail"));
		QCOMPARE(tabs->bar()->tabText(0), QStringLiteral("Mixer"));
		QVERIFY(!tabs->bar()->accessibleName().isEmpty());
		QCOMPARE(tabs->bar()->focusPolicy(), Qt::TabFocus);
		// The Edit view's main panel is the note editor: it is no tab below itself.
		world.shell.setView(ShellView::Edit);
		QVERIFY(!tabs->tabIds().contains(QStringLiteral("detail")));
		QCOMPARE(tabs->currentId(), world.shell.model().openIn(ShellRegion::Bottom));
	}

	void aTabOpensItsPanelAndTheHideButtonClosesTheRegion()
	{
		World world;
		ShellRegionTabs* tabs = world.shell.regionTabs(ShellRegion::Bottom);
		QVERIFY(tabs->choose(QStringLiteral("mixer")));
		QVERIFY(world.shell.model().isVisible(QStringLiteral("mixer")));
		QVERIFY(world.content.value(QStringLiteral("mixer"))->isVisible());
		QCOMPARE(tabs->currentId(), QStringLiteral("mixer"));
		QVERIFY(!tabs->choose(QStringLiteral("arranger")));  // no tab of its own here
		// A click on a tab is the same choice.
		tabs->bar()->setCurrentIndex(static_cast<int>(tabs->tabIds().indexOf(QStringLiteral("devices"))));
		QVERIFY(world.shell.model().isVisible(QStringLiteral("devices")));
		ShellButton* hide = nullptr;
		for (ShellButton* button : tabs->findChildren<ShellButton*>())
		{
			if (button->accessibleName().startsWith(QStringLiteral("Hide"))) { hide = button; }
		}
		QVERIFY(hide != nullptr);
		hide->click();
		QVERIFY(!world.shell.model().isVisible(QStringLiteral("devices")));
		QVERIFY(!world.shell.findChild<QWidget*>(QStringLiteral("shellBottom"))->isVisible());
	}

	void aRegionWithOnePanelShowsNoTabs()
	{
		World world;
		for (ShellRegion region : {ShellRegion::Left, ShellRegion::Right})
		{
			ShellRegionTabs* tabs = world.shell.regionTabs(region);
			QVERIFY(tabs != nullptr);
			QCOMPARE(tabs->tabIds().size(), 1);
			QVERIFY(!tabs->isVisible());
		}
		QVERIFY(world.shell.regionTabs(ShellRegion::Center) == nullptr);
	}

	void aFloatingPanelLeavesTheTabs()
	{
		World world;
		ShellRegionTabs* tabs = world.shell.regionTabs(ShellRegion::Bottom);
		QVERIFY(world.shell.detachPanel(QStringLiteral("devices")));
		QVERIFY(!tabs->tabIds().contains(QStringLiteral("devices")));
		QVERIFY(world.shell.attachPanel(QStringLiteral("devices")));
		QVERIFY(tabs->tabIds().contains(QStringLiteral("devices")));
	}

	void aRowLabelsThenCompactsThenOverflows()
	{
		QWidget host;  // no layout: the test sets the row's width itself
		auto* row = new ShellOverflowRow(QStringLiteral("More"), &host);
		QList<ShellButton*> buttons;
		for (int i = 0; i < 6; ++i)
		{
			auto* button = new ShellButton;
			button->setIcon(shellicons::icon(QStringLiteral("mixer")));
			button->setText(QStringLiteral("Panel number %1").arg(i));
			row->addButton(button, i == 0 ? 1 : 0);
			buttons << button;
		}
		host.show();
		row->resize(2000, 32);
		QCOMPARE(row->fitting(), shelloverflow::Fit::Labelled);
		QCOMPARE(buttons.first()->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
		QVERIFY(!row->moreButton()->isVisible());
		const int labelled = row->layout()->sizeHint().width();
		row->resize(labelled - 1, 32);
		QCOMPARE(row->fitting(), shelloverflow::Fit::Compact);
		QCOMPARE(buttons.first()->toolButtonStyle(), Qt::ToolButtonIconOnly);
		// Narrowing step by step: the last button goes into More first, the one kept longest last. More lists its
		// buttons in row order, and one step can move two (the step that first shows More pays for More's own
		// width), so each step is read from its end: within a step the later button went first.
		QList<QToolButton*> order;
		for (int width = labelled; width > 0; width -= 3)
		{
			row->resize(width, 32);
			const QList<QToolButton*> overflowed = row->moreButton()->overflowed();
			for (auto gone = overflowed.crbegin(); gone != overflowed.crend(); ++gone)
			{
				if (!order.contains(*gone)) { order.append(*gone); }
			}
		}
		QCOMPARE(row->fitting(), shelloverflow::Fit::Overflowing);
		QVERIFY(row->moreButton()->isVisible());
		QCOMPARE(order.size(), buttons.size());
		QCOMPARE(order.first(), static_cast<QToolButton*>(buttons.last()));
		QCOMPARE(order.last(), static_cast<QToolButton*>(buttons.first()));
		// A button the user hid stays hidden, and More does not offer it.
		buttons.at(2)->setProperty(shelloverflow::userHiddenProperty(), true);
		row->resize(2000, 32);
		QCOMPARE(row->fitting(), shelloverflow::Fit::Labelled);
		QVERIFY(!buttons.at(2)->isVisible());
		QVERIFY(row->moreButton()->overflowed().isEmpty());
	}

	void aLabelPreservesAnEscapedAmpersand()
	{
		ShellButton button;
		button.setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		button.setText(QStringLiteral("Rock && Roll"));
		const QFontMetrics fm(tokens::font(tokens::TextRole::Small));
		const int expected = button.iconSize().width() + tokens::space::xs +
			fm.horizontalAdvance(QStringLiteral("Rock & Roll")) + 2 * tokens::space::md;
		QCOMPARE(button.sizeHint().width(), expected);
	}

	void theFooterHasAToolsHomeForEveryToolWindow()
	{
		World world;
		auto* tools = world.shell.findChild<ShellButton*>(QStringLiteral("footerTools"));
		QVERIFY(tools != nullptr);
		QVERIFY(!tools->accessibleName().isEmpty());
		QVERIFY(tools->menu() != nullptr);
		emit tools->menu()->aboutToShow();
		QStringList commands;
		int toggles = 0;
		for (const QAction* action : tools->menu()->actions())
		{
			if (action->isSeparator()) { continue; }
			commands << action->property("controlCommand").toString();
			toggles += action->isCheckable() ? 1 : 0;
		}
		QCOMPARE(commands.size(), shelltools::tools().size());
		QVERIFY(commands.contains(QStringLiteral("window.undo_history")));
		QVERIFY(commands.contains(QStringLiteral("window.tempo_map")));
		QCOMPARE(toggles, 4);  // the four window.toggle windows: controller rack, notes, microtuner, classic piano roll
		QCOMPARE(shelltools::state().size(), shelltools::tools().size());
	}

	void aNarrowHeaderPutsItsRightButtonsInMore()
	{
		World world;
		ShellHeader* header = world.shell.header();
		const qsizetype listed = header->buttons().size();
		header->strip()->resize(40, header->strip()->height());
		header->refit();
		QVERIFY(header->moreButton()->isVisible());
		const QJsonObject tools = headerEntry(header, QStringLiteral("project_tools"));
		QVERIFY(tools.value(QStringLiteral("overflowed")).toBool());
		QVERIFY(tools.value(QStringLiteral("visible")).toBool());  // the user still shows it: it is in More
		QCOMPARE(header->buttons().size(), listed);  // More is not one of the header's buttons
		header->strip()->resize(4000, header->strip()->height());
		header->refit();
		QVERIFY(!header->moreButton()->isVisible());
		QVERIFY(!headerEntry(header, QStringLiteral("project_tools")).value(QStringLiteral("overflowed")).toBool());
	}

	void theStateReportsTheRegionsAndTheFooterFit()
	{
		World world;
		const QJsonObject state = world.shell.state();
		const QJsonObject bottom = state.value(QStringLiteral("regions")).toObject().value(QStringLiteral("bottom")).toObject();
		QCOMPARE(bottom.value(QStringLiteral("open")).toString(), QStringLiteral("detail"));
		QVERIFY(bottom.value(QStringLiteral("tabs")).toArray().contains(QStringLiteral("mixer")));
		const QStringList fits{QStringLiteral("labelled"), QStringLiteral("compact"), QStringLiteral("overflowing")};
		QVERIFY(fits.contains(state.value(QStringLiteral("footer")).toString()));
	}

	void aProjectLayoutPutsThePersonalOneAsideOnce()
	{
		World world;
		world.shell.restoreConfiguredLayout();
		world.shell.setView(ShellView::Mix);  // the user's own layout
		QVERIFY(!Shell::personalAside());
		world.shell.setPersonalAside();
		world.shell.setView(ShellView::Edit);  // a project's layout on screen
		world.shell.setPersonalAside();        // a second project's: the user's stays aside
		QVERIFY(Shell::personalAside());
		QVERIFY(world.shell.restorePersonal());
		QCOMPARE(world.shell.model().view(), ShellView::Mix);
		QVERIFY(!Shell::personalAside());
		QVERIFY(!world.shell.restorePersonal());  // nothing aside: nothing changes
		QCOMPARE(world.shell.model().view(), ShellView::Mix);
	}

	void theNextStartPrefersThePersonalLayout()
	{
		{
			World world;
			world.shell.restoreConfiguredLayout();
			world.shell.setView(ShellView::Mix);
			world.shell.setPersonalAside();
			world.shell.setView(ShellView::Edit);  // persisted as the layout on screen
		}
		World again;
		again.shell.restoreConfiguredLayout();
		QCOMPARE(again.shell.model().view(), ShellView::Mix);
		QVERIFY(!Shell::personalAside());
	}

	void replacingASavedLayoutAsksFirst()
	{
		const QString layouts = QStringLiteral("{\"layouts\":{\"mine\":{\"view\":\"mix\"}}}");
		ConfigManager::inst()->setValue(QStringLiteral("ui"), QStringLiteral("shell.layouts"), layouts);
		QString asked;
		LayoutMenu::setQuestion([&asked](const QString& question) {
			asked = question;
			return false;
		});
		QMenu menu;
		LayoutMenu::populate(&menu);
		QAction* replace = item(&menu, QStringLiteral("Replace"));
		QVERIFY(replace != nullptr && replace->menu() != nullptr);
		QAction* mine = item(replace->menu(), QStringLiteral("mine"));
		QVERIFY(mine != nullptr);
		QCOMPARE(mine->property("controlCommand").toString(), QStringLiteral("window.layout_save"));
		mine->trigger();
		QVERIFY2(asked.contains(QStringLiteral("mine")), qPrintable(asked));
		// The answer was No: the saved layout is as it was.
		QCOMPARE(ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("shell.layouts")), layouts);
		// The menu says what a layout keeps and what it does not, and whose layouts these are.
		bool limits = false;
		for (const QAction* action : menu.actions())
		{
			limits = limits || (!action->isEnabled() && action->text().contains(QStringLiteral("not the zoom")));
		}
		QVERIFY(limits);
		LayoutMenu::setQuestion({});
	}

	void aToolWindowIsFittedIntoItsScreen()
	{
		const QRect area(0, 0, 1000, 800);
		QCOMPARE(toolwindows::fitted(QRect(100, 100, 300, 200), area), QRect(100, 100, 300, 200));
		QCOMPARE(toolwindows::fitted(QRect(5000, 5000, 300, 200), area), QRect(700, 600, 300, 200));
		QCOMPARE(toolwindows::fitted(QRect(-50, -50, 2000, 2000), area), area);
		QCOMPARE(toolwindows::fitted(QRect(10, 10, 30, 20), QRect()), QRect(10, 10, 30, 20));
		// A window that is no tool has no remembered place and is never placed from one.
		QWidget other;
		QVERIFY(toolwindows::keyOf(nullptr).isEmpty());
		QVERIFY(toolwindows::keyOf(&other).isEmpty());
		QVERIFY(!toolwindows::placeRemembered(&other));
		toolwindows::remember(&other);
		other.show();
		other.move(40, 40);
		QVERIFY(toolwindows::remembered().isEmpty());
	}

	//! The inherited SubWindow's float path asks the memory first: a window that is no tool still floats as before,
	//! centred, and nothing starts remembering it.
	void aWindowThatIsNoToolFloatsAsBefore()
	{
		const bool before = SubWindow::floatsOutsideWorkspace();
		SubWindow::setFloatOutsideWorkspace(true);
		QMdiArea area;
		area.resize(800, 600);
		area.show();
		auto* content = new QWidget;
		content->resize(300, 200);
		auto* window = new SubWindow(area.viewport());
		window->setWidget(content);
		area.addSubWindow(window);
		window->show();
		QVERIFY(content->isWindow());
		QVERIFY(content->findChild<QObject*>(QStringLiteral("zeneToolGeometry"), Qt::FindDirectChildrenOnly) == nullptr);
		QVERIFY(toolwindows::remembered().isEmpty());
		SubWindow::setFloatOutsideWorkspace(before);
	}

private:
	QTemporaryDir m_dir;
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
	ShellHomesTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "ShellHomesTest.moc"
