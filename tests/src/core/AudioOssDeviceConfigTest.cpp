/*
 * AudioOssDeviceConfigTest.cpp - the OSS device chosen in the setup widget
 *                                must be the device probed at startup
 *
 * Copyright (c) LMMS contributors
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <QtTest>

#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QWidget>

#include <vector>

#include "AudioOss.h"
#include "ConfigManager.h"
#include "Engine.h"

namespace lmms
{

namespace
{

// Sentinel default used to tell an absent key apart from an empty one.
const QString kAbsent = QStringLiteral("\x01<absent>");

struct SavedKey
{
	QString cls;
	QString attribute;
	QString value;
	bool present;
};

SavedKey snapshotKey(const QString& cls, const QString& attribute)
{
	const QString v = ConfigManager::inst()->value(cls, attribute, kAbsent);
	return SavedKey{cls, attribute, v == kAbsent ? QString() : v, v != kAbsent};
}

void restoreKey(const SavedKey& key)
{
	if (key.present)
	{
		ConfigManager::inst()->setValue(key.cls, key.attribute, key.value);
	}
	else
	{
		ConfigManager::inst()->deleteValue(key.cls, key.attribute);
	}
}

// Creates a writable file inside the given directory and returns its path.
QString makeWritableDevice(QTemporaryDir& dir, const QString& name)
{
	QFile file(dir.filePath(name));
	if (!file.open(QIODevice::WriteOnly))
	{
		return QString();
	}
	return file.fileName();
}

} // namespace

class AudioOssDeviceConfigTest : public QObject
{
	Q_OBJECT

private:
	std::vector<SavedKey> m_savedKeys;
	bool m_hadAudioDev = false;
	QByteArray m_savedAudioDev;

	static void clearDeviceKeys()
	{
		ConfigManager::inst()->deleteValue("audiooss", "device");
		ConfigManager::inst()->deleteValue("AudioOss", "Device");
	}

private slots:
	void initTestCase()
	{
		m_savedKeys = {
			snapshotKey("audiooss", "device"),
			snapshotKey("AudioOss", "Device"),
			snapshotKey("audiooss", "channels"),
		};
		m_hadAudioDev = qEnvironmentVariableIsSet("AUDIODEV");
		m_savedAudioDev = qgetenv("AUDIODEV");
		Engine::init(true);  // a model needs the engine, as in KnobKeyboardTest
	}

	void cleanupTestCase()
	{
		for (const auto& key : m_savedKeys)
		{
			restoreKey(key);
		}
		if (m_hadAudioDev)
		{
			qputenv("AUDIODEV", m_savedAudioDev);
		}
		else
		{
			qunsetenv("AUDIODEV");
		}
		Engine::destroy();
	}

	// A device saved through the setup widget must be the one probed on the next startup.
	void savedDeviceIsProbedOnNextStartup()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString tempPath = makeWritableDevice(dir, "saved-dsp");
		QVERIFY(!tempPath.isEmpty());

		clearDeviceKeys();
		qunsetenv("AUDIODEV");

		QWidget parent;
		AudioOss::setupWidget widget(&parent);
		QLineEdit *edit = widget.findChild<QLineEdit*>();
		QVERIFY(edit != nullptr);
		edit->setText(tempPath);
		widget.saveSettings();

		QCOMPARE(AudioOss::probeDevice(), tempPath);
	}

	// A device saved through the setup widget must prefill the next setup widget.
	void savedDevicePrefillsTheNextSetupWidget()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString tempPath = makeWritableDevice(dir, "prefill-dsp");
		QVERIFY(!tempPath.isEmpty());

		clearDeviceKeys();
		qunsetenv("AUDIODEV");

		QWidget parent;
		AudioOss::setupWidget first(&parent);
		QLineEdit *firstEdit = first.findChild<QLineEdit*>();
		QVERIFY(firstEdit != nullptr);
		firstEdit->setText(tempPath);
		first.saveSettings();

		QWidget parentNext;
		AudioOss::setupWidget next(&parentNext);
		QLineEdit *nextEdit = next.findChild<QLineEdit*>();
		QVERIFY(nextEdit != nullptr);
		QCOMPARE(nextEdit->text(), tempPath);
	}

	// With no saved device, AUDIODEV still takes precedence over the default device.
	void emptyKeyKeepsEnvironmentPrecedence()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString envPath = makeWritableDevice(dir, "env-dsp");
		QVERIFY(!envPath.isEmpty());

		clearDeviceKeys();
		qputenv("AUDIODEV", envPath.toLocal8Bit());

		QCOMPARE(AudioOss::probeDevice(), envPath);
	}
};

} // namespace lmms

QTEST_MAIN(lmms::AudioOssDeviceConfigTest)
#include "AudioOssDeviceConfigTest.moc"
