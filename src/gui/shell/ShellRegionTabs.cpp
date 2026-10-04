/*
 * ShellRegionTabs.cpp - the labelled tabs over a side region of the 0.5.0 shell
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

#include "ShellRegionTabs.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QSignalBlocker>
#include <QTabBar>

#include "Accessibility.h"
#include "ControlWindowCommands.h"
#include "DesignTokens.h"
#include "MainWindow.h"
#include "Shell.h"
#include "ShellChrome.h"
#include "ShellIcons.h"
#include "ShellOverflow.h"

namespace lmms::gui
{

namespace
{

constexpr ShellRegion kSideRegions[] = {ShellRegion::Left, ShellRegion::Bottom, ShellRegion::Right};

//! The tabs drawn from the tokens: muted names, the chosen one in full text, bold and underlined in the accent -
//! told by weight and the line as well as by colour.
QString tabStyle()
{
	const tokens::Palette& pal = tokens::palette();
	return QStringLiteral("QTabBar { background: transparent; }"
		"QTabBar::tab { background: transparent; color: %1; border: none; border-bottom: 2px solid transparent;"
		" padding: 0px %2px; min-height: %3px; }"
		"QTabBar::tab:selected { color: %4; font-weight: bold; border-bottom: 2px solid %5; }"
		"QTabBar::tab:hover:!selected { background: %6; }")
		.arg(pal.textMuted.name()).arg(tokens::space::md).arg(tokens::targetSize())
		.arg(pal.text.name(), pal.accent.name(), pal.controlHover.name());
}

ShellButton* stripButton(QWidget* parent, const QString& icon, const QString& name, const QString& description)
{
	auto* button = new ShellButton(parent);
	button->setIcon(shellicons::icon(icon));
	button->setToolTip(name);
	a11y::announce(button, name, description);
	button->setProperty("controlCommand", QStringLiteral("window.toggle"));
	return button;
}

QString tabTip(const ShellPanelSpec& spec, bool pinned)
{
	QString tip = spec.shortcut.isEmpty() ? spec.title : ShellRegionTabs::tr("%1 (%2)").arg(spec.title, spec.shortcut);
	if (pinned) { tip += ShellRegionTabs::tr(" - pinned: open in every view that offers it"); }
	return tip;
}

} // namespace

ShellRegionTabs::ShellRegionTabs(Shell* shell, ShellRegion region, QWidget* parent)
	: QWidget(parent)
	, m_shell(shell)
	, m_region(region)
{
	setObjectName(QStringLiteral("shellTabs_") + ShellLayout::regionName(region));
	setFixedHeight(tokens::targetSize() + tokens::space::xs);
	auto* row = new QHBoxLayout(this);
	row->setContentsMargins(tokens::space::xs, 0, tokens::space::xs, 0);
	row->setSpacing(tokens::space::xxs);

	m_bar = new QTabBar(this);
	m_bar->setObjectName(QStringLiteral("shellRegionTabBar"));
	m_bar->setDrawBase(false);
	m_bar->setExpanding(false);
	m_bar->setUsesScrollButtons(true);
	m_bar->setElideMode(Qt::ElideRight);
	m_bar->setFocusPolicy(Qt::TabFocus);
	m_bar->setProperty("zeneFocusRing", true);  // the shared focus ring (AccessibleWidgets.h)
	m_bar->setIconSize(QSize(tokens::size::icon, tokens::size::icon));
	m_bar->setFont(tokens::font(tokens::TextRole::Small));
	m_bar->setStyleSheet(tabStyle());
	a11y::announce(m_bar, tr("%1 panels").arg(regionTitle()),
		tr("One tab per panel that opens here. The arrow keys move between them, and the chosen one opens."));
	connect(m_bar, &QTabBar::currentChanged, this, &ShellRegionTabs::onCurrentChanged);
	row->addWidget(m_bar, 1);

	m_list = stripButton(this, QStringLiteral("chevron"), tr("Every %1 panel").arg(regionTitle().toLower()),
		tr("Lists every panel that opens here, those that do not fit as tabs and those in a window of their own too."));
	m_list->setPopupMode(QToolButton::InstantPopup);
	auto* menu = new QMenu(m_list);
	connect(menu, &QMenu::aboutToShow, this, [this, menu] { fillList(menu); });
	m_list->setMenu(menu);
	row->addWidget(m_list);

	m_hide = stripButton(this, QStringLiteral("close"), tr("Hide the %1 panels").arg(regionTitle().toLower()),
		tr("Closes this area; its switch in the footer opens it again."));
	connect(m_hide, &QToolButton::clicked, this, [this] {
		if (const QString open = currentId(); !open.isEmpty()) { toggle(open); }
	});
	row->addWidget(m_hide);
	hide();
}

QString ShellRegionTabs::regionTitle() const
{
	switch (m_region)
	{
	case ShellRegion::Left: return tr("Left");
	case ShellRegion::Bottom: return tr("Bottom");
	case ShellRegion::Right: return tr("Right");
	case ShellRegion::Center: break;
	}
	return tr("Centre");
}

QStringList ShellRegionTabs::panelsIn(const Shell& shell, ShellRegion region)
{
	const ShellView view = shell.model().view();
	QStringList out;
	for (const ShellPanelSpec& spec : ShellLayout::panels())
	{
		const bool here = spec.home == region && ShellLayout::offers(view, spec.id) && spec.id != ShellLayout::centerOf(view);
		if (here && !shell.isDetached(spec.id)) { out << spec.id; }
	}
	return out;
}

QStringList ShellRegionTabs::tabIds() const
{
	QStringList out;
	for (int i = 0; i < m_bar->count(); ++i) { out << m_bar->tabData(i).toString(); }
	return out;
}

QString ShellRegionTabs::currentId() const
{
	return m_bar->currentIndex() >= 0 ? m_bar->tabData(m_bar->currentIndex()).toString() : QString();
}

void ShellRegionTabs::rebuild(const QStringList& ids)
{
	while (m_bar->count() > 0) { m_bar->removeTab(0); }
	for (const QString& id : ids)
	{
		const int index = m_bar->addTab(shellicons::icon(id), ShellLayout::spec(id).title);
		m_bar->setTabData(index, id);
	}
}

void ShellRegionTabs::sync()
{
	const QStringList ids = panelsIn(*m_shell, m_region);
	const QString open = m_shell->model().openIn(m_region);
	m_syncing = true;
	{
		const QSignalBlocker block(m_bar);
		if (tabIds() != ids) { rebuild(ids); }
		for (int i = 0; i < m_bar->count(); ++i)
		{
			const QString id = ids.at(i);
			m_bar->setTabToolTip(i, tabTip(ShellLayout::spec(id), m_shell->model().isPinned(id)));
		}
		if (const int index = static_cast<int>(ids.indexOf(open)); index >= 0) { m_bar->setCurrentIndex(index); }
	}
	m_syncing = false;
	setVisible(ids.size() >= 2);
}

void ShellRegionTabs::toggle(const QString& id)
{
	const MainWindow* main = controlMainWindow();
	if (main == nullptr || main->shell() != m_shell)
	{
		m_shell->togglePanel(id);
		return;
	}
	dispatchWindowToggle(id);
}

bool ShellRegionTabs::choose(const QString& id)
{
	if (!tabIds().contains(id)) { return false; }
	if (!m_shell->model().isVisible(id)) { toggle(id); }
	sync();  // a refused or unreachable command leaves the tabs saying what is really open
	return true;
}

void ShellRegionTabs::onCurrentChanged(int index)
{
	if (m_syncing || index < 0) { return; }
	choose(m_bar->tabData(index).toString());
}

void ShellRegionTabs::fillList(QMenu* menu)
{
	menu->clear();
	const QStringList docked = panelsIn(*m_shell, m_region);
	for (const ShellPanelSpec& spec : ShellLayout::panels())
	{
		const bool floating = spec.home == m_region && m_shell->isDetached(spec.id);
		if (!docked.contains(spec.id) && !floating) { continue; }
		QAction* action = menu->addAction(shellicons::icon(spec.id),
			floating ? tr("%1 - in its own window").arg(spec.title) : spec.title);
		action->setProperty("controlCommand", QStringLiteral("window.toggle"));
		action->setCheckable(true);
		action->setChecked(floating || m_shell->model().isVisible(spec.id));
		const QString id = spec.id;
		// A floating panel's entry brings its window forward (Shell::togglePanel); a docked one opens it here.
		connect(action, &QAction::triggered, this, [this, id, floating] {
			if (floating || !m_shell->model().isVisible(id)) { toggle(id); }
			sync();
		});
	}
}

void ShellRegionTabs::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), tokens::palette().raised);
	p.fillRect(QRect(0, height() - 1, width(), 1), tokens::palette().separator);
}

QJsonObject Shell::chromeState() const
{
	QJsonObject regions;
	for (ShellRegion region : kSideRegions)
	{
		regions.insert(ShellLayout::regionName(region), QJsonObject{
			{QStringLiteral("open"), m_layout.openIn(region)},
			{QStringLiteral("tabs"), QJsonArray::fromStringList(ShellRegionTabs::panelsIn(*this, region))}});
	}
	const ShellOverflowRow* row = m_footer != nullptr ? m_footer->switches() : nullptr;
	return QJsonObject{{QStringLiteral("regions"), regions},
		{QStringLiteral("footer"), row != nullptr ? shelloverflow::fitName(row->fitting()) : QString()}};
}

} // namespace lmms::gui
