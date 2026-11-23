/*
 * RenderMenuQueueTest.cpp - the track menus' render entries start a cancellable queue job
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

/*! Cluster 917: a render started from a menu must enter the cancellable render
 *  queue, not call the legacy synchronous bounce/freeze/flatten command whose
 *  waitForFinished() freezes the interface for the render's whole duration.
 *
 *  Each case builds the real menu, triggers the entry, and looks for the job
 *  the queue recorded. The assertion is made before the child finishes, so it
 *  says what the MENU did rather than how fast the render was: the legacy
 *  command records no queue job at all. The child is this binary pretending to
 *  be `zene render` (the same seam tests/src/core/ControlRenderJobTest.cpp
 *  uses), so no real engine start is paid. */

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <sndfile.h>

#include "ArrangementEdits.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "LibraryCommandMenus.h"
#include "MenuBarExtras.h"
#include "MenuTestSupport.h"
#include "RenderQueue.h"
#include "ShellCommandMenus.h"
#include "Song.h"
#include "Track.h"
#include "TrackRenderMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr int FakeRate = 44100;
constexpr int FakeSeconds = 2;

//! The fake `zene render` child: write a valid WAV at the path the queue asked for.
int fakeRender(int argc, char** argv)
{
	QString out;
	int rate = FakeRate;
	for (int i = 1; i + 1 < argc; ++i)
	{
		const QString arg = QString::fromLocal8Bit(argv[i]);
		if (arg == QLatin1String("-o")) { out = QString::fromLocal8Bit(argv[++i]); }
		else if (arg == QLatin1String("-s")) { rate = QString::fromLocal8Bit(argv[++i]).toInt(); }
	}
	if (out.isEmpty()) { return 4; }
	// main.cpp replaces the requested suffix with the render format's; the bounce child asks for WAV.
	const QFileInfo requested(out);
	const QString output = requested.dir().filePath(requested.completeBaseName() + QStringLiteral(".wav"));
	SF_INFO info{};
	info.channels = 2;
	info.samplerate = rate;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	SNDFILE* wav = sf_open(QFile::encodeName(output).constData(), SFM_WRITE, &info);
	if (wav == nullptr) { return 5; }
	std::vector<float> block(2 * 1024, 0.1f);
	const qint64 total = static_cast<qint64>(rate) * FakeSeconds;
	for (qint64 done = 0; done < total; done += 1024)
	{
		const int frames = static_cast<int>(std::min<qint64>(1024, total - done));
		if (sf_writef_float(wav, block.data(), frames) != frames) { sf_close(wav); return 5; }
	}
	return sf_close(wav) == 0 ? 0 : 5;
}

QJsonArray queueJobs()
{
	return ControlRegistry::instance()->invoke(QStringLiteral("render.queue_list"))
		.result.value(QStringLiteral("jobs")).toArray();
}

int jobCountFor(const QString& operation)
{
	int count = 0;
	for (const QJsonValue& value : queueJobs())
	{
		if (value.toObject().value(QStringLiteral("operation")).toString() == operation) { ++count; }
	}
	return count;
}

//! A clip makes the track renderable; the fake child supplies the audio.
Track* trackWithClip(const QString& name)
{
	Track* track = Track::create(Track::Type::Sample, Engine::getSong());
	track->setName(name);
	track->createClip(TimePos(0));
	return track;
}

} // namespace

class RenderMenuQueueTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	//! Cancel what the last case queued and wait for the child, so a case never
	//! starts over another case's job (the same bounded drain ControlRenderJobTest uses).
	void cancelAndDrain()
	{
		ControlRegistry::instance()->invoke(QStringLiteral("render.queue_cancel"), {{QStringLiteral("all"), true}});
		QElapsedTimer timer;
		timer.start();
		while (timer.elapsed() < 10000)
		{
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			bool open = false;
			for (const QJsonValue& value : queueJobs())
			{
				const QString state = value.toObject().value(QStringLiteral("state")).toString();
				open = open || state == QLatin1String("queued") || state == QLatin1String("running");
			}
			if (!open) { break; }
			QTest::qWait(5);
		}
		ControlRegistry::instance()->invoke(QStringLiteral("render.queue_clear"));
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
		cancelAndDrain();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		cancelAndDrain();
		ArrangerRange::instance().clear();
		Engine::getSong()->clearProject();
	}

	//! The track menu's Bounce in place: async (no waitForFinished in the menu thread).
	void trackMenuBounceInPlaceQueuesTheOutputBounce()
	{
		Track* track = trackWithClip(QStringLiteral("Track menu"));
		QMenu menu;
		addTrackRenderActions(&menu, track);
		QAction* action = test::menuAction(&menu, {QStringLiteral("Bounce in place")});
		QVERIFY(action != nullptr);
		action->trigger();
		QCOMPARE(jobCountFor(QStringLiteral("bounce")), 1);
		cancelAndDrain();
		// render.queue_add is output-only: the track is untouched and no undo step exists.
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), 1);
	}

	//! The shell's Render submenu (the Inspector and the track's More menu).
	void shellRenderMenuQueuesEveryEntry()
	{
		Track* track = trackWithClip(QStringLiteral("Shell menu"));
		QMenu menu;
		ShellCommandMenus::populateTrack(&menu, track);
		QAction* bounce = test::menuAction(&menu, {QStringLiteral("Render"), QStringLiteral("Bounce in Place")});
		QVERIFY(bounce != nullptr);
		bounce->trigger();
		QCOMPARE(jobCountFor(QStringLiteral("bounce")), 1);
		cancelAndDrain();

		QAction* land = test::menuAction(&menu, {QStringLiteral("Render"), QStringLiteral("Bounce to New Track")});
		QVERIFY(land != nullptr);
		land->trigger();
		QCOMPARE(jobCountFor(QStringLiteral("bounce_land")), 1);
		cancelAndDrain();

		QAction* freeze = test::menuAction(&menu, {QStringLiteral("Render"), QStringLiteral("Freeze")});
		QVERIFY(freeze != nullptr);
		freeze->trigger();
		QCOMPARE(jobCountFor(QStringLiteral("freeze_track")), 1);
	}

	//! The library track section's Flatten entry.
	void libraryFlattenQueuesTheFlatten()
	{
		Track* track = trackWithClip(QStringLiteral("Library flatten"));
		QMenu menu;
		LibraryCommandMenus::populateTrack(&menu, track);
		QAction* action = test::menuAction(&menu, {QStringLiteral("Flatten Track (Replace With Its Audio)")});
		QVERIFY(action != nullptr);
		action->trigger();
		QCOMPARE(jobCountFor(QStringLiteral("flatten")), 1);
	}

	//! Navigate > Range > Bounce to a New Track.
	void navigateRangeBounceQueuesTheRangeBounce()
	{
		Track* track = trackWithClip(QStringLiteral("Range menu"));
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("range.set"),
			{{QStringLiteral("start"), 0}, {QStringLiteral("end"), 192},
			 {QStringLiteral("tracks"), QJsonArray{control::trackIdOf(track)}}}).ok);
		QMenuBar bar;
		QMenu* navigate = installNavigateMenu(&bar);
		QVERIFY(navigate != nullptr);
		QAction* action = test::menuAction(navigate, {QStringLiteral("Range"),
			QStringLiteral("Bounce to a New Track")});
		QVERIFY(action != nullptr);
		action->trigger();
		QCOMPARE(jobCountFor(QStringLiteral("range_bounce")), 1);
	}
};

int main(int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "render") == 0) { return fakeRender(argc, argv); }
	QApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	RenderMenuQueueTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "RenderMenuQueueTest.moc"
