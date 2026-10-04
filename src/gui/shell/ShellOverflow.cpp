/*
 * ShellOverflow.cpp - labelled, compact and overflowing rows of the 0.5.0 shell's chrome
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

#include "ShellOverflow.h"

#include <algorithm>

#include <QAction>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>

#include "Accessibility.h"
#include "DesignTokens.h"
#include "ShellIcons.h"

namespace lmms::gui
{

namespace shelloverflow
{

namespace
{

bool userHidden(const QToolButton* button)
{
	return button->property(userHiddenProperty()).toBool();
}

void setStyle(const QList<QToolButton*>& buttons, Qt::ToolButtonStyle style)
{
	for (QToolButton* button : buttons) { button->setToolButtonStyle(style); }
}

} // namespace

const char* userHiddenProperty()
{
	return "zeneUserHidden";
}

Fit fit(const QList<QToolButton*>& buttons, ShellOverflowButton* more, int available, const std::function<int()>& measure,
	bool labels)
{
	for (QToolButton* button : buttons) { button->setVisible(!userHidden(button)); }
	more->setOverflowed({});
	if (labels)
	{
		setStyle(buttons, Qt::ToolButtonTextBesideIcon);
		if (measure() <= available) { return Fit::Labelled; }
	}
	setStyle(buttons, Qt::ToolButtonIconOnly);
	if (measure() <= available) { return Fit::Compact; }
	more->show();
	QList<QToolButton*> hidden;
	for (auto it = buttons.crbegin(); it != buttons.crend() && measure() > available; ++it)
	{
		if ((*it)->isHidden()) { continue; }
		(*it)->hide();
		hidden.prepend(*it);
	}
	more->setOverflowed(hidden);
	return hidden.isEmpty() ? Fit::Compact : Fit::Overflowing;
}

QString fitName(Fit fit)
{
	switch (fit)
	{
	case Fit::Labelled: return QStringLiteral("labelled");
	case Fit::Compact: return QStringLiteral("compact");
	case Fit::Overflowing: return QStringLiteral("overflowing");
	}
	return {};
}

} // namespace shelloverflow

namespace
{

//! The More menu's entry for a button that has no action and does not just open a menu: it clicks the button, and
//! declares what the button declares (the agent-surface gate reads the menu as it reads the strip).
void addClickEntry(QMenu* menu, QToolButton* button)
{
	const QString label = button->toolTip().isEmpty() ? button->text() : button->toolTip();
	QAction* entry = menu->addAction(button->icon(), label);
	entry->setProperty("controlCommand", button->property("controlCommand"));
	entry->setCheckable(button->isCheckable());
	entry->setChecked(button->isChecked());
	QObject::connect(entry, &QAction::triggered, button, [button] { button->click(); });
}

void addEntry(QMenu* menu, QToolButton* button)
{
	if (QAction* action = button->defaultAction())
	{
		menu->addAction(action);
		return;
	}
	if (QMenu* own = button->menu(); own != nullptr && button->popupMode() == QToolButton::InstantPopup)
	{
		own->setTitle(button->toolTip().isEmpty() ? button->text() : button->toolTip());
		own->setIcon(button->icon());
		menu->addMenu(own);
		return;
	}
	addClickEntry(menu, button);
}

} // namespace

ShellOverflowButton::ShellOverflowButton(const QString& name, QWidget* parent)
	: ShellButton(parent)
{
	setIcon(shellicons::icon(QStringLiteral("chevron")));
	setToolTip(name);
	a11y::announce(this, name, tr("The controls that do not fit here, one click away."));
	setPopupMode(QToolButton::InstantPopup);
	auto* menu = new QMenu(this);
	connect(menu, &QMenu::aboutToShow, this, [this, menu] { fillMenu(menu); });
	setMenu(menu);
	hide();
}

void ShellOverflowButton::setOverflowed(const QList<QToolButton*>& buttons)
{
	m_overflowed.clear();
	for (QToolButton* button : buttons) { m_overflowed.append(button); }
	setVisible(!buttons.isEmpty());
}

QList<QToolButton*> ShellOverflowButton::overflowed() const
{
	QList<QToolButton*> out;
	for (const QPointer<QToolButton>& button : m_overflowed)
	{
		if (!button.isNull()) { out.append(button.data()); }
	}
	return out;
}

void ShellOverflowButton::fillMenu(QMenu* menu) const
{
	menu->clear();
	for (QToolButton* button : overflowed()) { addEntry(menu, button); }
}

ShellOverflowRow::ShellOverflowRow(const QString& moreName, QWidget* parent)
	: QWidget(parent)
{
	m_row = new QHBoxLayout(this);
	m_row->setContentsMargins(0, 0, 0, 0);
	m_row->setSpacing(tokens::space::xxs);
	m_more = new ShellOverflowButton(moreName, this);
	m_row->addWidget(m_more);
	m_row->addStretch(1);
}

void ShellOverflowRow::addButton(QToolButton* button, int keep)
{
	button->setParent(this);
	m_row->insertWidget(m_row->indexOf(m_more), button);
	m_buttons.append({button, keep});
}

void ShellOverflowRow::addSpacing(int pixels)
{
	m_row->insertSpacing(m_row->indexOf(m_more), pixels);
}

void ShellOverflowRow::setLabels(bool on)
{
	m_labels = on;
	refit();
}

QList<QToolButton*> ShellOverflowRow::hideOrder() const
{
	// The last of the list hides first: the highest keep first, in row order within a keep.
	QList<QPair<QToolButton*, int>> sorted = m_buttons;
	std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
	QList<QToolButton*> out;
	for (const auto& entry : sorted) { out.append(entry.first); }
	return out;
}

void ShellOverflowRow::refit()
{
	if (m_fitting || m_buttons.isEmpty()) { return; }
	m_fitting = true;
	m_fit = shelloverflow::fit(hideOrder(), m_more, width(), [this] {
		m_row->invalidate();
		return m_row->sizeHint().width();
	}, m_labels);
	m_fitting = false;
}

QSize ShellOverflowRow::minimumSizeHint() const
{
	return {m_more->sizeHint().width(), m_more->sizeHint().height()};
}

void ShellOverflowRow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	refit();
}

} // namespace lmms::gui
