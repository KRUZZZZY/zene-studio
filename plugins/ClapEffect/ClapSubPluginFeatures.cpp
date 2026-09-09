/*
 * ClapSubPluginFeatures.cpp - CLAP module discovery for the plugin browser
 *
 * Copyright (c) 2026 LMMS contributors
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

#include "ClapSubPluginFeatures.h"

#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QVBoxLayout>

#include "ClapHost.h"
#include "ConfigManager.h"

namespace lmms
{

namespace
{
//! stop descending at this depth so that a stray symlink loop cannot hang us
constexpr int MaxScanDepth = 4;
}

ClapSubPluginFeatures::ClapSubPluginFeatures(Plugin::Type type) :
	SubPluginFeatures(type)
{
}

auto ClapSubPluginFeatures::displayName(const Key& key) const -> QString
{
	return key.attributes.value(QStringLiteral("name"),
		key.attributes.value(QStringLiteral("id"), key.name));
}

auto ClapSubPluginFeatures::description(const Key& key) const -> QString
{
	return QObject::tr("CLAP: %1").arg(QFileInfo{key.attributes.value(QStringLiteral("file"))}.fileName());
}

auto ClapSubPluginFeatures::additionalFileExtensions(const Key&) const -> QString
{
	return QStringLiteral("clap");
}

void ClapSubPluginFeatures::collectModules(const QString& dir, QStringList& result, int depth)
{
	if (depth > MaxScanDepth) { return; }
	QDir directory{dir};
	if (!directory.exists()) { return; }

	// CLAP modules are shared libraries with a .clap extension; some packages
	// ship them as a directory bundle with the same extension.
	const auto entries = directory.entryInfoList(
		{QStringLiteral("*.clap")}, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
	for (const auto& entry : entries)
	{
		result.push_back(entry.absoluteFilePath());
	}

	const auto subDirs = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto& subDir : subDirs)
	{
		collectModules(subDir.absoluteFilePath(), result, depth + 1);
	}
}

void ClapSubPluginFeatures::listSubPluginKeys(const Plugin::Descriptor* descriptor,
	KeyList& result) const
{
	QStringList modules;
	collectModules(ConfigManager::inst()->vstDir(), modules, 0);

	const bool wantInstrument = m_type == Plugin::Type::Instrument;
	for (const auto& path : modules)
	{
		QString error;
		for (const auto& info : clap::listClasses(path, &error))
		{
			if (info.isInstrument != wantInstrument) { continue; }

			Key key;
			key.desc = descriptor;
			key.name = info.name;
			key.attributes[QStringLiteral("file")] = path;
			key.attributes[QStringLiteral("id")] = info.id;
			key.attributes[QStringLiteral("name")] = info.name;
			result.push_back(key);
		}
	}
}

void ClapSubPluginFeatures::fillDescriptionWidget(QWidget* parent, const Key* key) const
{
	if (!key || !key->isValid()) { return; }

	auto* layout = new QVBoxLayout(parent);
	layout->addWidget(new QLabel(
		QObject::tr("<h3>%1</h3>").arg(displayName(*key).toHtmlEscaped()), parent));
	layout->addWidget(new QLabel(
		QObject::tr("Id: %1").arg(key->attributes.value(QStringLiteral("id")).toHtmlEscaped()),
		parent));
	layout->addWidget(new QLabel(
		QObject::tr("Module: %1").arg(key->attributes.value(QStringLiteral("file")).toHtmlEscaped()),
		parent));
	layout->addStretch();
}

} // namespace lmms
