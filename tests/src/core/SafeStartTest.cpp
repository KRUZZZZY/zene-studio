/*
 * SafeStartTest.cpp - safe-start mode after a crash, end to end.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio (an LMMS-derived product).
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

// Acceptance proofs for safe-start mode (feature row 77 of
// docs/FEATURE-LIST-0.3.0.md, OWNER-31 item 31, board task #666):
//
//   * a CHILD process that really dies by a signal leaves the crash MARKER for the
//     next launch, which reads the crashed session's record (its pid) and starts
//     safe - SIGKILL too, which no handler can catch: hence the marker is
//     "written at session start, unlinked on the clean path";
//   * the PREDICATE is wired into the real load path (Plugin::instantiate()
//     hands back the engine's DummyPlugin for a THIRD-PARTY module file, and with
//     the session switch off the SAME call really loads it). That half is
//     SafeStartLoadPathTest, the file-length ratchet's reason;
//   * the NEGATIVE CONTROL holds (a clean exit: no marker, no acknowledgement, no
//     skipped instance, no safe start offered), the acknowledgement makes the
//     launch after the next one normal and is consumed by it (so a second crash
//     cannot be masked by the first decision), and the marker stays inside its
//     byte cap even for absurd input.
//
// The signal half is POSIX-only (on Windows these cases are skipped, the state
// machine half still runs); the load-path half is SafeStartLoadPathTest.
//
// THE CRASHING CHILD IS FORKED **AND EXECUTED** (runCrashChild): macOS
// CoreFoundation ABORTS a fork-only child on its first call into the framework
// ("Process ... was forked to ... without calling exec(). This is not supported by
// FileManager. Aborting." - run 35212797698, both mac jobs), and beginSession()
// makes one; child mode is the same engine calls in a process image of its own.

#include "SafeStart.h"

#include <QtTest>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#ifndef Q_OS_WIN
#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{

using namespace lmms::safestart;

//! A path no test machine owns, used for the classification assertions. The
//! file need not exist: the predicate resolves what a path really is when it
//! can and falls back to its absolute form.
const std::string ThirdPartyPath = "/tmp/some-user-drop-in/libtotallythirdparty.so";

#ifndef Q_OS_WIN

//! A wait() status in words, so the message names the mechanism, not a bare FALSE.
QString describeStatus(int status)
{
	if (status == 0) { return QStringLiteral("no status yet"); }
	if (WIFSTOPPED(status)) { return QStringLiteral("stopped by signal %1").arg(WSTOPSIG(status)); }
	if (WIFSIGNALED(status)) { return QStringLiteral("died by signal %1").arg(WTERMSIG(status)); }
	if (WIFEXITED(status)) { return QStringLiteral("exited with code %1").arg(WEXITSTATUS(status)); }
	return QStringLiteral("unclassified wait status %1").arg(status);
}

//! The child's death must be the KERNEL's disposition for the signal, never a
//! handler a runtime installed over it (qtestlib's turns a crash into SIGABRT).
void resetFatalSignalsToDefault()
{
	const int fatalSignals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGPIPE, 0 };
	for (int i = 0; fatalSignals[i] != 0; ++i)
	{
		struct sigaction dfl;
		std::memset(&dfl, 0, sizeof(dfl));
		dfl.sa_handler = SIG_DFL;
		sigemptyset(&dfl.sa_mask);
		::sigaction(fatalSignals[i], &dfl, nullptr);
	}
}

//! The child mode's argv[1]: the crash slots fork AND EXEC this binary with it, so
//! the child (argv[2] = the working directory, argv[3] = "segv" or "kill") runs the
//! engine's calls in a process image of its own - see the file header.
const char* const kCrashChildArg = "--safe-start-crash-child";

int runCrashChild(char** argv)
{
	if (!install(argv[2])) { ::_exit(83); }   // no directory, no session: fail loudly
	beginSession();            // begins THIS process's session and writes its marker, with its own pid
	resetFatalSignalsToDefault();
	// SIGKILL when the slot asks for it: uncatchable by construction.
	if (std::strcmp(argv[3], "kill") == 0) { ::kill(::getpid(), SIGKILL); ::_exit(81); }
	volatile int* p = reinterpret_cast<volatile int*>(static_cast<uintptr_t>(1));
	*p = 1;                            // SIGSEGV with si_addr == 0x1
	::_exit(80);                       // must not be reached
}

//! Fork, then exec THIS binary in child mode: whatever a fork-only child's runtime
//! would do to it, what the parent's wait() sees is death by the intended signal.
//! Returns the pid (or -1); a failed exec shows as "exited with code 82".
pid_t forkCrashChild(const std::string& workDir, const char* mode)
{
	const std::string exe = QCoreApplication::applicationFilePath().toStdString();
	const pid_t pid = ::fork();
	if (pid != 0) { return pid; }
	char* argv[] = { const_cast<char*>(exe.c_str()), const_cast<char*>(kCrashChildArg),
		const_cast<char*>(workDir.c_str()), const_cast<char*>(mode), nullptr };
	::execv(exe.c_str(), argv);
	::_exit(82);                       // exec failed: the parent names this code
}

//! Wait for a child, bounded (the CrashReporterTest helper: the same child-process
//! pattern). A timeout is a HANG, which this module must never cause, and every
//! caller asserts on it. A merely STOPPED child is continued and waited for again -
//! Darwin reports a stopped child to a WNOHANG waitpid, and death is still demanded.
bool waitBounded(pid_t pid, int timeoutMs, int& status)
{
	for (int waited = 0; waited < timeoutMs; waited += 20)
	{
		const pid_t r = ::waitpid(pid, &status, WNOHANG);
		if (r == pid)
		{
			if (WIFSTOPPED(status))
			{
				::kill(pid, SIGCONT);
				continue;
			}
			return true;
		}
		if (r < 0) { return false; }
		usleep(20000);
	}
	return false;
}

#endif // !Q_OS_WIN

} // namespace


class SafeStartTest : public QObject
{
	Q_OBJECT

private slots:

	// -----------------------------------------------------------------------
	// 1. THE NEGATIVE CONTROL. A session that exits cleanly leaves nothing: no
	//    marker, no acknowledgement, no skipped instance, no safe start.
	// -----------------------------------------------------------------------
	void cleanExitLeavesNoMarkerAndNoStaleState()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));
		QVERIFY(isInstalled());

		beginSession();
		QVERIFY2(markerExists(), "beginSession() must write the marker, or the whole "
			"detection rests on a file that is never created");
		QVERIFY2(!safeStartActive(), "a session with no marker behind it must NOT start safe");
		// The predicate is false in a normal session: this is the proof that
		// the feature is inert unless a crash marker was found.
		QVERIFY(!shouldSkipPluginInstance("totallythirdparty", ThirdPartyPath));

		endSession();
		QVERIFY2(!markerExists(), "a clean exit must clear the crash marker");
		QVERIFY2(!acknowledged(), "a clean exit must leave no acknowledgement behind");
		QVERIFY(previousRunExitedCleanly());
		QVERIFY(!safeStartActive());
		QCOMPARE(skippedCount(), 0);
		QVERIFY2(!shouldSkipPluginInstance("totallythirdparty", ThirdPartyPath),
			"a clean run must not skip anything");
		QVERIFY2(QDir(dir.path()).entryList(QDir::Files).isEmpty(),
			"a clean run must leave no file of ours in the working directory");
	}

	// -----------------------------------------------------------------------
	// 2. THE KEY ARTEFACT. A child that REALLY faults leaves the marker for the
	//    next launch, which reads the crashed session's own record and starts
	//    safe - and the predicate follows it.
	// -----------------------------------------------------------------------
	void realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe()
	{
#ifdef Q_OS_WIN
		QSKIP("a Windows test host cannot load plugin modules, and this case is "
			"about the POSIX signal path (the module is a documented no-op there)");
#else
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		const std::string workDir = dir.path().toStdString();
		const std::string project = "/tmp/safe start test.mmp";

		// The session BEFORE the crash: clean, and it does not skip anything.
		QVERIFY(install(workDir));
		beginSession();
		QVERIFY(!safeStartActive());
		QVERIFY(!shouldSkipPluginInstance("totallythirdparty", ThirdPartyPath));
		// The open project, recorded the way main() records it - the crash marker
		// that is left behind then names the file the user was working on.
		setProjectPath(project);
		QCOMPARE(QString::fromStdString(projectPath()), QString::fromStdString(project));
		// ... and it dies abnormally, exactly as the product would: the child (a
		// fork AND exec of this binary, child mode) installs over the same working
		// directory, begins ITS OWN session - rewriting the marker with its pid.
		::fflush(nullptr);
		const pid_t pid = forkCrashChild(workDir, "segv");
		QVERIFY2(pid >= 0, "fork failed");
		int status = 0;
		QVERIFY2(waitBounded(pid, 15000, status),
			qPrintable(QString("the crashing child neither died nor resumed inside 15s (%1): "
				"safe-start mode must not deadlock a dying process").arg(describeStatus(status))));
		QVERIFY2(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
			qPrintable(QString("the child must still die by SIGSEGV, as it would without any "
				"of this: %1").arg(describeStatus(status))));

		// THE NEXT LAUNCH: a fresh install + beginSession over the same directory.
		QVERIFY(install(workDir));
		beginSession();
		QVERIFY2(!previousRunExitedCleanly(),
			"a signal death must leave the marker, so the next launch can tell it from a quit");
		QVERIFY2(safeStartActive(), "the launch after a crash must start safe");
		const SessionRecord previous = lastSession();
		QVERIFY2(previous.present, "the marker's own record must be readable");
		// The count is "How many consecutive sessions have started safe; 1 for the
		// first", counted by the engine as previous.safeStartRuns + 1 over an
		// unacknowledged marker. THIS test builds: the parent's session (runs 0,
		// normal) -> the child's session (starts over the parent's marker, counted
		// as safe start 1, then dies by SIGSEGV) -> this session (starts over the
		// child's marker, safe start 2). The child HAS to run beginSession() to
		// write its own pid into the marker, which is what makes it a safe start -
		// so 2 is the engine's own answer.
		QCOMPARE(safeStartRunCount(), previous.safeStartRuns + 1);
		QCOMPARE(safeStartRunCount(), 2ULL);
		QVERIFY2(previous.processId == static_cast<unsigned long long>(pid),
			"the record must be the CRASHED session's, not a leftover of the test process");
		QCOMPARE(QString::fromStdString(previous.projectPath), QString::fromStdString(project));

		// THE PREDICATE, in the state the feature actually creates: a third-party
		// module file is skipped, and the record proves it was not a load failure.
		QVERIFY2(shouldSkipPluginInstance("totallythirdparty", ThirdPartyPath),
			"the launch after a crash must skip third-party plugin instances");
		noteSkippedInstance("totallythirdparty", ThirdPartyPath, "test");
		QCOMPARE(skippedCount(), 1);
		QCOMPARE(QString::fromStdString(skippedInstances().front().pluginName),
			QStringLiteral("totallythirdparty"));

		// A clean exit from the safe session clears everything for the next launch.
		endSession();
		QVERIFY(!markerExists());
		QVERIFY(!safeStartActive());
#endif
	}

	// -----------------------------------------------------------------------
	// 3. SIGKILL: the case that decides the marker's design. Nothing can run on the
	//    way out, so the file must have been written at session START - and it is
	//    therefore still there for the next launch.
	// -----------------------------------------------------------------------
	void sigkillAlsoLeavesTheMarker()
	{
#ifdef Q_OS_WIN
		QSKIP("POSIX signal path; the module is a documented no-op on Windows");
#else
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		const std::string workDir = dir.path().toStdString();
		QVERIFY(install(workDir));
		beginSession();

		::fflush(nullptr);
		const pid_t pid = forkCrashChild(workDir, "kill");
		QVERIFY2(pid >= 0, "fork failed");
		int status = 0;
		QVERIFY2(waitBounded(pid, 15000, status),
			qPrintable(QString("the killed child neither died nor resumed inside 15s (%1)")
				.arg(describeStatus(status))));
		QVERIFY2(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
			qPrintable(QString("the child must die by SIGKILL: %1 (marker on disk: %2)")
				.arg(describeStatus(status)).arg(markerExists() ? "yes" : "no")));

		QVERIFY(install(workDir));
		beginSession();
		QVERIFY2(safeStartActive(),
			"an uncatchable death must still leave the marker: it was written at session start");
		QCOMPARE(lastSession().processId, static_cast<unsigned long long>(pid));
		endSession();
#endif
	}

	// -----------------------------------------------------------------------
	// 4. The acknowledgement: "the NEXT launch is the normal one", consumed by it
	//    so a second crash cannot be masked by the first decision.
	// -----------------------------------------------------------------------
	void acknowledgeMakesTheNextLaunchNormal()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));

		// A marker left by a previous run, written the way a crashed session would
		// have (the cases above prove that path for real; this needs the state).
		{
			QFile marker(QString::fromStdString(markerPath()));
			QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
			marker.write("Zene Studio safe-start marker v1\npid=4242\ntime_unix=1700000000\n"
				"safe_start_runs=0\nproject=/tmp/a crashed session.mmp\n");
		}

		beginSession();
		QVERIFY(safeStartActive());
		QCOMPARE(lastSession().processId, 4242ULL);
		QCOMPARE(QString::fromStdString(lastSession().projectPath),
			QStringLiteral("/tmp/a crashed session.mmp"));

		// The offer is accepted.
		acknowledge();
		QVERIFY2(acknowledged(), "accepting the offer must write the acknowledgement");
		QVERIFY2(markerExists(), "accepting the offer must NOT delete the marker");
		QVERIFY2(safeStartActive(), "the CURRENT session is still the safe one");
		endSession();

		// The NEXT launch consumes it and is a normal one - and it does so even
		// though the marker is there again, because the definition of a safe
		// start is "a marker and no acknowledgement".
		{
			QFile marker(QString::fromStdString(markerPath()));
			QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
			marker.write("Zene Studio safe-start marker v1\npid=4243\ntime_unix=1700000001\n");
		}
		acknowledge();
		QVERIFY(acknowledged());
		beginSession();
		QVERIFY2(!acknowledged(),
			"the acknowledgement must be CONSUMED by the launch it was written for");
		QVERIFY2(!safeStartActive(), "the acknowledged launch must be a normal one");
		QVERIFY2(!shouldSkipPluginInstance("totallythirdparty", ThirdPartyPath),
			"a normal launch must not skip third-party plugin instances");
		// A crash of that normal launch is still a crash: the marker stays.
		QVERIFY(markerExists());

		// clear() is the "the cause is fixed" verb.
		clear();
		QVERIFY(!markerExists());
		QVERIFY(!acknowledged());
		QVERIFY(!safeStartActive());
	}

	// -----------------------------------------------------------------------
	// 5. THE PREDICATE'S CLASSIFICATION. "Third-party" is a definition, so it is
	//    asserted: a file under a directory this build ships from is NOT
	//    third-party (LMMS_PLUGIN_DIR is how the build tree names one), anything
	//    else is, and an empty path is not (a missing plugin is not this feature's
	//    business).
	// -----------------------------------------------------------------------
	void thePredicateOnlySkipsThirdPartyFiles()
	{
		QTemporaryDir ownDirectory;   // stands in for the packager's plugin directory
		QVERIFY2(ownDirectory.isValid(), "could not create a temporary own-plugin directory");
		const std::string own = ownDirectory.path().toStdString();

		// The environment override the product honours, as the build tree uses it.
		const QByteArray saved = qgetenv("LMMS_PLUGIN_DIR");
		qputenv("LMMS_PLUGIN_DIR", QByteArray::fromStdString(own));
		const std::vector<std::string> ownDirectories = ownPluginDirectories();
		const bool known = std::find(ownDirectories.begin(), ownDirectories.end(), own)
			!= ownDirectories.end();
		QVERIFY2(known, "LMMS_PLUGIN_DIR must be one of the directories treated as this build's own");

		QVERIFY2(!isThirdPartyPluginFile(own + "/libtripleoscillator.so"),
			"a module this build ships is not third-party");
		QVERIFY2(isThirdPartyPluginFile(ThirdPartyPath),
			"a module outside every directory this build ships is third-party");
		QVERIFY2(!isThirdPartyPluginFile(""),
			"an empty path is not third-party: a plugin that resolved to no file at all is the "
			"missing-plugin case Plugin::instantiate already handles");
		// The component-wise rule: a SIBLING whose name merely starts with the own
		// directory's name is not inside it ("/a/lib" does not own
		// "/a/lib-sibling/x.so"), which a bare startsWith() would get wrong.
		QVERIFY2(isThirdPartyPluginFile(own + "-sibling/libtripleoscillator.so"),
			"a sibling directory with a shared name prefix is not inside an own directory");
		// And a file in a SUBdirectory of an own directory IS under it: "A plugin
		// file under one of these is a file this build ships" - the rule the
		// predicate is built on, a directory rule not a file list. (The factory's
		// discovery scan is flat - PluginFactory.cpp reads each search path with
		// QDir::entryInfoList - so the tree rule is the absence's only meaning.)
		QVERIFY2(!isThirdPartyPluginFile(own + "/subdir/libtripleoscillator.so"),
			"a module under an own plugin directory is a file this build ships");

		if (saved.isEmpty()) { qunsetenv("LMMS_PLUGIN_DIR"); }
		else { qputenv("LMMS_PLUGIN_DIR", saved); }
	}

	// -----------------------------------------------------------------------
	// 6. The session-scoped switch - the ONLY cause of a skip in this state.
	// -----------------------------------------------------------------------
	void setSkipTurnsThePredicateOffForThisSession()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));
		{
			QFile marker(QString::fromStdString(markerPath()));
			QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
			marker.write("Zene Studio safe-start marker v1\npid=4244\n");
		}
		beginSession();
		QVERIFY(safeStartActive());
		QVERIFY2(skipEnabled(), "the skip is armed by the session, not by the caller");
		QVERIFY(shouldSkipPluginInstance("totallythirdparty", ThirdPartyPath));

		setSkipEnabled(false);
		QVERIFY(!skipEnabled());
		QVERIFY2(!shouldSkipPluginInstance("totallythirdparty", ThirdPartyPath),
			"with the session switch off, nothing is skipped");
		QVERIFY2(safeStartActive(), "the switch does not end the safe-start state, it lifts one half of it");
		QVERIFY(markerExists());

		// A new session re-arms it: the switch is session-scoped.
		beginSession();
		QVERIFY(skipEnabled());
		QVERIFY(shouldSkipPluginInstance("totallythirdparty", ThirdPartyPath));
		endSession();
	}

	// -----------------------------------------------------------------------
	// 7. Bounds: the marker cannot be grown past its cap by anything.
	// -----------------------------------------------------------------------
	void boundsTheMarkerCannotExceedItsCap()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));

		// A marker written by something else, with an absurd project path: the reader
		// must bound it too, and this session's own marker must stay inside the cap.
		{
			QFile marker(QString::fromStdString(markerPath()));
			QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
			marker.write("Zene Studio safe-start marker v1\npid=1\nproject=");
			marker.write(QByteArray(8192, 'A'));
			marker.write("\n");
		}

		beginSession();
		QVERIFY(safeStartActive());
		const QFileInfo written(QString::fromStdString(markerPath()));
		QVERIFY(written.exists());
		QVERIFY2(static_cast<unsigned long>(written.size()) <= kMaxMarkerBytes,
			"the marker exceeded kMaxMarkerBytes");
		QVERIFY2(lastSession().projectPath.size() <= kMaxProjectPathBytes,
			"the project hint exceeded kMaxProjectPathBytes");
		endSession();
	}

};

int main(int argc, char** argv)
{
#ifndef Q_OS_WIN
	// Child mode (kCrashChildArg): the crash slots re-exec this binary; never returns.
	if (argc == 4 && std::strcmp(argv[1], kCrashChildArg) == 0) { return runCrashChild(argv); }
#endif
	QCoreApplication app(argc, argv);
	app.setAttribute(Qt::AA_Use96Dpi, true);
	SafeStartTest tc;
	QTEST_SET_MAIN_SOURCE_PATH
	return QTest::qExec(&tc, argc, argv);
}
#include "SafeStartTest.moc"
