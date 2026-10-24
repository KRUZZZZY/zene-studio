/*
 * ControlProjectTemplateTest.cpp - saving over a template is one undo step.
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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

class ControlProjectTemplateTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ConfigManager::inst()->setWorkingDir(m_dir.path());
		ConfigManager::inst()->setValue(QStringLiteral("app"), QStringLiteral("disablebackup"),
			QStringLiteral("0"));
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void replacingTemplateCanBeUndoneAndRedone_data()
	{
		QTest::addColumn<QString>("name");
		QTest::newRow("default") << QString();
		QTest::newRow("named") << QStringLiteral("user-template");
	}

	void replacingTemplateCanBeUndoneAndRedone()
	{
		QFETCH(QString, name);
		const QString fileName = name.isEmpty() ? QStringLiteral("default") : name;
		const QString path = ConfigManager::inst()->userTemplateDir() + fileName + QStringLiteral(".mpt");
		const QString backupPath = path + QStringLiteral(".bak");
		QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
		const QByteArray previous("previous template bytes");
		const QByteArray staleBackup("previous backup bytes");
		QFile original(path);
		QVERIFY(original.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QCOMPARE(original.write(previous), static_cast<qint64>(previous.size()));
		original.close();
		QFile oldBackup(backupPath);
		QVERIFY(oldBackup.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QCOMPARE(oldBackup.write(staleBackup), static_cast<qint64>(staleBackup.size()));
		oldBackup.close();

		QJsonObject args;
		if (!name.isEmpty()) { args.insert(QStringLiteral("name"), name); }
		const ControlResult saved = run(QStringLiteral("project.save_as_template"), args);
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QCOMPARE(saved.result.value(QStringLiteral("replaced")).toBool(), true);
		QFile published(path);
		QVERIFY(published.open(QIODevice::ReadOnly));
		const QByteArray current = published.readAll();
		QVERIFY(current != previous);
		QFile publishedBackup(backupPath);
		QVERIFY(publishedBackup.open(QIODevice::ReadOnly));
		QCOMPARE(publishedBackup.readAll(), previous);

		const ControlResult undo = run(QStringLiteral("control.undo"));
		QVERIFY2(undo.ok && undo.result.value(QStringLiteral("undone")).toBool(),
			qPrintable(undo.errorMessage));
		QFile restored(path);
		QVERIFY(restored.open(QIODevice::ReadOnly));
		QCOMPARE(restored.readAll(), previous);
		QFile restoredBackup(backupPath);
		QVERIFY(restoredBackup.open(QIODevice::ReadOnly));
		QCOMPARE(restoredBackup.readAll(), staleBackup);

		const ControlResult redo = run(QStringLiteral("control.redo"));
		QVERIFY2(redo.ok && redo.result.value(QStringLiteral("redone")).toBool(),
			qPrintable(redo.errorMessage));
		QFile redone(path);
		QVERIFY(redone.open(QIODevice::ReadOnly));
		QCOMPARE(redone.readAll(), current);
		QFile redoneBackup(backupPath);
		QVERIFY(redoneBackup.open(QIODevice::ReadOnly));
		QCOMPARE(redoneBackup.readAll(), previous);
	}

	void creatingNamedTemplateCanBeUndoneAndRedone()
	{
		const QString path = ConfigManager::inst()->userTemplateDir() + QStringLiteral("created.mpt");
		QVERIFY(!QFileInfo::exists(path));
		const ControlResult saved = run(QStringLiteral("project.save_as_template"),
			{{QStringLiteral("name"), QStringLiteral("created")}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QFile published(path);
		QVERIFY(published.open(QIODevice::ReadOnly));
		const QByteArray current = published.readAll();

		const ControlResult undo = run(QStringLiteral("control.undo"));
		QVERIFY2(undo.ok && undo.result.value(QStringLiteral("undone")).toBool(),
			qPrintable(undo.errorMessage));
		QVERIFY(!QFileInfo::exists(path));

		const ControlResult redo = run(QStringLiteral("control.redo"));
		QVERIFY2(redo.ok && redo.result.value(QStringLiteral("redone")).toBool(),
			qPrintable(redo.errorMessage));
		QFile redone(path);
		QVERIFY(redone.open(QIODevice::ReadOnly));
		QCOMPARE(redone.readAll(), current);
	}
};

QTEST_MAIN(ControlProjectTemplateTest)
#include "ControlProjectTemplateTest.moc"
