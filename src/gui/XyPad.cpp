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

#include <QComboBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
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
	m_area(new XyPadArea(this))
{
	ControlResult ignored;
	const MixerChannel* target = control::resolveMixerChannel(channel, &ignored);
	// Which channel's rack the pad plays: several pads can be open at once.
	setWindowTitle(target != nullptr ? QObject::tr("XY Pad - %1").arg(target->m_name) : QObject::tr("XY Pad"));
	setAttribute(Qt::WA_DeleteOnClose);
	a11y::announce(m_x, QObject::tr("X macro"), QObject::tr("The rack macro the pad's left-right position drives."));
	a11y::announce(m_y, QObject::tr("Y macro"), QObject::tr("The rack macro the pad's up-down position drives."));
	m_area->setProperty("controlCommand", QStringLiteral("rack.macro_set"));
	auto* form = new QFormLayout;
	form->addRow(QObject::tr("X:"), m_x);
	form->addRow(QObject::tr("Y:"), m_y);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(m_area, 1);
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
	ControlRegistry* registry = ControlRegistry::instance();
	registry->invoke(QStringLiteral("rack.macro_set"), {{QStringLiteral("channel"), m_channel},
		{QStringLiteral("macro"), m_x->currentData().toString()}, {QStringLiteral("value"), point.x()}});
	if (m_y->currentData() != m_x->currentData())
	{
		registry->invoke(QStringLiteral("rack.macro_set"), {{QStringLiteral("channel"), m_channel},
			{QStringLiteral("macro"), m_y->currentData().toString()}, {QStringLiteral("value"), point.y()}});
	}
}

} // namespace lmms::gui
