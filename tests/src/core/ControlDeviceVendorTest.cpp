/*
 * ControlDeviceVendorTest.cpp - plugin.list's `vendor` is the device picker's
 *                               vendor (Q050-090).
 *
 * Every device entry carries who made it, as its own format reports it, and the
 * device picker reads the same core string and only adds its "Not reported"
 * words - the wire and the window cannot disagree. Split out of
 * ControlDeviceCatalogueTest.cpp, which the file-length ratchet had already
 * flagged, so the vendor proof lives in its own registered binary.
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

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "DevicePicker.h"
#include "Engine.h"

using namespace lmms;

namespace
{

//! The plugin.list entry of @a format named @a name (its "name" or "display_name"), or an empty object.
QJsonObject listedDevice(const QString& format, const QString& name)
{
	const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("plugin.list"),
		QJsonObject{{QStringLiteral("format"), format}});
	for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
	{
		const QJsonObject device = value.toObject();
		if (device.value(QStringLiteral("name")).toString() == name
			|| device.value(QStringLiteral("display_name")).toString() == name)
		{
			return device;
		}
	}
	return QJsonObject();
}

} // namespace

//! plugin.list's `vendor` is the picker's vendor, minus the window's
//! "Not reported" words - the wire and the window cannot disagree.
class ControlDeviceVendorTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// Tell PluginFactory where the build-tree plugins live *before* it is
		// first instantiated (it reads LMMS_PLUGIN_DIR in its constructor).
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void listedVendorIsThePickerVendor()
	{
		const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("plugin.list"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		const QJsonArray devices = listed.result.value(QStringLiteral("devices")).toArray();
		QVERIFY(!devices.isEmpty());
		for (const QJsonValue& value : devices)
		{
			const QJsonObject device = value.toObject();
			const QString id = device.value(QStringLiteral("id")).toString();
			QVERIFY2(device.contains(QStringLiteral("vendor")),
				qPrintable(QStringLiteral("device %1 carries no vendor").arg(id)));
			ControlDeviceEntry entry;
			int index = -1;
			ControlResult error;
			QVERIFY(controlDeviceById(id, &entry, &index, &error));
			const QString picker = gui::devicelabels::vendorOf(entry);
			const QString wire = device.value(QStringLiteral("vendor")).toString();
			if (wire.isEmpty())
			{
				QCOMPARE(picker, QStringLiteral("Not reported"));
			}
			else
			{
				QCOMPARE(wire, picker);
			}
		}
		// A built-in names its maker.
		const QJsonObject amplifier = listedDevice(QStringLiteral("builtin"), QStringLiteral("amplifier"));
		QVERIFY(!amplifier.isEmpty());
		QCOMPARE(amplifier.value(QStringLiteral("vendor")).toString(), QStringLiteral("Zene Studio"));
	}
};

QTEST_GUILESS_MAIN(ControlDeviceVendorTest)
#include "ControlDeviceVendorTest.moc"
