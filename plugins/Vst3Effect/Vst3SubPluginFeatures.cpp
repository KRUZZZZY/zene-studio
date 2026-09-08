/*
 * Vst3SubPluginFeatures.cpp - VST3 bundle discovery for the plugin browser
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

#include "Vst3SubPluginFeatures.h"

#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "Vst3Host.h"

namespace lmms
{

namespace
{
//! stop descending at this depth so that a stray symlink loop cannot hang us
constexpr int MaxScanDepth = 4;
}

Vst3SubPluginFeatures::Vst3SubPluginFeatures(Plugin::Type type) :
	SubPluginFeatures(type)
{
}

auto Vst3SubPluginFeatures::displayName(const Key& key) const -> QString
{
	return key.attributes.value(QStringLiteral("class"), key.name);
}

auto Vst3SubPluginFeatures::description(const Key& key) const -> QString
{
	return QObject::tr("VST3: %1").arg(QFileInfo{key.attributes.value(QStringLiteral("file"))}.fileName());
}

auto Vst3SubPluginFeatures::additionalFileExtensions(const Key&) const -> QString
{
	return QStringLiteral("vst3");
}

void Vst3SubPluginFeatures::collectModules(const QString& dir, QStringList& result, int depth)
{
	if (depth > MaxScanDepth) { return; }
	QDir directory{dir};
	if (!directory.exists()) { return; }

	const auto entries = directory.entryInfoList(
		{QStringLiteral("*.vst3"), QStringLiteral("*.so")},
		QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
	for (const auto& entry : entries)
	{
		if (entry.isDir() && entry.fileName().endsWith(QStringLiteral(".vst3")))
		{
			result.push_back(entry.absoluteFilePath());
			continue;
		}
		if (entry.isFile() && entry.fileName().endsWith(QStringLiteral(".vst3")))
		{
			result.push_back(entry.absoluteFilePath());
		}
	}

	const auto subDirs = directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto& subDir : subDirs)
	{
		collectModules(subDir.absoluteFilePath(), result, depth + 1);
	}
}

void Vst3SubPluginFeatures::listSubPluginKeys(const Plugin::Descriptor* descriptor,
	KeyList& result) const
{
	QStringList modules;
	collectModules(ConfigManager::inst()->vstDir(), modules, 0);

	const bool wantInstrument = m_type == Plugin::Type::Instrument;
	for (const auto& path : modules)
	{
		QString error;
		for (const auto& info : vst3::listClasses(path, &error))
		{
			if (info.isInstrument != wantInstrument) { continue; }

			Key key;
			key.desc = descriptor;
			key.name = info.name;
			key.attributes[QStringLiteral("file")] = path;
			key.attributes[QStringLiteral("class")] = info.name;
			result.push_back(key);
		}
	}
}

void Vst3SubPluginFeatures::fillDescriptionWidget(QWidget* parent, const Key* key) const
{
	if (!key || !key->isValid()) { return; }

	auto* layout = new QVBoxLayout(parent);
	layout->addWidget(new QLabel(
		QObject::tr("<h3>%1</h3>").arg(displayName(*key).toHtmlEscaped()), parent));
	layout->addWidget(new QLabel(
		QObject::tr("Class: %1").arg(key->attributes.value(QStringLiteral("class")).toHtmlEscaped()),
		parent));
	layout->addWidget(new QLabel(
		QObject::tr("Module: %1").arg(key->attributes.value(QStringLiteral("file")).toHtmlEscaped()),
		parent));
	layout->addStretch();
}

} // namespace lmms
