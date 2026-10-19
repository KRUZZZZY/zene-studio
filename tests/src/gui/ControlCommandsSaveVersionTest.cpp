/*
 * ControlCommandsSaveVersionTest.cpp - the agent surface's project.save_version must REFUSE a
 * project whose file name carries a version number that cannot be advanced, and it must do so
 * at once: the name would not change, so the "first free name" loop would look at the same file
 * for ever.
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QFile>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QByteArray readBytes(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

//! Takes the file away from a handler that is still running, so a regression cannot hang the
//! suite. The command's loop looks for the first free name and keeps finding the same file, so
//! without this the test would never end. The budget is a safety net, not an assertion: the
//! outcome of the test is the same whether the file is taken away before or during the loop.
class StuckLoopBreaker : public QThread
{
public:
	explicit StuckLoopBreaker(const QString& path) : m_path(path) {}

	//! The handler returned in time: nothing is taken away.
	void release()
	{
		QMutexLocker locker(&m_mutex);
		m_released = true;
		m_condition.wakeAll();
	}

protected:
	void run() override
	{
		QMutexLocker locker(&m_mutex);
		while (!m_released)
		{
			if (!m_condition.wait(&m_mutex, kBudgetMs))
			{
				locker.unlock();
				QFile::remove(m_path);
				return;
			}
		}
	}

private:
	//! Long enough that a handler which returns at all has returned long before.
	static const int kBudgetMs = 5000;
	QString m_path;
	QMutex m_mutex;
	QWaitCondition m_condition;
	bool m_released = false;
};

} // namespace

class ControlCommandsSaveVersionTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A project whose name ends in the widest version number: there is no next version to
	//! write, so project.save_version must REFUSE - at once - and leave the file it cannot
	//! replace exactly as it was.
	void refusesWhenTheVersionCannotBeAdvanced()
	{
		Song* song = Engine::getSong();
		QVERIFY(song != nullptr);
		const QString path = m_home.filePath(QStringLiteral("mix-18446744073709551615.mmp"));
		QVERIFY2(song->guiSaveProjectAs(path), "the test could not write the project it needs");
		QCOMPARE(song->projectFileName(), path);
		const QByteArray before = readBytes(path);
		QVERIFY(!before.isEmpty());

		StuckLoopBreaker breaker(path);
		breaker.start();
		const ControlResult result =
			ControlRegistry::instance()->invoke(QStringLiteral("project.save_version"));
		breaker.release();
		breaker.wait();

		QVERIFY2(!result.ok, "project.save_version did not refuse a version it cannot advance");
		QVERIFY(result.errorKind == ControlErrorKind::Refused);
		QVERIFY2(result.errorMessage.contains(QStringLiteral("version")),
			qPrintable(result.errorMessage));
		QCOMPARE(readBytes(path), before);
		QCOMPARE(song->projectFileName(), path);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(ControlCommandsSaveVersionTest)
#include "ControlCommandsSaveVersionTest.moc"
