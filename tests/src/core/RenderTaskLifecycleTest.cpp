/*
 * RenderTaskLifecycleTest.cpp - a render followed and cancelled through app.tasks (Q050-010)
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

#include <cstring>
#include <vector>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <sndfile.h>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "RenderQueue.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

bool writeSilence(const QString& path)
{
	SF_INFO info{};
	info.channels = 2;
	info.samplerate = 44100;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (wav == nullptr) { return false; }
	const std::vector<float> block(2 * 4410, 0.0f);
	sf_writef_float(wav, block.data(), 4410);
	return sf_close(wav) == 0;
}

//! The render child: this binary again (RenderQueue starts the application with "render"). It waits as long as the
//! test asks, then writes what the queue expects as a bounce's output, so nothing here renders for real.
int fakeRender(int argc, char** argv)
{
	const QString pidPath = qEnvironmentVariable("ZENE_FAKE_RENDER_PID_PATH");
	if (!pidPath.isEmpty())
	{
		QFile pidFile(pidPath);
		if (!pidFile.open(QIODevice::WriteOnly)) { return 6; }
		pidFile.write(QByteArray::number(QCoreApplication::applicationPid()));
	}
	const int delay = qEnvironmentVariableIntValue("ZENE_FAKE_RENDER_DELAY_MS");
	if (delay > 0) { QThread::msleep(static_cast<unsigned long>(delay)); }
	for (int i = 3; i + 1 < argc; ++i)
	{
		if (std::strcmp(argv[i], "-o") != 0) { continue; }
		const QFileInfo requested(QString::fromLocal8Bit(argv[i + 1]));
		const QString output = requested.dir().filePath(requested.completeBaseName() + QStringLiteral(".wav"));
		return writeSilence(output) ? 0 : 5;
	}
	return 4;
}

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString stateIn(const QString& id)
{
	for (const QJsonValue& value : run(QStringLiteral("app.tasks")).result.value(QStringLiteral("tasks")).toArray())
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == id) { return value.toObject().value(QStringLiteral("state")).toString(); }
	}
	return QString();
}

} // namespace

class RenderTaskLifecycleTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	QString out(const QString& name) const { return QDir(m_dir.path()).filePath(name); }

	Track* trackWithClip(const QString& name)
	{
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		track->setName(name);
		track->createClip(TimePos(0));
		return track;
	}

	ControlResult queueBounce(Track* source, const QString& path)
	{
		return run(QStringLiteral("render.queue_add"), {{QStringLiteral("operation"), QStringLiteral("bounce")},
			{QStringLiteral("track"), control::trackIdOf(source)}, {QStringLiteral("out"), path}});
	}

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
		qunsetenv("ZENE_FAKE_RENDER_DELAY_MS");
		qunsetenv("ZENE_FAKE_RENDER_PID_PATH");
		Engine::getSong()->clearProject();
	}

	void aRenderIsFollowedThroughAppTasksWhileTheEventLoopTurns()
	{
		Track* source = trackWithClip(QStringLiteral("Listed"));
		const QString pidPath = out(QStringLiteral("listed.pid"));
		qputenv("ZENE_FAKE_RENDER_PID_PATH", pidPath.toLocal8Bit());
		qputenv("ZENE_FAKE_RENDER_DELAY_MS", "700");
		const QString path = out(QStringLiteral("listed.wav"));
		int ticks = 0;
		QTimer ticker;
		ticker.setInterval(5);
		connect(&ticker, &QTimer::timeout, this, [&ticks] { ++ticks; });
		ticker.start();
		const ControlResult accepted = queueBounce(source, path);
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		const QString id = accepted.result.value(QStringLiteral("task")).toString();
		QCOMPARE(id, QStringLiteral("render-%1").arg(accepted.result.value(QStringLiteral("job")).toInt()));
		QVERIFY(stateIn(id) == QLatin1String("queued") || stateIn(id) == QLatin1String("running"));
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidPath), 3000);  // the child is up and sleeping
		const int before = ticks;
		QTRY_VERIFY_WITH_TIMEOUT(ticks >= before + 10, 500);
		QCOMPARE(stateIn(id), QStringLiteral("running"));
		QTRY_COMPARE_WITH_TIMEOUT(stateIn(id), QStringLiteral("done"), 10000);
		QVERIFY(QFileInfo::exists(path));
	}

	void aRenderCancelledThroughAppTasksLeavesTheDestinationAndNoScratch()
	{
		Track* source = trackWithClip(QStringLiteral("Stopped"));
		const QString keptPath = out(QStringLiteral("kept.wav"));
		QFile kept(keptPath);
		QVERIFY(kept.open(QIODevice::WriteOnly));
		kept.write("previous");
		kept.close();
		const QString pidPath = out(QStringLiteral("stopped.pid"));
		qputenv("ZENE_FAKE_RENDER_PID_PATH", pidPath.toLocal8Bit());
		qputenv("ZENE_FAKE_RENDER_DELAY_MS", "5000");
		const ControlResult accepted = queueBounce(source, keptPath);
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidPath), 3000);
		const QString id = accepted.result.value(QStringLiteral("task")).toString();
		const QString scratch = RenderQueue::instance().job(accepted.result.value(QStringLiteral("job")).toInt())->bounce.scratch->path();
		QVERIFY(run(QStringLiteral("app.task_cancel"), {{QStringLiteral("id"), id}}).ok);
		QTRY_COMPARE_WITH_TIMEOUT(stateIn(id), QStringLiteral("cancelled"), 10000);
		QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(scratch), 5000);
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), QByteArray("previous"));
	}
};

int main(int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "render") == 0) { return fakeRender(argc, argv); }
	QCoreApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	RenderTaskLifecycleTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "RenderTaskLifecycleTest.moc"
