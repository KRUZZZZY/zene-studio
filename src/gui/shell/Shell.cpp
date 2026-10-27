/*
 * Shell.cpp - the 0.5.0 single-window shell: header, four-region body, footer
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

#include "Shell.h"

#include <utility>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QSet>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "ControlWindowCommands.h"
#include "DesignTokens.h"
#include "ShellChrome.h"
#include "ShellIcons.h"
#include "ArrangerNavigator.h"
#include "ArrangerOverlays.h"
#include "ModifierTools.h"
#include "GuiApplication.h"
#include "MacroMenu.h"
#include "SongEditor.h"
#include "MenuBarExtras.h"
#include "RecordLaunchMenus.h"
#include "ContextHelp.h"
#include "HelpCenter.h"
#include "LayoutMenu.h"
#include "PanelShortcuts.h"
#include "ShellPanel.h"
#include "ShellRegionTabs.h"
#include "ShortcutMap.h"
#include "UiConfigStore.h"
#include "WorkspaceMenus.h"

namespace lmms::gui
{

namespace
{

const QString kConfigKey = QStringLiteral("shell.layout");
const QString kLauncher = QStringLiteral("launcher");

QWidget* makeHost(QWidget* parent, const QString& name)
{
	auto* host = new QWidget(parent);
	host->setObjectName(name);
	auto* box = new QVBoxLayout(host);
	box->setContentsMargins(0, 0, 0, 0);
	box->setSpacing(0);
	return host;
}

QJsonArray sizesOf(const QSplitter* splitter)
{
	QJsonArray out;
	for (int s : splitter->sizes()) { out.append(s); }
	return out;
}

void applySizes(QSplitter* splitter, const QJsonArray& sizes)
{
	if (sizes.size() != splitter->count()) { return; }
	QList<int> list;
	for (const QJsonValue& v : sizes) { list << std::max(0, v.toInt()); }
	splitter->setSizes(list);
}

//! The shell panel @a w sits in (looking no higher than @a stop), or nullptr.
ShellPanel* panelOf(QWidget* w, const QWidget* stop)
{
	for (; w != nullptr && w != stop; w = w->parentWidget())
	{
		if (auto* frame = qobject_cast<ShellPanel*>(w)) { return frame; }
	}
	return nullptr;
}

} // namespace

Shell::Shell(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("shell"));
	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	m_header = new ShellHeader(this);
	outer->addWidget(m_header);
	buildBody();
	outer->addWidget(m_columns, 1);
	buildActions();
	buildFloating();
	m_footer = new ShellFooter(viewActions(), panelActions(), this);
	outer->addWidget(m_footer);

	connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) { onFocusChanged(now); });
	apply();
}

Shell::~Shell() = default;

void Shell::showEvent(QShowEvent* event)
{
	QWidget::showEvent(event);
	if (m_shownOnce) { return; }
	m_shownOnce = true;
	// The floating panels a restored layout listed open once the main window is on screen.
	if (!m_pendingFloats.isEmpty())
	{
		QTimer::singleShot(0, this, [this]() { restoreFloating(std::exchange(m_pendingFloats, QJsonArray())); });
	}
	// The user's customisations, once the menus and the header exist: the shortcut overrides
	// (ShortcutMap) and the header's buttons (ui/header).
	QTimer::singleShot(0, this, [this]() {
		if (auto* main = qobject_cast<QMainWindow*>(window()))
		{
			installNavigateMenu(main->menuBar());
			installRecordLaunchMenus(main->menuBar());  // 0.5.0 L03 / L02
			installHelpEntries(main->menuBar());  // 0.5.0 L14: What's This?, the manual, tours, appearance
			installWorkspaceMenus(main->menuBar());  // 0.5.0 W3 L13 / L14: the Window menu, What's New, Ask
			ContextHelp::install();
			if (getGUI() != nullptr && getGUI()->songEditor() != nullptr)
			{
				installArrangerOverlays(getGUI()->songEditor()->m_editor);
				installArrangerNavigator(getGUI()->songEditor());  // 0.5.0 L04 (ED-053)
				modifiertools::install();  // 0.5.0 L04 (ED-014)
			}
			MacroMenu::install(main->menuBar());
			LayoutMenu::followNewProjects(this);  // theme 22: a new project brings the personal layout back
			ShortcutMap::apply(main->menuBar());
			panelshortcuts::install();  // 0.5.0 L13 stage 1: the four panels' own scoped keys
		}
		m_header->customise(uistore::read(QStringLiteral("header")));
	});
	// The offscreen platform's nominal screen is 800 x 800, and the window opens maximized onto
	// it (main.cpp), but window.screenshot there is how an agent sees the interface: give the
	// window the working size once it exists. A real screen keeps the user's window state.
	if (QGuiApplication::platformName() == QLatin1String("offscreen") && window() != this)
	{
		QWidget* top = window();
		QTimer::singleShot(0, top, [top]() {
			top->showNormal();
			top->resize(top->size().expandedTo(QSize(1600, 1000)));
		});
	}
}

void Shell::buildBody()
{
	m_columns = new QSplitter(Qt::Horizontal, this);
	m_rows = new QSplitter(Qt::Vertical, m_columns);
	m_centre = new QSplitter(Qt::Horizontal, m_rows);
	for (QSplitter* s : {m_columns, m_rows, m_centre})
	{
		s->setChildrenCollapsible(false);
		s->setHandleWidth(2);
	}

	m_hosts.insert(static_cast<int>(ShellRegion::Left), makeHost(m_columns, QStringLiteral("shellLeft")));
	m_columns->addWidget(m_hosts.value(static_cast<int>(ShellRegion::Left)));
	m_columns->addWidget(m_rows);
	m_hosts.insert(static_cast<int>(ShellRegion::Right), makeHost(m_columns, QStringLiteral("shellRight")));
	m_columns->addWidget(m_hosts.value(static_cast<int>(ShellRegion::Right)));

	m_launcherHost = makeHost(m_centre, QStringLiteral("shellLauncher"));
	m_centre->addWidget(m_launcherHost);
	m_hosts.insert(static_cast<int>(ShellRegion::Center), makeHost(m_centre, QStringLiteral("shellCenter")));
	m_centre->addWidget(m_hosts.value(static_cast<int>(ShellRegion::Center)));
	m_rows->addWidget(m_centre);
	m_hosts.insert(static_cast<int>(ShellRegion::Bottom), makeHost(m_rows, QStringLiteral("shellBottom")));
	m_rows->addWidget(m_hosts.value(static_cast<int>(ShellRegion::Bottom)));

	m_hosts.value(static_cast<int>(ShellRegion::Left))->setMinimumWidth(200);
	m_hosts.value(static_cast<int>(ShellRegion::Right))->setMinimumWidth(220);
	m_hosts.value(static_cast<int>(ShellRegion::Bottom))->setMinimumHeight(120);
	m_columns->setStretchFactor(1, 1);
	m_rows->setStretchFactor(0, 1);
	m_centre->setStretchFactor(1, 1);
	m_columns->setSizes({tokens::size::inspectorWidth, 1000, tokens::size::browserWidth});
	m_rows->setSizes({600, tokens::size::bottomHeight});
	m_centre->setSizes({360, 1000});

	m_parking = new QWidget(this);
	m_parking->setObjectName(QStringLiteral("shellParking"));
	m_parking->hide();
	for (const ShellPanelSpec& spec : ShellLayout::panels())
	{
		auto* frame = new ShellPanel(spec.id, spec.title, m_parking);
		m_panels.insert(spec.id, frame);
		addFloatButton(frame);
	}
	// Theme 8: each side region names the panels it holds, a tab each, above the open one.
	for (ShellRegion region : {ShellRegion::Left, ShellRegion::Bottom, ShellRegion::Right})
	{
		QWidget* host = m_hosts.value(static_cast<int>(region));
		auto* tabs = new ShellRegionTabs(this, region, host);
		host->layout()->addWidget(tabs);
		m_tabs.insert(static_cast<int>(region), tabs);
	}
	for (QSplitter* s : {m_columns, m_rows, m_centre})
	{
		connect(s, &QSplitter::splitterMoved, this, [this]() { persist(); });
	}
}

void Shell::buildActions()
{
	m_viewGroup = new QActionGroup(this);
	m_viewGroup->setExclusive(true);
	const QStringList titles{tr("&Arrange"), tr("Mi&x"), tr("&Edit")};
	int index = 0;
	for (ShellView v : {ShellView::Arrange, ShellView::Mix, ShellView::Edit})
	{
		auto* action = new QAction(titles.at(index), this);
		action->setCheckable(true);
		action->setShortcut(QKeySequence(QStringLiteral("Alt+%1").arg(index + 1)));
		action->setToolTip(tr("%1 view (Alt+%2)").arg(QString(titles.at(index)).remove(QLatin1Char('&'))).arg(index + 1));
		action->setProperty("controlCommand", QStringLiteral("window.set_view"));
		const QString name = ShellLayout::viewName(v);
		// The checks follow the layout, not the click: a refused command leaves them as they were (review ux08 #101).
		connect(action, &QAction::triggered, this, [this, name]() {
			dispatchShellCommand(QStringLiteral("window.set_view"), QJsonObject{{QStringLiteral("view"), name}});
			syncActions();
		});
		m_viewGroup->addAction(action);
		addAction(action);
		m_viewActions.insert(static_cast<int>(v), action);
		++index;
	}

	for (const ShellPanelSpec& spec : ShellLayout::panels())
	{
		auto* action = new QAction(shellicons::icon(spec.id), spec.title, this);
		action->setCheckable(true);
		if (!spec.shortcut.isEmpty()) { action->setShortcut(QKeySequence(spec.shortcut)); }
		action->setToolTip(spec.shortcut.isEmpty() ? spec.title
			: tr("%1 (%2)").arg(spec.title, spec.shortcut));
		action->setProperty("controlCommand", QStringLiteral("window.toggle"));
		// The footer groups its buttons by this; the arranger is a view, not a footer switch.
		const bool inFooter = spec.id != QStringLiteral("arranger");
		action->setProperty("shellRegion", inFooter ? ShellLayout::regionName(spec.home) : QString());
		const QString id = spec.id;
		connect(action, &QAction::triggered, this, [this, id]() {
			dispatchWindowToggle(id);
			syncActions();
		});
		addAction(action);
		m_panelActions.insert(spec.id, action);
	}
}

QList<QAction*> Shell::viewActions() const
{
	return {m_viewActions.value(0), m_viewActions.value(1), m_viewActions.value(2)};
}

QList<QAction*> Shell::panelActions() const
{
	QList<QAction*> out;
	for (const ShellPanelSpec& spec : ShellLayout::panels()) { out << m_panelActions.value(spec.id); }
	return out;
}

QWidget* Shell::toolStrip() const { return m_header->strip(); }

void Shell::mount(const QString& id, QWidget* content)
{
	if (ShellPanel* frame = m_panels.value(id)) { frame->setContent(content); }
}

void Shell::placeInto(QWidget* host, ShellPanel* frame)
{
	if (frame->parentWidget() != host)
	{
		frame->setParent(host);
		host->layout()->addWidget(frame);
	}
	frame->show();
}

QWidget* Shell::hostFor(const ShellPanelSpec& spec) const
{
	if (spec.id == ShellLayout::centerOf(m_layout.view())) { return m_hosts.value(static_cast<int>(ShellRegion::Center)); }
	if (spec.id == kLauncher) { return m_layout.launcherBesideCenter() ? m_launcherHost : nullptr; }
	return m_layout.isVisible(spec.id) ? m_hosts.value(static_cast<int>(spec.home)) : nullptr;
}

void Shell::apply()
{
	QHash<QString, QWidget*> target;
	for (const ShellPanelSpec& spec : ShellLayout::panels()) { target.insert(spec.id, hostFor(spec)); }
	const ShellPanel* hadFocus = panelOf(QApplication::focusWidget(), this);
	// Park first, so a region never holds its old panel and its new one at once.
	parkUnused(target);
	QSet<QWidget*> used;
	for (auto it = target.cbegin(); it != target.cend(); ++it)
	{
		if (it.value() == nullptr || m_floating.contains(it.key())) { continue; }
		placeInto(it.value(), m_panels.value(it.key()));
		used.insert(it.value());
	}
	for (QWidget* host : {m_hosts.value(static_cast<int>(ShellRegion::Left)), m_hosts.value(static_cast<int>(ShellRegion::Right)),
		m_hosts.value(static_cast<int>(ShellRegion::Bottom)), m_launcherHost})
	{
		host->setVisible(used.contains(host));
	}
	// A view switch that parked the focused panel hands the keys to the new centre, not to whatever
	// widget Qt's focus chain finds next (review ux08 #100).
	if (hadFocus != nullptr && hadFocus->parentWidget() == m_parking) { focusPanel(ShellLayout::centerOf(m_layout.view())); }
	syncCentrePlaceholder();
	syncActions();
	for (ShellRegionTabs* tabs : m_tabs) { tabs->sync(); }
	persist();
	emit layoutChanged();
}

void Shell::parkUnused(const QHash<QString, QWidget*>& target)
{
	for (auto it = m_panels.cbegin(); it != m_panels.cend(); ++it)
	{
		// A panel in its own window (detachPanel) stays there whatever the layout says.
		if (m_floating.contains(it.key())) { continue; }
		if (target.value(it.key()) == nullptr && it.value()->parentWidget() != m_parking)
		{
			it.value()->hide();
			it.value()->setParent(m_parking);
		}
	}
}

void Shell::focusPanel(const QString& id)
{
	if (ShellPanel* frame = m_panels.value(id); frame != nullptr && frame->content() != nullptr)
	{
		frame->content()->setFocus();
	}
}

void Shell::syncActions()
{
	for (auto it = m_viewActions.cbegin(); it != m_viewActions.cend(); ++it)
	{
		it.value()->setChecked(it.key() == static_cast<int>(m_layout.view()));
	}
	for (auto it = m_panelActions.cbegin(); it != m_panelActions.cend(); ++it)
	{
		// A panel in its own window is on screen too: its switch brings the window forward (togglePanel).
		it.value()->setChecked(m_layout.isVisible(it.key()) || isDetached(it.key()));
	}
	for (ShellPanel* frame : m_panels) { frame->setPinnedMark(m_layout.isPinned(frame->id())); }
}

ShellView Shell::viewOffering(const QString& id)
{
	// Only the arranger and the launcher are missing from some view, and Arrange offers both.
	Q_UNUSED(id)
	return ShellView::Arrange;
}

void Shell::setView(ShellView view)
{
	m_layout.setView(view);
	apply();
}

bool Shell::togglePanel(const QString& id, QString* reason)
{
	if (!ShellLayout::isPanel(id))
	{
		if (reason != nullptr) { *reason = tr("there is no panel called \"%1\"").arg(id); }
		return false;
	}
	if (isDetached(id)) { return raiseFloating(id); }  // its own window: show it, never hide it unseen
	if (!ShellLayout::offers(m_layout.view(), id)) { return revealPanel(id); }
	const bool visible = m_layout.toggle(id, nullptr);
	apply();
	return visible;
}

bool Shell::setPanelPinned(const QString& id, bool pinned)
{
	if (!m_layout.setPinned(id, pinned)) { return false; }
	apply();
	return true;
}

bool Shell::revealPanel(const QString& id)
{
	if (!ShellLayout::isPanel(id)) { return false; }
	if (isDetached(id)) { return raiseFloating(id); }
	if (!ShellLayout::offers(m_layout.view(), id)) { m_layout.setView(viewOffering(id)); }
	m_layout.open(id, nullptr);
	apply();
	focusPanel(id);
	return m_layout.isVisible(id);
}

QJsonObject Shell::state() const
{
	QJsonArray panels;
	for (const ShellPanelSpec& spec : ShellLayout::panels())
	{
		panels.append(QJsonObject{
			{QStringLiteral("id"), spec.id},
			{QStringLiteral("title"), spec.title},
			{QStringLiteral("region"), ShellLayout::regionName(m_layout.regionOf(spec.id))},
			{QStringLiteral("visible"), m_layout.isVisible(spec.id)},
			{QStringLiteral("available"), ShellLayout::offers(m_layout.view(), spec.id)},
			{QStringLiteral("shortcut"), spec.shortcut},
			{QStringLiteral("mounted"), m_panels.value(spec.id)->content() != nullptr},
			{QStringLiteral("floating"), isDetached(spec.id)},
			{QStringLiteral("pinned"), m_layout.isPinned(spec.id)}});
	}
	QJsonObject out{
		{QStringLiteral("view"), ShellLayout::viewName(m_layout.view())},
		{QStringLiteral("panels"), panels},
		{QStringLiteral("focused"), m_layout.focused()},
		{QStringLiteral("launcher"), m_layout.launcherBesideCenter()}};
	const QJsonObject chrome = chromeState();
	for (auto it = chrome.begin(); it != chrome.end(); ++it) { out.insert(it.key(), it.value()); }
	return out;
}

void Shell::onFocusChanged(QWidget* now)
{
	const ShellPanel* focused = panelOf(now, this);
	if (focused == nullptr) { return; }
	const QString id = focused->id();
	m_layout.setFocused(id);
	for (ShellPanel* frame : m_panels) { frame->setFocusedPanel(frame->id() == id); }
}

QJsonObject Shell::snapshot() const
{
	QJsonObject doc = QJsonObject::fromVariantMap(m_layout.save());
	doc.insert(QStringLiteral("columns"), sizesOf(m_columns));
	doc.insert(QStringLiteral("rows"), sizesOf(m_rows));
	doc.insert(QStringLiteral("centre"), sizesOf(m_centre));
	doc.insert(QStringLiteral("floating"), floatingSnapshot());
	return doc;
}

void Shell::restoreSnapshot(const QJsonObject& doc)
{
	m_restoring = true;
	m_layout.restore(doc.toVariantMap());
	applySizes(m_columns, doc.value(QStringLiteral("columns")).toArray());
	applySizes(m_rows, doc.value(QStringLiteral("rows")).toArray());
	applySizes(m_centre, doc.value(QStringLiteral("centre")).toArray());
	// A layout saved before 0.5.0 G has no `floating`: the panels floating now stay as they are.
	if (doc.contains(QStringLiteral("floating"))) { restoreFloating(doc.value(QStringLiteral("floating")).toArray()); }
	m_restoring = false;
	apply();
}

void Shell::persist() const
{
	// Nothing is written until the saved layout has been read: the first apply() in the
	// constructor would otherwise overwrite the user's layout with the default.
	if (m_restoring || !m_restoreAttempted) { return; }
	ConfigManager::inst()->setValue(QStringLiteral("ui"), kConfigKey,
		QString::fromUtf8(QJsonDocument(snapshot()).toJson(QJsonDocument::Compact)));
}

void Shell::restoreConfiguredLayout()
{
	m_restoreAttempted = true;
	// Theme 22: a personal layout put aside for a project's own (ShellLayoutScope.cpp) is the one to start with; the
	// project, when it opens again, puts its own back over it.
	const QString aside = ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("shell.personal"));
	const QString text = aside.isEmpty() ? ConfigManager::inst()->value(QStringLiteral("ui"), kConfigKey) : aside;
	dropPersonalAside();
	const QJsonObject doc = QJsonDocument::fromJson(text.toUtf8()).object();
	if (doc.isEmpty())
	{
		persist();
		return;
	}
	restoreSnapshot(doc);
}

} // namespace lmms::gui
