/*
 * VstSubPluginFeaturesTest.cpp - VST2 plugin directory scanning
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
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <filesystem>
#include <system_error>

#include "ConfigManager.h"
#include "Plugin.h"
#include "VstSubPluginFeatures.h"

using namespace lmms;

class VstSubPluginFeaturesTest : public QObject
{
	Q_OBJECT

private slots:
	void directorySymlinkToAncestorIsNotTraversed()
	{
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const QString vstRoot = QDir(temporary.path()).filePath(QStringLiteral("vst"));
		QVERIFY(QDir().mkpath(vstRoot));

#ifdef LMMS_BUILD_LINUX
		const QString pluginName = QStringLiteral("fixture.so");
#else
		const QString pluginName = QStringLiteral("fixture.dll");
#endif
		QFile plugin(QDir(vstRoot).filePath(pluginName));
		QVERIFY(plugin.open(QIODevice::WriteOnly));
		QCOMPARE(plugin.write("fixture"), qint64(7));
		plugin.close();

		const QString loopPath = QDir(vstRoot).filePath(QStringLiteral("ancestor"));
		std::error_code error;
		std::filesystem::create_directory_symlink(vstRoot.toStdString(), loopPath.toStdString(), error);
		QVERIFY2(!error, qPrintable(QString::fromLocal8Bit(error.message().c_str())));

		ConfigManager::inst()->setVSTDir(vstRoot);
		VstSubPluginFeatures features(Plugin::Type::Effect);
		Plugin::Descriptor::SubPluginFeatures::KeyList keys;
		features.listSubPluginKeys(nullptr, keys);

		QCOMPARE(keys.size(), 1);
		QCOMPARE(keys.constFirst().attributes.value(QStringLiteral("file")), pluginName);
	}
};

QTEST_MAIN(VstSubPluginFeaturesTest)
#include "VstSubPluginFeaturesTest.moc"
