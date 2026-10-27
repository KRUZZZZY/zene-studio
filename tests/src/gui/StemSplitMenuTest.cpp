/*
 * StemSplitMenuTest.cpp - a sample clip's Split to stems submenu picks the stem-job quality
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

/*! Q050-062.2: the sample clip's Split to stems submenu (the one SampleClipView
 *  builds with StemSplitController::addSplitToStemsMenu) passes the chosen
 *  quality to the registered stem.job_start, and the job UI names the setting
 *  the job ran at and leaves the source clip on its track.
 *
 *  The job runs for real on the external-process backend with the committed
 *  4-output stub model (tests/data/stub-4stem-linear.onnx, not HTDemucs), the
 *  same setup as StemSplitPipelineTest.
 */

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QTimer>
#include <QtTest>

#include "ControlRegistry.h"
#include "ControlStemSupport.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "StemSplitController.h"
#include "TimePos.h"
#include "Track.h"

#include "StemSeparation/ExternalProcessStemSeparator.h"
#include "StemSeparation/StemTypes.h"

using namespace lmms;

namespace
{

std::shared_ptr<const SampleBuffer> makeSineMix(int seconds)
{
	const int sampleRate = StemModelSampleRate;
	const int frames = sampleRate * seconds;
	std::vector<SampleFrame> data(static_cast<std::size_t>(frames));
	for (int i = 0; i < frames; ++i)
	{
		const auto t = static_cast<float>(i) / static_cast<float>(sampleRate);
		data[static_cast<std::size_t>(i)] = SampleFrame(
			0.4f * std::sin(2.0f * float(M_PI) * 220.0f * t),
			0.4f * std::sin(2.0f * float(M_PI) * 330.0f * t));
	}
	return std::make_shared<const SampleBuffer>(std::move(data), sampleRate);
}

} // namespace

class StemSplitMenuTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init(true);
		// The registry refuses engine commands until startup has finished.
		ControlRegistry::setReady(true);

		qputenv("LMMS_STEM_MODEL",
			QFile::encodeName(QStringLiteral(LMMS_TEST_DATA_DIR) + QStringLiteral("/stub-4stem-linear.onnx")));
		qputenv("LMMS_STEM_CLI", QFile::encodeName(QStringLiteral(LMMS_STEM_CLI_DEFAULT)));
		qputenv("LMMS_STEM_PYTHON", QByteArrayLiteral("/usr/bin/python3"));

		QString error;
		if (!ExternalProcessStemSeparator().isAvailable(&error))
		{
			QSKIP(qPrintable(QStringLiteral("external-process backend unavailable: ") + error));
		}
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	/* Q050-062.2: the sample clip's "Split to stems" submenu offers each
	 * quality stem.job_start accepts. Triggering "High quality" passes
	 * `quality: high` through the registered command (the job the registry
	 * reports carries it, and the worker ran at it), the progress and completion
	 * dialogs name the effective setting, and the source clip is still on its
	 * track with its own audio afterwards. */
	void testClipMenuSplitsAtTheChosenQualityAndKeepsTheSource()
	{
		auto* song = Engine::getSong();
		const auto before = song->tracks().size();
		auto* sourceTrack = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		QVERIFY(sourceTrack != nullptr);
		auto* clip = dynamic_cast<SampleClip*>(sourceTrack->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setName(QStringLiteral("Menu mix"));
		clip->setSampleBuffer(makeSineMix(1));
		const QPointer<SampleClip> source(clip);
		const auto sourceBuffer = clip->sample().buffer();

		QMenu menu;
		gui::StemSplitController::addSplitToStemsMenu(&menu, clip);
		QCOMPARE(menu.actions().size(), 1);
		QMenu* sub = menu.actions().front()->menu();
		QVERIFY(sub != nullptr);
		QCOMPARE(sub->title(), QStringLiteral("Split to stems"));
		QStringList names;
		QAction* high = nullptr;
		for (QAction* action : sub->actions())
		{
			names << action->data().toString();
			QVERIFY(action->isCheckable());
			if (action->data().toString() == QLatin1String("high")) { high = action; }
		}
		// The same names, in the same order, the command advertises.
		QCOMPARE(names, control::stemQualityNames());
		QVERIFY(high != nullptr);

		const int lastJobBefore = [] {
			int last = -1;
			const QJsonObject all = ControlRegistry::instance()
				->invoke(QStringLiteral("stem.job_status")).result;
			for (const QJsonValue& job : all.value(QStringLiteral("jobs")).toArray())
			{
				last = std::max(last, job.toObject().value(QStringLiteral("job_id")).toInt());
			}
			return last;
		}();

		high->trigger();

		// The job the registry started is a new one, at the chosen quality.
		int jobId = -1;
		QJsonObject job;
		const QJsonObject all = ControlRegistry::instance()->invoke(QStringLiteral("stem.job_status")).result;
		for (const QJsonValue& value : all.value(QStringLiteral("jobs")).toArray())
		{
			const QJsonObject candidate = value.toObject();
			if (candidate.value(QStringLiteral("job_id")).toInt() > jobId)
			{
				jobId = candidate.value(QStringLiteral("job_id")).toInt();
				job = candidate;
			}
		}
		QVERIFY2(jobId > lastJobBefore, "the menu item started no stem.job_start job");
		QCOMPARE(job.value(QStringLiteral("quality")).toString(), QStringLiteral("high"));
		QCOMPARE(job.value(QStringLiteral("overlap")).toDouble(), 0.75);

		QPointer<QProgressDialog> progress;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			auto* dialog = qobject_cast<QProgressDialog*>(widget);
			if (dialog != nullptr && dialog->isVisible()) { progress = dialog; }
		}
		QVERIFY(!progress.isNull());
		QVERIFY2(progress->labelText().contains(QStringLiteral("high quality, 75% overlap")),
			qPrintable(progress->labelText()));

		QStringList messages;
		QTimer closeMessageBox;
		closeMessageBox.setInterval(10);
		connect(&closeMessageBox, &QTimer::timeout, [&messages] {
			for (QWidget* widget : QApplication::topLevelWidgets())
			{
				if (auto* message = qobject_cast<QMessageBox*>(widget))
				{
					messages << message->text();
					message->accept();
				}
			}
		});
		closeMessageBox.start();
		QTRY_VERIFY_WITH_TIMEOUT(progress.isNull(), 180000);
		QTRY_VERIFY_WITH_TIMEOUT(!messages.isEmpty(), 5000);
		closeMessageBox.stop();

		// The worker ran inference at the chosen setting, and the completion
		// message names it and where the source is.
		const QJsonObject finished = ControlRegistry::instance()->invoke(QStringLiteral("stem.job_status"),
			QJsonObject{{QStringLiteral("job_id"), jobId}}).result
			.value(QStringLiteral("jobs")).toArray().first().toObject();
		QCOMPARE(finished.value(QStringLiteral("state")).toString(), QStringLiteral("completed"));
		QCOMPARE(finished.value(QStringLiteral("inference_quality")).toString(), QStringLiteral("high"));
		const QString done = messages.join(QLatin1Char('\n'));
		QVERIFY2(done.contains(QStringLiteral("Created 4 stem tracks at high quality, 75% overlap")), qPrintable(done));
		QVERIFY2(done.contains(QStringLiteral("The original clip \"Menu mix\" is unchanged on its track.")),
			qPrintable(done));

		// The source clip is still there, on its track, with its own audio.
		QCOMPARE(song->tracks().size(), before + 5);
		QVERIFY(!source.isNull());
		QCOMPARE(static_cast<int>(sourceTrack->getClips().size()), 1);
		QCOMPARE(sourceTrack->getClips().front(), static_cast<Clip*>(source.data()));
		QVERIFY(source->sample().buffer() == sourceBuffer);

		// The menu remembers the setting the command took.
		QCOMPARE(gui::StemSplitController::instance()->lastQuality(), QStringLiteral("high"));
		QMenu again;
		gui::StemSplitController::addSplitToStemsMenu(&again, clip);
		for (QAction* action : again.actions().front()->menu()->actions())
		{
			QCOMPARE(action->isChecked(), action->data().toString() == QLatin1String("high"));
		}

		const ControlResult undone = ControlRegistry::instance()->invoke(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(song->tracks().size(), before + 1);
		delete sourceTrack;
	}

	/* Q050-062.2: the job UI's wording of the effective setting, from the job
	 * object's own fields - the worker's `inference_quality` once it ran. */
	void testQualityTextNamesTheEffectiveSetting()
	{
		using gui::StemSplitController;
		QCOMPARE(StemSplitController::qualityText(QJsonObject{{QStringLiteral("quality"), QStringLiteral("fast")}}),
			QStringLiteral("fast quality, 25% overlap"));
		QCOMPARE(StemSplitController::qualityText(QJsonObject{{QStringLiteral("quality"), QStringLiteral("high")},
			{QStringLiteral("inference_quality"), QStringLiteral("standard")}}),
			QStringLiteral("standard quality, 50% overlap"));
		QCOMPARE(StemSplitController::qualityText(QJsonObject{}), QString());
	}
};

QTEST_MAIN(StemSplitMenuTest)
#include "StemSplitMenuTest.moc"
