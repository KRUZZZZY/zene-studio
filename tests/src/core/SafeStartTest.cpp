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
//   * a CHILD process that really dies by a signal leaves the crash MARKER
//     behind, and the next launch - a fresh beginSession() over the same working
//     directory - reads the record the crashed session wrote (its pid) and comes
//     up in safe-start mode;
//   * the same holds for SIGKILL, which no handler can catch: the design is
//     "written at session start, unlinked on the clean path", and this is the
//     case that proves why that is the only construction that works;
//   * the PREDICATE is wired into the real load path: with safe-start active,
//     Plugin::instantiate() really returns the engine's DummyPlugin for a
//     THIRD-PARTY module file, and with the session switch off the SAME call
//     really loads the module - so the skip is the mode's doing and not a plugin
//     that failed to load. That half needs a real plugin module and its own
//     binary (SafeStartLoadPathTest, the file-length ratchet's reason);
//   * the NEGATIVE CONTROL holds: a session that exits cleanly leaves no marker,
//     no acknowledgement, no skipped instance and no safe start offered;
//   * the acknowledgement makes the launch AFTER the next one normal, and it is
//     consumed by that launch, so a second crash cannot be masked by a decision
//     taken about the first;
//   * the marker stays within its byte cap even for absurd input.
//
// The signal half is POSIX-only; on Windows the fork-and-signal cases are
// skipped with the reason the suite's other plugin-module tests give, and the
// state machine half still runs. The load-path half is
// tests/src/core/SafeStartLoadPathTest.cpp, registered beside this one.

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

//! A wait() status in words: the macOS runners reported these slots as bare FALSE
//! (run 35126160372, no child output), so the message names the mechanism.
QString describeStatus(int status)
{
	if (status == 0) { return QStringLiteral("no status yet"); }
	if (WIFSTOPPED(status)) { return QStringLiteral("stopped by signal %1").arg(WSTOPSIG(status)); }
	if (WIFSIGNALED(status)) { return QStringLiteral("died by signal %1").arg(WTERMSIG(status)); }
	if (WIFEXITED(status)) { return QStringLiteral("exited with code %1").arg(WEXITSTATUS(status)); }
	return QStringLiteral("unclassified wait status %1").arg(status);
}

//! qtestlib's own crash handler (FatalSignalHandler, qtestcase.cpp) is inherited by a
//! forked child, and it converts a crash: on linux-x86_64 the SIGSEGV slot's child ran
//! it ("QFATAL : ... Received signal 11") and died by qFatal()'s SIGABRT. "As it would
//! without any of this" is the default-action death - give the child that.
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

//! Wait for a child, bounded. A timeout is a HANG, which this module must never
//! cause, and every caller asserts on it. (The CrashReporterTest helper, which
//! this suite copies because it is the same child-process pattern.)
//! A child that is merely STOPPED is continued and waited for again - Darwin reports a
//! stopped child to a WNOHANG waitpid, and the signal death is still demanded after.
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
	// 1. THE NEGATIVE CONTROL. A session that exits cleanly leaves nothing:
	//    no marker, no acknowledgement, no skipped instance, no safe start.
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
		// The open project, recorded the way main() records it - so the marker
		// the crash leaves behind names the file the user was working on.
		setProjectPath(project);
		QCOMPARE(QString::fromStdString(projectPath()), QString::fromStdString(project));
		// ... and it dies abnormally, exactly as the product would: the child
		// installs over the same working directory, begins its own session
		// (which rewrites the marker with ITS pid) and then really faults.
		::fflush(nullptr);
		const pid_t pid = ::fork();
		QVERIFY2(pid >= 0, "fork failed");
		if (pid == 0)
		{
			// CHILD - the session that crashes.
			install(workDir);
			beginSession();
			resetFatalSignalsToDefault();
			volatile int* p = reinterpret_cast<volatile int*>(static_cast<uintptr_t>(1));
			*p = 1;                       // SIGSEGV with si_addr == 0x1
			::_exit(80);                  // must not be reached
		}
		int status = 0;
		QVERIFY2(waitBounded(pid, 15000, status),
			qPrintable(QString("the crashing child neither died nor resumed inside 15s (%1): "
				"safe-start mode must not deadlock a dying process").arg(describeStatus(status))));
		QVERIFY2(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
			qPrintable(QString("the child must still die by SIGSEGV, as it would without any "
				"of this: %1").arg(describeStatus(status))));

		// THE NEXT LAUNCH: a fresh install + beginSession over the same working
		// directory, which is exactly what main() does at startup.
		QVERIFY(install(workDir));
		beginSession();
		QVERIFY2(!previousRunExitedCleanly(),
			"a signal death must leave the marker, so the next launch can tell it from a quit");
		QVERIFY2(safeStartActive(), "the launch after a crash must start safe");
		const SessionRecord previous = lastSession();
		QVERIFY2(previous.present, "the marker's own record must be readable");
		// The count is "How many consecutive sessions have started safe; 1 for
		// the first" (include/SafeStart.h), counted by the engine as
		// previous.safeStartRuns + 1 whenever a session starts over an
		// unacknowledged marker. The sequence THIS test builds is:
		//   1. the parent's session  - idle working directory, runs 0, normal;
		//   2. the child's session   - starts over the parent's marker, so the
		//                              engine counts it as safe start 1, and
		//                              then dies by SIGSEGV;
		//   3. this session          - starts over the child's marker, safe
		//                              start 2.
		// The child HAS to run beginSession() to write its own pid into the
		// marker (the assertion below), and that is exactly what makes it a
		// safe start - so 2 consecutive safe starts is the engine's own
		// answer, and 1 would describe a sequence where the crashing session
		// started with no marker on disk at all.
		QCOMPARE(safeStartRunCount(), previous.safeStartRuns + 1);
		QCOMPARE(safeStartRunCount(), 2ULL);
		QVERIFY2(previous.processId == static_cast<unsigned long long>(pid),
			"the record must be the CRASHED session's, not a leftover of the test process");
		QCOMPARE(QString::fromStdString(previous.projectPath), QString::fromStdString(project));

		// THE PREDICATE, in the state the feature actually creates: a
		// third-party module file is skipped, and the mode's own record proves
		// it was the mode and not a load failure.
		QVERIFY2(shouldSkipPluginInstance("totallythirdparty", ThirdPartyPath),
			"the launch after a crash must skip third-party plugin instances");
		noteSkippedInstance("totallythirdparty", ThirdPartyPath, "test");
		QCOMPARE(skippedCount(), 1);
		QCOMPARE(QString::fromStdString(skippedInstances().front().pluginName),
			QStringLiteral("totallythirdparty"));

		// A clean exit from the safe session clears everything, so the launch
		// after it is a normal one.
		endSession();
		QVERIFY(!markerExists());
		QVERIFY(!safeStartActive());
#endif
	}

	// -----------------------------------------------------------------------
	// 3. SIGKILL: the case that decides the marker's design. Nothing can run on
	//    the way out, so the file has to have been written at session START -
	//    and it is therefore still there for the next launch.
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
		const pid_t pid = ::fork();
		QVERIFY2(pid >= 0, "fork failed");
		if (pid == 0)
		{
			install(workDir);
			beginSession();
			resetFatalSignalsToDefault();
			::kill(::getpid(), SIGKILL);  // uncatchable by construction
			::_exit(81);                  // unreachable
		}
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
	// 4. The acknowledgement: "the NEXT launch is the normal one", consumed by
	//    that launch so a second crash cannot be masked by the first decision.
	// -----------------------------------------------------------------------
	void acknowledgeMakesTheNextLaunchNormal()
	{
		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));

		// A marker left by a previous run, written the way a crashed session
		// would have written it (the child-process cases above prove that path
		// for real; this one needs the state, not the signal).
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
	//    asserted rather than assumed: a file under a directory this build ships
	//    from is NOT third-party (LMMS_PLUGIN_DIR names such a directory, which
	//    is how the product's own tests point at the build tree), anything else
	//    is, and an empty path is not (a missing plugin is not this feature's
	//    business).
	// -----------------------------------------------------------------------
	void thePredicateOnlySkipsThirdPartyFiles()
	{
		QTemporaryDir ownDirectory;   // stands in for the packager's plugin directory
		QVERIFY2(ownDirectory.isValid(), "could not create a temporary own-plugin directory");
		const std::string own = ownDirectory.path().toStdString();

		// The environment override the product honours, used exactly as a
		// packager or the build tree uses it.
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
		// The component-wise rule. A SIBLING whose name merely starts with the
		// own directory's name is not inside it ("/a/lib" does not own
		// "/a/lib-sibling/x.so"), which a bare startsWith() on the directory
		// string would get wrong.
		QVERIFY2(isThirdPartyPluginFile(own + "-sibling/libtripleoscillator.so"),
			"a sibling directory with a shared name prefix is not inside an own directory");
		// And a file in a SUBdirectory of an own directory IS under it, so it
		// is not third-party: "A plugin file under one of these is a file this
		// build ships" (the ownPluginDirectories() comment above), which is the
		// rule the predicate is built on - a directory rule, not a file list.
		// (The factory's own discovery scan is flat - PluginFactory.cpp reads
		// each search path with QDir::entryInfoList - so such a file is not
		// reachable through the search paths either way; the tree rule is what
		// the absence of a per-file list has to mean.)
		QVERIFY2(!isThirdPartyPluginFile(own + "/subdir/libtripleoscillator.so"),
			"a module under an own plugin directory is a file this build ships");

		if (saved.isEmpty()) { qunsetenv("LMMS_PLUGIN_DIR"); }
		else { qputenv("LMMS_PLUGIN_DIR", saved); }
	}

	// -----------------------------------------------------------------------
	// 6. The session-scoped switch, and the fact that it is the ONLY cause of a
	//    skip in this state.
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

		// A marker written by something else, with an absurd project path: the
		// reader must bound it too, and the marker this session writes must stay
		// inside the cap.
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

QTEST_GUILESS_MAIN(SafeStartTest)
#include "SafeStartTest.moc"
