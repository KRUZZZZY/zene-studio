/*
 * MixerZones.cpp - the mixer's dockable regions and their resizable separators
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

#include "MixerZones.h"

#include <algorithm>

#include <QJsonValue>

namespace lmms::gui::mixerzones
{

namespace
{

const QString kMaster = QStringLiteral("master");
const QString kChannels = QStringLiteral("channels");
const QString kVca = QStringLiteral("vca");
const QString kRack = QStringLiteral("rack");

const QString kLeft = QStringLiteral("left");
const QString kMiddle = QStringLiteral("middle");
const QString kRight = QStringLiteral("right");

//! A stored width is usable only between the bounds a caller may set: anything else is the default.
bool usableSize(int size)
{
	return size >= MinZoneSize && size <= MaxZoneSize;
}

} // namespace

const QStringList& regionIds()
{
	static const QStringList ids{kMaster, kChannels, kVca, kRack};
	return ids;
}

const QStringList& sideRegionIds()
{
	static const QStringList ids{kMaster, kVca, kRack};
	return ids;
}

const QStringList& dockNames()
{
	static const QStringList names{kLeft, kMiddle, kRight};
	return names;
}

bool isRegion(const QString& id)
{
	return regionIds().contains(id);
}

QString regionTitle(const QString& id)
{
	if (id == kMaster) { return QObject::tr("Master Strip"); }
	if (id == kChannels) { return QObject::tr("Channels"); }
	if (id == kVca) { return QObject::tr("VCA Strips"); }
	if (id == kRack) { return QObject::tr("Effect Rack"); }
	return QString();
}

QString dockTitle(const QString& dock)
{
	if (dock == kLeft) { return QObject::tr("Left"); }
	if (dock == kMiddle) { return QObject::tr("Middle"); }
	if (dock == kRight) { return QObject::tr("Right"); }
	return QString();
}

int defaultSize(const QString& id)
{
	// The widths the fixed layout already used: the master strip's hint, the VCA area's own strip, and
	// the rack's DEFAULT_WIDTH (EffectRackView::DEFAULT_WIDTH).
	if (id == kMaster) { return 100; }
	if (id == kVca) { return 90; }
	if (id == kRack) { return 245; }
	return 0;
}

Layout defaultLayout()
{
	Layout layout;
	layout.docks.insert(kMaster, kLeft);
	layout.docks.insert(kChannels, kMiddle);
	layout.docks.insert(kVca, kRight);
	layout.docks.insert(kRack, kRight);
	for (const QString& id : regionIds())
	{
		if (id != kChannels) { layout.sizes.insert(id, defaultSize(id)); }
	}
	return layout;
}

QJsonObject toJson(const Layout& layout)
{
	QJsonObject zones;
	for (const QString& id : regionIds())
	{
		const QString dock = layout.docks.value(id);
		if (!dock.isEmpty()) { zones.insert(id, dock); }
	}
	QJsonObject sizes;
	for (const QString& id : regionIds())
	{
		const int size = layout.sizes.value(id, 0);
		if (usableSize(size)) { sizes.insert(id, size); }
	}
	return QJsonObject{{QStringLiteral("zones"), zones}, {QStringLiteral("sizes"), sizes}};
}

Layout fromJson(const QJsonObject& doc)
{
	Layout layout = defaultLayout();
	const QJsonObject zones = doc.value(QStringLiteral("zones")).toObject();
	for (auto it = zones.begin(); it != zones.end(); ++it)
	{
		if (!isRegion(it.key())) { continue; }
		const QString dock = it.value().toString();
		if (!dockNames().contains(dock)) { continue; }
		if (it.key() == kChannels && dock != kMiddle) { continue; }  // the scrolling area never leaves the middle
		layout.docks.insert(it.key(), dock);
	}
	// A side region stored as "middle" is where the channels are: it would be drawn on top of them. Keep
	// the default dock for it instead of inventing a second middle.
	for (const QString& id : sideRegionIds())
	{
		if (layout.docks.value(id) == kMiddle) { layout.docks.insert(id, defaultLayout().docks.value(id)); }
	}
	const QJsonObject sizes = doc.value(QStringLiteral("sizes")).toObject();
	for (auto it = sizes.begin(); it != sizes.end(); ++it)
	{
		if (!isRegion(it.key())) { continue; }
		const int size = it.value().toInt(0);
		if (usableSize(size)) { layout.sizes.insert(it.key(), size); }
	}
	return layout;
}

bool parse(const QJsonObject& args, Layout* base, QString* error)
{
	Layout next = *base;
	if (args.contains(QStringLiteral("zones")))
	{
		const QJsonValue zonesValue = args.value(QStringLiteral("zones"));
		if (!zonesValue.isObject())
		{
			*error = QStringLiteral("'zones' is an object of region id -> dock (%1)").arg(dockNames().join(QStringLiteral(" / ")));
			return false;
		}
		const QJsonObject zones = zonesValue.toObject();
		for (auto it = zones.begin(); it != zones.end(); ++it)
		{
			if (!isRegion(it.key()))
			{
				*error = QStringLiteral("'%1' is not a mixer region (%2)").arg(it.key(), regionIds().join(QStringLiteral(" / ")));
				return false;
			}
			if (!it.value().isString() || !dockNames().contains(it.value().toString()))
			{
				*error = QStringLiteral("'%1' docks %2").arg(it.key(), dockNames().join(QStringLiteral(" / ")));
				return false;
			}
			const QString dock = it.value().toString();
			if (it.key() == QStringLiteral("channels") && dock != QStringLiteral("middle"))
			{
				*error = QStringLiteral("the channels are the scrolling area; they stay in the middle");
				return false;
			}
			if (it.key() != QStringLiteral("channels") && dock == QStringLiteral("middle"))
			{
				*error = QStringLiteral("'%1' docks left or right; the middle holds the channels").arg(it.key());
				return false;
			}
			next.docks.insert(it.key(), dock);
		}
	}
	if (args.contains(QStringLiteral("sizes")))
	{
		const QJsonValue sizesValue = args.value(QStringLiteral("sizes"));
		if (!sizesValue.isObject())
		{
			*error = QStringLiteral("'sizes' is an object of region id -> pixels (%1..%2)")
				.arg(MinZoneSize).arg(MaxZoneSize);
			return false;
		}
		const QJsonObject sizes = sizesValue.toObject();
		for (auto it = sizes.begin(); it != sizes.end(); ++it)
		{
			if (!isRegion(it.key()))
			{
				*error = QStringLiteral("'%1' is not a mixer region (%2)").arg(it.key(), regionIds().join(QStringLiteral(" / ")));
				return false;
			}
			if (!it.value().isDouble() || !usableSize(it.value().toInt()))
			{
				*error = QStringLiteral("'%1' is a width in pixels, %2..%3").arg(it.key()).arg(MinZoneSize).arg(MaxZoneSize);
				return false;
			}
			next.sizes.insert(it.key(), it.value().toInt());
		}
	}
	*base = next;
	return true;
}

MixerZoneState::MixerZoneState() :
	m_layout(defaultLayout())
{
}

MixerZoneState* MixerZoneState::instance()
{
	static MixerZoneState* state = new MixerZoneState();
	return state;
}

void MixerZoneState::set(const Layout& layout)
{
	m_layout = layout;
	emit changed();
}

void MixerZoneState::setSizes(const QHash<QString, int>& sizes)
{
	m_layout.sizes = sizes;
	emit sizesChanged();
}

void MixerZoneState::reset()
{
	set(defaultLayout());
}

} // namespace lmms::gui::mixerzones
