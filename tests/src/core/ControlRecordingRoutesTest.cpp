/*
 * ControlRecordingRoutesTest.cpp - the record.arm_track undo contract when a
 *                                   take writes outside the project journal.
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

#include <cstdint>
#include <vector>

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"
#include "RecordingJournal.h"
#include "SampleFrame.h"
#include "TrackRecorder.h"

#include <sndfile.h>

using namespace lmms;

namespace
{

//! Make sure a failed assertion cannot leave the test engine writing to a
//! temporary directory after that directory is removed.
struct StopRecorderOnExit
{
	MultiTrackRecorder& recorder;
	~StopRecorderOnExit() { recorder.disarmAll(); }
};

} // namespace

class ControlRecordingRoutesTest : public QObject
{
	Q_OBJECT

	//! Record \a frames frames of constant \a value into route 0's armed take.
	void stage(int frames, float value)
	{
		AudioEngine* audio = Engine::audioEngine();
		MultiTrackRecorder& recorder = audio->recorder();
		const int channels = recorder.inputChannelCapacity();
		std::vector<sample_t> input(static_cast<std::size_t>(channels) * frames, value);
		recorder.processInputInterleaved(input.data(), channels, frames);
	}

	//! Byte content of \a path, for sentinel preservation checks.
	static QByteArray contents(const QString& path) { QFile f(path); f.open(QIODevice::ReadOnly); return f.readAll(); }

	//! The take's samples read back as int32 (24-bit PCM through libsndfile).
	static std::vector<std::int32_t> takeSamples(const QString& path, std::size_t frames)
	{
		SF_INFO info{}; info.format = 0;
		SNDFILE* sf = sf_open(path.toUtf8().constData(), SFM_READ, &info);
		if (sf == nullptr) { return {}; }
		std::vector<std::int32_t> out;
		out.resize(frames * 1);
		sf_readf_int(sf, out.data(), static_cast<sf_count_t>(frames));
		sf_close(sf);
		return out;
	}

	//! Point the default-take directory at \a dir for the rest of the test, then restore it.
	static auto useRecoveryDir(const QTemporaryDir& dir)
	{
		const QString previous = ConfigManager::inst()->workingDir();
		ConfigManager::inst()->setWorkingDir(dir.path());
		return qScopeGuard([previous] { ConfigManager::inst()->setWorkingDir(previous); });
	}

private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void undoRefusesToPretendTheRetainedTakeWasRemoved()
	{
		AudioEngine* audio = Engine::audioEngine();
		QVERIFY(audio != nullptr);
		MultiTrackRecorder& recorder = audio->recorder();
		QVERIFY(recorder.inputChannelCapacity() > 0);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		StopRecorderOnExit stopRecorder{recorder};
		const QString takePath = dir.filePath(QStringLiteral("route.wav"));
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult armed = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}, {QStringLiteral("file"), takePath}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		QVERIFY(recorder.track(0).isArmed());
		QVERIFY(QFileInfo::exists(takePath));

		constexpr f_cnt_t Frames = 4;
		const int channels = recorder.inputChannelCapacity();
		std::vector<sample_t> input(static_cast<std::size_t>(channels) * Frames, 0.25f);
		recorder.processInputInterleaved(input.data(), channels, Frames);
		QCOMPARE(recorder.track(0).framesPushed(), static_cast<std::uint64_t>(Frames));

		const ControlResult undo = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY2(!undo.ok,
			"control.undo claimed to reverse record.arm_track while its take file is retained");
		QCOMPARE(undo.errorKind, ControlErrorKind::Irreversible);
		QVERIFY(undo.errorMessage.contains(QStringLiteral("record.arm_track")));
		QVERIFY(undo.errorMessage.contains(QStringLiteral("record.disarm_track")));
		QVERIFY(recorder.track(0).isArmed());
		QVERIFY(QFileInfo::exists(takePath));

		const ControlRegistry::Transaction* transaction = registry->lastTransaction();
		QVERIFY(transaction != nullptr);
		QCOMPARE(transaction->command, QStringLiteral("record.arm_track"));
		QCOMPARE(transaction->cls, QStringLiteral("irreversible"));
		QVERIFY(!transaction->reversible);
		QVERIFY(transaction->inverse.value(QStringLiteral("op")).toString().isEmpty());
		const control::ReversibilityEntry* row = control::ReversibilityTable::instance()
			.lookup(QStringLiteral("record.arm_track"));
		QVERIFY(row != nullptr);
		QCOMPARE(row->cls, control::ReversibilityClass::Irreversible);
		QVERIFY(!row->reversible);
		QVERIFY(row->fallback.contains(QStringLiteral("record.disarm_track")));
		QVERIFY(row->fallback.contains(QStringLiteral("delete it manually")));
		QVERIFY(registry->command(QStringLiteral("record.arm_track"))->description
			.contains(QStringLiteral("Not reversible")));
		QVERIFY(!registry->command(QStringLiteral("record.disarm_track"))->description
			.contains(QStringLiteral("recorded inverse")));

		const ControlResult stopped = registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(stopped.ok, qPrintable(stopped.errorMessage));
		QVERIFY(!recorder.track(0).isArmed());
		QVERIFY(QFileInfo::exists(takePath));
		QVERIFY(QFileInfo(takePath).size() > 44);
	}

	//! A rejected input channel must not strand an allocated default take name.
	void invalidDefaultArmDoesNotLeaveReservation()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const int invalidChannel = Engine::audioEngine()->recorder().track(0).inputChannelCapacity();
		QStringList routeArtifacts;
		QStringList trackArtifacts;

		QTemporaryDir routeDir;
		QVERIFY(routeDir.isValid());
		{
			auto restore = useRecoveryDir(routeDir);
			const ControlResult rejected = registry->invoke(QStringLiteral("record.arm_track"),
				QJsonObject{{QStringLiteral("route"), 0},
					{QStringLiteral("input_channel"), invalidChannel}});
			QVERIFY(!rejected.ok);
			routeArtifacts = QDir(routeDir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
		}

		QTemporaryDir trackDir;
		QVERIFY(trackDir.isValid());
		{
			auto restore = useRecoveryDir(trackDir);
			const ControlResult added = registry->invoke(QStringLiteral("track.add"),
				QJsonObject{{QStringLiteral("type"), QStringLiteral("sample")}});
			QVERIFY2(added.ok, qPrintable(added.errorMessage));
			const ControlResult rejected = registry->invoke(QStringLiteral("track.set_arm"),
				QJsonObject{{QStringLiteral("track"), added.result.value(QStringLiteral("track")).toString()},
					{QStringLiteral("armed"), true}, {QStringLiteral("input_channel"), invalidChannel}});
			QVERIFY(!rejected.ok);
			trackArtifacts = QDir(trackDir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
		}
		QVERIFY2(routeArtifacts.isEmpty() && trackArtifacts.isEmpty(),
			qPrintable(QStringLiteral("record.arm_track left [%1], track.set_arm left [%2]")
				.arg(routeArtifacts.join(QStringLiteral(", ")))
				.arg(trackArtifacts.join(QStringLiteral(", ")))));
	}

	//! new-p17-24: re-arming with no `file` must not reuse the previous take.
	void secondDefaultArmDoesNotTruncateFirstTake()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		StopRecorderOnExit stopRecorder{Engine::audioEngine()->recorder()};
		auto restore = useRecoveryDir(dir);
		ControlRegistry* registry = ControlRegistry::instance();

		const ControlResult first = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(first.ok, qPrintable(first.errorMessage));
		stage(8, 0.5f);
		const QString firstPath = first.result.value(QStringLiteral("file")).toString();
		QVERIFY(QFileInfo::exists(firstPath));
		QVERIFY(registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}}).ok);
		const auto firstBytes = contents(firstPath);
		QVERIFY(firstBytes.size() > 44);
		const auto firstSamples = takeSamples(firstPath, 8);
		QVERIFY(!firstSamples.empty());

		const ControlResult second = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(second.ok, qPrintable(second.errorMessage));
		const QString secondPath = second.result.value(QStringLiteral("file")).toString();
		QVERIFY2(secondPath != firstPath,
			qPrintable(QStringLiteral("second default arm reused %1").arg(firstPath)));
		stage(8, -0.25f);
		QVERIFY(registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}}).ok);

		QCOMPARE(contents(firstPath), firstBytes);
		QCOMPARE(takeSamples(firstPath, 8), firstSamples);
		QVERIFY(contents(secondPath).size() > 44);
		QVERIFY(takeSamples(secondPath, 8) != firstSamples);
	}

	//! new-p17-24: a default arm must skip a candidate whose file or journal exists.
	void defaultArmSkipsExistingTakeAndJournal()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		StopRecorderOnExit stopRecorder{Engine::audioEngine()->recorder()};
		auto restore = useRecoveryDir(dir);
		ControlRegistry* registry = ControlRegistry::instance();

		const QString seeded = dir.filePath(QStringLiteral("zene-take-route0.wav"));
		const QByteArray sentinel(QStringLiteral("KEEP-ME-WAV-BYTES").toUtf8());
		{ QFile f(seeded); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(sentinel); }
		const QString seededJournal = recordingjournal::journalPathFor(seeded);
		const QByteArray journalSentinel(QStringLiteral("KEEP-ME-JOURNAL-BYTES").toUtf8());
		{ QFile f(seededJournal); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(journalSentinel); }

		const ControlResult armed = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		const QString allocated = armed.result.value(QStringLiteral("file")).toString();
		QVERIFY2(allocated != seeded,
			qPrintable(QStringLiteral("arm reused the occupied %1").arg(seeded)));
		QCOMPARE(contents(seeded), sentinel);
		QCOMPARE(contents(seededJournal), journalSentinel);
		QVERIFY(QFileInfo::exists(allocated));
		QVERIFY(registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}}).ok);
		QCOMPARE(contents(seeded), sentinel);
		QCOMPARE(contents(seededJournal), journalSentinel);
	}

	//! new-p17-24: exhausting every candidate refuses the arm and leaves no residue.
	void defaultArmExhaustionRefusesAndLeavesNoResidue()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		StopRecorderOnExit stopRecorder{Engine::audioEngine()->recorder()};
		auto restore = useRecoveryDir(dir);
		ControlRegistry* registry = ControlRegistry::instance();

		// Occupy the whole bounded candidate space the allocator may try.
		for (int index = 0; index < 1000; ++index)
		{
			const QString suffix = index == 0 ? QString() : QStringLiteral("-%1").arg(index);
			const QString path = dir.filePath(QStringLiteral("zene-take-route0%1.wav").arg(suffix));
			QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x");
			QFile j(recordingjournal::journalPathFor(path)); QVERIFY(j.open(QIODevice::WriteOnly)); j.write("y");
		}

		const ControlResult refused = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(!refused.ok,
			"arm succeeded with every candidate occupied");
		QVERIFY(!Engine::audioEngine()->recorder().track(0).isArmed());
		for (int index = 0; index < 1000; ++index)
		{
			const QString suffix = index == 0 ? QString() : QStringLiteral("-%1").arg(index);
			const QString path = dir.filePath(QStringLiteral("zene-take-route0%1.wav").arg(suffix));
			QCOMPARE(contents(path), QByteArray("x"));
			QCOMPARE(contents(recordingjournal::journalPathFor(path)), QByteArray("y"));
		}

		// Removing occupation frees the allocator again: the route is retryable.
		QFile::remove(dir.filePath(QStringLiteral("zene-take-route0.wav")));
		QFile::remove(recordingjournal::journalPathFor(dir.filePath(QStringLiteral("zene-take-route0.wav"))));
		const ControlResult retry = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(retry.ok, qPrintable(retry.errorMessage));
		QVERIFY(registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}}).ok);
	}

	//! new-p17-24: writer-start failure after allocation cleans only its own artefacts.
	void defaultArmFailureLeavesRouteRetryableAndSentinels()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		StopRecorderOnExit stopRecorder{Engine::audioEngine()->recorder()};
		auto restore = useRecoveryDir(dir);
		ControlRegistry* registry = ControlRegistry::instance();

		const QString seeded = dir.filePath(QStringLiteral("zene-take-route0.wav"));
		const QByteArray sentinel(QStringLiteral("KEEP-ROUTE0").toUtf8());
		{ QFile f(seeded); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(sentinel); }
		const QString seededJournal = recordingjournal::journalPathFor(seeded);
		{ QFile f(seededJournal); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("keep-journal"); }

		TrackRecorder::failNextWriterThreadStartForTesting();
		const ControlResult failed = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(!failed.ok, "arm reported success despite the injected writer failure");
		QVERIFY(!Engine::audioEngine()->recorder().track(0).isArmed());
		QCOMPARE(contents(seeded), sentinel);
		QCOMPARE(contents(seededJournal), QByteArray("keep-journal"));

		const ControlResult retry = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(retry.ok, qPrintable(retry.errorMessage));
		const QString allocated = retry.result.value(QStringLiteral("file")).toString();
		QVERIFY(allocated != seeded);
		QCOMPARE(contents(seeded), sentinel);
		QVERIFY(registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}}).ok);
		QCOMPARE(contents(seeded), sentinel);
	}

	//! new-p17-27: disarm must report the journal's actual retirement result.
	void disarmJournalRetirementReflectsRemovalResult()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		StopRecorderOnExit stopRecorder{Engine::audioEngine()->recorder()};
		ControlRegistry* registry = ControlRegistry::instance();
		const QString take = dir.filePath(QStringLiteral("journalled.wav"));
		{
			// Arm on a route, disarm, then inspect what the reply said about the journal.
		}
		const ControlResult armed = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}, {QStringLiteral("file"), take}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		const QString journal = armed.result.value(QStringLiteral("journal")).toString();
		QVERIFY(!journal.isEmpty());
		QVERIFY(QFileInfo::exists(journal));
		const ControlResult disarmed = registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(disarmed.ok, qPrintable(disarmed.errorMessage));
		QCOMPARE(disarmed.result.value(QStringLiteral("journal_retired")).toBool(), true);
		QVERIFY(!QFileInfo::exists(journal));
	}

	//! new-p17-27: a journal removal failure must surface as journal_retired=false.
	void disarmJournalRemovalFailureReportsFalse()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		StopRecorderOnExit stopRecorder{Engine::audioEngine()->recorder()};
		auto restore = useRecoveryDir(dir);
		ControlRegistry* registry = ControlRegistry::instance();

		const ControlResult armed = registry->invoke(QStringLiteral("record.arm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(armed.ok, qPrintable(armed.errorMessage));
		TrackRecorder& target = Engine::audioEngine()->recorder().track(0);
		const QString journal = QString::fromStdString(target.journalPath());
		QVERIFY(!journal.isEmpty());
		QVERIFY(QFileInfo::exists(journal));

		// Make the journal non-removable while keeping it observable: replace it
		// by a DIRECTORY of the same name. RecordingJournal::remove() (QFile::remove)
		// cannot remove a directory, so retirement fails deterministically on every platform.
		QVERIFY(QFile::remove(journal));
		QVERIFY(QDir().mkdir(journal));
		const ControlResult disarmed = registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(disarmed.ok, qPrintable(disarmed.errorMessage));
		QCOMPARE(disarmed.result.value(QStringLiteral("journal_retired")).toBool(), false);
		QCOMPARE(QString::fromStdString(target.journalPath()), journal);
		QVERIFY(QFileInfo(journal).isDir());
		const ControlResult retryFailure = registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(retryFailure.ok, qPrintable(retryFailure.errorMessage));
		QCOMPARE(retryFailure.result.value(QStringLiteral("journal_retired")).toBool(), false);
		QCOMPARE(QString::fromStdString(target.journalPath()), journal);
		QVERIFY(QDir().rmdir(journal));
		const ControlResult retrySuccess = registry->invoke(QStringLiteral("record.disarm_track"),
			QJsonObject{{QStringLiteral("route"), 0}});
		QVERIFY2(retrySuccess.ok, qPrintable(retrySuccess.errorMessage));
		QCOMPARE(retrySuccess.result.value(QStringLiteral("journal_retired")).toBool(), true);
		QVERIFY(QString::fromStdString(target.journalPath()).isEmpty());
	}

	//! new-p17-24: track.set_arm's default must also be unique per take.
	void trackSetArmDefaultSecondTakeIsDistinct()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		StopRecorderOnExit stopRecorder{Engine::audioEngine()->recorder()};
		auto restore = useRecoveryDir(dir);
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult added = registry->invoke(QStringLiteral("track.add"),
			QJsonObject{{QStringLiteral("type"), QStringLiteral("sample")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString track = added.result.value(QStringLiteral("track")).toString();
		QVERIFY(!track.isEmpty());

		const ControlResult first = registry->invoke(QStringLiteral("track.set_arm"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("armed"), true}});
		QVERIFY2(first.ok, qPrintable(first.errorMessage));
		const QString firstPath = first.result.value(QStringLiteral("file")).toString();
		QVERIFY(QFileInfo::exists(firstPath));
		QVERIFY(registry->invoke(QStringLiteral("track.set_arm"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("armed"), false}}).ok);
		const auto firstBytes = contents(firstPath);

		const ControlResult second = registry->invoke(QStringLiteral("track.set_arm"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("armed"), true}});
		QVERIFY2(second.ok, qPrintable(second.errorMessage));
		const QString secondPath = second.result.value(QStringLiteral("file")).toString();
		QVERIFY2(secondPath != firstPath,
			qPrintable(QStringLiteral("track.set_arm second default reused %1").arg(firstPath)));
		QVERIFY(registry->invoke(QStringLiteral("track.set_arm"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("armed"), false}}).ok);
		QCOMPARE(contents(firstPath), firstBytes);
		QVERIFY(QFileInfo::exists(secondPath));
	}
};

QTEST_GUILESS_MAIN(ControlRecordingRoutesTest)

#include "ControlRecordingRoutesTest.moc"
