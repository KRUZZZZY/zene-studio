/*
 * ZynSeparateProcessTest.cpp - the per-plugin "run in a separate process"
 *                              toggle: where it is stored, and what it does
 *
 * WHAT THIS DRIVES
 *
 * ZynAddSubFx is the one plugin family in this tree that ships both an
 * in-process implementation (LocalZynAddSubFx, the default) and a client that
 * runs as a separate process (RemoteZynAddSubFx, started by RemotePlugin), so
 * it is where the opt-in "run in a separate process" toggle lives
 * (plugins/ZynAddSubFx/ZynAddSubFx.cpp). This test loads the real plugin
 * module, drives the toggle through the element the project file carries, and
 * pins three things:
 *
 *   - defaultIsInProcess(): with no separateprocess attribute - every project
 *     written before the toggle existed - the instrument runs in-process
 *     exactly as it did, and no client process is spawned.
 *
 *   - separateProcessChoiceSpawnsAClient(): the stored choice really does start
 *     RemoteZynAddSubFx as a *child process of this process*: the test finds it
 *     in the host's own process table, so "separate process" is a separate PID,
 *     not a second thread.
 *
 *   - savedChoiceRoundTripsThroughSaveAndReload(): the choice is written by the
 *     plugin's own saveSettings (the same BoolModel convention forwardmidicc
 *     uses) and read back by loadSettings of a *new* instance, which comes up
 *     in the stored mode. That is the save/reload round trip a project makes.
 *
 *   - hostSurvivesAndNoticesTheClientExit(): SIGKILL the client and the host
 *     process keeps running; the instrument reports that its client is gone.
 *     Killing the *host's own* in-process synth would take the host down; this
 *     case is what the toggle buys, and it is the strongest claim this test
 *     can make - the client is killed by the test, not by a real plugin crash.
 *
 * HOW THE SEPARATE-PID CLAIM IS READ
 *
 * Out of the host's own process table, read the way the platform provides it:
 * /proc where procfs is mounted (exact, and what this test was written on), `ps
 * -A -o pid=,ppid=,comm=` where it is not - Darwin has no /proc at all, and the
 * macOS jobs redded three of the cases below because an empty /proc answer was
 * read as "no client is running". Neither mechanism is *assumed* to work: a
 * control child the test starts itself has to be found in the table by name
 * first, and the cases SKIP - never pass - where it is not.
 * `ZYN_TEST_PROCESS_OBSERVER=proc|ps` forces one mechanism, which is how the
 * fallback is exercised where it is not the default.
 *
 * WHAT THIS DOES NOT PROVE (kept explicit so a green run is not misread)
 *
 *   - No plugin is actually crashed: the client is SIGKILLed from outside, so
 *     the isolation shown is "a dead client cannot take the host down", not
 *     "this VST2/Zyn crash was survived".
 *   - Audio is not compared here. The unchanged-default proof is a render
 *     comparison (docs/OOP-HOSTING.md), not an assertion.
 *   - Windows cannot load a plugin module from a test host (a module's import
 *     descriptor names zene.exe, the executable the modules link - see
 *     AudioPluginTest.cpp), so the whole suite skips there. The skip happens in
 *     initTestCase() before this test starts the engine, and cleanupTestCase()
 *     therefore tears down only what the test actually started (Engine::destroy()
 *     on an engine that was never initialised dereferences a null
 *     ProjectJournal - the msvc-x64 job segfaulted exactly there after the skip).
 *     Nothing here is observable on Windows, and it is not a subset either: every
 *     case below reaches the instrument through the module's own lmms_plugin_main
 *     (initTestCase() resolves it after QLibrary::load, and instantiate() calls
 *     it), so with no loadable module there is no subject to assert about. A
 *     stated skip is the whole of what this platform can honestly produce.
 *   - Where the host can read no process table at all, the separate-PID half of
 *     the toggle is UNEXERCISED and the three cases that read one report
 *     *Skipped*, naming that: their hosting-state assertions still ran, and the
 *     client spawn is proven independently by RemotePluginClientE2ETest (same
 *     client, real handshake) on the same job.
 *
 * Copyright (c) 2026 LMMS developers
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

#include <QCoreApplication>
#include <QLibrary>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QProcess>
#include <QString>
#include <QtTest>

#include <memory>

#ifndef Q_OS_WIN
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#endif

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Plugin.h"
#include "Song.h"

using namespace lmms;

namespace
{

//! Plugin entry point resolved from the loaded plugin module
using MainFn = Plugin* (*)(Model*, void*);

//! The project attribute the toggle is stored as: <zynaddsubfx separateprocess="1"/>
inline constexpr auto SeparateProcessAttribute = "separateprocess";

//! Element name the ZynAddSubFx instrument saves its settings under
inline constexpr auto PluginElementName = "zynaddsubfx";

//! Name of the client executable RemotePlugin::init() starts
inline constexpr auto RemoteClientName = "RemoteZynAddSubFx";

//! The instrument's own answer for "how am I hosted right now"
inline constexpr auto InProcessState = "in-process";
inline constexpr auto SeparateProcessState = "separate-process";

#ifdef ZYN_PLUGIN_PATH
inline auto pluginModulePath() -> QString { return QStringLiteral(ZYN_PLUGIN_PATH); }
#else
inline auto pluginModulePath() -> QString { return {}; }
#endif

#ifdef ZYN_REMOTE_CLIENT_PATH
inline auto remoteClientPath() -> QString { return QStringLiteral(ZYN_REMOTE_CLIENT_PATH); }
#else
inline auto remoteClientPath() -> QString { return {}; }
#endif

#ifndef Q_OS_WIN

//! Whether this run reads /proc (present, and exact) or `ps` (its absence - Darwin);
//! ZYN_TEST_PROCESS_OBSERVER=proc|ps forces one, which is how the fallback is exercised here
auto useProcFs() -> bool
{
	const auto forced = qEnvironmentVariable("ZYN_TEST_PROCESS_OBSERVER");
	if (forced == QLatin1String("proc")) { return true; }
	if (forced == QLatin1String("ps")) { return false; }
	return QFile::exists(QStringLiteral("/proc/self/status"));
}

//! Whether a table's name for a process is `wanted`. `ps` reports the kernel's own name, which
//! Linux truncates to 15 characters ("RemoteZynAddSubF"), so a truncation either way still counts.
auto processNameMatches(const QString& reported, const QString& wanted) -> bool
{
	const auto name = QFileInfo{reported}.fileName();
	if (name == wanted) { return true; }
	if (name.size() >= 8 && wanted.startsWith(name)) { return true; }
	return wanted.size() >= 8 && name.startsWith(wanted);
}

//! Children of this process whose executable is `name`, out of /proc: exact, and what this test
//! was written on - see psClientPids() for the hosts that have no procfs. Empty when nothing
//! matches, and equally empty when /proc cannot be read at all (control: the file header).
auto procFsClientPids(const QString& name) -> QList<long>
{
	QList<long> pids;

	auto* proc = opendir("/proc");
	if (proc == nullptr) { return pids; }

	const auto self = static_cast<long>(getpid());
	while (auto* entry = readdir(proc))
	{
		const QByteArray pid{entry->d_name};
		if (pid.isEmpty() || pid.at(0) < '0' || pid.at(0) > '9') { continue; }

		char target[4096];
		const QByteArray exe = "/proc/" + pid + "/exe";
		const auto length = ::readlink(exe.constData(), target, sizeof(target) - 1);
		if (length <= 0) { continue; }
		target[length] = '\0';
		if (QFileInfo{QString::fromLocal8Bit(target)}.fileName() != name) { continue; }

		QFile status{"/proc/" + pid + "/status"};
		if (!status.open(QIODevice::ReadOnly)) { continue; }
		const QByteArray text = status.readAll();
		const auto marker = text.indexOf("PPid:");
		if (marker < 0) { continue; }
		// "PPid:	<pid>\nName:..." - the value up to the end of the line
		const auto ppid = text.mid(marker + 5, 32).split('\n').first().trimmed().toLong();
		if (ppid == self) { pids.append(pid.toLong()); }
	}
	closedir(proc);
	return pids;
}

//! The same, from `ps -A -o pid=,ppid=,comm=`: `-A` is every process on macOS and Linux alike and
//! the trailing `=` suppresses each field's header, so the output is parsed positionally - pid,
//! parent, then the rest of the line as the executable. Empty when the call cannot be made or read.
auto psClientPids(const QString& name) -> QList<long>
{
	QList<long> pids;
	if (!QFile::exists(QStringLiteral("/bin/ps"))) { return pids; }

	QProcess ps;
	ps.start(QStringLiteral("/bin/ps"), {QStringLiteral("-A"), QStringLiteral("-o"),
		QStringLiteral("pid=,ppid=,comm=")});
	if (!ps.waitForStarted(5000) || !ps.waitForFinished(5000) || ps.exitCode() != 0) { return pids; }

	const auto self = QCoreApplication::applicationPid();
	for (const auto& line : QString::fromLocal8Bit(ps.readAllStandardOutput())
								.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
	{
		const auto fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		bool pidOk = false;
		bool parentOk = false;
		const long pid = fields.value(0).toLong(&pidOk);
		const long parent = fields.value(1).toLong(&parentOk);
		if (fields.size() < 3 || !pidOk || !parentOk || parent != self) { continue; }
		if (processNameMatches(fields.mid(2).join(QLatin1Char(' ')), name)) { pids.append(pid); }
	}
	return pids;
}

//! PIDs of the client executable running *under this process now*: children of this process whose
//! executable is `name`, read the way this host can read a process table
auto runningClientPids(const QString& name) -> QList<long>
{
	return useProcFs() ? procFsClientPids(name) : psClientPids(name);
}

//! How many clients are running under this process right now
auto runningClients(const QString& name) -> int { return runningClientPids(name).size(); }

//! Whether this host's process table can be trusted for the claims above - proven with a control
//! child of this process that this test starts itself.
//!
//! An empty table is not evidence that no client is running, only that this host cannot be looked
//! at, and the "no client" assertions below would then hold for the wrong reason. The live
//! `/bin/sleep` child has to show up *by the name this test looks for the client by*, which
//! exercises the mechanism, the parent filter and the name rule in one step. Cases QSKIP where it
//! does not hold - they never pass on a table they cannot read.
auto clientObservationIsUsable() -> bool
{
	QProcess control;
	control.start(QStringLiteral("/bin/sleep"), {QStringLiteral("5")});
	if (!control.waitForStarted(5000)) { return false; }

	const bool found = runningClientPids(QStringLiteral("sleep")).contains(control.processId());
	control.kill();
	control.waitForFinished(2000);
	return found;
}

//! Why the cases that read the process table QSKIP where it cannot be read, and never "pass"
auto noProcessTableReason() -> const char*
{
	return "this host can read no process table (/proc is absent and `ps -A -o pid=,ppid=,comm=` did "
		   "not report this process's own control child by name), so whether the separate-process "
		   "client is a SEPARATE PID - what /proc proves on Linux - is UNEXERCISED by this run";
}

#else // Q_OS_WIN: initTestCase() QSKIPs the suite, but the file still has to compile

auto clientObservationIsUsable() -> bool { return false; }
auto runningClientPids(const QString&) -> QList<long> { return {}; }
auto runningClients(const QString&) -> int { return 0; }
auto noProcessTableReason() -> const char* { return "no process table on Windows"; }

#endif

} // namespace

/*!
 * The plugin module exposes the instrument through lmms_plugin_main(); the
 * class itself has no header a test host can include (it is a plugin), so the
 * hosting state is read back through the meta-object by name - the instrument
 * declares it Q_INVOKABLE for exactly this reason.
 */
class ZynSeparateProcessTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef Q_OS_WIN
		QSKIP("cannot load a plugin module from a Windows test host: plugin modules link "
			"the zene executable, so their import descriptor names zene.exe and a test "
			"host cannot satisfy it (the product loads them inside zene.exe where that "
			"resolves by construction; CI msvc-x64: QLibrary::load -> "
			"ERROR_MOD_NOT_FOUND, 126): see AudioPluginTest.cpp");
	#endif

		Engine::init(true);
		m_engineInitialised = true;
		QVERIFY2(Engine::audioEngine() != nullptr, "engine failed to initialise");
		Engine::audioEngine()->audioDev()->stopProcessing();

		const auto path = pluginModulePath();
		if (path.isEmpty() || !QFile::exists(path))
		{
			QSKIP("the ZynAddSubFx plugin module is not part of this build "
				"(no FLTK): the separate-process toggle is UNEXERCISED by this run");
		}

		m_library.setFileName(path);
		m_library.setLoadHints(QLibrary::PreventUnloadHint);
		QVERIFY2(m_library.load(), qPrintable(m_library.errorString()));

		m_main = reinterpret_cast<MainFn>(m_library.resolve("lmms_plugin_main"));
		QVERIFY2(m_main != nullptr, "the plugin module exports no lmms_plugin_main");

		if (!QFile::exists(remoteClientPath()))
		{
			QSKIP("the separate-process client (RemoteZynAddSubFx) is not part of "
				"this build: the toggle's effect is UNEXERCISED by this run");
		}

		// RemotePlugin::init() resolves the client executable through the
		// "plugins:" search path, so the build's plugin directory has to be
		// given to it (the same thing the production binary does by being
		// installed next to its plugins).
		qputenv("LMMS_PLUGIN_DIR", QFileInfo{remoteClientPath()}.absolutePath().toUtf8());
		QVERIFY2(QFile::exists(QStringLiteral("%1/%2")
				.arg(QFileInfo{remoteClientPath()}.absolutePath(),
					QString::fromLatin1(RemoteClientName))),
			"the client executable is not in the plugin directory the toggle "
			"will resolve it from");
	}

	void cleanupTestCase()
	{
		// Tear down only what this test started. Engine::destroy()
		// (src/core/Engine.cpp:95) dereferences s_projectJournal without a
		// null check, and only Engine::init() ever sets it - so destroying an
		// engine that was never initialised is a null dereference. On Windows
		// initTestCase() QSKIPs before Engine::init(), and this call was the
		// crash the msvc-x64 job reported:
		//   SKIP : ZynSeparateProcessTest::initTestCase() cannot load a plugin
		//          module from a Windows test host ...
		//   A crash occurred in ...\ZynSeparateProcessTest.exe.
		//   While testing cleanupTestCase
		//   # 8: QHash<unsigned int,lmms::JournallingObject *>::begin()
		//   # 9: lmms::ProjectJournal::stopAllJournalling()
		//  #10: lmms::Engine::destroy()
		// (job 103724228360, run 34757467632). A skip must be a skip.
		if (m_engineInitialised) { Engine::destroy(); }
	}

	//! No attribute (every project saved before the toggle existed): unchanged
	void defaultIsInProcess()
	{
		InstrumentTrack track{Engine::getSong()};
		auto plugin = instantiate(track);

		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(InProcessState));
		if (!clientObservationIsUsable())
		{
			QSKIP(noProcessTableReason());
		}
		QCOMPARE(runningClients(QString::fromLatin1(RemoteClientName)), 0);
	}

	//! The stored choice starts a client, and the client is a child process of this one: the
	//! hosting-state half runs on every platform, the process table is read last, because that
	//! is the half a host can be unable to see.
	void separateProcessChoiceSpawnsAClient()
	{
		InstrumentTrack track{Engine::getSong()};
		auto plugin = instantiate(track);

		loadChoice(*plugin, QStringLiteral("1"));
		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(SeparateProcessState));

		// ... and the choice is not one-way: turning it off takes the client away again.
		loadChoice(*plugin, QStringLiteral("0"));
		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(InProcessState));

		if (!clientObservationIsUsable())
		{
			QSKIP(noProcessTableReason());
		}

		loadChoice(*plugin, QStringLiteral("1"));
		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(SeparateProcessState));
		// The client is started asynchronously by RemotePlugin::init(), so it
		// takes a moment to appear in the process table.
		QTRY_COMPARE_WITH_TIMEOUT(runningClients(QString::fromLatin1(RemoteClientName)), 1, 10000);
		qInfo("separate process: RemoteZynAddSubFx is a child of pid %lld",
			static_cast<long long>(QCoreApplication::applicationPid()));

		loadChoice(*plugin, QStringLiteral("0"));
		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(InProcessState));
		QTRY_COMPARE_WITH_TIMEOUT(runningClients(QString::fromLatin1(RemoteClientName)), 0, 10000);
	}

	//! save -> load of the plugin's own element, with a fresh instance
	void savedChoiceRoundTripsThroughSaveAndReload()
	{
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("instrument"));

		QString saved;
		{
			InstrumentTrack track{Engine::getSong()};
			auto plugin = instantiate(track);
			loadChoice(*plugin, QStringLiteral("1"));

			// The public serialisation entry point, i.e. exactly what a project
			// save calls (SerializingObject::saveState -> the plugin's own
			// saveSettings).
			const QDomElement element = plugin->saveState(doc, parent);
			saved = element.attribute(QString::fromLatin1(SeparateProcessAttribute));
		}
		QCOMPARE(saved, QStringLiteral("1"));

		// A fresh instrument, as a reloaded project makes one: it must come up
		// in the stored mode without anyone touching the setting again.
		InstrumentTrack track{Engine::getSong()};
		auto plugin = instantiate(track);
		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(InProcessState));

		loadChoice(*plugin, saved);
		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(SeparateProcessState));

		if (!clientObservationIsUsable())
		{
			QSKIP(noProcessTableReason());
		}
		QTRY_COMPARE_WITH_TIMEOUT(runningClients(QString::fromLatin1(RemoteClientName)), 1, 10000);
	}

	//! A dead client must not take the host with it
	void hostSurvivesAndNoticesTheClientExit()
	{
		InstrumentTrack track{Engine::getSong()};
		auto plugin = instantiate(track);
		loadChoice(*plugin, QStringLiteral("1"));
		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(SeparateProcessState));

		// No unkilled-client half to assert first here: the pid *is* the subject.
		if (!clientObservationIsUsable())
		{
			QSKIP(noProcessTableReason());
		}

		QTRY_VERIFY_WITH_TIMEOUT(clientPid() > 0, 10000);
		const auto client = clientPid();
		QVERIFY2(client > 0, "the client process could not be found in /proc");
#ifndef Q_OS_WIN
		// POSIX-only: MSVC has no ::kill, no pid_t and no SIGKILL, so this call site kept
		// the whole file from compiling on the MSVC job (run 34725347297:
		//   ZynSeparateProcessTest.cpp(307): error C2039: 'kill': is not a member of the
		//   global namespace; error C2061: syntax error: identifier 'pid_t').
		// initTestCase() QSKIPs this suite on Windows, so the guarded-out line never needs
		// a Windows equivalent -- the rest of the file already follows that pattern, and
		// this call site was the one that escaped it.
		QCOMPARE(::kill(static_cast<pid_t>(client), SIGKILL), 0);
#endif

		// The host is still running here, and it notices: the instrument stops
		// reporting a live separate process.
		QTRY_VERIFY_WITH_TIMEOUT(hostingState(plugin.get()) != QString::fromLatin1(SeparateProcessState),
			10000);
		QTRY_COMPARE_WITH_TIMEOUT(runningClients(QString::fromLatin1(RemoteClientName)), 0, 10000);
		qInfo("client pid %lld was killed and the host kept running (pid %lld)",
			static_cast<long long>(client),
			static_cast<long long>(QCoreApplication::applicationPid()));
	}

private:
	//! Instantiates the real ZynAddSubFx instrument for `track`
	auto instantiate(InstrumentTrack& track) const -> std::unique_ptr<Plugin>
	{
		return std::unique_ptr<Plugin>{m_main(&track, nullptr)};
	}

	//! Feeds one saved `separateprocess` attribute to the instrument, the way
	//! loading a project does (SerializingObject::restoreState -> the plugin's
	//! own loadSettings)
	void loadChoice(Plugin& plugin, const QString& value) const
	{
		QDomDocument doc;
		QDomElement element = doc.createElement(QString::fromLatin1(PluginElementName));
		element.setAttribute(QString::fromLatin1(SeparateProcessAttribute), value);
		plugin.restoreState(element);
	}

	//! The instrument's own report of its hosting state (Q_INVOKABLE, so the
	//! test host needs no header for the plugin class)
	auto hostingState(Plugin* plugin) const -> QString
	{
		QString state;
		const bool ok = QMetaObject::invokeMethod(plugin, "hostingState",
			Q_RETURN_ARG(QString, state));
		if (!ok) { state = QStringLiteral("<no hostingState()>"); }
		return state;
	}

	//! PID of the running client, or -1
	auto clientPid() const -> long
	{
		const auto pids = runningClientPids(QString::fromLatin1(RemoteClientName));
		return pids.isEmpty() ? -1 : pids.first();
	}

	QLibrary m_library;
	MainFn m_main = nullptr;
	//! Whether this test started the engine (initTestCase ran past the Windows
	//! skip), so cleanupTestCase destroys only an engine that exists.
	bool m_engineInitialised = false;
};

QTEST_GUILESS_MAIN(ZynSeparateProcessTest)

#include "ZynSeparateProcessTest.moc"
