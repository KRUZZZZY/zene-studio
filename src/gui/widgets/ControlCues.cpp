/*
 * ControlCues.cpp - the shared controls' interaction cues: hover, hit targets and the help that says how they work
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

#include "ControlCues.h"

#include <algorithm>

#include <QApplication>
#include <QHelpEvent>
#include <QLayout>
#include <QToolTip>

#include "AutomatableButton.h"
#include "AutomatableSlider.h"
#include "ComboBox.h"
#include "DesignTokens.h"
#include "Fader.h"
#include "FloatModelEditorBase.h"
#include "LcdFloatSpinBox.h"
#include "LcdSpinBox.h"
#include "LedCheckBox.h"

namespace lmms::gui::cues
{

namespace
{

const char* const FilterName = "zeneControlCues";

//! Whether @a layout, or a layout inside it, holds @a widget.
bool holds(const QLayout* layout, const QWidget* widget)
{
	for (int i = 0; layout != nullptr && i < layout->count(); ++i)
	{
		QLayoutItem* item = layout->itemAt(i);
		if (item == nullptr) { continue; }  // QLayout::itemAt may return nullptr for an index below count()
		if (item->widget() == widget || holds(item->layout(), widget)) { return true; }
	}
	return false;
}

//! The text of the right-click menu every model control shares (AutomatableModelView::addDefaultActions).
QString rightClick()
{
	return QApplication::tr("Right-click: reset, copy and paste the value, automation and controller connections.");
}

//! Shows a control's tooltip with its one-line reminder under it.
class CueFilter : public QObject
{
public:
	explicit CueFilter(QObject* parent) : QObject(parent) { setObjectName(QLatin1String(FilterName)); }

protected:
	bool eventFilter(QObject* watched, QEvent* event) override
	{
		if (event->type() != QEvent::Polish && event->type() != QEvent::ToolTip) { return false; }
		auto* widget = qobject_cast<QWidget*>(watched);
		if (widget == nullptr) { return false; }
		if (event->type() == QEvent::Polish)
		{
			prepare(widget);
			return false;
		}
		return showTip(widget, static_cast<QHelpEvent*>(event));
	}

private:
	static bool showTip(QWidget* widget, const QHelpEvent* help)
	{
		const Kind kind = kindOf(widget);
		// Only the value controls whose wheel and right button are not obvious: a knob shows its value in its own
		// floating label, a button is plainly clicked (its What's This still says the rest), and a control without a
		// tooltip shows none.
		const bool valueControl = kind == Kind::Fader || kind == Kind::Slider || kind == Kind::Number || kind == Kind::Choice;
		if (!valueControl || widget->toolTip().isEmpty()) { return false; }
		QToolTip::showText(help->globalPos(), widget->toolTip() + QLatin1Char('\n') + interactionHint(kind), widget);
		return true;
	}
};

void startCues()
{
	install();
}

// Every binary that links the interface - the application and each test - gets the cues from its first event loop turn.
Q_COREAPP_STARTUP_FUNCTION(startCues)

} // namespace

Kind kindOf(const QWidget* widget)
{
	if (dynamic_cast<const FloatModelEditorBase*>(widget) != nullptr) { return Kind::Knob; }
	if (dynamic_cast<const Fader*>(widget) != nullptr) { return Kind::Fader; }
	if (dynamic_cast<const AutomatableSlider*>(widget) != nullptr) { return Kind::Slider; }
	if (dynamic_cast<const LcdSpinBox*>(widget) != nullptr || dynamic_cast<const LcdFloatSpinBox*>(widget) != nullptr)
	{
		return Kind::Number;
	}
	if (dynamic_cast<const ComboBox*>(widget) != nullptr) { return Kind::Choice; }
	if (const auto* button = dynamic_cast<const AutomatableButton*>(widget))
	{
		return dynamic_cast<const LedCheckBox*>(widget) != nullptr || button->isCheckable() ? Kind::Toggle : Kind::Button;
	}
	return Kind::None;
}

QString interactionHelp(Kind kind)
{
	switch (kind)
	{
		case Kind::Knob: return QApplication::tr("Drag up or down, or turn the mouse wheel, to change it (wheel with Shift: "
			"coarse steps, Ctrl: fine, Alt: finer; drag with Shift: slower). Double-click to type a value. The arrow keys step "
			"it, Page Up and Page Down in coarse steps, Home and End go to the ends. Ctrl+drag it onto an automation clip to "
			"automate it. ") + rightClick();
		case Kind::Fader: return QApplication::tr("Drag the cap, or click the track to move it there. The mouse wheel moves "
			"it 1 dB (Shift: 3 dB, Ctrl: 0.1 dB). Double-click to type a value. The arrow keys step it, Page Up and Page Down "
			"by 3 dB, Home and End go to the ends. ") + rightClick();
		case Kind::Slider: return QApplication::tr("Drag it or turn the mouse wheel to change it. Double-click to type a "
			"value. ") + rightClick();
		case Kind::Number: return QApplication::tr("Drag up or down, or turn the mouse wheel, to change it. Double-click or "
			"press Enter to type a value. The arrow keys step it, Page Up and Page Down in larger steps, Home and End go to "
			"the ends. ") + rightClick();
		case Kind::Choice: return QApplication::tr("Click it, or press Space or Enter, to open the list. The mouse wheel and "
			"the arrow keys step through the choices. ") + rightClick();
		case Kind::Toggle: return QApplication::tr("Click it or press Space to switch it on or off. ") + rightClick();
		case Kind::Button: return QApplication::tr("Click it or press Space. ") + rightClick();
		case Kind::None: break;
	}
	return QString();
}

QString interactionHint(Kind kind)
{
	switch (kind)
	{
		case Kind::Knob:
		case Kind::Fader:
		case Kind::Slider:
		case Kind::Number:
			return QApplication::tr("Drag or wheel: change. Double-click: type a value. Right-click: more. Shift+F1: how it works.");
		case Kind::Choice: return QApplication::tr("Click or wheel: choose. Right-click: more. Shift+F1: how it works.");
		case Kind::Toggle:
		case Kind::Button: return QApplication::tr("Click or Space. Right-click: more. Shift+F1: how it works.");
		case Kind::None: break;
	}
	return QString();
}

bool hovered(const QWidget* widget)
{
	return widget != nullptr && widget->isEnabled() && widget->underMouse();
}

void applyTarget(QWidget* widget)
{
	const QWidget* parent = widget->parentWidget();
	if (!tokens::accessibleMode() || parent == nullptr || !holds(parent->layout(), widget)) { return; }
	const int side = tokens::targetSize();
	// Per side: a fixed one (a knob's width, a combo box's height) is grown, never shrunk; a free one gets a minimum.
	if (widget->minimumWidth() == widget->maximumWidth()) { widget->setFixedWidth(std::max(widget->maximumWidth(), side)); }
	else { widget->setMinimumWidth(std::max(widget->minimumWidth(), side)); }
	if (widget->minimumHeight() == widget->maximumHeight()) { widget->setFixedHeight(std::max(widget->maximumHeight(), side)); }
	else { widget->setMinimumHeight(std::max(widget->minimumHeight(), side)); }
}

void prepare(QWidget* widget)
{
	const Kind kind = kindOf(widget);
	if (kind == Kind::None) { return; }
	widget->setAttribute(Qt::WA_Hover, true);  // a repaint when the pointer enters and leaves: the hover state
	const QString help = interactionHelp(kind);
	const QString own = widget->whatsThis();
	if (!own.contains(help)) { widget->setWhatsThis(own.isEmpty() ? help : own + QStringLiteral("\n\n") + help); }
	applyTarget(widget);
}

void install()
{
	QCoreApplication* app = QCoreApplication::instance();
	// Looked up by name: CueFilter has no Q_OBJECT, so findChild<CueFilter*> only ever matched on QObject's meta-object
	// (and Qt 6.12 refuses it at compile time).
	if (app == nullptr || app->findChild<QObject*>(QLatin1String(FilterName), Qt::FindDirectChildrenOnly) != nullptr) { return; }
	app->installEventFilter(new CueFilter(app));
}

} // namespace lmms::gui::cues
