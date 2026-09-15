/*
 * ControlCommandsCrashControl.cpp - the crash reporter's ARM and DISARM verbs
 *                                   (crash.enable / crash.disable, SPEC A11-A16).
 *
 * Feature row 54 of docs/FEATURE-LIST-0.3.0.md ("Crash reporter"), board task
 * #643. The read and the two writers the module really has live in
 * src/core/ControlCommandsCrash.cpp; this file is the one verb pair that file
 * stated could not exist - and it exists because the ENGINE now has the other
 * direction. include/CrashReporter.h grew uninstall(), handlersArmed() and
 * reportDirectory() (commit "feat(crash): the reporter can be disarmed and
 * re-armed"), which is precisely what the old note said was missing: "there is
 * no crash.enable / crash.disable for the same reason install() is not
 * registered: the reporter is installed by main() before this surface is
 * reachable, and it has no uninstall."
 *
 * WHY IT IS STILL TRUE THAT main() ARMS THE REPORTER. It does, and it should:
 * a crash before the control socket exists must still be reported. What changed
 * is that arming is now reversible, so the pair is answerable over the socket:
 *   crash.enable   arm the handlers (install()). With no arguments it re-arms to
 *                  the directory the reporter REMEMBERS; an explicit `directory`
 *                  arms a different one, which is exactly install()'s own
 *                  argument. Refused when the reporter is already armed, or when
 *                  the directory does not exist (install() never creates it, so
 *                  it cannot answer the first-run "create it?" prompt for the
 *                  user).
 *   crash.disable  disarm them (uninstall()). Refused when it is not armed. It
 *                  deletes NOTHING: the pending report, the `offered` sentinel
 *                  and the session marker are files, not arming, and
 *                  crash.list_reports keeps naming them (and the directory) while
 *                  disarmed, so a user who disables the reporter does not lose
 *                  the report they were about to attach.
 *
 * THE HONEST PREDICATE IS THE KERNEL'S, NOT A FLAG. Both verbs report `armed`
 * from crashreporter::handlersArmed(), which asks sigaction() what the
 * disposition of each signal IS, and `installed` from the module's own
 * crashreporter::isInstalled(). They can only disagree if something replaced our
 * handler, which is exactly the case a flag would hide - so the result carries
 * both and `agree` says whether they do.
 *
 * A16: `snapshot`, and the inverse is the PAIRED COMMAND. The reporter is not a
 * JournallingObject and none of this is project state - nothing on the project's
 * undo stack describes a signal disposition - so no ProjectJournal checkpoint can
 * hold it. Each verb therefore records the other with the directory THIS call
 * displaced, and control.undo dispatches it through the registry
 * (`applies: command`): undoing an enable disarms, undoing a disable re-arms to
 * the directory that was recorded before the write. That is the same class and
 * mechanism plugin.scan_cache_quarantine_add/remove carry
 * (src/core/ControlCommandsPluginScanEdit.cpp) for the same reason.
 *
 * WHY THE SIGNAL SET IS NOT WRITTEN DOWN HERE. `signals` in the result comes from
 * crashreporter::handledSignalList(), which is built from the ONE list
 * install()/uninstall()/handlersArmed() read. A second copy in this file is how
 * a surface comes to promise protection from a signal the handler does not
 * catch.
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

#include <string>

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

#include "ControlRegistry.h"

#include "ConfigManager.h"
#include "ControlEdit.h"
#include "ControlVocabulary.h"
#include "CrashReporter.h"

namespace lmms
{

namespace
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

const QString EnableName = QStringLiteral("crash.enable");
const QString DisableName = QStringLiteral("crash.disable");

QString wire(const std::string& text) { return QString::fromStdString(text); }

std::string clean(const std::string& path)
{
	if (path.empty()) { return path; }
	return QDir::cleanPath(QString::fromStdString(path)).toStdString();
}

/*! The directory a call actually arms, in this order:
 *    1. the caller's explicit `directory` (install()'s own argument);
 *    2. the directory the reporter REMEMBERS (reportDirectory()), which is what
 *       main() handed it and what crash.disable keeps across a disarm;
 *    3. ConfigManager::workingDir() - the same value main() installs with - so a
 *       caller that has never seen a report directory still gets the real one
 *       rather than a refusal it cannot act on.
 *
 *  Never creates the directory: install() opens it and fails when it is not
 *  there, and the refusal below says which directory that was.
 */
std::string armDirectory(const QJsonObject& args)
{
	const QString explicitDir = args.value(QStringLiteral("directory")).toString();
	if (!explicitDir.isEmpty()) { return clean(explicitDir.toStdString()); }
	const std::string remembered = crashreporter::reportDirectory();
	if (!remembered.empty()) { return clean(remembered); }
	return clean(ConfigManager::inst()->workingDir().toStdString());
}

//! The reporter's state as the two verbs (and their refusals) report it.
QJsonObject armState(const std::string& directory)
{
	const bool installed = crashreporter::isInstalled();
	const bool armed = crashreporter::handlersArmed();
	QJsonObject out;
	out.insert(QStringLiteral("enabled"), armed);
	out.insert(QStringLiteral("armed"), armed);
	out.insert(QStringLiteral("installed"), installed);
	// A disagreement is not a number to hide: it means something replaced the
	// handler we installed, and the honest predicate is the one that says so.
	out.insert(QStringLiteral("agree"), armed == installed);
	out.insert(QStringLiteral("report_directory"), wire(clean(directory)));
	out.insert(QStringLiteral("report_path"),
		wire(clean(crashreporter::pendingReportPath())));
	out.insert(QStringLiteral("signals"), wire(crashreporter::handledSignalList()));
	out.insert(QStringLiteral("max_report_bytes"), qint64(crashreporter::kMaxReportBytes));
	return out;
}

//! The before-state a paired-command inverse is recorded against.
QJsonObject armBeforeState(const std::string& directory)
{
	QJsonObject out;
	out.insert(QStringLiteral("enabled"), crashreporter::handlersArmed());
	out.insert(QStringLiteral("armed"), crashreporter::handlersArmed());
	out.insert(QStringLiteral("installed"), crashreporter::isInstalled());
	out.insert(QStringLiteral("report_directory"), wire(clean(directory)));
	return out;
}

//! The transaction whose inverse is the paired command, `applies: command` - the
//! shape plugin.scan_cache_quarantine_add/remove build for the same reason.
QJsonObject pairedCommandTransaction(const QJsonObject& before, const QString& inverseOp,
	const QJsonObject& inverseArgs, const QString& mechanism)
{
	QJsonObject transaction =
		transactionPayload(before, inverseOp, inverseArgs, true, mechanism);
	QJsonObject inverse = transaction.value(QStringLiteral("inverse")).toObject();
	inverse.insert(QStringLiteral("applies"), QStringLiteral("command"));
	transaction.insert(QStringLiteral("inverse"), inverse);
	return transaction;
}

/*! crash.enable - arm the reporter's handlers.
 *
 *  Refused when it is ALREADY armed, rather than reported as a success: this is
 *  what makes the pair's inverse exact. "Already armed" is a state the calling
 *  client can see (crash.list_reports), and a success that changed nothing would
 *  record an inverse (disable) that does change something - so the transaction
 *  would be a lie about a call that did nothing.
 */
ControlResult handleEnable(const QJsonObject& args)
{
	const std::string directory = armDirectory(args);
	if (crashreporter::handlersArmed() && crashreporter::isInstalled())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1: the crash reporter is ALREADY armed in this instance (report directory "
				"%2), so nothing was changed. Its enabled/disabled state is crash.list_reports' "
				"`installed`/`armed`; crash.disable is the call that changes it")
				.arg(EnableName, wire(crashreporter::reportDirectory())));
	}

	const QJsonObject before = armBeforeState(directory);
	if (!crashreporter::install(directory))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1: the reporter could not be armed to %2. install() opens that directory "
				"and never creates it - the reporter must not answer the first-run \"create the working "
				"directory?\" prompt for the user - so an arm to a directory that does not exist is "
				"refused rather than silently registered. Nothing was installed")
				.arg(EnableName, wire(directory)));
	}

	QJsonObject result = armState(directory);
	// Read back rather than assumed: install() returning true is not the claim,
	// "a crash in this process is now reported" is. handlersArmed() asks the
	// kernel; if it disagrees, the arming did not happen and the call fails.
	if (!crashreporter::handlersArmed())
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("%1: install() reported success but the signal dispositions do not name the "
				"crash handler, so this instance is NOT protected and nothing is claimed. %2")
				.arg(EnableName, QString::fromStdString(crashreporter::handledSignalList())));
	}

	result.insert(QStringLiteral("note"),
		QStringLiteral("the reporter is armed: %1 is now written from the signal handler on %2, and the "
			"process still dies by the signal it caught. Nothing about the project changed, nothing was "
			"created, and crash.disable is the inverse (control.undo dispatches it)")
			.arg(wire(crashreporter::pendingReportPath()),
				wire(crashreporter::handledSignalList())));
	result.insert(QStringLiteral("__transaction"),
		pairedCommandTransaction(before, DisableName, QJsonObject{},
			QStringLiteral("snapshot: no ProjectJournal checkpoint can hold a signal disposition - the "
				"reporter is not a JournallingObject and none of this is project state - so the recorded "
				"inverse is the paired COMMAND crash.disable (applies=command). It restores the default "
				"disposition for the same signal set; the report directory, the pending report and the "
				"session marker are files and are untouched by either direction")));
	return ControlResult::success(result);
}

/*! crash.disable - disarm the reporter's handlers.
 *
 *  Refused when it is not armed (again: the exact inverse is the point). It
 *  deletes nothing, and the inverse recorded is crash.enable with the directory
 *  captured BEFORE the write, so one control.undo re-arms to the place this call
 *  took the reporter away from even if the working directory changed since.
 */
ControlResult handleDisable()
{
	if (!crashreporter::isInstalled())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1: the crash reporter is not armed in this instance, so there is nothing to "
				"disarm and nothing was changed. main() arms it on POSIX before the control socket "
				"exists; on Windows the module is a documented no-op (include/CrashReporter.h) and never "
				"arms. crash.list_reports reports both states").arg(DisableName));
	}

	const std::string directory = crashreporter::reportDirectory();
	const QJsonObject before = armBeforeState(directory);

	if (!crashreporter::uninstall())
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("%1: the reporter was installed but uninstall() reported it was not, so the "
				"signal dispositions were left as they were. Nothing is claimed").arg(DisableName));
	}

	QJsonObject result = armState(directory);
	if (crashreporter::handlersArmed())
	{
		// Read back, in the other direction: a disarm that did not take is a
		// claim this command must not make.
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("%1: uninstall() reported success but the kernel's dispositions still name "
				"the crash handler, so this instance is still protected and the disarm is NOT claimed")
				.arg(DisableName));
	}

	// Read the state that survives, because "disarmed" must not read as "the
	// crash state was cleared".
	const QFileInfo report(wire(crashreporter::pendingReportPath()));
	result.insert(QStringLiteral("report"), QJsonObject{
		{QStringLiteral("path"), wire(clean(crashreporter::pendingReportPath()))},
		{QStringLiteral("exists"), report.exists()},
		{QStringLiteral("bytes"), report.isFile() ? qint64(report.size()) : qint64(0)}});
	result.insert(QStringLiteral("pending"), crashreporter::hasPendingReport());
	result.insert(QStringLiteral("note"),
		QStringLiteral("the reporter is disarmed: a crash in this instance is NOT reported and the "
			"process dies exactly as it would without the reporter. NOTHING was deleted - the report "
			"directory %1, its report and the session marker are still there, and crash.list_reports "
			"still names them; crash.enable (or one control.undo) arms the handlers again")
			.arg(wire(clean(directory))));
	result.insert(QStringLiteral("__transaction"),
		pairedCommandTransaction(before, EnableName,
			QJsonObject{{QStringLiteral("directory"), wire(clean(directory))}},
			QStringLiteral("snapshot: no ProjectJournal checkpoint can hold a signal disposition, so the "
				"recorded inverse is the paired COMMAND crash.enable with the report directory captured "
				"BEFORE the write (applies=command), because the reporter forgets nothing while disarmed "
				"- it keeps that directory, so the inverse arms the SAME place this call took it away "
				"from")));
	return ControlResult::success(result);
}

} // namespace

void registerCrashControlCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = EnableName;
		cmd.group = QStringLiteral("crash");
		cmd.verb = QStringLiteral("enable");
		cmd.description = QStringLiteral("Arm the crash reporter's signal handlers (crashreporter::install). "
			"With no arguments it re-arms to the report directory the reporter remembers; an explicit "
			"`directory` arms that one instead. The result reports `armed` from the kernel's own signal "
			"dispositions and `installed` from the module's flag, plus the signal set and the report path. "
			"Refuses when it is already armed and when the directory does not exist (install() never "
			"creates it). Reversible through control.undo, which dispatches crash.disable.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("directory"), stringProperty()},
		});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("armed"), booleanProperty()},
			{QStringLiteral("installed"), booleanProperty()},
			{QStringLiteral("agree"), booleanProperty()},
			{QStringLiteral("report_directory"), stringProperty()},
			{QStringLiteral("report_path"), stringProperty()},
			{QStringLiteral("signals"), stringProperty()},
			{QStringLiteral("max_report_bytes"), integerProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handleEnable(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = DisableName;
		cmd.group = QStringLiteral("crash");
		cmd.verb = QStringLiteral("disable");
		cmd.description = QStringLiteral("Disarm the crash reporter's signal handlers (crashreporter::"
			"uninstall): the default disposition is restored for exactly the signals install() claimed, "
			"so a crash is no longer reported and the process dies as it would without the reporter. "
			"Deletes NOTHING - the report, its directory and the session marker survive, and "
			"crash.list_reports still names them. Refuses when it is not armed. Reversible through "
			"control.undo, which dispatches crash.enable with the directory captured before the call.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("armed"), booleanProperty()},
			{QStringLiteral("installed"), booleanProperty()},
			{QStringLiteral("agree"), booleanProperty()},
			{QStringLiteral("report_directory"), stringProperty()},
			{QStringLiteral("report_path"), stringProperty()},
			{QStringLiteral("signals"), stringProperty()},
			{QStringLiteral("max_report_bytes"), integerProperty()},
			// {path, exists, bytes}
			{QStringLiteral("report"), objectProperty()},
			{QStringLiteral("pending"), booleanProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleDisable(); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
