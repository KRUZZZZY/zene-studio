/*
 * StemSplitPipelineTest.cpp - end-to-end: mix -> 4 stems -> 4 SampleTracks
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

#include <algorithm>
#include <array>

#include <cmath>
#include <memory>
#include <vector>

#include <QElapsedTimer>
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMenu>
#include <QPointer>
#include <QProgressDialog>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTimer>
#include <QTemporaryDir>

#include "ControlStemSupport.h"
#include "Engine.h"
#include "ControlRegistry.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "StemSplitController.h"
#include "TimePos.h"
#include "Track.h"
#include "TrackContainer.h"

#include "StemSeparation/ExternalProcessStemSeparator.h"
#include "StemSeparation/StemJobManager.h"
#include "StemSeparation/StemTrackBuilder.h"

using namespace lmms;

namespace
{

void closeMessageBoxes()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (auto* message = qobject_cast<QMessageBox*>(widget)) { message->accept(); }
	}
}

// The committed fixture is a tiny linear 4-output ONNX graph (458 bytes), NOT
// HTDemucs. It exists so the whole pipeline can be exercised without the
// optional 166 MB model download.
QString stubModelPath()
{
	return QStringLiteral(LMMS_TEST_DATA_DIR) + QStringLiteral("/stub-4stem-linear.onnx");
}

QString cliPath()
{
	return QStringLiteral(LMMS_STEM_CLI_DEFAULT);
}

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

//! How many `stem.tracks_create` transactions the registry has recorded.
int stemTracksCreateRecords()
{
	const QJsonObject records = ControlRegistry::instance()
		->invoke(QStringLiteral("control.transactions")).result;
	int count = 0;
	for (const QJsonValue& value : records.value(QStringLiteral("transactions")).toArray())
	{
		if (value.toObject().value(QStringLiteral("command")).toString()
			== QLatin1String("stem.tracks_create"))
		{
			++count;
		}
	}
	return count;
}

} // namespace

class StemSplitPipelineTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init(true);

		// The GUI controller drives the SAME registered `stem.*` commands the
		// agent surface does, and the registry refuses engine commands until it
		// is told startup finished (main.cpp's control-server path does this in
		// a real instance). Without it every invoke would return the typed
		// engine_starting refusal.
		ControlRegistry::setReady(true);

		qputenv("LMMS_STEM_MODEL", QFile::encodeName(stubModelPath()));
		qputenv("LMMS_STEM_CLI", QFile::encodeName(cliPath()));
		qputenv("LMMS_STEM_PYTHON", QByteArrayLiteral("/usr/bin/python3"));

		QString error;
		if (!ExternalProcessStemSeparator().isAvailable(&error))
		{
			QSKIP(qPrintable(QStringLiteral("external-process backend unavailable: ") + error));
		}
		qInfo("backend: %s", qPrintable(ExternalProcessStemSeparator().backendName()));
		qInfo("python:  %s", qPrintable(ExternalProcessStemSeparator::locatePython()));
		qInfo("cli:     %s", qPrintable(ExternalProcessStemSeparator::locateCli()));
		qInfo("model:   %s", qPrintable(ExternalProcessStemSeparator::defaultModelPath()));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	/* Bug (cluster new-p8-120): isAvailable() must honour the model path the
	 *  instance was configured with - the same one separate() uses - not the
	 *  class default. */
	void testAvailabilityHonoursConfiguredModelPath()
	{
		const QString configured = stubModelPath();
		const QString missing = QStringLiteral(LMMS_TEST_DATA_DIR)
			+ QStringLiteral("/no-such-model.onnx");

		// The class default is absent, but this instance is configured with a
		// model that exists: it must report available.
		qputenv("LMMS_STEM_MODEL", QFile::encodeName(missing));
		ExternalProcessStemSeparator withModel(QString(), QString(), configured);
		{
			QString error;
			QVERIFY2(withModel.isAvailable(&error),
				qPrintable(QStringLiteral("configured model present: ") + error));
		}

		// The class default exists, but this instance's model is absent: it must
		// report unavailable.
		qputenv("LMMS_STEM_MODEL", QFile::encodeName(configured));
		ExternalProcessStemSeparator missingModel(QString(), QString(), missing);
		{
			QString error;
			QVERIFY2(!missingModel.isAvailable(&error) && !error.isEmpty(),
				"configured model absent: availability must report unavailable");
		}
	}

	void testSeparatorProducesFourStemsWithProgress()
	{
		ExternalProcessStemSeparator separator;
		const auto mix = makeSineMix(1);

		StemSet stems;
		QString error;
		float lastProgress = -1.0f;
		int progressCalls = 0;
		bool nonMonotonic = false;

		QElapsedTimer timer;
		timer.start();
		const auto status = separator.separate(*mix, StemModelSampleRate, 16384, StemQuality::Standard,
			[&](float fraction) {
				if (fraction < lastProgress)
				{
					nonMonotonic = true;
				}
				lastProgress = fraction;
				++progressCalls;
				return true;
			},
			stems, error);
		const auto elapsedMs = timer.elapsed();

		qInfo("separator wall time: %lld ms for %zu frames (%d s at %d Hz)",
			static_cast<long long>(elapsedMs), mix->size(), 1, StemModelSampleRate);
		QVERIFY2(status == StemSeparator::Status::Success, qPrintable(error));
		QVERIFY(progressCalls >= 2);
		QVERIFY2(!nonMonotonic, "progress fractions must be monotonically non-decreasing");
		QCOMPARE(lastProgress, 1.0f);

		for (int s = 0; s < NumStems; ++s)
		{
			QVERIFY2(stems[static_cast<std::size_t>(s)] != nullptr, stemName(static_cast<Stem>(s)));
			QCOMPARE(stems[static_cast<std::size_t>(s)]->size(), mix->size());
		}
	}

	/*! Q050-062.1: the quality travels to tools/stem_split_cli.py (--quality) and sets how many
	 *  segments it runs. The CLI reports chunks_done / chunks after each one, so its smallest
	 *  positive fraction is 1 / runs: a one-second mix at a 16384-frame segment takes 6 / 9 / 17
	 *  runs at 25% / 50% / 75% overlap (the same counts as OnnxRuntimeStemSeparatorTest). */
	void testQualitySetsTheSegmentCountThroughTheCli()
	{
		const struct { StemQuality quality; int runs; } cases[] = {
			{StemQuality::Fast, 6}, {StemQuality::Standard, 9}, {StemQuality::High, 17},
		};
		const auto mix = makeSineMix(1);
		for (const auto& c : cases)
		{
			ExternalProcessStemSeparator separator;
			StemSet stems;
			QString error;
			float smallest = 2.0f;
			const auto status = separator.separate(*mix, StemModelSampleRate, 16384, c.quality,
				[&](float fraction) {
					if (fraction > 0.0f) { smallest = std::min(smallest, fraction); }
					return true;
				},
				stems, error);
			QVERIFY2(status == StemSeparator::Status::Success, qPrintable(error));
			const int runs = static_cast<int>(std::lround(1.0 / smallest));
			QVERIFY2(runs == c.runs, qPrintable(QStringLiteral("%1: %2 runs, expected %3")
				.arg(QString::fromLatin1(stemQualityName(c.quality))).arg(runs).arg(c.runs)));
			for (int s = 0; s < NumStems; ++s)
			{
				QVERIFY(stems[static_cast<std::size_t>(s)] != nullptr);
				QCOMPARE(stems[static_cast<std::size_t>(s)]->size(), mix->size());
			}
		}
	}

	void testSeparatorRejectsStemWhenReportedSizeDoesNotMatchFrames()
	{
#ifndef Q_OS_UNIX
		QSKIP("named-pipe size behavior is exercised on Unix platforms");
#else
		QTemporaryDir cliDir;
		QVERIFY(cliDir.isValid());
		QFile cli(cliDir.filePath(QStringLiteral("fifo-stem-cli.py")));
		QVERIFY(cli.open(QIODevice::WriteOnly | QIODevice::Truncate));
		const QByteArray script = R"PY(
import argparse
import os
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--input", required=True)
parser.add_argument("--out-dir", required=True)
args, _ = parser.parse_known_args()
with open(args.input, "rb") as mix:
    payload = mix.read()
for name in ("bass", "other", "vocals"):
    with open(os.path.join(args.out_dir, name + ".f32"), "wb") as stem:
        stem.write(payload)
fifo_path = os.path.join(args.out_dir, "drums.f32")
os.mkfifo(fifo_path)
writer = "import sys\nwith open(sys.argv[1], 'wb') as stem:\n    stem.write(bytes(8))\n"
subprocess.Popen(
    [sys.executable, "-c", writer, fifo_path],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
    start_new_session=True,
    close_fds=True,
)
)PY";
		QCOMPARE(cli.write(script), static_cast<qint64>(script.size()));
		cli.close();

		ExternalProcessStemSeparator separator(
			ExternalProcessStemSeparator::locatePython(), cli.fileName(), stubModelPath());
		SampleBuffer mix(std::vector<SampleFrame>{ SampleFrame(0.25f, -0.25f) }, StemModelSampleRate);
		StemSet stems;
		QString error;
		const auto status = separator.separate(mix, StemModelSampleRate, 1, StemQuality::Standard,
			[](float) { return true; }, stems, error);

		QCOMPARE(static_cast<int>(status), static_cast<int>(StemSeparator::Status::Failed));
		QVERIFY2(error.contains(QStringLiteral("exit 0")), qPrintable(error));
#endif
	}

	/*! Owner decision 14: the public HTDemucs exports put all four stems in ONE
	 *  [1, 4, 2, T] output. The stacked stub carries the same gains as the
	 *  four-output one, so the two layouts must split a mix into the same
	 *  samples, bit for bit - the loader's slicing is the only thing between them.
	 */
	void testStackedOutputModelSplitsLikeTheFourOutputOne()
	{
		const auto mix = makeSineMix(1);
		const QString stacked = QStringLiteral(LMMS_TEST_DATA_DIR) + QStringLiteral("/stub-4stem-stacked.onnx");
		std::array<StemSet, 2> results;
		const std::array<QString, 2> models{ stubModelPath(), stacked };
		for (std::size_t m = 0; m < models.size(); ++m)
		{
			ExternalProcessStemSeparator separator(QString(), QString(), models[m]);
			QString error;
			const auto status = separator.separate(*mix, StemModelSampleRate, 16384, StemQuality::Standard,
				[](float) { return true; }, results[m], error);
			QVERIFY2(status == StemSeparator::Status::Success, qPrintable(models[m] + ": " + error));
		}
		for (int s = 0; s < NumStems; ++s)
		{
			const auto& four = results[0][static_cast<std::size_t>(s)];
			const auto& one = results[1][static_cast<std::size_t>(s)];
			QVERIFY(four != nullptr && one != nullptr);
			QCOMPARE(one->size(), four->size());
			int differing = 0;
			for (std::size_t i = 0; i < four->size(); ++i)
			{
				differing += four->data()[i][0] != one->data()[i][0] || four->data()[i][1] != one->data()[i][1];
			}
			QVERIFY2(differing == 0, qPrintable(QStringLiteral("%1: %2 frames differ between the layouts")
				.arg(stemName(static_cast<Stem>(s))).arg(differing)));
		}
	}

	void testSeparatorCanBeCancelledMidRun()
	{
		ExternalProcessStemSeparator separator;
		const auto mix = makeSineMix(1);

		// Slow the reference CLI down so the cancel file is observed reliably
		// between chunks (test hook, see ExternalProcessStemSeparator.cpp).
		qputenv("LMMS_STEM_CHUNK_DELAY_MS", QByteArrayLiteral("150"));
		StemSet stems;
		QString error;
		int progressCalls = 0;
		const auto status = separator.separate(*mix, StemModelSampleRate, 16384, StemQuality::Standard,
			[&](float) {
				// cancel after the first chunk
				return ++progressCalls < 2;
			},
			stems, error);
		qunsetenv("LMMS_STEM_CHUNK_DELAY_MS");

		QCOMPARE(static_cast<int>(status),
			static_cast<int>(StemSeparator::Status::Cancelled));
		QVERIFY(progressCalls < 10);
	}

	void testJobManagerRunsTheRealBackend()
	{
		StemJobManager manager;
		manager.setSeparator(std::make_unique<ExternalProcessStemSeparator>());
		QSignalSpy progressSpy(&manager, &StemJobManager::jobProgress);
		QSignalSpy finishedSpy(&manager, &StemJobManager::jobFinished);
		QSignalSpy failedSpy(&manager, &StemJobManager::jobFailed);

		const auto mix = makeSineMix(1);
		QElapsedTimer timer;
		timer.start();
		const int id = manager.submit(mix, StemModelSampleRate, 16384);
		QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() + failedSpy.count() >= 1, 180000);
		const auto elapsedMs = timer.elapsed();
		qInfo("job wall time: %lld ms", static_cast<long long>(elapsedMs));

		QCOMPARE(failedSpy.count(), 0);
		QCOMPARE(static_cast<int>(manager.state(id)),
			static_cast<int>(StemJobManager::State::Completed));
		QVERIFY(progressSpy.count() >= 2);

		const auto stems = manager.result(id);
		for (int s = 0; s < NumStems; ++s)
		{
			QVERIFY(stems[static_cast<std::size_t>(s)] != nullptr);
			QCOMPARE(stems[static_cast<std::size_t>(s)]->size(), mix->size());
		}

		// The stub model's four gains (0.4, 0.3, 0.2, 0.1) sum to 1.0, so the
		// overlap-add output must reconstruct the input sample-for-sample.
		float maxError = 0.0f;
		for (std::size_t i = 0; i < mix->size(); i += 97)
		{
			float sumLeft = 0.0f;
			float sumRight = 0.0f;
			for (int s = 0; s < NumStems; ++s)
			{
				sumLeft += stems[static_cast<std::size_t>(s)]->data()[i].left();
				sumRight += stems[static_cast<std::size_t>(s)]->data()[i].right();
			}
			maxError = std::max(maxError, std::abs(sumLeft - mix->data()[i].left()));
			maxError = std::max(maxError, std::abs(sumRight - mix->data()[i].right()));
		}
		qInfo("max |sum(stems) - mix| = %.3e", static_cast<double>(maxError));
		QVERIFY2(maxError < 1.0e-4f,
			qPrintable(QStringLiteral("stems do not reconstruct the mix, max error %1")
				.arg(static_cast<double>(maxError))));
	}

	void testStemTracksAreCreatedInTheSong()
	{
		StemSet stems;
		QString error;
		const auto mix = makeSineMix(1);
		ExternalProcessStemSeparator separator;
		const auto status = separator.separate(*mix, StemModelSampleRate, 16384, StemQuality::Standard,
			[](float) { return true; }, stems, error);
		QVERIFY2(status == StemSeparator::Status::Success, qPrintable(error));

		auto* song = Engine::getSong();
		const auto before = song->tracks().size();
		const auto tracks = StemTrackBuilder::createStemTracks(
			song, stems, TimePos(0), QStringLiteral("mix"), &error);

		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(tracks.size(), 4);
		QCOMPARE(song->tracks().size(), before + 4);

		for (int s = 0; s < NumStems; ++s)
		{
			auto* track = tracks.at(s);
			QVERIFY(track != nullptr);
			QCOMPARE(static_cast<int>(track->type()),
				static_cast<int>(Track::Type::Sample));
			QVERIFY2(track->name().contains(
					QString::fromLatin1(stemName(static_cast<Stem>(s)))),
				qPrintable(track->name()));
			QCOMPARE(static_cast<int>(track->getClips().size()), 1);
			QVERIFY(song->tracks().end() !=
				std::find(song->tracks().begin(), song->tracks().end(), track));
		}
	}

	/* Portability (Q050-035 rework): the GUI's "Split to stems" calls
	 * `stem.job_start` with `PathUtil::toAbsolute()`'s result, which on Windows
	 * is a drive path (`C:/…`) or a UNC share (`//server/share/…`). The validator
	 * used to accept only a leading '/', so the GUI route could never start a job
	 * on Windows. This proves the validator accepts those forms and still refuses
	 * relative ones - and, through the registered command the GUI calls, that a
	 * Windows path gets PAST the validator to the engine's own refusal (a missing
	 * file), not the argument refusal. */
	void testValidatorAcceptsWindowsAbsolutePathsThroughJobStart()
	{
		const QString source = QStringLiteral("source");
		QVERIFY2(control::stemRequireAbsolutePath(QStringLiteral("/tmp/mix.wav"), source).isEmpty(),
			"a Unix absolute path is accepted");
		QVERIFY2(!control::stemRequireAbsolutePath(QStringLiteral("mix.wav"), source).isEmpty(),
			"a relative path is refused");
		QVERIFY2(!control::stemRequireAbsolutePath(QStringLiteral("C:relative.wav"), source).isEmpty(),
			"a drive-relative path is refused");
		QVERIFY2(!control::stemRequireAbsolutePath(QStringLiteral("C:"), source).isEmpty(),
			"a bare drive letter is refused");

		const QString windows[] = {
			QStringLiteral("C:\\music\\mix.wav"),
			QStringLiteral("C:/music/mix.wav"),
			QStringLiteral("\\\\server\\share\\mix.wav"),
			QStringLiteral("//server/share/mix.wav") };
		for (const QString& path : windows)
		{
			QVERIFY2(control::stemRequireAbsolutePath(path, source).isEmpty(),
				qPrintable(QStringLiteral("Windows absolute path accepted: ") + path));
		}

		const QString missing[] = {
			QStringLiteral("C:/definitely-not-here/mix.wav"),
			QStringLiteral("\\\\server\\share\\definitely-not-here\\mix.wav") };
		for (const QString& path : missing)
		{
			const ControlResult reply = ControlRegistry::instance()->invoke(
				QStringLiteral("stem.job_start"), QJsonObject{{QStringLiteral("source"), path}});
			QVERIFY2(!reply.ok && reply.errorKind == ControlErrorKind::Refused
					&& reply.errorMessage.contains(QStringLiteral("no source file")),
				qPrintable(QStringLiteral("Windows path reaches the engine's refusal: %1 -> %2")
					.arg(path, reply.errorMessage)));
		}
		const ControlResult relative = ControlRegistry::instance()->invoke(
			QStringLiteral("stem.job_start"),
			QJsonObject{{QStringLiteral("source"), QStringLiteral("mix.wav")}});
		QVERIFY2(!relative.ok && relative.errorKind == ControlErrorKind::InvalidArgs,
			qPrintable(QStringLiteral("a relative path is still refused typed: ") + relative.errorMessage));
	}

	/* The GUI action and the agent surface take the SAME registered command,
	 * materialise the four stems as ONE undo step, and one control.undo removes
	 * all four together. This is the "shared command + one undo step" contract
	 * of SPEC A11/A16 for the user-facing half of stem separation. */
	void testGuiSplitSharesTheRegisteredCommandAndIsOneUndoStep()
	{
		auto* song = Engine::getSong();
		const auto before = song->tracks().size();
		auto* sourceTrack = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		QVERIFY(sourceTrack != nullptr);
		auto* source = dynamic_cast<SampleClip*>(sourceTrack->createClip(TimePos(0)));
		QVERIFY(source != nullptr);
		source->setName(QStringLiteral("Mix"));
		source->setSampleBuffer(makeSineMix(1));

		// The command the GUI action calls is a registered agent command.
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("stem.job_start")));
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("stem.tracks_create")));
		const int recordsBefore = stemTracksCreateRecords();

		gui::StemSplitController controller;
		controller.splitClipToStems(source);

		QPointer<QProgressDialog> progress;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (auto* dialog = qobject_cast<QProgressDialog*>(widget)) { progress = dialog; }
		}
		QVERIFY(!progress.isNull());
		QTimer closeMessageBox;
		closeMessageBox.setInterval(10);
		connect(&closeMessageBox, &QTimer::timeout, &closeMessageBoxes);
		closeMessageBox.start();
		QTRY_VERIFY_WITH_TIMEOUT(progress.isNull(), 180000);
		closeMessageBox.stop();

		QCOMPARE(song->tracks().size(), before + 5);   // the source + four stems
		// The controller did NOT build the tracks itself: it went through the
		// registered command, which is what recorded the transaction.
		QCOMPARE(stemTracksCreateRecords(), recordsBefore + 1);

		const ControlResult undone = ControlRegistry::instance()->invoke(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(song->tracks().size(), before + 1);   // ONE undo removed all four
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

	void controllerUsesClipSnapshotAfterSourceClipIsDeleted()
	{
		auto* song = Engine::getSong();
		const auto trackIndex = song->tracks().size();
		auto* sourceTrack = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		QVERIFY(sourceTrack != nullptr);

		const TimePos submittedPosition(384);
		auto* source = dynamic_cast<SampleClip*>(sourceTrack->createClip(submittedPosition));
		QVERIFY(source != nullptr);
		source->setName(QStringLiteral("Submitted source"));
		source->setSampleBuffer(makeSineMix(1));

		gui::StemSplitController controller;
		controller.splitClipToStems(source);

		// The worker owns its input buffer. The controller must not need the
		// source clip after submission; reusing its allocation makes stale reads
		// observable in the names and positions of the generated tracks.
		sourceTrack->deleteClips();
		auto* replacement = dynamic_cast<SampleClip*>(sourceTrack->createClip(TimePos(768)));
		QVERIFY(replacement != nullptr);
		replacement->setName(QStringLiteral("Replacement source"));
		replacement->setSampleBuffer(makeSineMix(1));

		QPointer<QProgressDialog> progress;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (auto* dialog = qobject_cast<QProgressDialog*>(widget)) { progress = dialog; }
		}
		QVERIFY(!progress.isNull());

		QTimer closeMessageBox;
		closeMessageBox.setInterval(10);
		connect(&closeMessageBox, &QTimer::timeout, &closeMessageBoxes);
		closeMessageBox.start();
		QTRY_VERIFY_WITH_TIMEOUT(progress.isNull(), 180000);
		closeMessageBox.stop();

		QCOMPARE(song->tracks().size(), trackIndex + 5);
		for (int s = 0; s < NumStems; ++s)
		{
			auto* stemTrack = dynamic_cast<SampleTrack*>(song->tracks().at(trackIndex + 1 + s));
			QVERIFY(stemTrack != nullptr);
			QVERIFY(stemTrack->name().startsWith(QStringLiteral("Submitted source - ")));
			QCOMPARE(stemTrack->getClips().front()->startPosition().getTicks(), submittedPosition.getTicks());
		}

		for (int s = 0; s < NumStems; ++s) { delete song->tracks().at(trackIndex + 1); }
		delete sourceTrack;
	}

	void controllerSkipsResultsWhenSourceTrackWasDeleted()
	{
		auto* song = Engine::getSong();
		const auto trackCount = song->tracks().size();
		auto* sourceTrack = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		QVERIFY(sourceTrack != nullptr);
		auto* source = dynamic_cast<SampleClip*>(sourceTrack->createClip(TimePos(384)));
		QVERIFY(source != nullptr);
		source->setName(QStringLiteral("Deleted track source"));
		source->setSampleBuffer(makeSineMix(1));

		gui::StemSplitController controller;
		controller.splitClipToStems(source);
		delete sourceTrack;

		QPointer<QProgressDialog> progress;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			if (auto* dialog = qobject_cast<QProgressDialog*>(widget)) { progress = dialog; }
		}
		QVERIFY(!progress.isNull());
		QTimer closeMessageBox;
		closeMessageBox.setInterval(10);
		connect(&closeMessageBox, &QTimer::timeout, &closeMessageBoxes);
		closeMessageBox.start();
		QTRY_VERIFY_WITH_TIMEOUT(progress.isNull(), 180000);
		closeMessageBox.stop();
		QCOMPARE(song->tracks().size(), trackCount);
	}

	/* Bug (cluster new-p17-176): pollJob set the model-download label while
	 *  fetching_model was true and never restored it, so the separation ran
	 *  under the download text. This drives the REAL pollJob through three
	 *  scripted statuses (fetching, separating, cancelled) using the status
	 *  reader seam, and checks the dialog after each poll. */
	void pollRestoresPhaseLabelWhenFetchingEnds()
	{
		auto* song = Engine::getSong();
		auto* sourceTrack = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, song));
		QVERIFY(sourceTrack != nullptr);
		auto* source = dynamic_cast<SampleClip*>(sourceTrack->createClip(TimePos(0)));
		QVERIFY(source != nullptr);
		source->setName(QStringLiteral("Poll label source"));
		source->setSampleBuffer(makeSineMix(1));

		struct Step
		{
			const char* state;
			double progress;
			bool fetching;
		};
		const std::array<Step, 3> steps{{
			{"running", 0.25, true},       // first poll: the model is being fetched
			{"running", 0.50, false},      // second poll: fetching has ended, separation runs
			{"cancelled", 0.50, false}}};  // third poll: the job ends and the dialog closes
		int jobId = -1;
		std::size_t polls = 0;

		// Restores the real status reader and cancels the job the test submitted,
		// on every exit path (QVERIFY returns early).
		struct Cleanup
		{
			int* jobId;
			~Cleanup()
			{
				gui::StemSplitController::setStatusReaderForTest(gui::StemSplitController::StatusReader{});
				if (*jobId >= 0)
				{
					ControlRegistry::instance()->invoke(QStringLiteral("stem.job_cancel"),
						QJsonObject{{QStringLiteral("job_id"), *jobId}});
				}
			}
		} cleanup{&jobId};

		gui::StemSplitController::setStatusReaderForTest(
			[&jobId, &polls, &steps](int submitted) {
				jobId = submitted;
				const std::size_t index = polls < steps.size() ? polls : steps.size() - 1;
				++polls;
				const Step& step = steps[index];
				const QJsonObject job{
					{QStringLiteral("job_id"), submitted},
					{QStringLiteral("state"), QString::fromLatin1(step.state)},
					{QStringLiteral("progress"), step.progress},
					{QStringLiteral("fetching_model"), step.fetching}};
				return ControlResult::success(QJsonObject{{QStringLiteral("jobs"), QJsonArray{job}}});
			});

		gui::StemSplitController controller;
		controller.splitClipToStems(source);

		QProgressDialog* dialog = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			auto* candidate = qobject_cast<QProgressDialog*>(widget);
			if (candidate != nullptr && candidate->isVisible()) { dialog = candidate; }
		}
		QVERIFY(dialog != nullptr);
		QPointer<QProgressDialog> progress(dialog);

		// Each poll is the controller's own pollJob, which reads its status from
		// the script. No event loop runs, so the poll timer cannot interleave.
		QVERIFY(QMetaObject::invokeMethod(&controller, "pollJob", Qt::DirectConnection));
		QCOMPARE(progress->value(), 25);
		QVERIFY(progress->labelText().contains(QStringLiteral("Downloading the stem model")));

		QVERIFY(QMetaObject::invokeMethod(&controller, "pollJob", Qt::DirectConnection));
		QCOMPARE(progress->value(), 50);
		QVERIFY2(!progress->labelText().contains(QStringLiteral("Downloading")),
			qPrintable(progress->labelText()));
		QVERIFY(progress->labelText().contains(QStringLiteral("Separating stems")));

		QVERIFY(QMetaObject::invokeMethod(&controller, "pollJob", Qt::DirectConnection));
		QCOMPARE(polls, std::size_t{3});
		QVERIFY(progress.isNull() || !progress->isVisible());

		delete sourceTrack;
	}

	void testStemTrackBuilderRejectsAnIncompleteSet()
	{
		auto* song = Engine::getSong();
		const auto before = song->tracks().size();
		StemSet partial;
		QString error;
		const auto tracks = StemTrackBuilder::createStemTracks(
			song, partial, TimePos(0), QStringLiteral("mix"), &error);
		QVERIFY(tracks.isEmpty());
		QVERIFY(!error.isEmpty());
		// all-or-nothing: nothing may be left behind in the project
		QCOMPARE(song->tracks().size(), before);
	}
};

QTEST_MAIN(StemSplitPipelineTest)
#include "StemSplitPipelineTest.moc"
