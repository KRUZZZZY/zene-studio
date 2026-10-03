/*
 * Appearance.cpp - interface scale, text size and the palette
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
#include "Appearance.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QSaveFile>

#include "ConfigManager.h"

namespace lmms::appearance
{

namespace
{

const QString kSection = QStringLiteral("ui");

double configDouble(const char* key, double fallback)
{
	bool ok = false;
	const double value = ConfigManager::inst()->value(kSection, QLatin1String(key), QString::number(fallback)).toDouble(&ok);
	return ok ? value : fallback;
}

} // namespace

const QStringList& paletteNames()
{
	static const QStringList names{QStringLiteral("dark"), QStringLiteral("high_contrast"), QStringLiteral("light")};
	return names;
}

const QStringList& densityNames()
{
	static const QStringList names{QStringLiteral("standard"), QStringLiteral("comfortable"), QStringLiteral("touch")};
	return names;
}

double densityFactor(const QString& name)
{
	if (name == QLatin1String("touch")) { return 1.8; }
	return name == QLatin1String("comfortable") ? 1.35 : 1.0;
}

Options options()
{
	Options out;
	out.scale = std::clamp(configDouble("scale", 1.0), 0.5, 3.0);
	out.fontScale = std::clamp(configDouble("fontscale", 1.0), 0.75, 2.0);
	const QString palette = ConfigManager::inst()->value(kSection, QStringLiteral("palette"), QStringLiteral("dark"));
	out.palette = paletteNames().contains(palette) ? palette : QStringLiteral("dark");
	const QString density = ConfigManager::inst()->value(kSection, QStringLiteral("density"), QStringLiteral("standard"));
	out.density = densityNames().contains(density) ? density : QStringLiteral("standard");
	return out;
}

void setOptions(const Options& next)
{
	ConfigManager::inst()->setValue(kSection, QStringLiteral("scale"), QString::number(std::clamp(next.scale, 0.5, 3.0)));
	ConfigManager::inst()->setValue(kSection, QStringLiteral("fontscale"), QString::number(std::clamp(next.fontScale, 0.75, 2.0)));
	ConfigManager::inst()->setValue(kSection, QStringLiteral("palette"), next.palette);
	ConfigManager::inst()->setValue(kSection, QStringLiteral("density"), next.density);
	QSaveFile file(scaleFilePath());
	if (file.open(QIODevice::WriteOnly))
	{
		file.write(QByteArray::number(std::clamp(next.scale, 0.5, 3.0)));
		file.commit();
	}
}

QJsonObject toJson(const Options& o)
{
	return {{QStringLiteral("scale"), o.scale}, {QStringLiteral("font_scale"), o.fontScale}, {QStringLiteral("palette"), o.palette},
		{QStringLiteral("density"), o.density}, {QStringLiteral("applies"), QStringLiteral("next_start")}};
}

QString scaleFilePath()
{
	const QString overridden = qEnvironmentVariable("ZENE_UI_SCALE_FILE");
	return !overridden.isEmpty() ? overridden : QDir::home().filePath(QStringLiteral(".zenestudio-scale"));
}

double storedScale()
{
	QFile file(scaleFilePath());
	if (!file.open(QIODevice::ReadOnly)) { return 0.0; }
	bool ok = false;
	const double scale = file.readAll().trimmed().toDouble(&ok);
	return ok && scale >= 0.5 && scale <= 3.0 ? scale : 0.0;
}

void applyStoredScale()
{
	if (!qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) { return; }
	if (const double scale = storedScale(); scale > 0.0 && scale != 1.0) { qputenv("QT_SCALE_FACTOR", QByteArray::number(scale)); }
}

} // namespace lmms::appearance
