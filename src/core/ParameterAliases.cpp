/*
 * ParameterAliases.cpp - the names a user gives a device's parameters, keyed by each parameter's stable key
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

#include "ParameterAliases.h"

#include <QHash>
#include <QSet>

#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "DeviceOptions.h"
#include "Effect.h"
#include "ParameterControls.h"

namespace lmms::parameteraliases
{

namespace
{

const QString kKeyPrefix = QStringLiteral("id:");

//! A display name an older project keyed an alias by: anything that is not a key.
QString legacyAlias(const DeviceOptions& options, const QString& name)
{
	return name.startsWith(kKeyPrefix) ? QString() : options.alias(name);
}

//! The key the device gives @a model itself: the model's own name, else its ParameterControls key; empty if none.
QString ownKeyOf(const AutomatableModel* model, const ParameterControls* registered)
{
	if (!model->objectName().isEmpty()) { return model->objectName(); }
	if (registered == nullptr) { return {}; }
	for (const auto& [parameter, key] : registered->parameters())
	{
		if (parameter == model) { return key; }
	}
	return {};
}

//! The key of the @a count-th parameter naming itself @a raw: the name itself first, then
//! "#2", "#3", ... after it - skipping any that @a taken already holds, so a device that owns
//! both a port "gain" twice and a port named "gain#2" still keys each parameter on its own.
QString uniqueKey(const QString& raw, int count, const QSet<QString>& taken)
{
	QString key = count > 1 ? QStringLiteral("%1#%2").arg(raw).arg(count) : raw;
	while (taken.contains(kKeyPrefix + key))
	{
		++count;
		key = QStringLiteral("%1#%2").arg(raw).arg(count);
	}
	return kKeyPrefix + key;
}

//! The key of every parameter in @a all, @a effect's whole list in its own order.
QStringList listKeys(Effect* effect, const QList<AutomatableModel*>& all)
{
	const auto* registered = dynamic_cast<const ParameterControls*>(effect->controls());
	QStringList keys;
	QHash<QString, int> seen;
	QSet<QString> taken;
	for (int i = 0; i < all.size(); ++i)
	{
		QString key = ownKeyOf(all.at(i), registered);
		if (key.isEmpty()) { key = QStringLiteral("#%1").arg(i); }
		keys << uniqueKey(key, ++seen[key], taken);
		taken.insert(keys.last());
	}
	return keys;
}

//! The alias of the parameter keyed @a key and named @a name: by its key, else by the name an older project used.
QString aliasFor(const DeviceOptions& options, const QString& key, const QString& name)
{
	const QString keyed = key.isEmpty() ? QString() : options.alias(key);
	return keyed.isEmpty() ? legacyAlias(options, name) : keyed;
}

} // namespace

QStringList keysOf(Effect* effect, const QList<AutomatableModel*>& models)
{
	const QList<AutomatableModel*> all = controlEffectParameters(effect);
	const QStringList keys = listKeys(effect, all);
	QHash<const AutomatableModel*, int> position;
	for (int i = 0; i < all.size(); ++i) { position.insert(all.at(i), i); }
	QStringList out;
	for (const AutomatableModel* model : models)
	{
		const int at = position.value(model, -1);
		out << (at >= 0 ? keys.at(at) : QString());
	}
	return out;
}

QStringList aliasesOf(Effect* effect, const QList<AutomatableModel*>& models, const DeviceOptions& options)
{
	const QStringList keys = keysOf(effect, models);
	QStringList out;
	for (int i = 0; i < models.size(); ++i) { out << aliasFor(options, keys.at(i), models.at(i)->displayName()); }
	return out;
}

QStringList shownNamesOf(Effect* effect, const QList<AutomatableModel*>& models)
{
	QStringList out = aliasesOf(effect, models, effect->deviceOptions());
	for (int i = 0; i < out.size(); ++i)
	{
		if (out.at(i).isEmpty()) { out[i] = models.at(i)->displayName(); }
	}
	return out;
}

void setAlias(Effect* effect, AutomatableModel* model, const QString& alias, DeviceOptions* options)
{
	adoptLegacyAliases(effect, options);
	const QString name = model->displayName();
	const QString key = keysOf(effect, {model}).value(0);
	// A model outside the device's list has no key: it keeps the name it was always keyed by.
	options->setAlias(key.isEmpty() ? name : key, alias.trimmed() == name ? QString() : alias);
}

bool adoptLegacyAliases(Effect* effect, DeviceOptions* options)
{
	const QList<AutomatableModel*> all = controlEffectParameters(effect);
	const QStringList keys = listKeys(effect, all);
	DeviceOptions adopted = *options;
	QSet<QString> moved;
	for (int i = 0; i < all.size(); ++i)
	{
		const QString name = all.at(i)->displayName();
		const QString alias = legacyAlias(*options, name);
		if (alias.isEmpty()) { continue; }
		if (adopted.alias(keys.at(i)).isEmpty())
		{
			adopted.setAlias(keys.at(i), alias);
			if (!adopted.alias(keys.at(i)).isEmpty()) { moved.insert(name); }
		}
	}
	for (const QString& name : moved) { adopted.setAlias(name, QString()); }
	if (!moved.isEmpty()) { *options = adopted; }
	return !moved.isEmpty();
}

QJsonObject toJson(Effect* effect, const DeviceOptions& options)
{
	const QList<AutomatableModel*> all = controlEffectParameters(effect);
	const QStringList keys = listKeys(effect, all);
	QJsonObject out;
	QSet<QString> reported;
	const QSet<QString> stableKeys(keys.cbegin(), keys.cend());
	QSet<QString> emitted;  // every JSON key written so far: an object names each entry once
	for (int i = 0; i < all.size(); ++i)
	{
		const QString name = all.at(i)->displayName();
		const QString alias = aliasFor(options, keys.at(i), name);
		if (alias.isEmpty()) { continue; }
		// A display name is the key only when no other entry can need it: it is not another
		// parameter's stable key (that parameter may be reported under it) and no earlier entry
		// took it. Otherwise the entry is reported under its own stable key, which no display
		// name can take, so no alias overwrites another's.
		const bool nameIsFree = !stableKeys.contains(name) && !emitted.contains(name);
		const QString key = nameIsFree ? name : keys.at(i);
		out.insert(key, alias);
		emitted.insert(key);
		reported << keys.at(i) << name;
	}
	for (auto it = options.aliases().cbegin(); it != options.aliases().cend(); ++it)
	{
		if (!reported.contains(it.key()) && !emitted.contains(it.key())) { out.insert(it.key(), it.value()); }
	}
	return out;
}

} // namespace lmms::parameteraliases
