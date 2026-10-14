/*
 * XyPad.cpp - two rack macros on one square pad
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
#include "XyPad.h"

#include <algorithm>
#include <utility>

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include "AccessibleCanvas.h"
#include "AccessibleWidgets.h"
#include "Accessibility.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "ItemList.h"
#include "Mixer.h"
#include "ShellChrome.h"

namespace lmms::gui
{

namespace
{

//! The macros of @a channel's rack, as rack.get_state lists them (each with its name and value).
QJsonArray rackMacros(const QString& channel)
{
	return ControlRegistry::instance()->invoke(QStringLiteral("rack.get_state"), {{QStringLiteral("channel"), channel}})
		.result.value(QStringLiteral("macros")).toArray();
}

//! W1: the ONE call a pad position makes - both macros as one step (rack.macro_set_pair), or X's macro alone through
//! rack.macro_set when both axes name it (Y is not sent then, so the two axes never fight over one value).
std::pair<QString, QJsonObject> padCall(const QString& channel, const QString& x, const QString& y, QPointF point)
{
	if (x == y)
	{
		return {QStringLiteral("rack.macro_set"), {{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), x},
			{QStringLiteral("value"), point.x()}}};
	}
	return {QStringLiteral("rack.macro_set_pair"), {{QStringLiteral("channel"), channel}, {QStringLiteral("x_macro"), x},
		{QStringLiteral("x_value"), point.x()}, {QStringLiteral("y_macro"), y}, {QStringLiteral("y_value"), point.y()}}};
}

constexpr double FineStep = 0.01;   //!< an arrow key: 1 % of an axis
constexpr double CoarseStep = 0.1;  //!< with Shift, or Page Up / Page Down: 10 %

QString percent(double value) { return QStringLiteral("%1 %").arg(qRound(value * 100.0)); }

//! How far an arrow (by @a step) or Page Up / Page Down (by CoarseStep, on Y) moves the dot; nothing for another key.
QPointF keyDelta(int key, double step)
{
	switch (key)
	{
		case Qt::Key_Left: return {-step, 0.0};
		case Qt::Key_Right: return {step, 0.0};
		case Qt::Key_Up: return {0.0, step};
		case Qt::Key_Down: return {0.0, -step};
		case Qt::Key_PageUp: return {0.0, CoarseStep};
		case Qt::Key_PageDown: return {0.0, -CoarseStep};
		default: return {};
	}
}

} // namespace

XyPadArea::XyPadArea(QWidget* parent) :
	QWidget(parent)
{
	setMinimumSize(160, 160);
	setAccessibleName(QObject::tr("XY pad"));
	setAccessibleDescription(QObject::tr("Drag, or use the arrow keys: Left and Right move X, Up and Down move Y, by 1 % "
		"(10 % with Shift; Page Up and Page Down move Y by 10 %); Home and End put X at its ends. Ctrl+Shift+L lists the "
		"axes to type their values."));
	setAttribute(Qt::WA_Hover);  // theme 23: the hover cue follows the pointer
	a11y::describeCanvas(this, QAccessible::Slider, [this] { return accessibleValue(); });
}

void XyPadArea::setAxisNames(const QString& x, const QString& y)
{
	m_xName = x;
	m_yName = y;
	update();
}

QString XyPadArea::accessibleValue() const
{
	if (!isEnabled()) { return QObject::tr("No macros"); }
	return QObject::tr("X %1 %2, Y %3 %4").arg(m_xName, percent(m_point.x()), m_yName, percent(m_point.y())).simplified();
}

bool XyPadArea::event(QEvent* event)
{
	// The pad's keys are its own while it has the focus: an arrow is not a window shortcut's here.
	if (event->type() == QEvent::ShortcutOverride && keyed(static_cast<QKeyEvent*>(event)) != m_point)
	{
		event->accept();
		return true;
	}
	return QWidget::event(event);
}

QPointF XyPadArea::keyed(const QKeyEvent* event) const
{
	const Qt::KeyboardModifiers mods = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
	if (mods != Qt::NoModifier) { return m_point; }
	const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? CoarseStep : FineStep;
	const int key = event->key();
	QPointF to = m_point + keyDelta(key, step);
	if (key == Qt::Key_Home || key == Qt::Key_End) { to.setX(key == Qt::Key_Home ? 0.0 : 1.0); }
	return QPointF(std::clamp(to.x(), 0.0, 1.0), std::clamp(to.y(), 0.0, 1.0));
}

void XyPadArea::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_L && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))
	{
		ControlRegistry::instance()->invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("xy")}});
		return;
	}
	const QPointF to = keyed(event);
	if (to == m_point || !isEnabled())
	{
		QWidget::keyPressEvent(event);
		return;
	}
	setPoint(to);
	if (moved) { moved(m_point); }
}

void XyPadArea::setPoint(QPointF point)
{
	m_point = QPointF(std::clamp(point.x(), 0.0, 1.0), std::clamp(point.y(), 0.0, 1.0));
	update();
}

void XyPadArea::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.control);
	p.setPen(QPen(pal.separator, 1));
	p.drawLine(width() / 2, 0, width() / 2, height());
	p.drawLine(0, height() / 2, width(), height() / 2);
	// The last pixel is the end of the range here too, as in moveTo(): drawing over width() would
	// put the dot's centre outside the widget and clip the circle at a range endpoint.
	const QPointF at(m_point.x() * std::max(1, width() - 1), (1.0 - m_point.y()) * std::max(1, height() - 1));
	p.setRenderHint(QPainter::Antialiasing);
	p.setBrush(pal.accent);
	p.setPen(Qt::NoPen);
	p.drawEllipse(at, 7.0, 7.0);
	// Theme 23: where the keys act is outlined while the pad has the focus, and hover and disabled look as on every canvas.
	if (hasFocus()) { a11y::paintKeyMark(p, QRectF(at.x() - 11, at.y() - 11, 22, 22).toRect()); }
	a11y::paintCanvasCues(p, this);
}

void XyPadArea::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton) { moveTo(event->pos()); }  // as a drag is: another button sets nothing (review L20 #7)
}

void XyPadArea::mouseMoveEvent(QMouseEvent* event)
{
	if (event->buttons() & Qt::LeftButton) { moveTo(event->pos()); }
}

void XyPadArea::moveTo(const QPoint& at)
{
	// The last pixel is the end of the range: x runs 0 .. width() - 1 (review L20 #8).
	setPoint(QPointF(static_cast<double>(at.x()) / std::max(1, width() - 1), 1.0 - static_cast<double>(at.y()) / std::max(1, height() - 1)));
	if (moved) { moved(m_point); }
}

XyPadDialog::XyPadDialog(const QString& channel, QWidget* parent) :
	QDialog(parent),
	m_channel(channel),
	m_x(new QComboBox(this)),
	m_y(new QComboBox(this)),
	m_area(new XyPadArea(this)),
	m_status(new QLabel(this))
{
	ControlResult ignored;
	const MixerChannel* target = control::resolveMixerChannel(channel, &ignored);
	// Which channel's rack the pad plays: several pads can be open at once.
	setWindowTitle(target != nullptr ? QObject::tr("XY Pad - %1").arg(target->m_name) : QObject::tr("XY Pad"));
	setAttribute(Qt::WA_DeleteOnClose);
	a11y::announce(m_x, QObject::tr("X macro"), QObject::tr("The rack macro the pad's left-right position drives."));
	a11y::announce(m_y, QObject::tr("Y macro"), QObject::tr("The rack macro the pad's up-down position drives."));
	m_area->setProperty("controlCommand", QStringLiteral("rack.macro_set_pair"));
	m_status->setWordWrap(true);
	m_status->setAccessibleName(QObject::tr("XY pad status"));
	auto* form = new QFormLayout;
	form->addRow(QObject::tr("X:"), m_x);
	form->addRow(QObject::tr("Y:"), m_y);
	// Theme 10: beside the axes, the pad's two axes as an editable list (window.item_list, as Ctrl+Shift+L on the pad).
	ShellButton* list = itemlists::listButton(QStringLiteral("xy"), m_area, this);
	list->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	auto* top = new QHBoxLayout;
	top->addLayout(form, 1);
	top->addWidget(list, 0, Qt::AlignTop);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(top);
	layout->addWidget(m_area, 1);
	layout->addWidget(m_status);
	fillMacros();
	m_area->moved = [this](QPointF point) { send(point); };
	// A new choice of macro shows where that macro stands, not where the last one was.
	for (QComboBox* axis : {m_x, m_y}) { connect(axis, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { syncPoint(); }); }
}

QString XyPadDialog::macroId(bool y) const
{
	return (y ? m_y : m_x)->currentData().toString();
}

QString XyPadDialog::macroName(bool y) const
{
	return (y ? m_y : m_x)->currentText();
}

void XyPadDialog::fillMacros()
{
	const QJsonArray macros = rackMacros(m_channel);
	for (int i = 0; i < macros.size(); ++i)
	{
		const QString name = macros.at(i).toObject().value(QStringLiteral("name")).toString();
		m_x->addItem(name, QStringLiteral("macro-%1").arg(i));
		m_y->addItem(name, QStringLiteral("macro-%1").arg(i));
	}
	if (macros.size() > 1) { m_y->setCurrentIndex(1); }
	m_area->setEnabled(!macros.isEmpty());
	syncPoint();
}

void XyPadDialog::syncPoint()
{
	if (m_x->count() == 0)
	{
		m_area->setToolTip(QObject::tr("This channel's rack has no macros yet: add one with Rack > Add macro... in the channel's menu."));
		return;
	}
	const QJsonArray macros = rackMacros(m_channel);
	const auto valueOf = [&macros](const QComboBox* axis) {
		return macros.at(std::max(0, axis->currentIndex())).toObject().value(QStringLiteral("value")).toDouble(0.5);
	};
	m_area->setPoint(QPointF(valueOf(m_x), valueOf(m_y)));
	m_area->setAxisNames(m_x->currentText(), m_y->currentText());
	emit changed();
	// One macro on both axes is driven by X alone (send), and the pad says so rather than ignore Y silently.
	m_area->setToolTip(m_x->currentIndex() == m_y->currentIndex()
		? QObject::tr("Both axes name the same macro: only left-right moves it.") : QString());
}

void XyPadDialog::send(QPointF point)
{
	const QString xMacro = m_x->currentData().toString();
	const QString yMacro = m_y->currentData().toString();
	const auto [id, args] = padCall(m_channel, xMacro, yMacro, point);
	const ControlResult result = ControlRegistry::instance()->invoke(id, args);
	// The result is read (W1): a refused move - a macro an agent removed while the pad was open, say - is named on the
	// status line, and the dot goes back to where the macros really stand rather than where the pointer is.
	m_status->setText(result.ok ? QString() : QObject::tr("Not set: %1").arg(result.errorMessage));
	if (!result.ok)
	{
		a11y::announceTransition(m_status, a11y::Transition::Refused, result.errorMessage);  // theme 17
		syncPoint();
		return;
	}
	if (xMacro == yMacro) { m_area->setPoint(QPointF(point.x(), point.x())); }
	emit changed();
}

} // namespace lmms::gui
