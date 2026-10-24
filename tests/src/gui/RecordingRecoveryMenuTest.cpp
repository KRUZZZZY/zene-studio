/*
 * RecordingRecoveryMenuTest.cpp - File > Recover Recordings places or dismisses an interrupted take
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

/*! Two real WAV takes journalled as in progress (record.journal_begin / journal_update, the state an
 *  abnormal exit leaves) in the instance's own recovery directory: the rebuilt submenu counts them
 *  and names each with its recoverable length; Place on a New Sample Track restores the first and
 *  loads it into a clip at bar 1 of a new sample track; Dismiss drops the second's journal and
 *  keeps its audio file; the next rebuild offers neither. */

#include <QtTest>

#include <QAction>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QMenu>
#include <QStringList>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRecordingSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "PathUtil.h"
#include "ProjectJournal.h"
#include "RecordingRecoveryMenu.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr int kRate = 44100;
constexpr int kFrames = 22050;

//! A 16-bit mono WAV of @a frames frames of a quiet square wave.
bool writeWav(const QString& path, int frames)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	const quint32 data = static_cast<quint32>(frames) * 2;
	out.writeRawData("RIFF", 4); out << quint32(36 + data);
	out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1) << quint32(kRate)
		<< quint32(kRate * 2) << quint16(2) << quint16(16);
	out.writeRawData("data", 4); out << data;
	for (int i = 0; i < frames; ++i) { out << qint16((i / 50) % 2 ? 3000 : -3000); }
	return true;
}

/*! A 16-bit mono WAV whose header declares \a declaredFrames but which only holds
 *  \a writtenFrames of them - what a crash or a truncated write leaves: the sizes in
 *  the header are the sizes the writer INTENDED, not the bytes that reached the disk. */
bool writeTruncatedWav(const QString& path, int declaredFrames, int writtenFrames)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	const quint32 data = static_cast<quint32>(declaredFrames) * 2;
	out.writeRawData("RIFF", 4); out << quint32(36 + data);
	out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1) << quint32(kRate)
		<< quint32(kRate * 2) << quint16(2) << quint16(16);
	out.writeRawData("data", 4); out << data;
	for (int i = 0; i < writtenFrames; ++i) { out << qint16((i / 50) % 2 ? 3000 : -3000); }
	return true;
}

//! The `state=` line of a take's journal side file ("" when unreadable).
QString journalState(const QString& journalPath)
{
	QFile file(journalPath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { return QString(); }
	for (const QByteArray& line : file.readAll().split('\n'))
	{
		if (line.startsWith("state=")) { return QString::fromUtf8(line.mid(6)).trimmed(); }
	}
	return QString();
}

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject{})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QAction* item(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
	}
	return nullptr;
}

//! The warnings the test saw. qInstallMessageHandler takes a plain function pointer, so the
//! capture cannot be a lambda and lives here.
QStringList g_warnings;

void captureWarning(QtMsgType type, const QMessageLogContext&, const QString& message)
{
	if (type == QtWarningMsg) { g_warnings.append(message); }
}

} // namespace

class RecordingRecoveryMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_world.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
		ConfigManager::inst()->setWorkingDir(m_world.path());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theTextNamesTheLength()
	{
		QCOMPARE(recoveredTakeText({{QStringLiteral("take"), QStringLiteral("/x/take-1.wav")},
			{QStringLiteral("sample_rate"), 48000}, {QStringLiteral("frames_recoverable"), 72000}}),
			QStringLiteral("take-1.wav (1.5 s recoverable)"));
	}

	void anInterruptedTakeIsPlacedOrDismissed()
	{
		const ControlResult scanned = run(QStringLiteral("record.recovery_get_state"));
		QVERIFY2(scanned.ok, qPrintable(scanned.errorMessage));
		const QString dir = scanned.result.value(QStringLiteral("dir")).toString();
		const QString placed = QDir(dir).filePath(QStringLiteral("zene-test-take-placed.wav"));
		const QString dismissed = QDir(dir).filePath(QStringLiteral("zene-test-take-dismissed.wav"));
		for (const QString& take : {placed, dismissed})
		{
			QVERIFY(writeWav(take, kFrames));
			const ControlResult begun = run(QStringLiteral("record.journal_begin"), {{QStringLiteral("take"), take},
				{QStringLiteral("sample_rate"), kRate}, {QStringLiteral("channels"), 1}});
			QVERIFY2(begun.ok, qPrintable(begun.errorMessage));
			QVERIFY(run(QStringLiteral("record.journal_update"), {{QStringLiteral("take"), take},
				{QStringLiteral("frames_on_disk"), kFrames}}).ok);
		}

		QMenu parent;
		QMenu* menu = addRecordingRecoveryMenu(&parent);
		emit menu->aboutToShow();
		QCOMPARE(menu->actions().first()->text(), QStringLiteral("2 interrupted recording(s)"));
		QAction* placedTake = item(menu, QStringLiteral("zene-test-take-placed.wav (0.5 s recoverable)"));
		QAction* dismissedTake = item(menu, QStringLiteral("zene-test-take-dismissed.wav (0.5 s recoverable)"));
		QVERIFY(placedTake != nullptr && placedTake->menu() != nullptr);
		QVERIFY(dismissedTake != nullptr && dismissedTake->menu() != nullptr);

		const auto tracksBefore = Engine::getSong()->tracks().size();
		item(placedTake->menu(), QStringLiteral("Place on a New Sample Track"))->trigger();
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore + 1);
		auto* track = dynamic_cast<SampleTrack*>(Engine::getSong()->tracks().back());
		QVERIFY(track != nullptr);
		QCOMPARE(track->name(), QStringLiteral("Recovered: zene-test-take-placed"));
		QCOMPARE(track->numOfClips(), 1);
		auto* clip = dynamic_cast<SampleClip*>(track->getClip(0));
		QVERIFY(clip != nullptr);
		// The clip stores the path relative to a user directory when it can (PathUtil), so the
		// file it names is compared resolved.
		QCOMPARE(QFileInfo(PathUtil::toAbsolute(clip->sampleFile())).canonicalFilePath(),
			QFileInfo(placed).canonicalFilePath());
		QVERIFY(clip->sampleLength().getTicks() > 0);

		// The menu ran record.recovery_place: the journal is resolved, and ONE control.undo takes the
		// track away and puts the offer back; one control.redo places it again.
		const QString placedJournal = placed + QStringLiteral(".rec-journal");
		QCOMPARE(journalState(placedJournal), QStringLiteral("restored"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore);
		QCOMPARE(journalState(placedJournal), QStringLiteral("in_progress"));
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore + 1);
		QCOMPARE(journalState(placedJournal), QStringLiteral("restored"));

		item(dismissedTake->menu(), QStringLiteral("Dismiss (keep the file)"))->trigger();
		QVERIFY(QFileInfo::exists(dismissed));
		QVERIFY(!QFileInfo::exists(dismissed + QStringLiteral(".rec-journal")));

		emit menu->aboutToShow();
		QCOMPARE(menu->actions().first()->text(), QStringLiteral("No interrupted recording"));
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	}

	//! Acceptance 1: the startup offer asks only when record.recovery_get_state reports an interrupted take.
	void startupAsksOnlyWhenATakeWasInterrupted()
	{
		int asked = 0;
		const auto neverAsked = [&asked](const QJsonArray&) { ++asked; return StartupRecoveryChoice::Place; };
		QCOMPARE(run(QStringLiteral("record.recovery_get_state")).result.value(QStringLiteral("count")).toInt(), 0);
		QVERIFY(offerRecordingRecovery(nullptr, neverAsked) == StartupRecoveryChoice::NothingToOffer);
		QCOMPARE(asked, 0);

		const QString dir = run(QStringLiteral("record.recovery_get_state")).result.value(QStringLiteral("dir")).toString();
		const QString take = QDir(dir).filePath(QStringLiteral("zene-test-take-startup.wav"));
		QVERIFY(writeWav(take, kFrames));
		QVERIFY(run(QStringLiteral("record.journal_begin"), {{QStringLiteral("take"), take},
			{QStringLiteral("sample_rate"), kRate}, {QStringLiteral("channels"), 1}}).ok);
		QVERIFY(run(QStringLiteral("record.journal_update"), {{QStringLiteral("take"), take},
			{QStringLiteral("frames_on_disk"), kFrames}}).ok);
		const auto tracksBefore = Engine::getSong()->tracks().size();

		// Decide Later: asked once, with the take named, and nothing changes.
		int takesOffered = 0;
		QVERIFY(offerRecordingRecovery(nullptr, [&takesOffered](const QJsonArray& takes) {
			takesOffered = static_cast<int>(takes.size()); return StartupRecoveryChoice::Later; }) == StartupRecoveryChoice::Later);
		QCOMPARE(takesOffered, 1);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore);
		QCOMPARE(journalState(take + QStringLiteral(".rec-journal")), QStringLiteral("in_progress"));

		// Place: one new track, the offer is resolved, and a second start offers nothing.
		QVERIFY(offerRecordingRecovery(nullptr, neverAsked) == StartupRecoveryChoice::Place);
		QCOMPARE(asked, 1);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore + 1);
		QCOMPARE(journalState(take + QStringLiteral(".rec-journal")), QStringLiteral("restored"));
		QVERIFY(offerRecordingRecovery(nullptr, neverAsked) == StartupRecoveryChoice::NothingToOffer);
		QCOMPARE(asked, 1);

		// One control.undo takes the placement back and the offer returns.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore);
		QCOMPARE(run(QStringLiteral("record.recovery_get_state")).result.value(QStringLiteral("takes")).toArray().size(), 1);
		QVERIFY(run(QStringLiteral("record.recovery_discard"), {{QStringLiteral("take"), take}}).ok);
		QFile::remove(take);
	}

	//! A take with no audio is refused, typed, and leaves the journal alone.
	void aTakeWithNoAudioCannotBePlaced()
	{
		const QString dir = run(QStringLiteral("record.recovery_get_state")).result.value(QStringLiteral("dir")).toString();
		const QString take = QDir(dir).filePath(QStringLiteral("zene-test-take-empty.wav"));
		QVERIFY(writeWav(take, 0));
		QVERIFY(run(QStringLiteral("record.journal_begin"), {{QStringLiteral("take"), take},
			{QStringLiteral("sample_rate"), kRate}, {QStringLiteral("channels"), 1}}).ok);
		const auto tracksBefore = Engine::getSong()->tracks().size();
		const ControlResult placed = run(QStringLiteral("record.recovery_place"), {{QStringLiteral("take"), take}});
		QVERIFY(!placed.ok);
		QCOMPARE(placed.errorKind, ControlErrorKind::Refused);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore);
		QCOMPARE(journalState(take + QStringLiteral(".rec-journal")), QStringLiteral("in_progress"));
		QVERIFY(run(QStringLiteral("record.recovery_discard"), {{QStringLiteral("take"), take}}).ok);
		QFile::remove(take);
	}

	//! frames_in_file is what the take's bytes can back, never what its header
	//! declares: a crashed header still says 48000 frames while only half of them
	//! reached the disk, and an offer built on the declared size promises audio
	//! that is not there.
	void aTruncatedTakeIsOfferedWhatIsActuallyOnDisk()
	{
		const ControlResult scanned = run(QStringLiteral("record.recovery_get_state"));
		QVERIFY2(scanned.ok, qPrintable(scanned.errorMessage));
		const QString dir = scanned.result.value(QStringLiteral("dir")).toString();
		const QString take = QDir(dir).filePath(QStringLiteral("zene-test-take-truncated.wav"));
		QVERIFY(writeTruncatedWav(take, 2 * kFrames, kFrames));
		QVERIFY(run(QStringLiteral("record.journal_begin"), {{QStringLiteral("take"), take},
			{QStringLiteral("sample_rate"), kRate}, {QStringLiteral("channels"), 1}}).ok);
		// The journal claims the whole declared length - a recorder that had not
		// flushed its last update is exactly the state recovery exists for.
		QVERIFY(run(QStringLiteral("record.journal_update"), {{QStringLiteral("take"), take},
			{QStringLiteral("frames_on_disk"), 2 * kFrames}}).ok);

		const QJsonObject entry = run(QStringLiteral("record.recovery_restore"),
			{{QStringLiteral("take"), take}}).result;
		QCOMPARE(entry.value(QStringLiteral("frames_in_file")).toInt(), kFrames);
		QCOMPARE(entry.value(QStringLiteral("frames_recoverable")).toInt(), kFrames);

		QVERIFY(run(QStringLiteral("record.recovery_discard"), {{QStringLiteral("take"), take}}).ok);
		QFile::remove(take);
	}

//! A take named by a RELATIVE path is refused, typed invalid_args, by every
	//! journal read: record.journal_begin already refuses one, and a relative
	//! path names a different file in the next process's working directory -
	//! which is the run the journal exists to be found by.
	void aRelativeTakeIsRefused()
	{
		const ControlResult begun = run(QStringLiteral("record.journal_begin"),
			{{QStringLiteral("take"), QStringLiteral("relative-take.wav")},
				{QStringLiteral("sample_rate"), kRate}});
		QVERIFY(!begun.ok);
		QCOMPARE(begun.errorKind, ControlErrorKind::InvalidArgs);

		for (const QString& id : {QStringLiteral("record.journal_update"),
			QStringLiteral("record.journal_finish"), QStringLiteral("record.recovery_restore"),
			QStringLiteral("record.recovery_place"), QStringLiteral("record.recovery_discard")})
		{
			const ControlResult result = run(id, {{QStringLiteral("take"), QStringLiteral("relative-take.wav")}});
			QVERIFY2(!result.ok, qPrintable(id + QStringLiteral(" accepted a relative take")));
			QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		}
	}

	/*! A FAILED record.recovery_get_state is NOT "there is nothing to recover": the takes are
	 *  UNKNOWN, not absent. The startup offer reports the failure instead of skipping recovery
	 *  in silence: it is logged, it does not ask to place takes it could not list, and it does not
	 *  return "nothing to offer". */
	void aFailedQueryIsNotNothingToRecover()
	{
		const QString dir = run(QStringLiteral("record.recovery_get_state")).result.value(QStringLiteral("dir")).toString();
		const QString take = QDir(dir).filePath(QStringLiteral("zene-test-take-failed-query.wav"));
		QVERIFY(writeWav(take, kFrames));
		QVERIFY(run(QStringLiteral("record.journal_begin"), {{QStringLiteral("take"), take},
			{QStringLiteral("sample_rate"), kRate}, {QStringLiteral("channels"), 1}}).ok);
		QVERIFY(run(QStringLiteral("record.journal_update"), {{QStringLiteral("take"), take},
			{QStringLiteral("frames_on_disk"), kFrames}}).ok);

		// The model is not addressable (every engine command is refused with the typed 'busy'
		// until the application is up), so the query FAILS while the journal is on the disk.
		QStringList warnings;
		const QtMessageHandler previous = qInstallMessageHandler(captureWarning);
		ControlRegistry::setReady(false);
		int asked = 0;
		const StartupRecoveryChoice choice = offerRecordingRecovery(nullptr,
			[&asked](const QJsonArray&) { ++asked; return StartupRecoveryChoice::Place; });
		ControlRegistry::setReady(true);
		qInstallMessageHandler(previous);
		warnings = g_warnings;

		// The failure is reported, not swallowed.
		QVERIFY(choice != StartupRecoveryChoice::NothingToOffer);
		QCOMPARE(asked, 0);
		bool reported = false;
		for (const QString& line : warnings)
		{
			if (line.contains(QStringLiteral("record.recovery_get_state"))) { reported = true; }
		}
		QVERIFY2(reported, "the failed recovery query was not reported");
		// Nothing was placed and nothing was discarded: the journal is exactly as it was.
		QCOMPARE(journalState(take + QStringLiteral(".rec-journal")), QStringLiteral("in_progress"));
		QVERIFY(run(QStringLiteral("record.recovery_discard"), {{QStringLiteral("take"), take}}).ok);
		QFile::remove(take);
	}

	//! A redo that cannot place the take changes nothing and records nothing: control.redo fails typed, nothing is
	//! placed, the journal is not marked restored, the take is still offered, and the undo stack gets no step for the
	//! redo - the step stays on the redo stack. Track creation is made to fail between the undo and the redo.
	void aRedoThatCannotPlaceKeepsTheOffer()
	{
		const QString dir = run(QStringLiteral("record.recovery_get_state")).result.value(QStringLiteral("dir")).toString();
		const QString take = QDir(dir).filePath(QStringLiteral("zene-test-take-redo.wav"));
		QVERIFY(writeWav(take, kFrames));
		QVERIFY(run(QStringLiteral("record.journal_begin"), {{QStringLiteral("take"), take},
			{QStringLiteral("sample_rate"), kRate}, {QStringLiteral("channels"), 1}}).ok);
		QVERIFY(run(QStringLiteral("record.journal_update"), {{QStringLiteral("take"), take},
			{QStringLiteral("frames_on_disk"), kFrames}}).ok);
		const QString journal = take + QStringLiteral(".rec-journal");
		const auto tracksBefore = Engine::getSong()->tracks().size();
		QJsonArray takesBefore;
		QVERIFY(interruptedTakes(&takesBefore).ok);

		QVERIFY(run(QStringLiteral("record.recovery_place"), {{QStringLiteral("take"), take}}).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore);
		QCOMPARE(journalState(journal), QStringLiteral("in_progress"));

		// The undo stack and the redo stack as the undo left them: the redo must add nothing to the first and
		// keep the step on the second.
		ProjectJournal* history = Engine::projectJournal();
		QVERIFY(history != nullptr);
		const int undoDepthBefore = history->undoDepth();
		QVERIFY(history->canRedo());

		// The seam is set only around the redo, and reset before any assertion can return early.
		control::setRecoveredTakeFailsForTest(true);
		const ControlResult redone = run(QStringLiteral("control.redo"));
		control::setRecoveredTakeFailsForTest(false);
		QCOMPARE(history->undoDepth(), undoDepthBefore);
		QVERIFY(!redone.ok);
		QCOMPARE(redone.errorKind, ControlErrorKind::Refused);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore);
		QCOMPARE(journalState(journal), QStringLiteral("in_progress"));
		QJsonArray takesAfter;
		QVERIFY(interruptedTakes(&takesAfter).ok);
		QCOMPARE(takesAfter.size(), takesBefore.size());
		QVERIFY(history->canRedo());

		QVERIFY(run(QStringLiteral("record.recovery_discard"), {{QStringLiteral("take"), take}}).ok);
		QFile::remove(take);
	}

	//! The successful path is unchanged: place, undo, redo, undo is one undo step each way. The redo records exactly
	//! one step, and the undo after it takes that step back, so the undo stack is as it was before the placement.
	void aPlaceUndoRedoIsOneStep()
	{
		const QString dir = run(QStringLiteral("record.recovery_get_state")).result.value(QStringLiteral("dir")).toString();
		const QString take = QDir(dir).filePath(QStringLiteral("zene-test-take-one-step.wav"));
		QVERIFY(writeWav(take, kFrames));
		QVERIFY(run(QStringLiteral("record.journal_begin"), {{QStringLiteral("take"), take},
			{QStringLiteral("sample_rate"), kRate}, {QStringLiteral("channels"), 1}}).ok);
		QVERIFY(run(QStringLiteral("record.journal_update"), {{QStringLiteral("take"), take},
			{QStringLiteral("frames_on_disk"), kFrames}}).ok);
		const QString journal = take + QStringLiteral(".rec-journal");
		const auto tracksBefore = Engine::getSong()->tracks().size();
		ProjectJournal* history = Engine::projectJournal();
		QVERIFY(history != nullptr);
		const int undoDepthBefore = history->undoDepth();

		QVERIFY(run(QStringLiteral("record.recovery_place"), {{QStringLiteral("take"), take}}).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore + 1);
		QCOMPARE(history->undoDepth(), undoDepthBefore + 1);
		QVERIFY(history->canUndo());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore);
		QCOMPARE(journalState(journal), QStringLiteral("in_progress"));
		QCOMPARE(history->undoDepth(), undoDepthBefore);
		QVERIFY(history->canRedo());

		const ControlResult redone = run(QStringLiteral("control.redo"));
		QVERIFY(redone.ok);
		QVERIFY(redone.result.value(QStringLiteral("redone")).toBool());
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore + 1);
		QCOMPARE(journalState(journal), QStringLiteral("restored"));
		QCOMPARE(history->undoDepth(), undoDepthBefore + 1);
		QVERIFY(history->canUndo());

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::getSong()->tracks().size(), tracksBefore);
		QCOMPARE(journalState(journal), QStringLiteral("in_progress"));
		QCOMPARE(history->undoDepth(), undoDepthBefore);
		QVERIFY(run(QStringLiteral("record.recovery_discard"), {{QStringLiteral("take"), take}}).ok);
		QFile::remove(take);
	}

private:
	QTemporaryDir m_world;
};

QTEST_MAIN(RecordingRecoveryMenuTest)
#include "RecordingRecoveryMenuTest.moc"
