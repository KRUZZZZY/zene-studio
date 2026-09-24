/*
 * ControlMasteringSupport.cpp - the shared helpers of the `mastering.*` command
 *                              group (SPEC A11-A16). See the header for what
 *                              each one is for.
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

#include "ControlMasteringSupport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QProcess>
#include <QTemporaryDir>

#include "BounceInPlace.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"

namespace lmms
{
namespace control
{

namespace
{

//! The instance's memory of its own last run (see the header). Process-local
//! on purpose: it is the surface's record of a command's answer, not project
//! state, and loading another project must not resurrect it.
QJsonObject& lastRunStore()
{
	static QJsonObject store;
	return store;
}

/*! The run's state and, when it failed, why (see the header). A pair rather
 *  than one string because a failure has to say what went wrong, and the state
 *  alone cannot: `failed` with no reason is a report nobody can act on.
 */
struct RunState
{
	QString state = QStringLiteral("idle");
	QString error;
};

RunState& runStateStore()
{
	static RunState store;
	return store;
}

//! True for a name this group's outputs carry: a .wav file, whatever the case
//! of the suffix.
bool isWavName(const QString& name)
{
	return name.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive);
}

} // namespace

QStringList masteringWavFiles(const QString& directory)
{
	const QDir dir(directory);
	if (!dir.exists())
	{
		return QStringList();
	}

	QStringList files;
	const QStringList entries = dir.entryList(QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
		QDir::Name);
	for (const QString& name : entries)
	{
		if (isWavName(name))
		{
			files.append(dir.absoluteFilePath(name));
		}
	}
	return files;
}

bool captureMasteringWavDirectory(const QString& directory, QMap<QString, QByteArray>* capture,
	ControlResult* error)
{
	capture->clear();
	const QStringList files = masteringWavFiles(directory);

	// The bound is checked against the SIZES before a byte is read, so an
	// oversized directory is refused without loading any of it.
	qint64 total = 0;
	for (const QString& path : files)
	{
		total += QFileInfo(path).size();
	}
	if (total > MasteringCaptureLimitBytes)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 already holds %2 bytes of wav files and this command's recorded "
				"inverse is bounded at %3: mastering.run is refused rather than performed "
				"without an inverse. Master into an empty directory, or move those files away "
				"first")
				.arg(QDir::toNativeSeparators(directory)).arg(total)
				.arg(MasteringCaptureLimitBytes));
		return false;
	}

	for (const QString& path : files)
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			*error = ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("cannot read %1 to record this command's inverse: %2")
					.arg(path, file.errorString()));
			return false;
		}
		capture->insert(path, file.readAll());
	}
	return true;
}

QJsonArray masteringFileFacts(const QStringList& paths)
{
	QJsonArray facts;
	for (const QString& path : paths)
	{
		const QFileInfo info(path);
		QJsonObject entry;
		entry.insert(QStringLiteral("path"), path);
		entry.insert(QStringLiteral("exists"), info.exists());
		entry.insert(QStringLiteral("bytes"), info.exists() ? info.size() : 0);
		// Measured from the file, not asserted: an empty hash means the file is
		// gone or unreadable, which is exactly what a reader must be able to see.
		entry.insert(QStringLiteral("sha256"),
			info.exists() ? BounceInPlace::sha256OfFile(path) : QString());
		facts.append(entry);
	}
	return facts;
}

QJsonObject masteringCaptureJson(const QString& directory, const QMap<QString, QByteArray>& before,
	const QStringList& created)
{
	QJsonArray held;
	for (auto it = before.constBegin(); it != before.constEnd(); ++it)
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("path"), it.key());
		entry.insert(QStringLiteral("bytes"), it.value().size());
		entry.insert(QStringLiteral("sha256"), controlSha256OfBytes(it.value()));
		held.append(entry);
	}

	QJsonArray createdNames;
	for (const QString& path : created) { createdNames.append(path); }

	QJsonObject out;
	out.insert(QStringLiteral("directory"), directory);
	out.insert(QStringLiteral("held_before"), held);
	out.insert(QStringLiteral("held_before_count"), held.size());
	out.insert(QStringLiteral("created"), createdNames);
	out.insert(QStringLiteral("created_count"), createdNames.size());
	out.insert(QStringLiteral("capture_limit_bytes"), MasteringCaptureLimitBytes);
	return out;
}

void recordMasteringUndo(const QStringList& created, const QMap<QString, QByteArray>& before)
{
	if (created.isEmpty() && before.isEmpty())
	{
		// Nothing on disk changed, so there is nothing to take back. Recording a
		// step here would cost the caller one Ctrl+Z for a no-op.
		return;
	}
	const QStringList createdCopy = created;
	const QMap<QString, QByteArray> beforeCopy = before;
	addUndoStep([createdCopy, beforeCopy]() {
		// 1. What the run created is REMOVED. This half is the whole inverse on
		//    a first run into an empty directory: a revision that never existed
		//    cannot be restored, only deleted.
		for (const QString& path : createdCopy) { QFile::remove(path); }
		// 2. What the run REPLACED is written back byte for byte. A file the run
		//    left untouched is rewritten with identical bytes, which is a no-op
		//    rather than a decision this step has to make.
		for (auto it = beforeCopy.constBegin(); it != beforeCopy.constEnd(); ++it)
		{
			QFile file(it.key());
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { continue; }
			file.write(it.value());
		}
	});
}

QJsonObject masteringLastRun()
{
	return lastRunStore();
}

void setMasteringLastRun(const QJsonObject& report)
{
	lastRunStore() = report;
}

QString masteringRunState()
{
	return runStateStore().state;
}

QString masteringRunError()
{
	return runStateStore().error;
}

void setMasteringRunState(const QString& state, const QString& error)
{
	runStateStore().state = state;
	runStateStore().error = error;
}

bool readMasteringReportFile(const QString& path, QJsonObject* report,
	ControlResult* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the mastering run wrote no report at %1").arg(path));
		return false;
	}
	const QByteArray bytes = file.readAll();
	QJsonParseError parse{};
	const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
	if (parse.error != QJsonParseError::NoError || !document.isObject())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the mastering run's report at %1 is not a JSON object: %2")
				.arg(path, parse.errorString()));
		return false;
	}
	*report = document.object();
	return true;
}

namespace
{

//! How much of a render child's stderr a failure quotes: enough for the
//! engine's own refusal text, bounded so a runaway child cannot fill memory.
constexpr int ChildErrorKeep = 600;

//! The child's own stderr text, bounded (see ChildErrorKeep). It is what a
//! failure quotes: the engine's refusal, not this handler's guess at it.
QString childErrorText(const QString& errorPath)
{
	QFile errorFile(errorPath);
	if (!errorFile.open(QIODevice::ReadOnly)) { return QString(); }
	return QString::fromUtf8(errorFile.readAll().right(ChildErrorKeep)).trimmed();
}

QStringList filesCreatedBy(const QStringList& before, const QStringList& after)
{
	QStringList created;
	for (const QString& path : after)
	{
		if (!before.contains(path)) { created.append(path); }
	}
	return created;
}

/*! The document the run publishes, and the shape `mastering.get_state`'s
 *  `last_run` has carried since the verb existed: the child's own report
 *  (candidate_count, render_count, the per-candidate measurements) plus the
 *  facts this side measured - the directory, the files as they are now, what the
 *  run created and how many revisions it replaced.
 */
QJsonObject runResultDocument(const QJsonObject& report, const QString& outDir,
	const QStringList& created, const QMap<QString, QByteArray>& before)
{
	QJsonObject result = report;
	result.insert(QStringLiteral("out_dir"), outDir);
	result.insert(QStringLiteral("files"), masteringFileFacts(masteringWavFiles(outDir)));
	result.insert(QStringLiteral("created"), QJsonArray::fromStringList(created));
	result.insert(QStringLiteral("created_count"), created.size());
	result.insert(QStringLiteral("replaced_count"), before.size());
	result.insert(QStringLiteral("renderer"),
		QStringLiteral("zene master (the shipped CLI action, in a child process on a serialised "
			"copy of the session; the running instance's audio engine is not touched)"));
	result.insert(QStringLiteral("render_sample_rate"), MasteringRenderSampleRate);
	result.insert(QStringLiteral("note"),
		QStringLiteral("%1 candidate files were written into out_dir by ONE project render, and "
			"every one was measured against its own named target. No candidate is preferred and "
			"none is ranked or called best - see mastering.list_candidates' note. The session is "
			"NOT modified: the child renders a serialised copy. UNDO takes the files back (the "
			"created ones are removed, replaced revisions are restored); `mastering.get_state` "
			"reads the same report back, and its `files` field is hashed live, so an edit made "
			"after the run is visible").arg(result.value(QStringLiteral("candidate_count")).toInt()));
	result.insert(QStringLiteral("__transaction"), masteringRunInverse(outDir, created, before));
	return result;
}

} // namespace


MasteringPendingRun::MasteringPendingRun() = default;

/*! Owns both members: `process` (deleting a QProcess that is still running kills
 *  its child, which is what a run cut short needs) and `scratch` (whose
 *  destructor removes the directory). Without this the run's serialised session
 *  and report would stay in the temp directory after the run - the leak the
 *  pre-fix shape had, in a new place.
 */
MasteringPendingRun::~MasteringPendingRun()
{
	delete process;
	delete scratch;
}

namespace
{
//! The one slot: at most one mastering.run is in flight, so the pointer doubles
//! as the busy fact a second concurrent run is refused on.
MasteringPendingRun*& pendingSlot()
{
	static MasteringPendingRun* pending = nullptr;
	return pending;
}
} // namespace

MasteringPendingRun* masteringPendingRun()
{
	return pendingSlot();
}

void setMasteringPendingRun(MasteringPendingRun* run)
{
	pendingSlot() = run;
}

//! The inverse descriptor that travels with the result. There is no command that
//! deletes a candidate set, so the recorded operation is named for what it does
//! and `applies` stays at its default "journal": the action checkpoint on the
//! engine's own undo stack is what control.undo unwinds.
//!
//! \a createdKnown is FALSE for the ACK, and that is the one place this verb's
//! asynchronous shape costs something: the registry records a transaction from
//! the REPLY (ControlRegistry::recordTransactionOf), so the ACK must describe
//! the inverse before the run has created anything. Which files the run creates
//! is MEASURED from the directory when it finishes rather than predicted here,
//! so the ACK declares exactly that (`created_not_yet_known`) instead of naming
//! files it has not seen; the completed run's document carries the real
//! `created`/`created_count`.
QJsonObject masteringRunInverse(const QString& outDir, const QStringList& created,
	const QMap<QString, QByteArray>& before, bool createdKnown)
{
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("out_dir"), outDir);
	inverseArgs.insert(QStringLiteral("created"), QJsonArray::fromStringList(created));

	QJsonObject inverse;
	inverse.insert(QStringLiteral("op"),
		QStringLiteral("remove the files this run created and write the captured revisions back"));
	inverse.insert(QStringLiteral("args"), inverseArgs);
	inverse.insert(QStringLiteral("applies"), QStringLiteral("journal"));

	QJsonObject capture = masteringCaptureJson(outDir, before, created);
	if (!createdKnown) { capture.insert(QStringLiteral("created_not_yet_known"), true); }

	QJsonObject payload;
	payload.insert(QStringLiteral("before"), capture);
	payload.insert(QStringLiteral("inverse"), inverse);
	payload.insert(QStringLiteral("reversible"), true);
	payload.insert(QStringLiteral("mechanism"),
		QStringLiteral("action checkpoint: the run's outputs are FILES in a directory outside the "
			"project, so no Song checkpoint carries them and no live object restores them. The "
			"recorded step removes every file the run created (before.created) and writes the "
			"revisions the directory already held (before.held_before) back byte for byte, both "
			"captured before the first write and bounded at before.capture_limit_bytes. A "
			"mastering run answers with an ACK before it finishes, so this record is written while "
			"the render is still in flight: `before.created` is empty and `before."
			"created_not_yet_known` says why - the set is measured from the directory when the run "
			"completes and reported as `created`/`created_count` in the run's own document "
			"(mastering.get_state's `last_run`) - and the recorded undo step is applied on the "
			"engine's undo stack at that same completion, so control.undo must not be asked to "
			"take this run back until mastering.get_state's `state` reads completed. ONE-WAY: "
			"there is no redo half (a faithful redo would have to hold the run's own outputs), so "
			"control.redo has nothing to replay - re-issue mastering.run instead"));
	return payload;
}


/*! Finish one run, on the application thread the child's QProcess lives on: read
 *  the report the child wrote, record the inverse of the files it created, and
 *  publish the result for `mastering.get_state`. A failure is published too - the
 *  ACK has already left the socket, so this is the only place a client can learn
 *  that the run produced no candidate set.
 *
 *  Takes ownership of \a run and deletes it, which kills any surviving child and
 *  removes the run's scratch directory.
 */
void completeMasteringRun(MasteringPendingRun* run, bool finished, int exitCode)
{
	// The one guard that makes the signal lambdas safe: a QProcess is deleted in
	// the same breath as its run, so a signal already queued for a run that has
	// been completed or aborted must be a no-op. Only the POINTER is compared
	// here, so a stale id is ignored without ever being dereferenced.
	if (masteringPendingRun() != run) { return; }
	setMasteringPendingRun(nullptr);

	QJsonObject report;
	ControlResult reportError;
	if (!finished || exitCode != 0
		|| !readMasteringReportFile(run->reportPath, &report, &reportError))
	{
		const QString childError = childErrorText(run->errorPath);
		const QString why = childError.isEmpty() ? reportError.errorMessage : childError;
		setMasteringRunState(QStringLiteral("failed"),
			QStringLiteral("the mastering run failed (exit %1) and the session is unchanged: %2")
				.arg(exitCode)
				.arg(why.isEmpty() ? QStringLiteral("the child process reported no reason") : why));
	}
	else
	{
		const QStringList created =
			filesCreatedBy(run->before.keys(), masteringWavFiles(run->outDir));
		recordMasteringUndo(created, run->before);
		setMasteringLastRun(runResultDocument(report, run->outDir, created, run->before));
		setMasteringRunState(QStringLiteral("completed"));
	}

	// `finished` is being emitted BY this process object, so it must not be
	// deleted inside its own emission: hand it to the event loop, and drop the
	// pointer so the run's destructor does not delete it a second time.
	run->process->deleteLater();
	run->process = nullptr;
	delete run;
}

/*! `mastering.run`: validate, capture the inverse, serialise the session, START
 *  the render child and answer. It never waits for the render - see this file's
 *  header for why that is the whole point of the verb's shape.
 */
} // namespace control
} // namespace lmms
