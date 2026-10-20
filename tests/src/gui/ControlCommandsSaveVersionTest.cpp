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
#include <QList>

#include <limits>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Song.h"
#include "VersionedSaveDialog.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QByteArray readBytes(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

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
		VersionedSaveDialog::setVersionNumberHook(nullptr);
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

		// The version numbers this name answers with, in order. The first one is the widest
		// version number, so the first answer refuses exactly as the digits in the name do.
		// The second one is a small number, so a caller that IGNORES the refusal gets a
		// name that is free and its loop ends: the test is bounded by what the hook says,
		// not by a timer and not by touching the file.
		QList<qulonglong> answers{std::numeric_limits<qulonglong>::max(), 42};
		VersionedSaveDialog::setVersionNumberHook(
			[answers](const QString&, qulonglong& version) mutable {
				if (answers.isEmpty()) { version = 1; return true; }
				version = answers.takeFirst();
				return true;
			});

		const ControlResult result =
			ControlRegistry::instance()->invoke(QStringLiteral("project.save_version"));

		VersionedSaveDialog::setVersionNumberHook(nullptr);

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
