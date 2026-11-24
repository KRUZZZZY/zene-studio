/*
 * JournalBeginGuardTest.cpp - record.journal_begin must not erase recovery
 *                             progress a take's journal already holds
 *                             (cluster 2073).
 *
 * THE BUG. journalBegin read a take's existing journal into `previous`, then
 * unconditionally wrote a fresh one with framesOnDisk = 0. Recovery reports
 * `min(frames_on_disk, frames in the file)` (RecordingJournal::recoverableFrames),
 * so starting a capture over an interrupted take dropped the offer to zero
 * recoverable frames even though the WAV and the journal it replaced both
 * recorded material on disk - the one thing the journal exists to keep.
 *
 * THE CONTRACT THIS TEST PINS DOWN.
 *  - no sidecar: the capture starts, and it reports zero frames on disk (a
 *    capture that has written nothing yet is exactly what a fresh journal says);
 *  - a sidecar that still protects an interrupted capture is REFUSED, typed,
 *    and left byte-identical (sha256 over the file), with the frames it records
 *    still offered afterwards;
 *  - a sidecar that exists but cannot be read is REFUSED as well - it is the only
 *    record of what a crashed capture reached, and a write here would replace it
 *    with something that reads as a clean start;
 *  - a retired journal (record.journal_finish) leaves no sidecar, so the take can
 *    be captured again;
 *  - the sidecar that is NOT an interrupted capture - the `restored` tombstone
 *    record.recovery_restore / record.recovery_place leave behind - is still
 *    writable, because its own undo INVERSE is this command putting the offer
 *    back (ControlReversibilityTableSnapshot.cpp:309 records the paired command,
 *    and RecordingRecoveryMenuTest drives that undo); a guard that refused those
 *    would break undo of a recovery, which is a worse loss than the one fixed.
 *  So the guard is exactly `isRecoverable(previous)`: the predicate the scan
 *  itself uses to decide whether a journal is offered.
 *
 * The command is the seam: every case goes through ControlRegistry::invoke, the
 * same entry point the socket, the menu and an agent use, with the engine up.
 * No timing, no threads and no sleeps: the journal's bytes are compared by
 * sha256 before and after the refused call.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "RecordingJournal.h"

using namespace lmms;
using namespace lmms::recordingjournal;

namespace
{

constexpr int kRate = 44100;
//! A tenth of a second of audio at kRate: the interval the disk-writer commits at.
constexpr int kFrames = kRate / 10;

//! A mono 16-bit WAV of \a frames frames, which is what framesInFile() measures.
bool writeWav(const QString& path, int frames)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	const quint32 data = static_cast<quint32>(frames) * 2u;
	out.writeRawData("RIFF", 4); out << quint32(36u + data);
	out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1) << quint32(kRate)
		<< quint32(kRate * 2) << quint16(2) << quint16(16);
	out.writeRawData("data", 4); out << data;
	for (int i = 0; i < frames; ++i) { out << qint16((i / 50) % 2 ? 3000 : -3000); }
	return true;
}

//! The side file's bytes as a digest, so "untouched" is one comparison.
QByteArray sha256Of(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QByteArray(); }
	return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

//! The `state=` line of a take's journal side file ("" when unreadable).
QString journalState(const QString& journalPath)
{
	QFile file(journalPath);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
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

//! The arguments every case starts a capture with.
QJsonObject beginArgs(const QString& take)
{
	return QJsonObject{{QStringLiteral("take"), take}, {QStringLiteral("sample_rate"), kRate},
		{QStringLiteral("channels"), 1}};
}

} // namespace

class JournalBeginGuardTest : public QObject
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

	//! Normal path: with no sidecar there is nothing to protect, so the capture
	//! starts and reports zero frames - the fresh-journal contract.
	void anAbsentSidecarStartsTheCapture()
	{
		const QString take = QDir(m_world.path()).filePath(QStringLiteral("take-absent.wav"));
		QVERIFY(writeWav(take, kFrames));
		QVERIFY(!QFileInfo::exists(journalPathFor(take)));

		const ControlResult begun = run(QStringLiteral("record.journal_begin"), beginArgs(take));
		QVERIFY2(begun.ok, qPrintable(begun.errorMessage));
		QCOMPARE(begun.result.value(QStringLiteral("frames_journalled")).toInt(), 0);
		QCOMPARE(begun.result.value(QStringLiteral("state")).toString(), QStringLiteral("in_progress"));
		QVERIFY(QFileInfo::exists(journalPathFor(take)));
	}

	//! THE BUG: an interrupted capture's journal is recovery progress. Beginning
	//! again is refused, the side file is byte-identical, and the frames it
	//! protects are still offered afterwards.
	void anInterruptedJournalIsRefusedAndLeftByteIdentical()
	{
		const QString take = QDir(m_world.path()).filePath(QStringLiteral("take-interrupted.wav"));
		const QString sidecar = journalPathFor(take);
		QVERIFY(writeWav(take, kFrames));
		QVERIFY2(run(QStringLiteral("record.journal_begin"), beginArgs(take)).ok,
			"the first capture could not be journalled");
		const ControlResult updated = run(QStringLiteral("record.journal_update"),
			{{QStringLiteral("take"), take}, {QStringLiteral("frames_on_disk"), kFrames}});
		QVERIFY2(updated.ok, qPrintable(updated.errorMessage));

		const QByteArray before = sha256Of(sidecar);
		const QJsonArray transactionsBefore = run(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray();

		const ControlResult again = run(QStringLiteral("record.journal_begin"), beginArgs(take));
		QVERIFY2(!again.ok, "record.journal_begin REPLACED an interrupted capture's journal, "
			"dropping the recovery offer to zero frames");
		QCOMPARE(again.errorKind, ControlErrorKind::Refused);
		// The refusal has to be actionable: it names the side file and the two
		// verbs that retire it.
		QVERIFY2(again.errorMessage.contains(sidecar), qPrintable(again.errorMessage));
		QVERIFY2(again.errorMessage.contains(QStringLiteral("record.journal_finish")),
			qPrintable(again.errorMessage));
		QVERIFY2(again.errorMessage.contains(QStringLiteral("record.recovery_discard")),
			qPrintable(again.errorMessage));

		QCOMPARE(sha256Of(sidecar), before);
		QCOMPARE(journalState(sidecar), QStringLiteral("in_progress"));

		// The offer still carries the frames the journal recorded...
		const ControlResult state = run(QStringLiteral("record.recovery_get_state"),
			{{QStringLiteral("dir"), m_world.path()}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		bool offered = false;
		for (const QJsonValue& entry : state.result.value(QStringLiteral("takes")).toArray())
		{
			if (entry.toObject().value(QStringLiteral("take")).toString() != take) { continue; }
			offered = true;
			QCOMPARE(entry.toObject().value(QStringLiteral("frames_journalled")).toInt(), kFrames);
			QCOMPARE(entry.toObject().value(QStringLiteral("frames_recoverable")).toInt(), kFrames);
		}
		QVERIFY2(offered, "the interrupted capture is no longer offered after the refused begin");

		// ...and a refusal is not an edit: no undo step was pushed.
		const QJsonArray transactionsAfter = run(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray();
		QCOMPARE(transactionsAfter.size(), transactionsBefore.size());
	}

	//! Failure path: a sidecar that exists but does not read is still the only
	//! record of what a crashed capture reached. Overwriting it is a clean-looking
	//! silent start, so it is refused and left exactly as it was.
	void anUnreadableSidecarIsRefusedAndLeftUntouched()
	{
		const QString take = QDir(m_world.path()).filePath(QStringLiteral("take-damaged.wav"));
		const QString sidecar = journalPathFor(take);
		QVERIFY(writeWav(take, kFrames));
		QFile damaged(sidecar);
		QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
		damaged.write("# zene-studio recording journal\nversion=2\ngarbage-with-no-take-line\n");
		damaged.close();
		const QByteArray before = sha256Of(sidecar);
		QVERIFY(!before.isEmpty());

		const ControlResult begun = run(QStringLiteral("record.journal_begin"), beginArgs(take));
		QVERIFY2(!begun.ok, "record.journal_begin overwrote a side file it could not read");
		QCOMPARE(begun.errorKind, ControlErrorKind::Refused);
		QVERIFY2(begun.errorMessage.contains(sidecar), qPrintable(begun.errorMessage));
		QCOMPARE(sha256Of(sidecar), before);
	}

	//! Reconfiguration path: a journal a clean stop retired leaves no sidecar, so
	//! the same take can be captured again.
	void aRetiredJournalCanBeStartedAgain()
	{
		const QString take = QDir(m_world.path()).filePath(QStringLiteral("take-retired.wav"));
		QVERIFY(writeWav(take, kFrames));
		QVERIFY2(run(QStringLiteral("record.journal_begin"), beginArgs(take)).ok,
			"the first capture could not be journalled");
		QVERIFY(run(QStringLiteral("record.journal_update"),
			{{QStringLiteral("take"), take}, {QStringLiteral("frames_on_disk"), kFrames}}).ok);
		const ControlResult finished = run(QStringLiteral("record.journal_finish"),
			{{QStringLiteral("take"), take}});
		QVERIFY2(finished.ok, qPrintable(finished.errorMessage));
		QVERIFY(!QFileInfo::exists(journalPathFor(take)));

		const ControlResult again = run(QStringLiteral("record.journal_begin"), beginArgs(take));
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QCOMPARE(again.result.value(QStringLiteral("frames_journalled")).toInt(), 0);
		QCOMPARE(journalState(journalPathFor(take)), QStringLiteral("in_progress"));
	}

	//! No collateral damage: the tombstone a recovery leaves (`restored`) is not
	//! an interrupted capture, and record.journal_begin is the recorded inverse
	//! that puts the offer back - so ONE control.undo of a restore still works.
	void undoOfARestoreStillPutsTheOfferBack()
	{
		const QString take = QDir(m_world.path()).filePath(QStringLiteral("take-restored.wav"));
		const QString sidecar = journalPathFor(take);
		QVERIFY(writeWav(take, kFrames));
		QVERIFY2(run(QStringLiteral("record.journal_begin"), beginArgs(take)).ok,
			"the capture could not be journalled");
		QVERIFY(run(QStringLiteral("record.journal_update"),
			{{QStringLiteral("take"), take}, {QStringLiteral("frames_on_disk"), kFrames}}).ok);
		const ControlResult restored = run(QStringLiteral("record.recovery_restore"),
			{{QStringLiteral("take"), take}});
		QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
		QCOMPARE(journalState(sidecar), QStringLiteral("restored"));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(undone.result.value(QStringLiteral("restored_by")).toString(),
			QStringLiteral("record.journal_begin"));
		QCOMPARE(journalState(sidecar), QStringLiteral("in_progress"));

		// The offer is back, which is what the undo is for.
		const ControlResult state = run(QStringLiteral("record.recovery_get_state"),
			{{QStringLiteral("dir"), m_world.path()}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		bool offered = false;
		for (const QJsonValue& entry : state.result.value(QStringLiteral("takes")).toArray())
		{
			if (entry.toObject().value(QStringLiteral("take")).toString() == take) { offered = true; }
		}
		QVERIFY2(offered, "undoing the restore left the take unoffered");
	}

	//! The recorded inverse is the paired command record.recovery_discard, so one
	//! control.undo removes the journal this call created.
	void oneUndoRemovesTheJournalThisCallCreated()
	{
		const QString take = QDir(m_world.path()).filePath(QStringLiteral("take-undone.wav"));
		QVERIFY(writeWav(take, kFrames));
		QVERIFY2(run(QStringLiteral("record.journal_begin"), beginArgs(take)).ok,
			"the capture could not be journalled");
		QVERIFY(QFileInfo::exists(journalPathFor(take)));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(undone.result.value(QStringLiteral("restored_by")).toString(),
			QStringLiteral("record.recovery_discard"));
		QVERIFY(!QFileInfo::exists(journalPathFor(take)));
	}

private:
	QTemporaryDir m_world;
};

QTEST_GUILESS_MAIN(JournalBeginGuardTest)
#include "JournalBeginGuardTest.moc"
