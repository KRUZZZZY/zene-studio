/*
 * BackgroundTasksGuiTest.cpp - the waveform view and Rescan Plugins run as tasks the Tasks list can stop
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

#include <atomic>
#include <chrono>
#include <thread>

#include <QAction>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include "../core/BrowserTestSupport.h"
#include "BrowserPeakCache.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "PeakJobs.h"
#include "PluginFactory.h"
#include "PluginQuarantineMenu.h"
#include "TaskCenter.h"
#include "TaskIndicator.h"
#include "WaveformPreview.h"

using namespace lmms;
using namespace lmms::gui;

class BackgroundTasksGuiTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;
	std::atomic<bool> m_release{false};
	std::atomic<bool> m_entered{false};

	QString wave(const QString& name, int frames = 48000 * 30)
	{
		const QString path = m_dir.filePath(name);
		browserfixture::WaveSpec spec;
		spec.frames = frames;
		spec.burstStartFrame = frames / 2;
		return browserfixture::writeWave(path, spec) ? path : QString();
	}

	void holdTheWorker()
	{
		m_release.store(false);
		m_entered.store(false);
		PeakJobs::instance().setBeforeComputeHookForTesting([this] {
			m_entered.store(true);
			QElapsedTimer bound;
			bound.start();
			while (!m_release.load() && bound.elapsed() < 20000) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
		});
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		qputenv("LMMS_PLUGIN_SCAN_CACHE", m_dir.filePath(QStringLiteral("plugin-scan-cache.json")).toLocal8Bit());
		ConfigManager::inst()->loadConfigFile(m_dir.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void cleanup()
	{
		m_release.store(true);
		PeakJobs::instance().waitForWorkerForTesting(30000);
		QCoreApplication::processEvents();
		PeakJobs::instance().setBeforeComputeHookForTesting({});
		BrowserPeakCache::instance().clear();
		TaskCenter::instance().clearFinished();
	}

	//! A short operation stays out of the Tasks list: clicking through small samples adds no rows.
	void aSmallFileIsDrawnAtOnceWithoutATask()
	{
		const QString path = wave(QStringLiteral("small.wav"), 4096);
		WaveformPreview preview;
		holdTheWorker();
		const std::size_t before = TaskCenter::instance().tasks().size();
		QVERIFY(preview.setPath(path));
		QVERIFY(!preview.peaks().isEmpty());
		QCOMPARE(TaskCenter::instance().tasks().size(), before);
	}

	void selectingALongFileReturnsAtOnceAndTheWaveformFollows()
	{
		const QString path = wave(QStringLiteral("long.wav"));
		WaveformPreview preview;
		holdTheWorker();
		int ticks = 0;
		QTimer ticker;
		ticker.setInterval(1);
		connect(&ticker, &QTimer::timeout, this, [&ticks] { ++ticks; });
		ticker.start();

		QVERIFY(!preview.setPath(path));  // returned with the worker held: nothing was read here
		QVERIFY(preview.peaks().isEmpty());
		QVERIFY(!preview.message().isEmpty());
		QTRY_VERIFY_WITH_TIMEOUT(m_entered.load(), 5000);
		QTRY_VERIFY_WITH_TIMEOUT(ticks >= 20, 5000);  // the event loop turns during the read
		QVERIFY(preview.peaks().isEmpty());

		m_release.store(true);
		QTRY_VERIFY_WITH_TIMEOUT(!preview.peaks().isEmpty(), 10000);
		QVERIFY(preview.message().isEmpty());
		QVERIFY(preview.accessibleDescription().contains(QStringLiteral("long.wav")));
		QVERIFY(preview.setPath(path));  // cached now: drawn straight away
	}

	void selectingSomethingElseCancelsTheReadInProgress()
	{
		const QString first = wave(QStringLiteral("first.wav"));
		const QString second = wave(QStringLiteral("second.wav"));
		WaveformPreview preview;
		holdTheWorker();
		preview.setPath(first);
		QTRY_VERIFY_WITH_TIMEOUT(m_entered.load(), 5000);
		preview.setPath(second);
		m_release.store(true);
		QTRY_VERIFY_WITH_TIMEOUT(!preview.peaks().isEmpty(), 10000);
		QVERIFY(!BrowserPeakCache::instance().isCurrent(first));
		QVERIFY(BrowserPeakCache::instance().isCurrent(second));
		QVERIFY(preview.accessibleDescription().contains(QStringLiteral("second.wav")));
	}

	void theTasksListsCancelButtonStopsAWaveformRead()
	{
		const QString path = wave(QStringLiteral("cancelled.wav"));
		WaveformPreview preview;
		holdTheWorker();
		preview.setPath(path);
		QTRY_VERIFY_WITH_TIMEOUT(m_entered.load(), 5000);

		QWidget host;
		host.resize(400, 300);
		auto* indicator = new TaskIndicator(&host);
		indicator->move(10, 260);
		host.show();
		QVERIFY(indicator->setListShown(1));
		QPushButton* cancel = indicator->list()->cancelButton(0);
		QVERIFY(cancel != nullptr);
		cancel->click();
		m_release.store(true);
		QTRY_COMPARE_WITH_TIMEOUT(preview.message(), QStringLiteral("Waveform analysis cancelled"), 10000);
		QVERIFY(preview.peaks().isEmpty());
		QVERIFY(!BrowserPeakCache::instance().isCurrent(path));
	}

	void rescanPluginsRunsInTheBackgroundAndTheTasksListStopsIt()
	{
		QMenu parent;
		QMenu* menu = addPluginQuarantineMenu(&parent);
		emit menu->aboutToShow();
		QAction* rescan = nullptr;
		for (QAction* action : menu->actions())
		{
			if (action->text() == QLatin1String("Rescan Plugins")) { rescan = action; }
		}
		QVERIFY(rescan != nullptr);
		const int before = PluginFactory::instance()->backgroundScanStatus().taskId;
		rescan->trigger();
		QVERIFY(PluginFactory::instance()->backgroundScanActive());  // the action came back with the run open
		QVERIFY(PluginFactory::instance()->backgroundScanStatus().taskId != before);

		QWidget host;
		host.resize(400, 300);
		auto* indicator = new TaskIndicator(&host);
		indicator->move(10, 260);
		host.show();
		QVERIFY(indicator->setListShown(1));
		QPushButton* cancel = indicator->list()->cancelButton(0);
		QVERIFY(cancel != nullptr);
		cancel->click();
		QTRY_VERIFY_WITH_TIMEOUT(!PluginFactory::instance()->backgroundScanActive(), 10000);
		QCOMPARE(PluginFactory::instance()->backgroundScanStatus().outcome, QStringLiteral("cancelled"));
	}
};

QTEST_MAIN(BackgroundTasksGuiTest)
#include "BackgroundTasksGuiTest.moc"
