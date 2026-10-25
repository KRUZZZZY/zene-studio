/*
 * Appearance.cpp - the interface's zoom, palette and accessible mode
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
#include <cmath>
#include <optional>

#include <QDomDocument>
#include <QDir>
#include <QFile>
#include <QObject>
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

std::optional<double> selectedConfigScale(const QString& configFile)
{
	if (configFile.isEmpty()) { return std::nullopt; }
	QFile file(configFile);
	if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }
	QDomDocument document;
	if (!document.setContent(&file)) { return std::nullopt; }
	const QDomElement ui = document.documentElement().firstChildElement(kSection);
	if (ui.isNull() || !ui.hasAttribute(QStringLiteral("scale"))) { return std::nullopt; }

	bool ok = false;
	const double scale = ui.attribute(QStringLiteral("scale")).toDouble(&ok);
	return ok && std::isfinite(scale) ? std::clamp(scale, 0.5, 3.0) : 1.0;
}

//! The zoom to store or draw with: a finite value inside 0.5..3. std::clamp alone does NOT do
//! this - every comparison against NaN is false, so a NaN scale is returned untouched and a
//! non-finite zoom would size the interface from garbage. @a fallback is used instead of one.
double boundedScale(double scale, double fallback)
{
	return std::isfinite(scale) ? std::clamp(scale, 0.5, 3.0) : fallback;
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

const QStringList& workspaceNames()
{
	static const QStringList names{QStringLiteral("standard"), QStringLiteral("touch")};
	return names;
}

bool workspaceIsTouch(const QString& name)
{
	return name == QLatin1String("touch");
}

bool densityIsAccessible(const QString& name)
{
	return name == QLatin1String("accessible") || name == QLatin1String("comfortable") || name == QLatin1String("touch");
}

QString effectivePalette(const Options& o)
{
	return o.accessible ? QStringLiteral("high_contrast") : o.palette;
}

Options options()
{
	Options out;
	out.scale = boundedScale(configDouble("scale", 1.0), 1.0);
	const QString palette = ConfigManager::inst()->value(kSection, QStringLiteral("palette"), QStringLiteral("dark"));
	out.palette = paletteNames().contains(palette) ? palette : QStringLiteral("dark");
	out.accessible = densityIsAccessible(ConfigManager::inst()->value(kSection, QStringLiteral("density")));
	// An absent setting is standard: a config from before this preference opens exactly as it did.
	const QString workspace = ConfigManager::inst()->value(kSection, QStringLiteral("workspace"), QStringLiteral("standard"));
	out.workspace = workspaceNames().contains(workspace) ? workspace : QStringLiteral("standard");
	return out;
}

bool setOptions(const Options& next)
{
	// A refused non-finite zoom keeps the zoom in force now, rather than storing a value the
	// interface cannot draw with.
	const double scale = boundedScale(next.scale, options().scale);
	ConfigManager::inst()->setValue(kSection, QStringLiteral("scale"), QString::number(scale));
	ConfigManager::inst()->setValue(kSection, QStringLiteral("palette"), next.palette);
	ConfigManager::inst()->setValue(kSection, QStringLiteral("density"),
		next.accessible ? QStringLiteral("accessible") : QStringLiteral("standard"));
	ConfigManager::inst()->setValue(kSection, QStringLiteral("workspace"),
		workspaceNames().contains(next.workspace) ? next.workspace : QStringLiteral("standard"));
	// Written now, as the other preference writers do (settings.set), so a crash cannot forget a choice
	// the person was told was saved.
	ConfigManager::inst()->saveConfigFile();
	return syncScaleFile();
}

bool syncScaleFile()
{
	QSaveFile file(scaleFilePath());
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	const QByteArray scale = QByteArray::number(options().scale);
	if (file.write(scale) != scale.size())
	{
		file.cancelWriting();
		return false;
	}
	return file.commit();
}

QJsonObject applyTiming()
{
	return {{QStringLiteral("applies"), QStringLiteral("next_start")}, {QStringLiteral("restart_required"), true},
		{QStringLiteral("applies_note"), QObject::tr("Saved. The interface keeps its current look until Zene Studio is "
			"restarted; the change applies from the next start.")}};
}

const Options& startedWith()
{
	static const Options started = options();  // read once, like the palette
	return started;
}

QJsonObject toJson(const Options& o)
{
	QJsonObject out{{QStringLiteral("scale"), o.scale}, {QStringLiteral("palette"), o.palette}, {QStringLiteral("accessible"), o.accessible},
		{QStringLiteral("workspace"), o.workspace},
		{QStringLiteral("font_scale"), 1.0}, {QStringLiteral("density"), o.accessible ? QStringLiteral("comfortable") : QStringLiteral("standard")},
		{QStringLiteral("restart_pending"), !(o == startedWith())}};
	const QJsonObject timing = applyTiming();
	for (auto it = timing.begin(); it != timing.end(); ++it) { out.insert(it.key(), it.value()); }
	return out;
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
	applyStoredScale(QString());
}

void applyStoredScale(const QString& configFile)
{
	if (!qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR")) { return; }
	const std::optional<double> configured = selectedConfigScale(configFile);
	const double scale = configured.value_or(storedScale());
	if (scale > 0.0 && scale != 1.0) { qputenv("QT_SCALE_FACTOR", QByteArray::number(scale)); }
}

} // namespace lmms::appearance
