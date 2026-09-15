/*
 * ControlCommandsCrash.cpp - the crash.* command group (SPEC A11-A16).
 *
 * Feature row 54 of docs/FEATURE-LIST-0.3.0.md ("Crash reporter"). The engine
 * side is in the tree and proven: include/CrashReporter.h (the module's whole
 * public API) and src/core/CrashReporter.cpp (a POSIX signal handler that writes
 * ONE bounded text file and dies), installed from main() before any socket
 * exists (src/core/main.cpp: crashreporter::install() / beginSession()), with
 * the registered ctest CrashReporterTest. The audit's row 54 says "no command
 * group" - this file registers one, and the id the boarded list names for the
 * read is crash.list_reports (docs/specs/AGENT-TOOLING.md:194).
 *
 * WHAT THE ENGINE ACTUALLY SUPPORTS, read off the header rather than assumed:
 *   isInstalled, hasPendingReport, pendingReportPath, acknowledgePendingReport,
 *   discardPendingReport, sessionMarkerExists, setProjectPath, install,
 *   beginSession/endSession, writeReportIfIdle.
 * Of those, the READ is the state (crash.list_reports), the two WRITES are
 * "stop offering this report" (acknowledgePendingReport: writes the `offered`
 * sentinel and keeps the file) and "clear it" (discardPendingReport: unlinks
 * both), and there is no third: nothing in the module uploads, disables or
 * deletes anything else. install()/endSession()/writeReportIfIdle() are NOT
 * registered, and that is a decision rather than an omission - install() runs
 * from main() BEFORE the control socket exists (so a command that called it
 * could only re-point an already-installed reporter, and could never undo the
 * handlers it installed), endSession() would unlink another process's marker
 * semantics on the clean path, and writeReportIfIdle() would let a caller FABRICATE
 * a crash report, which is a false artifact rather than a control of one.
 *
 * THE VERBS THAT DO NOT EXIST ARE REFUSED, NOT OMITTED. `crash.upload_report` is
 * registered and REFUSES every call, by name, because the absence is a stated
 * property of this module ("no upload, no socket, no DNS, no telemetry and no
 * network code of any kind in this translation unit", include/CrashReporter.h) -
 * the shape automation.mode_set uses for a capability that is not there
 * (src/core/ControlCommandsAutomation.cpp: "registered, and refused by name"), so
 * a client that asks to send a report is told why not and where the file is
 * instead of getting a bare not_found. There is no crash.enable / crash.disable
 * for the same reason install() is not registered: the reporter is installed by
 * main() before this surface is reachable, and it has no uninstall.
 *
 * A16: the two writers are `irreversible`, and each says so with the fallback
 * the engine leaves. The module is a plain C API over files (it is not a
 * JournallingObject, it has no checkpoint and its main path must be
 * async-signal-safe), and NOTHING in it writes a report from a caller's bytes -
 * writeReportIfIdle assembles one from a CrashInfo the handler fills in - so a
 * discarded report cannot be put back by any command, and an acknowledged one
 * cannot be un-acknowledged. control.undo therefore FAILS, typed, naming the
 * fallback, which is the contract for this class.
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
#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"

#include "ControlVocabulary.h"
#include "CrashReporter.h"

namespace lmms
{

namespace
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

const QString ListReportsName = QStringLiteral("crash.list_reports");
const QString AcknowledgeName = QStringLiteral("crash.acknowledge_report");
const QString DiscardName = QStringLiteral("crash.discard_report");
const QString UploadName = QStringLiteral("crash.upload_report");

/*! Every path the reporter owns, DERIVED from the module's own public API.
 *
 * There is no reportDirectory() accessor, and CrashReporter.cpp must not grow
 * one: it is grandfathered at 564 lines in tests/file-length-baseline.tsv and
 * that ratchet has a zero-line tolerance. pendingReportPath() is public and
 * returns the report file's full path, and the marker names are public
 * constexpr strings in the same header, so the directory is
 * `pendingReportPath()` minus its own two known components - the module's
 * stated layout ("Report / marker file names, relative to the report directory")
 * rather than a second guess at it.
 *
 * valid is false while the reporter has no directory (install() has not run, or
 * the working directory was empty): the read still answers, the writers refuse.
 */
struct CrashPaths
{
	std::string root;
	std::string reportDirectory;
	std::string report;
	std::string offered;
	std::string sessionMarker;
	bool valid = false;
};

/*! The module composes its paths by CONCATENATION, and the working directory it
 *  is handed carries its own trailing separator (ConfigManager::workingDir()),
 *  so pendingReportPath() comes back as "<dir>//crash-reports/zene-crash-report.txt".
 *  The reported paths are canonicalised with QDir::cleanPath, so a client gets a
 *  path it can compare and pass to a tool without knowing that quirk - the same
 *  file either way, and `report_path` is the file crashreporter::pendingReportPath()
 *  names.
 */
std::string clean(const std::string& path)
{
	if (path.empty()) { return path; }
	return QDir::cleanPath(QString::fromStdString(path)).toStdString();
}

CrashPaths crashPaths()
{
	CrashPaths paths;
	const std::string pending = crashreporter::pendingReportPath();
	const std::string suffix = std::string("/") + crashreporter::kReportDirName + "/"
		+ crashreporter::kReportFileName;
	if (pending.size() <= suffix.size()
		|| pending.compare(pending.size() - suffix.size(), suffix.size(), suffix) != 0)
	{
		// The reporter is not installed or has no directory: every path stays
		// empty and `valid` stays false, which is what the read reports.
		return paths;
	}
	paths.root = clean(pending.substr(0, pending.size() - suffix.size()));
	paths.reportDirectory = clean(paths.root + "/" + crashreporter::kReportDirName);
	paths.report = clean(pending);
	paths.offered = clean(paths.reportDirectory + "/" + crashreporter::kOfferedMarkerName);
	paths.sessionMarker = clean(paths.root + "/" + crashreporter::kSessionMarkerName);
	paths.valid = true;
	return paths;
}

QString wire(const std::string& text) { return QString::fromStdString(text); }

//! One file of the reporter's, as it stands: its path, whether it is there,
//! how big it is and when it was last written.
QJsonObject fileJson(const std::string& path)
{
	QJsonObject out;
	out.insert(QStringLiteral("path"), wire(path));
	if (path.empty())
	{
		out.insert(QStringLiteral("exists"), false);
		out.insert(QStringLiteral("bytes"), 0);
		out.insert(QStringLiteral("modified_unix"), 0);
		return out;
	}
	const QFileInfo info(wire(path));
	out.insert(QStringLiteral("exists"), info.exists());
	// A directory is not a report; bytes are 0 for one, as for a missing file.
	out.insert(QStringLiteral("bytes"), info.isFile() ? qint64(info.size()) : qint64(0));
	out.insert(QStringLiteral("modified_unix"),
		info.exists() ? qint64(info.lastModified().toSecsSinceEpoch()) : qint64(0));
	return out;
}

ControlResult noReporter(const QString& command)
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1: the crash reporter is not installed in this instance, so it has no report "
			"directory and nothing was written. main() installs it on POSIX; on Windows the module is a "
			"documented no-op (include/CrashReporter.h)").arg(command));
}

/*! crash.list_reports - the reporter's state, from the module's own public
 *  predicates. Read-only, and it answers in every configuration: an instance
 *  with no reporter reports no directory and no report rather than refusing,
 *  because "there is no crash state" is the useful answer to this question.
 */
ControlResult handleListReports()
{
	const CrashPaths paths = crashPaths();
	// A "report" is a file that is THERE: a directory with no report in it lists
	// nothing, and report_path below is where a crash would write one. So
	// report_count is a count of reports, not of a path the module could use.
	QJsonArray reports;
	if (paths.valid && QFileInfo::exists(wire(paths.report))) { reports.append(fileJson(paths.report)); }

	QJsonObject result;
	result.insert(QStringLiteral("installed"), crashreporter::isInstalled());
	result.insert(QStringLiteral("report_directory"), wire(paths.reportDirectory));
	result.insert(QStringLiteral("report_path"), wire(paths.report));
	result.insert(QStringLiteral("offered_marker_path"), wire(paths.offered));
	result.insert(QStringLiteral("session_marker_path"), wire(paths.sessionMarker));
	result.insert(QStringLiteral("reports"), reports);
	result.insert(QStringLiteral("report_count"), reports.size());
	// The module's OWN predicate, not a re-derivation of it: pending is "a
	// report exists and has not been offered yet" (CrashReporter.cpp:
	// hasPendingReport), which is exactly what the offer dialog is gated on.
	result.insert(QStringLiteral("pending"), crashreporter::hasPendingReport());
	result.insert(QStringLiteral("offered"),
		paths.valid && QFileInfo::exists(wire(paths.offered)));
	// A marker left behind means the previous run did not exit cleanly; the
	// module removes it on the clean path (endSession()).
	const bool markerLeft = crashreporter::sessionMarkerExists();
	result.insert(QStringLiteral("session_marker_present"), markerLeft);
	result.insert(QStringLiteral("previous_run_exited_cleanly"), !markerLeft);

	QJsonObject bounds;
	bounds.insert(QStringLiteral("max_report_bytes"), qint64(crashreporter::kMaxReportBytes));
	bounds.insert(QStringLiteral("max_project_path_bytes"), qint64(crashreporter::kMaxProjectPathBytes));
	result.insert(QStringLiteral("bounds"), bounds);

	QJsonObject upload;
	upload.insert(QStringLiteral("supported"), false);
	upload.insert(QStringLiteral("note"),
		QStringLiteral("this build has no upload: include/CrashReporter.h states that the module has no "
			"socket, no DNS, no telemetry and no network code of any kind, and the report is a local text "
			"file a human attaches to a bug report by hand. crash.upload_report is registered and refuses, "
			"so the absence is answerable rather than a mystery"));
	result.insert(QStringLiteral("upload"), upload);

	result.insert(QStringLiteral("note"),
		QStringLiteral("the reporter writes ONE bounded report file (crash-reports/zene-crash-report.txt "
			"under the working directory), created at crash time and never by a clean run; the next start "
			"offers it and then writes the `offered` sentinel so it is not offered twice. `pending` is the "
			"module's own predicate for that offer (hasPendingReport). Nothing here is project state and no "
			"command installs or disables the reporter: main() installs it before this surface is reachable, "
			"and the module has no uninstall"));
	return ControlResult::success(result);
}

//! The before-state of either writer: what the reporter's files held.
QJsonObject crashBeforeState(const CrashPaths& paths)
{
	QJsonObject out;
	out.insert(QStringLiteral("report_path"), wire(paths.report));
	out.insert(QStringLiteral("report"), fileJson(paths.report));
	out.insert(QStringLiteral("offered"), fileJson(paths.offered));
	return out;
}

/*! crash.acknowledge_report - stop offering the pending report, keeping the
 *  file. The engine operation is crashreporter::acknowledgePendingReport().
 */
ControlResult handleAcknowledgeReport()
{
	if (!crashreporter::isInstalled()) { return noReporter(AcknowledgeName); }
	const CrashPaths paths = crashPaths();
	if (!paths.valid) { return noReporter(AcknowledgeName); }
	const QFileInfo report(wire(paths.report));
	if (!report.exists())
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: there is no report to acknowledge at %2").arg(AcknowledgeName, wire(paths.report)));
	}

	const QJsonObject before = crashBeforeState(paths);
	crashreporter::acknowledgePendingReport();

	// The engine operation returns void, so the effect is READ BACK rather than
	// assumed: a sentinel that could not be written (a read-only working
	// directory) must not be reported as an acknowledge that happened.
	const bool offered = QFileInfo::exists(wire(paths.offered));
	if (!offered)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("%1: the report at %2 exists but its `offered` sentinel %3 could not be written, "
				"so the report will be offered again on the next start. Nothing else was changed")
				.arg(AcknowledgeName, wire(paths.report), wire(paths.offered)));
	}

	QJsonObject result;
	result.insert(QStringLiteral("acknowledged"), true);
	result.insert(QStringLiteral("report_path"), wire(paths.report));
	result.insert(QStringLiteral("offered_marker_path"), wire(paths.offered));
	result.insert(QStringLiteral("report"), fileJson(paths.report));
	result.insert(QStringLiteral("pending"), crashreporter::hasPendingReport());
	result.insert(QStringLiteral("offered"), true);
	result.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"), before},
			{QStringLiteral("reversible"), false},
			{QStringLiteral("mechanism"),
				QStringLiteral("none: acknowledging writes the reporter's `offered` sentinel, and no function in "
					"this engine removes it - acknowledgePendingReport() only writes it and "
					"discardPendingReport() deletes the sentinel WITH the report, which is not an inverse. "
					"FALLBACK: delete the file %1 and the report at %2 is pending again; the report itself is "
					"untouched by this command, which is why the file is still there to attach")
					.arg(wire(paths.offered), wire(paths.report))}});
	return ControlResult::success(result);
}

/*! crash.discard_report - clear the pending report: the engine operation is
 *  crashreporter::discardPendingReport(), which unlinks the report AND the
 *  offered sentinel (CrashReporter.cpp).
 */
ControlResult handleDiscardReport()
{
	if (!crashreporter::isInstalled()) { return noReporter(DiscardName); }
	const CrashPaths paths = crashPaths();
	if (!paths.valid) { return noReporter(DiscardName); }

	const QJsonObject before = crashBeforeState(paths);
	const bool hadReport = QFileInfo::exists(wire(paths.report));
	const bool hadOffered = QFileInfo::exists(wire(paths.offered));
	if (!hadReport && !hadOffered)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: there is no crash report to discard at %2 (and no offered sentinel beside "
				"it), so nothing was written").arg(DiscardName, wire(paths.report)));
	}

	crashreporter::discardPendingReport();

	QJsonArray removed;
	if (hadReport) { removed.append(wire(paths.report)); }
	if (hadOffered) { removed.append(wire(paths.offered)); }

	QJsonObject result;
	result.insert(QStringLiteral("discarded"), true);
	result.insert(QStringLiteral("removed"), removed);
	result.insert(QStringLiteral("removed_count"), removed.size());
	result.insert(QStringLiteral("report_path"), wire(paths.report));
	result.insert(QStringLiteral("report"), fileJson(paths.report));
	result.insert(QStringLiteral("pending"), crashreporter::hasPendingReport());
	result.insert(QStringLiteral("offered"), QFileInfo::exists(wire(paths.offered)));
	result.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"), before},
			{QStringLiteral("reversible"), false},
			{QStringLiteral("mechanism"),
				QStringLiteral("none: the report file's bytes are gone, and nothing in this engine writes a "
					"report from a caller's bytes - writeReportIfIdle() assembles one from a CrashInfo the "
					"signal handler fills in - so no command puts it back. FALLBACK: the report's CONTENT is "
					"not recoverable, but the crash it described is: re-run the action that crashed and the "
					"reporter writes a new report at %1, and the before-state recorded here names the file "
					"that was removed so a copy kept elsewhere can be recognised")
					.arg(wire(paths.report))}});
	return ControlResult::success(result);
}

/*! crash.upload_report - registered, and refused by name.
 *
 * The engine's own header states the absence as a design property, so this is
 * not a missing feature to be apologised for but a boundary to be REPORTED: a
 * client that asks to send a report is told that nothing was sent, why, and
 * which file to attach by hand instead. The shape automation.mode_set uses for
 * a capability the engine does not have.
 */
ControlResult handleUploadReport()
{
	const CrashPaths paths = crashPaths();
	const QString where = paths.valid
		? QStringLiteral(" The report to attach by hand is %1").arg(wire(paths.report))
		: QStringLiteral(" No report directory exists in this instance, so there is not even a file to attach");
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("%1: this build has NO upload. It is a stated property of the module, not a missing "
			"switch: include/CrashReporter.h says there is no socket, no DNS, no telemetry and no network "
			"code of any kind in it, and the reporter's contract is one local text file next to the working "
			"directory. Nothing was sent and nothing was written.%2").arg(UploadName, where));
}

} // namespace

void registerCrashReporterCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = ListReportsName;
		cmd.group = QStringLiteral("crash");
		cmd.verb = QStringLiteral("list_reports");
		cmd.description = QStringLiteral("The crash reporter's state: whether it is installed, where its "
			"report directory is, every report it holds (path, existence, size, last-written time), whether "
			"a report is still pending an offer, whether the offered sentinel is present, whether a session "
			"marker says the previous run exited uncleanly, its two hard bounds, and the upload policy. "
			"Read-only, and it answers in every configuration.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("installed"), booleanProperty()},
			{QStringLiteral("report_directory"), stringProperty()},
			{QStringLiteral("report_path"), stringProperty()},
			{QStringLiteral("offered_marker_path"), stringProperty()},
			{QStringLiteral("session_marker_path"), stringProperty()},
			{QStringLiteral("reports"), arrayProperty()},
			{QStringLiteral("report_count"), integerProperty()},
			{QStringLiteral("pending"), booleanProperty()},
			{QStringLiteral("offered"), booleanProperty()},
			{QStringLiteral("session_marker_present"), booleanProperty()},
			{QStringLiteral("previous_run_exited_cleanly"), booleanProperty()},
			// {max_report_bytes, max_project_path_bytes}
			{QStringLiteral("bounds"), objectProperty()},
			// {supported, note}
			{QStringLiteral("upload"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleListReports(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = AcknowledgeName;
		cmd.group = QStringLiteral("crash");
		cmd.verb = QStringLiteral("acknowledge_report");
		cmd.description = QStringLiteral("Acknowledge the pending crash report: write the reporter's "
			"`offered` sentinel so the report is not offered again on the next start, and KEEP the file so "
			"it can still be attached. Refuses when there is no report. Not reversible - no function in this "
			"engine removes the sentinel; control.undo names the sentinel to delete by hand.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("acknowledged"), booleanProperty()},
			{QStringLiteral("report_path"), stringProperty()},
			{QStringLiteral("offered_marker_path"), stringProperty()},
			// {path, exists, bytes, modified_unix}
			{QStringLiteral("report"), objectProperty()},
			{QStringLiteral("pending"), booleanProperty()},
			{QStringLiteral("offered"), booleanProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleAcknowledgeReport(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = DiscardName;
		cmd.group = QStringLiteral("crash");
		cmd.verb = QStringLiteral("discard_report");
		cmd.description = QStringLiteral("Clear the crash reporter's report: delete the report file and the "
			"offered sentinel beside it (crashreporter::discardPendingReport). Refuses when there is neither, "
			"so a call that would change nothing writes nothing. Not reversible - the report's bytes are "
			"gone and nothing writes a report from a caller's bytes; control.undo names the fallback.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("discarded"), booleanProperty()},
			{QStringLiteral("removed"), arrayProperty()},
			{QStringLiteral("removed_count"), integerProperty()},
			{QStringLiteral("report_path"), stringProperty()},
			{QStringLiteral("report"), objectProperty()},
			{QStringLiteral("pending"), booleanProperty()},
			{QStringLiteral("offered"), booleanProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleDiscardReport(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = UploadName;
		cmd.group = QStringLiteral("crash");
		cmd.verb = QStringLiteral("upload_report");
		cmd.description = QStringLiteral("Send a crash report somewhere. Refused, always: this build has no "
			"upload and no network code of any kind in the reporter (include/CrashReporter.h states it as a "
			"design property), so no send is faked. The refusal names the file to attach by hand.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("sent"), booleanProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleUploadReport(); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
