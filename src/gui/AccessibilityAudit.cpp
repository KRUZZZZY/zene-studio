/*
 * AccessibilityAudit.cpp - which controls a screen reader cannot name, and names for them
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

#include "AccessibilityAudit.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QTextDocument>

#include "Model.h"
#include "ModelView.h"

namespace lmms::gui
{

namespace
{

constexpr int MaxListed = 50;

//! A control a person operates: what a screen reader must be able to name.
bool interactive(const QWidget* widget)
{
	return qobject_cast<const QAbstractButton*>(widget) != nullptr || qobject_cast<const QAbstractSlider*>(widget) != nullptr
		|| qobject_cast<const QAbstractSpinBox*>(widget) != nullptr || qobject_cast<const QComboBox*>(widget) != nullptr
		|| qobject_cast<const QLineEdit*>(widget) != nullptr || qobject_cast<const QAbstractItemView*>(widget) != nullptr
		|| dynamic_cast<const ModelView*>(widget) != nullptr || (widget->focusPolicy() & Qt::TabFocus) != 0;
}

QString plain(const QString& text)
{
	if (!Qt::mightBeRichText(text)) { return text.simplified(); }
	QTextDocument document;
	document.setHtml(text);
	return document.toPlainText().simplified();
}

QJsonObject entryOf(const QWidget* widget)
{
	return {{QStringLiteral("class"), QString::fromLatin1(widget->metaObject()->className())},
		{QStringLiteral("object"), widget->objectName()},
		{QStringLiteral("parent"), widget->parentWidget() != nullptr ? widget->parentWidget()->objectName() : QString()}};
}

} // namespace

QString impliedAccessibleName(const QWidget* widget)
{
	if (const auto* view = dynamic_cast<const ModelView*>(widget); view != nullptr && view->model() != nullptr
		&& !view->model()->displayName().isEmpty())
	{
		return view->model()->displayName();
	}
	if (!widget->toolTip().isEmpty()) { return plain(widget->toolTip()); }
	const auto* button = qobject_cast<const QAbstractButton*>(widget);
	return button != nullptr ? plain(button->text()).remove(QLatin1Char('&')) : QString();
}

QJsonObject auditAccessibility(QWidget* root, bool fill)
{
	int checked = 0;
	int named = 0;
	int filled = 0;
	int implicit = 0;
	QJsonArray unnamed;
	for (QWidget* widget : root->findChildren<QWidget*>())
	{
		if (!widget->isVisible() || !interactive(widget)) { continue; }
		++checked;
		if (!widget->accessibleName().isEmpty()) { ++named; continue; }
		const QString implied = impliedAccessibleName(widget);
		if (!implied.isEmpty())
		{
			if (fill) { widget->setAccessibleName(implied); }
			++(fill ? filled : implicit);
			continue;
		}
		if (unnamed.size() < MaxListed) { unnamed.append(entryOf(widget)); }
	}
	return {{QStringLiteral("checked"), checked}, {QStringLiteral("named"), named}, {QStringLiteral("filled"), filled},
		{QStringLiteral("implied"), implicit}, {QStringLiteral("unnamed"), unnamed}};
}

} // namespace lmms::gui
