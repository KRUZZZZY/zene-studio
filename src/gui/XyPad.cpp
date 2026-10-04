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
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

#include "Accessibility.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Mixer.h"

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

} // namespace

XyPadArea::XyPadArea(QWidget* parent) :
	QWidget(parent)
{
	setMinimumSize(160, 160);
	setAccessibleName(QObject::tr("XY pad"));
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
	const QPointF at(m_point.x() * width(), (1.0 - m_point.y()) * height());
	p.setRenderHint(QPainter::Antialiasing);
	p.setBrush(pal.accent);
	p.setPen(Qt::NoPen);
	p.drawEllipse(at, 7.0, 7.0);
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
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(m_area, 1);
	layout->addWidget(m_status);
	fillMacros();
	m_area->moved = [this](QPointF point) { send(point); };
	// A new choice of macro shows where that macro stands, not where the last one was.
	for (QComboBox* axis : {m_x, m_y}) { connect(axis, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { syncPoint(); }); }
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
	// One macro on both axes is driven by X alone (send), and the pad says so rather than ignore Y silently.
	m_area->setToolTip(m_x->currentIndex() == m_y->currentIndex()
		? QObject::tr("Both axes name the same macro: only left-right moves it.") : QString());
}

void XyPadDialog::send(QPointF point)
{
	const auto [id, args] = padCall(m_channel, m_x->currentData().toString(), m_y->currentData().toString(), point);
	const ControlResult result = ControlRegistry::instance()->invoke(id, args);
	// The result is read (W1): a refused move - a macro an agent removed while the pad was open, say - is named on the
	// status line, and the dot goes back to where the macros really stand rather than where the pointer is.
	m_status->setText(result.ok ? QString() : QObject::tr("Not set: %1").arg(result.errorMessage));
	if (!result.ok) { syncPoint(); }
}

} // namespace lmms::gui
