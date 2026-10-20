/*
 * RenderSourceGuardTest.cpp - a render queued onto its own source project is refused and the project is kept
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
#include <QJsonObject>
#include <QTemporaryDir>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "ControlRegistry.h"
#include "Engine.h"
#include "RenderQueue.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QByteArray contents(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class RenderSourceGuardTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	QString out(const QString& name) const { return QDir(m_dir.path()).filePath(name); }

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		run(QStringLiteral("render.queue_cancel"), {{QStringLiteral("all"), true}});
		run(QStringLiteral("render.queue_clear"));
		Engine::getSong()->clearProject();
	}

	void renderOntoItsOwnSourceProjectIsRefusedAndKeepsTheProject()
	{
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		track->setName(QStringLiteral("Own source"));
		track->createClip(TimePos(0));
		const QString project = out(QStringLiteral("own-source.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		const QByteArray original = contents(project);
		QVERIFY(!original.isEmpty());

		// The same file under another spelling, through a symbolic link or through a hard link, is still the source.
		QStringList targets{project, m_dir.path() + QStringLiteral("/./own-source.mmp")};
#ifndef _WIN32
		const QString linked = out(QStringLiteral("own-source-link.mmp"));
		QVERIFY(QFile(project).link(linked));
		targets << linked;
		const QString hard = out(QStringLiteral("own-source-hard.mmp"));
		QVERIFY(::link(QFile::encodeName(project).constData(), QFile::encodeName(hard).constData()) == 0);
		targets << hard;
#endif
		for (const QString& target : targets)
		{
			const ControlResult refused = run(QStringLiteral("render.queue_add"),
				{{QStringLiteral("project"), project}, {QStringLiteral("out"), target}, {QStringLiteral("start"), false}});
			QVERIFY2(!refused.ok, qPrintable(target));
			QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		}
		QVERIFY(RenderQueue::instance().jobs().empty());
		QCOMPARE(contents(project), original);

		// A different output file is still accepted: the refusal is for the source only.
		const ControlResult accepted = run(QStringLiteral("render.queue_add"),
			{{QStringLiteral("project"), project}, {QStringLiteral("out"), out(QStringLiteral("own-source.wav"))},
				{QStringLiteral("start"), false}});
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QCOMPARE(RenderQueue::instance().jobs().size(), std::size_t(1));
		QCOMPARE(contents(project), original);
	}
};

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	RenderSourceGuardTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "RenderSourceGuardTest.moc"
