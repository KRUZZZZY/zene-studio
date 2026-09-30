/*
 * ExportQualityControls.cpp - the export dialog's dither and resampling choices
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

#include "ExportQualityControls.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

QString labelFor(const QString& wire)
{
	if (wire == QLatin1String("off")) { return QGroupBox::tr("Off"); }
	if (wire == QLatin1String("tpdf")) { return QGroupBox::tr("TPDF"); }
	if (wire == QLatin1String("noise_shaped")) { return QGroupBox::tr("Noise-shaped TPDF (16-bit)"); }
	if (wire == QLatin1String("linear")) { return QGroupBox::tr("Linear (the engine's default)"); }
	if (wire == QLatin1String("sinc_fastest")) { return QGroupBox::tr("Sinc - fastest"); }
	if (wire == QLatin1String("sinc_medium")) { return QGroupBox::tr("Sinc - medium"); }
	if (wire == QLatin1String("sinc_best")) { return QGroupBox::tr("Sinc - best"); }
	return wire;
}

//! A combo of @a choices (wire names) with @a current selected; choosing runs @a command with
//! { @a argument: the wire name }.
QComboBox* choiceBox(const QJsonArray& choices, const QString& current, const QString& command,
	const QString& argument, const QString& name, QWidget* parent)
{
	auto* box = new QComboBox(parent);
	box->setAccessibleName(name);
	box->setProperty("controlCommand", command);
	for (const QJsonValue& choice : choices) { box->addItem(labelFor(choice.toString()), choice.toString()); }
	box->setCurrentIndex(std::max(0, box->findData(current)));
	QObject::connect(box, &QComboBox::currentIndexChanged, box, [box, command, argument] {
		ControlRegistry::instance()->invoke(command, {{argument, box->currentData().toString()}});
	});
	return box;
}

} // namespace

QGroupBox* makeExportQualityControls(QWidget* parent)
{
	const QJsonObject settings = ControlRegistry::instance()->invoke(QStringLiteral("export.get_settings"), QJsonObject{}).result;
	auto* group = new QGroupBox(QGroupBox::tr("Render quality"), parent);
	auto* form = new QFormLayout(group);
	QComboBox* dither = choiceBox(settings.value(QStringLiteral("dither_mode_choices")).toArray(),
		settings.value(QStringLiteral("dither_mode")).toString(), QStringLiteral("export.set_dither"),
		QStringLiteral("mode"), QGroupBox::tr("Dither"), group);
	dither->setToolTip(QGroupBox::tr("For the integer formats. Off by default, so a render is byte-for-byte "
		"reproducible; noise shaping applies to 16-bit output (24-bit gets plain TPDF)."));
	QComboBox* resampling = choiceBox(settings.value(QStringLiteral("src_quality_choices")).toArray(),
		settings.value(QStringLiteral("src_quality")).toString(), QStringLiteral("export.set_src_quality"),
		QStringLiteral("src_quality"), QGroupBox::tr("Resampling"), group);
	resampling->setToolTip(QGroupBox::tr("The converter every sample at another rate is resampled with "
		"during the render. Linear is what the engine has always used."));
	form->addRow(QGroupBox::tr("Dither:"), dither);
	form->addRow(QGroupBox::tr("Resampling:"), resampling);
	return group;
}

} // namespace lmms::gui
