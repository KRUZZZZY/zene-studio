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
#include <QAccessible>
#include <QComboBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QMetaEnum>
#include <QTextDocument>

#include "AccessibleWidgets.h"
#include "AutomatableModel.h"
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

//! A control that shows a value a screen reader must say: a slider, a spin box, a combo box, or a parameter
//! control whose parameter is not an on/off one (a knob, a fader, an LCD number, a model combo box).
bool showsValue(const QWidget* widget)
{
	if (qobject_cast<const QAbstractSlider*>(widget) != nullptr || qobject_cast<const QAbstractSpinBox*>(widget) != nullptr
		|| qobject_cast<const QComboBox*>(widget) != nullptr)
	{
		return true;
	}
	const auto* view = dynamic_cast<const ModelView*>(widget);
	const auto* model = view != nullptr ? dynamic_cast<const AutomatableModel*>(view->model()) : nullptr;
	return model != nullptr && dynamic_cast<const BoolModel*>(model) == nullptr && qobject_cast<const QAbstractButton*>(widget) == nullptr;
}

//! A role that tells a screen reader nothing about what the control is.
bool genericRole(QAccessible::Role role)
{
	return role == QAccessible::NoRole || role == QAccessible::Client || role == QAccessible::Pane;
}

QString plain(const QString& text)
{
	if (!Qt::mightBeRichText(text)) { return text.simplified(); }
	QTextDocument document;
	document.setHtml(text);
	return document.toPlainText().simplified();
}

//! "Volume:" -> "Volume": a caption's colon is not part of the name.
QString nameFrom(const QString& text)
{
	QString name = plain(text);
	while (name.endsWith(QLatin1Char(':'))) { name.chop(1); }
	return name.trimmed();
}

QJsonObject entryOf(const QWidget* widget)
{
	return {{QStringLiteral("class"), QString::fromLatin1(widget->metaObject()->className())},
		{QStringLiteral("object"), widget->objectName()},
		{QStringLiteral("parent"), widget->parentWidget() != nullptr ? widget->parentWidget()->objectName() : QString()}};
}

//! One finding kind: how many, and the first MaxListed of them.
struct Findings
{
	QJsonArray listed;
	int count = 0;

	void add(const QJsonObject& entry)
	{
		if (++count <= MaxListed) { listed.append(entry); }
	}
};

//! The walk's counts (auditAccessibility's reply).
struct Tally
{
	bool fill = false;
	int checked = 0;
	int named = 0;
	int filled = 0;
	int implied = 0;
	int unreachable = 0;
	Findings unnamed;
	Findings roleless;
	Findings valueless;

	void checkName(QWidget* widget)
	{
		if (!widget->accessibleName().isEmpty())
		{
			++named;
			return;
		}
		const QString name = impliedAccessibleName(widget);
		if (name.isEmpty())
		{
			unnamed.add(entryOf(widget));
			return;
		}
		if (fill) { widget->setAccessibleName(name); }
		++(fill ? filled : implied);
	}

	//! What the platform bridge is told: a role that says what the control is, and the value it shows.
	void checkRoleAndValue(QWidget* widget)
	{
		QAccessibleInterface* face = QAccessible::queryAccessibleInterface(widget);
		const QAccessible::Role role = face != nullptr ? face->role() : QAccessible::NoRole;
		QJsonObject entry = entryOf(widget);
		entry.insert(QStringLiteral("role"), QString::fromLatin1(QMetaEnum::fromType<QAccessible::Role>().valueToKey(role)));
		if (genericRole(role)) { roleless.add(entry); }
		const bool valueGiven = face != nullptr && (face->valueInterface() != nullptr || !face->text(QAccessible::Value).isEmpty());
		if (showsValue(widget) && !valueGiven) { valueless.add(entry); }
	}

	void check(QWidget* widget)
	{
		++checked;
		checkName(widget);
		checkRoleAndValue(widget);
		if ((widget->focusPolicy() & Qt::TabFocus) == 0) { ++unreachable; }
	}

	QJsonObject reply() const
	{
		return {{QStringLiteral("checked"), checked}, {QStringLiteral("named"), named}, {QStringLiteral("filled"), filled},
			{QStringLiteral("implied"), implied}, {QStringLiteral("unnamed"), unnamed.listed},
			{QStringLiteral("unnamed_count"), unnamed.count}, {QStringLiteral("roleless"), roleless.listed},
			{QStringLiteral("roleless_count"), roleless.count}, {QStringLiteral("valueless"), valueless.listed},
			{QStringLiteral("valueless_count"), valueless.count}, {QStringLiteral("unreachable_count"), unreachable}};
	}
};

} // namespace

QString impliedAccessibleName(const QWidget* widget)
{
	// A custom control's constructed name (LMMS widgets keep it as their title - a window's title is its own), the
	// name FloatModelEditorBase::refreshAccessibleName puts first too; then its parameter's.
	if (!widget->isWindow() && !widget->windowTitle().trimmed().isEmpty()) { return nameFrom(widget->windowTitle()); }
	if (const auto* view = dynamic_cast<const ModelView*>(widget); view != nullptr && view->model() != nullptr
		&& !view->model()->displayName().isEmpty())
	{
		return nameFrom(view->model()->displayName());
	}
	if (!widget->toolTip().isEmpty()) { return plain(widget->toolTip()); }
	const auto* button = qobject_cast<const QAbstractButton*>(widget);
	return button != nullptr ? plain(button->text()).remove(QLatin1Char('&')) : QString();
}

QJsonObject auditAccessibility(QWidget* root, bool fill)
{
	a11y::installAccessibleWidgets();  // roles and values as the running application reports them
	Tally tally;
	tally.fill = fill;
	QList<QWidget*> widgets = root->findChildren<QWidget*>();
	widgets.prepend(root);  // the control it is given is one of those it checks
	for (QWidget* widget : widgets)
	{
		if (widget->isVisible() && interactive(widget)) { tally.check(widget); }
	}
	return tally.reply();
}

} // namespace lmms::gui
