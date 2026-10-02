/*
 * ModulatorCard.cpp - a modulator on the Device panel, beside the device it drives
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

#include "DevicePanel.h"

#include <QJsonArray>
#include <QLabel>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "TransportReadouts.h"

namespace lmms::gui
{

ModulatorCard::ModulatorCard(const QJsonObject& modulator, QWidget* parent)
	: QFrame(parent)
	, m_id(modulator.value(QStringLiteral("id")).toString())
{
	setObjectName(QStringLiteral("modulatorCard"));
	const QString name = modulator.value(QStringLiteral("name")).toString();
	setAccessibleName(tr("Modulator %1").arg(name));
	const QJsonObject source = modulator.value(QStringLiteral("source")).toObject();
	auto* column = new QVBoxLayout(this);
	column->setContentsMargins(tokens::space::md, tokens::space::sm, tokens::space::md, tokens::space::md);
	column->setSpacing(tokens::space::sm);
	auto* title = new QLabel(name.isEmpty() ? m_id : name, this);
	title->setObjectName(QStringLiteral("deviceCardTitle"));
	auto* shape = new QLabel(tr("LFO - %1").arg(source.value(QStringLiteral("shape")).toString()), this);
	shape->setObjectName(QStringLiteral("deviceCaption"));
	auto* rate = new ReadoutField(tr("Rate"), this);
	rate->bindText([source] { return QStringLiteral("%1 Hz").arg(source.value(QStringLiteral("rate")).toDouble(), 0, 'g', 3); },
		[id = m_id](const QString& typed) {
			bool ok = false;
			const double hz = QString(typed).remove(QStringLiteral("Hz")).trimmed().toDouble(&ok);
			return ok && ControlRegistry::instance()->invoke(QStringLiteral("modulator.rate_set"), {{QStringLiteral("modulator"), id},
				{QStringLiteral("rate"), hz}}).ok;
		});
	column->addWidget(title);
	column->addWidget(shape);
	column->addWidget(rate);
	for (const QJsonValue& value : modulator.value(QStringLiteral("targets")).toArray())
	{
		const QJsonObject target = value.toObject();
		auto* line = new QLabel(tr("\u2192 %1  %2%").arg(target.value(QStringLiteral("parameter")).toString())
			.arg(qRound(target.value(QStringLiteral("depth")).toDouble() * 100)), this);
		line->setObjectName(QStringLiteral("deviceCaption"));
		column->addWidget(line);
	}
	column->addStretch(1);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

} // namespace lmms::gui
