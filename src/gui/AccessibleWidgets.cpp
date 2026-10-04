/*
 * AccessibleWidgets.cpp - the custom controls as a screen reader should know them
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

#include "AccessibleWidgets.h"

#include <QAccessibleWidget>
#include <QEvent>
#include <QHBoxLayout>  // LeftRightNav.h needs it
#include <QTimer>

#include "AccessibilityAudit.h"
#include "AutomatableButton.h"
#include "ComboBox.h"
#include "Fader.h"
#include "Graph.h"
#include "GroupBox.h"
#include "Knob.h"
#include "LcdFloatSpinBox.h"
#include "LcdSpinBox.h"
#include "LedCheckBox.h"
#include "LeftRightNav.h"
#include "MeterDialog.h"
#include "TabBar.h"

namespace lmms::a11y
{

using namespace gui;

namespace
{

//! The shortest gap between two value announcements for one control (AccessibleWidgets.h).
constexpr int SettleMs = 250;

AutomatableModel* modelOf(QWidget* widget)
{
	auto* view = dynamic_cast<ModelView*>(widget);
	return view != nullptr ? dynamic_cast<AutomatableModel*>(view->model()) : nullptr;
}

//! Tells the screen reader when the value of the control it is on changes: only while the control has keyboard
//! focus, at most every SettleMs, and with the settled value last. It looks when the control repaints, which
//! every model change makes it do, so a model swapped in later is followed too.
class ValueAnnouncer : public QObject
{
public:
	explicit ValueAnnouncer(QWidget* control)
		: QObject(control)
		, m_control(control)
	{
		m_settle.setSingleShot(true);
		m_settle.setInterval(SettleMs);
		connect(&m_settle, &QTimer::timeout, this, [this] { if (m_pending) { announce(); } });
		control->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject*, QEvent* event) override
	{
		if (event->type() == QEvent::FocusIn) { m_said = valueText(m_control); }  // focus reads it anyway
		else if (event->type() == QEvent::Paint && m_control->hasFocus() && QAccessible::isActive()) { noteChange(); }
		return false;
	}

private:
	void noteChange()
	{
		if (valueText(m_control) == m_said) { return; }
		if (m_settle.isActive())
		{
			m_pending = true;
			return;
		}
		announce();
	}

	void announce()
	{
		m_pending = false;
		m_said = valueText(m_control);
		QAccessibleInterface* face = QAccessible::queryAccessibleInterface(m_control);
		QAccessibleValueInterface* value = face != nullptr ? face->valueInterface() : nullptr;
		if (value != nullptr)
		{
			QAccessibleValueChangeEvent changed(m_control, value->currentValue());
			QAccessible::updateAccessibility(&changed);
		}
		else
		{
			QAccessibleEvent changed(m_control, QAccessible::ValueChanged);
			QAccessible::updateAccessibility(&changed);
		}
		m_settle.start();
	}

	QWidget* m_control;
	QTimer m_settle;
	QString m_said;
	bool m_pending = false;
};

//! Any custom control: its role, its name - set, or the one it carries - and the value it shows.
class Control : public QAccessibleWidget
{
public:
	Control(QWidget* widget, QAccessible::Role role)
		: QAccessibleWidget(widget, role)
	{
	}

	QString text(QAccessible::Text t) const override
	{
		if (t == QAccessible::Name)
		{
			return !widget()->accessibleName().isEmpty() ? widget()->accessibleName() : impliedAccessibleName(widget());
		}
		return t == QAccessible::Value ? valueText(widget()) : QAccessibleWidget::text(t);
	}
};

//! A knob, a fader, a slider-like editor or an LCD number: its model's value, bounds and step, settable, and
//! the increase/decrease actions.
class ValueControl : public Control, public QAccessibleValueInterface
{
public:
	ValueControl(QWidget* widget, QAccessible::Role role)
		: Control(widget, role)
	{
		new ValueAnnouncer(widget);
	}

	void* interface_cast(QAccessible::InterfaceType type) override
	{
		return type == QAccessible::ValueInterface ? static_cast<QAccessibleValueInterface*>(this) : Control::interface_cast(type);
	}

	QVariant currentValue() const override { return read([](const AutomatableModel* m) { return m->value<float>(); }); }
	QVariant maximumValue() const override { return read([](const AutomatableModel* m) { return m->maxValue<float>(); }); }
	QVariant minimumValue() const override { return read([](const AutomatableModel* m) { return m->minValue<float>(); }); }
	QVariant minimumStepSize() const override { return read([](const AutomatableModel* m) { return m->step<float>(); }); }

	void setCurrentValue(const QVariant& value) override
	{
		if (AutomatableModel* model = modelOf(widget())) { model->setValue(value.toFloat()); }
	}

	QStringList actionNames() const override { return QStringList{increaseAction(), decreaseAction()} + Control::actionNames(); }

	void doAction(const QString& name) override
	{
		AutomatableModel* model = modelOf(widget());
		const float step = model != nullptr ? model->step<float>() : 0.f;
		if (name == increaseAction()) { applyKeyStep(model, KeyStep::Up, step, step); }
		else if (name == decreaseAction()) { applyKeyStep(model, KeyStep::Down, step, step); }
		else { Control::doAction(name); }
	}

private:
	template<typename Read>
	QVariant read(Read get) const
	{
		const AutomatableModel* model = modelOf(widget());
		return model != nullptr ? QVariant(get(model)) : QVariant();
	}
};

//! A model combo box: its current item is the value; its actions open the menu and step the choice.
class ComboControl : public Control
{
public:
	explicit ComboControl(ComboBox* combo)
		: Control(combo, QAccessible::ComboBox)
	{
		new ValueAnnouncer(combo);
	}

	QStringList actionNames() const override
	{
		return QStringList{showMenuAction(), increaseAction(), decreaseAction()} + Control::actionNames();
	}

	void doAction(const QString& name) override
	{
		auto* combo = static_cast<ComboBox*>(widget());
		// The menu runs its own event loop: opened later, so the screen reader's call returns first.
		if (name == showMenuAction()) { QMetaObject::invokeMethod(combo, "showMenu", Qt::QueuedConnection); }
		else if (name == increaseAction()) { combo->selectNext(); }
		else if (name == decreaseAction()) { combo->selectPrevious(); }
		else { Control::doAction(name); }
	}
};

//! A model button: an LED check box is a check box, any other a push button, checkable when it toggles; its
//! action presses it.
class ButtonControl : public Control
{
public:
	explicit ButtonControl(AutomatableButton* button)
		: Control(button, qobject_cast<LedCheckBox*>(button) != nullptr ? QAccessible::CheckBox : QAccessible::Button)
	{
	}

	QAccessible::State state() const override
	{
		QAccessible::State state = Control::state();
		state.checkable = button()->isCheckable();
		state.checked = button()->isChecked();
		return state;
	}

	QStringList actionNames() const override
	{
		return QStringList{button()->isCheckable() ? toggleAction() : pressAction()} + Control::actionNames();
	}

	void doAction(const QString& name) override
	{
		if (name == toggleAction() || name == pressAction()) { button()->click(); }
		else { Control::doAction(name); }
	}

private:
	QAbstractButton* button() const { return static_cast<QAbstractButton*>(widget()); }
};

QAccessibleInterface* valueControl(QWidget* widget)
{
	if (qobject_cast<Knob*>(widget) != nullptr) { return new ValueControl(widget, QAccessible::Dial); }
	if (qobject_cast<FloatModelEditorBase*>(widget) != nullptr || qobject_cast<Fader*>(widget) != nullptr)
	{
		return new ValueControl(widget, QAccessible::Slider);
	}
	if (qobject_cast<LcdSpinBox*>(widget) != nullptr || qobject_cast<LcdFloatSpinBox*>(widget) != nullptr)
	{
		return new ValueControl(widget, QAccessible::SpinBox);
	}
	return nullptr;
}

QAccessibleInterface* otherControl(QWidget* widget)
{
	if (auto* combo = qobject_cast<ComboBox*>(widget)) { return new ComboControl(combo); }
	if (auto* button = qobject_cast<AutomatableButton*>(widget)) { return new ButtonControl(button); }
	if (qobject_cast<Graph*>(widget) != nullptr) { return new Control(widget, QAccessible::Chart); }
	if (qobject_cast<TabBar*>(widget) != nullptr) { return new Control(widget, QAccessible::PageTabList); }
	if (qobject_cast<GroupBox*>(widget) != nullptr || qobject_cast<MeterDialog*>(widget) != nullptr
		|| qobject_cast<LeftRightNav*>(widget) != nullptr)
	{
		return new Control(widget, QAccessible::Grouping);
	}
	return nullptr;
}

//! Qt asks once per class of the object's chain, most derived first; the first ask decides, the rest pass.
QAccessibleInterface* describeControl(const QString& className, QObject* object)
{
	auto* widget = qobject_cast<QWidget*>(object);
	if (widget == nullptr || className != QLatin1String(object->metaObject()->className())) { return nullptr; }
	QAccessibleInterface* face = valueControl(widget);
	return face != nullptr ? face : otherControl(widget);
}

QString lcdText(QWidget* widget)
{
	if (auto* lcd = qobject_cast<LcdSpinBox*>(widget); lcd != nullptr && lcd->model() != nullptr)
	{
		return QString::number(lcd->model()->value() + lcd->displayOffset());
	}
	auto* lcd = qobject_cast<LcdFloatSpinBox*>(widget);
	const FloatModel* model = lcd != nullptr ? lcd->model() : nullptr;
	return model != nullptr ? QString::number(model->value(), 'f', model->getDigitCount()) : QString();
}

} // namespace

void installAccessibleWidgets()
{
	QAccessible::installFactory(&describeControl);  // Qt keeps one copy of a factory however often it is installed
}

KeyStep keyStep(int key)
{
	switch (key)
	{
		case Qt::Key_Up:
		case Qt::Key_Right: return KeyStep::Up;
		case Qt::Key_Down:
		case Qt::Key_Left: return KeyStep::Down;
		case Qt::Key_PageUp: return KeyStep::PageUp;
		case Qt::Key_PageDown: return KeyStep::PageDown;
		case Qt::Key_Home: return KeyStep::Home;
		case Qt::Key_End: return KeyStep::End;
		default: return KeyStep::None;
	}
}

bool applyKeyStep(AutomatableModel* model, KeyStep step, float fine, float coarse)
{
	if (model == nullptr || step == KeyStep::None) { return false; }
	const float value = model->value<float>();
	switch (step)
	{
		case KeyStep::Up: model->setValue(value + fine); break;
		case KeyStep::Down: model->setValue(value - fine); break;
		case KeyStep::PageUp: model->setValue(value + coarse); break;
		case KeyStep::PageDown: model->setValue(value - coarse); break;
		case KeyStep::Home: model->setValue(model->minValue<float>()); break;
		default: model->setValue(model->maxValue<float>()); break;
	}
	return true;
}

SpinKey spinKey(int key, AutomatableModel* model, float fine, float coarse)
{
	if (model == nullptr) { return SpinKey::None; }
	if (key == Qt::Key_Return || key == Qt::Key_Enter) { return SpinKey::Entered; }
	return applyKeyStep(model, keyStep(key), fine, coarse) ? SpinKey::Stepped : SpinKey::None;
}

QString valueText(QWidget* widget)
{
	if (auto* editor = qobject_cast<FloatModelEditorBase*>(widget)) { return editor->accessibleValueText(); }
	if (auto* fader = qobject_cast<Fader*>(widget)) { return fader->accessibleValueText(); }
	if (auto* combo = qobject_cast<ComboBox*>(widget); combo != nullptr && combo->model() != nullptr)
	{
		return combo->model()->currentText();
	}
	return lcdText(widget);
}

} // namespace lmms::a11y
