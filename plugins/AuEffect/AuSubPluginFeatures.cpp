/*
 * AuSubPluginFeatures.cpp - the Audio Unit components a host module offers (Q050-491, macOS only)
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
 */

#include "AuSubPluginFeatures.h"

#include <QLabel>
#include <QVBoxLayout>

#include "AuComponent.h"
#include "AuHost.h"
#include "PluginFactory.h"
#include "PluginScanCache.h"
#include "PluginVendors.h"

namespace lmms
{

AuSubPluginFeatures::AuSubPluginFeatures(Plugin::Type type) :
	SubPluginFeatures(type)
{
}

auto AuSubPluginFeatures::displayName(const Key& key) const -> QString
{
	return key.attributes.value(QStringLiteral("name"), key.attributes.value(QStringLiteral("component"), key.name));
}

auto AuSubPluginFeatures::description(const Key& key) const -> QString
{
	return QObject::tr("Audio Unit: %1").arg(key.attributes.value(QStringLiteral("component")));
}

void AuSubPluginFeatures::listSubPluginKeys(const Plugin::Descriptor* descriptor, KeyList& result) const
{
	PluginScanCache& cache = getPluginFactory()->scanCache();
	const au::ScanOutcome outcome = au::recordScan(au::listRegistry(m_type == Plugin::Type::Instrument), &cache);
	if (cache.isPersistent() && cache.isDirty()) { cache.save(); }
	for (const au::Component& component : outcome.listed)
	{
		const QString key = au::componentKey(component.id);
		// Who made it, for plugin.list's vendor and the picker's Vendor column; not part of the identity.
		pluginvendors::remember(QStringLiteral("au"), key, component.vendor);
		Key entry;
		entry.desc = descriptor;
		entry.name = component.name;
		entry.attributes[QStringLiteral("component")] = key;
		entry.attributes[QStringLiteral("name")] = component.name;
		result.push_back(entry);
	}
}

void AuSubPluginFeatures::fillDescriptionWidget(QWidget* parent, const Key* key) const
{
	if (key == nullptr || !key->isValid()) { return; }
	auto* layout = new QVBoxLayout(parent);
	layout->addWidget(new QLabel(QObject::tr("<h3>%1</h3>").arg(displayName(*key).toHtmlEscaped()), parent));
	layout->addWidget(new QLabel(QObject::tr("Component: %1")
		.arg(key->attributes.value(QStringLiteral("component")).toHtmlEscaped()), parent));
	layout->addStretch();
}

} // namespace lmms
