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

#include <QHash>
#include <QJsonArray>
#include <QLabel>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "TransportReadouts.h"

namespace lmms::gui
{

namespace
{

//! What a modulator is, from its source's shape: an LFO for the four waveforms, else the source's
//! own kind (modulationShapeNames' vocabulary), and "off" while it is inactive.
QString sourceCaption(const QJsonObject& source)
{
	const QString shape = source.value(QStringLiteral("shape")).toString();
	static const QStringList waveforms{QStringLiteral("sine"), QStringLiteral("triangle"), QStringLiteral("square"),
		QStringLiteral("saw")};
	const QHash<QString, QString> kinds{{QStringLiteral("random"), ModulatorCard::tr("Random")},
		{QStringLiteral("smooth_random"), ModulatorCard::tr("Smooth random")}, {QStringLiteral("steps"), ModulatorCard::tr("Step sequence")},
		{QStringLiteral("envelope"), ModulatorCard::tr("Envelope")}, {QStringLiteral("formula"), ModulatorCard::tr("Formula")}};
	const QString caption = waveforms.contains(shape) ? ModulatorCard::tr("LFO - %1").arg(shape) : kinds.value(shape, shape);
	return source.value(QStringLiteral("active")).toBool(true) ? caption : ModulatorCard::tr("%1 (off)").arg(caption);
}

//! A target line's tooltip: the whole destination (channel, effect, parameter) and whether it resolves.
QString targetTip(const QJsonObject& target)
{
	const QString where = ModulatorCard::tr("%1, effect %2, %3").arg(target.value(QStringLiteral("channel")).toString())
		.arg(target.value(QStringLiteral("effect")).toInt()).arg(target.value(QStringLiteral("parameter")).toString());
	const QString why = target.value(QStringLiteral("why")).toString();
	return why.isEmpty() ? where : ModulatorCard::tr("%1 - not driven: %2").arg(where, why);
}

} // namespace

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
	auto* shape = new QLabel(sourceCaption(source), this);
	setAccessibleDescription(shape->text());
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
		line->setToolTip(targetTip(target));
		column->addWidget(line);
	}
	column->addStretch(1);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

} // namespace lmms::gui
