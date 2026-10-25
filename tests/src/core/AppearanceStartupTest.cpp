/*
 * AppearanceStartupTest.cpp - the scale Qt receives before the GUI exists
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

#include <QFile>
#include <QTemporaryDir>

#include "Appearance.h"

using namespace lmms;

namespace
{

template<typename T>
void applyStartupScale(const T& configFile)
{
	if constexpr (requires { appearance::applyStoredScale(configFile); })
	{
		appearance::applyStoredScale(configFile);
	}
	else
	{
		// The original API had only the fallback-file path. Keeping this branch
		// lets the regression compile against it and fail on the wrong scale.
		appearance::applyStoredScale();
	}
}

} // namespace

class AppearanceStartupTest : public QObject
{
	Q_OBJECT

private slots:
	void init()
	{
		QVERIFY(m_dir.isValid());
		m_scaleFile = m_dir.filePath(QStringLiteral("stored-scale"));
		qputenv("ZENE_UI_SCALE_FILE", m_scaleFile.toLocal8Bit());
		qunsetenv("QT_SCALE_FACTOR");
	}

	void cleanup()
	{
		qunsetenv("ZENE_UI_SCALE_FILE");
		qunsetenv("QT_SCALE_FACTOR");
	}

	void selectedConfigScaleWinsOverTheFallbackFile()
	{
		QVERIFY(writeText(m_scaleFile, QStringLiteral("1.25")));
		const QString configPath = m_dir.filePath(QStringLiteral("alternate.xml"));
		QVERIFY(writeText(configPath, QStringLiteral("<lmms><ui scale=\"2\"/></lmms>")));

		applyStartupScale(configPath);

		QCOMPARE(qgetenv("QT_SCALE_FACTOR"), QByteArrayLiteral("2"));
	}

	void configWithoutScaleUsesTheStoredFallback()
	{
		QVERIFY(writeText(m_scaleFile, QStringLiteral("1.25")));
		const QString configPath = m_dir.filePath(QStringLiteral("alternate.xml"));
		QVERIFY(writeText(configPath, QStringLiteral("<lmms><ui palette=\"light\"/></lmms>")));

		applyStartupScale(configPath);

		QCOMPARE(qgetenv("QT_SCALE_FACTOR"), QByteArrayLiteral("1.25"));
	}

	void explicitQtScaleStillWins()
	{
		QVERIFY(writeText(m_scaleFile, QStringLiteral("1.25")));
		const QString configPath = m_dir.filePath(QStringLiteral("alternate.xml"));
		QVERIFY(writeText(configPath, QStringLiteral("<lmms><ui scale=\"2\"/></lmms>")));
		qputenv("QT_SCALE_FACTOR", QByteArrayLiteral("1.5"));

		applyStartupScale(configPath);

		QCOMPARE(qgetenv("QT_SCALE_FACTOR"), QByteArrayLiteral("1.5"));
	}

private:
	bool writeText(const QString& path, const QString& contents)
	{
		QFile file(path);
		return file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)
			&& file.write(contents.toUtf8()) == contents.toUtf8().size();
	}

	QTemporaryDir m_dir;
	QString m_scaleFile;
};

QTEST_GUILESS_MAIN(AppearanceStartupTest)
#include "AppearanceStartupTest.moc"
