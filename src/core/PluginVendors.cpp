/*
 * PluginVendors.cpp - who made each VST3 and CLAP class, as its own factory reports it
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

#include "PluginVendors.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace lmms::pluginvendors
{

namespace
{

QMutex& guard()
{
	static QMutex mutex;
	return mutex;
}

QHash<QString, QString>& vendors()
{
	static QHash<QString, QString> map;
	return map;
}

QString keyOf(const QString& module, const QString& id) { return module + QLatin1Char('\n') + id; }

} // namespace

void remember(const QString& module, const QString& id, const QString& vendor)
{
	const QMutexLocker lock(&guard());
	vendors().insert(keyOf(module, id), vendor.trimmed());
}

QString vendorOf(const QString& module, const QString& id)
{
	const QMutexLocker lock(&guard());
	return vendors().value(keyOf(module, id));
}

} // namespace lmms::pluginvendors
