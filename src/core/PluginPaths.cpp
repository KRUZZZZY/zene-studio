/*
 * PluginPaths.cpp - the folders the VST3, CLAP and LV2 hosts scan for plug-ins
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
#include "PluginPaths.h"

#include <QDir>
#include <QFileInfo>

#include "ConfigManager.h"

namespace lmms::pluginpaths
{

namespace
{

const QString kSection = QStringLiteral("paths");

QString keyOf(Format format) { return formatNames().at(static_cast<int>(format)) + QStringLiteral("dirs"); }

QStringList fromEnv(const char* name)
{
	return QString::fromLocal8Bit(qgetenv(name)).split(QDir::listSeparator(), Qt::SkipEmptyParts);
}

QString home(const char* sub) { return QDir::homePath() + QLatin1Char('/') + QLatin1String(sub); }

} // namespace

const QStringList& formatNames()
{
	static const QStringList names{QStringLiteral("vst3"), QStringLiteral("clap"), QStringLiteral("lv2")};
	return names;
}

bool formatFromName(const QString& name, Format* format)
{
	const int index = formatNames().indexOf(name);
	if (index < 0) { return false; }
	*format = static_cast<Format>(index);
	return true;
}

QStringList configured(Format format)
{
	return ConfigManager::inst()->value(kSection, keyOf(format)).split(QLatin1Char(';'), Qt::SkipEmptyParts);
}

void setConfigured(Format format, const QStringList& dirs)
{
	QStringList clean;
	for (const QString& dir : dirs)
	{
		const QString trimmed = dir.trimmed();
		if (!trimmed.isEmpty() && !clean.contains(trimmed)) { clean << trimmed; }
	}
	ConfigManager::inst()->setValue(kSection, keyOf(format), clean.join(QLatin1Char(';')));
}

QStringList defaults(Format format)
{
	const QString vstDir = ConfigManager::inst()->vstDir();
	switch (format)
	{
	case Format::Vst3:
		return {vstDir, home(".vst3"), QStringLiteral("/usr/lib/vst3"), QStringLiteral("/usr/local/lib/vst3")};
	case Format::Clap:
		return fromEnv("CLAP_PATH") + QStringList{home(".clap"), QStringLiteral("/usr/lib/clap"), vstDir};
	case Format::Lv2:
	{
		const QStringList env = fromEnv("LV2_PATH");
		return env.isEmpty() ? QStringList{home(".lv2"), QStringLiteral("/usr/lib/lv2"), QStringLiteral("/usr/local/lib/lv2")} : env;
	}
	}
	return {};
}

QStringList searchDirs(Format format)
{
	QStringList out;
	QStringList seen;
	for (const QString& dir : configured(format) + defaults(format))
	{
		const QFileInfo info(dir);
		const QString canonical = info.canonicalFilePath();
		if (dir.isEmpty() || !info.isDir() || canonical.isEmpty() || seen.contains(canonical)) { continue; }
		seen << canonical;
		out << dir;
	}
	return out;
}

} // namespace lmms::pluginpaths
