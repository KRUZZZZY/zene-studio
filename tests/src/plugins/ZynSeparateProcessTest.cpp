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
 *     in /proc, so "separate process" is a separate PID, not a second thread.
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
 * WHAT THIS DOES NOT PROVE (kept explicit so a green run is not misread)
 *
 *   - No plugin is actually crashed: the client is SIGKILLed from outside, so
 *     the isolation shown is "a dead client cannot take the host down", not
 *     "this VST2/Zyn crash was survived".
 *   - Audio is not compared here. The unchanged-default proof is a render
 *     comparison (docs/OOP-HOSTING.md), not an assertion.
 *   - Windows cannot load a plugin module from a test host (a module's import
 *     descriptor names lmms.exe - see AudioPluginTest.cpp), so the whole suite
 *     skips there.
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

#include <QLibrary>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>
#include <QList>
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

/*!
 * PIDs of the client executable running *under this process right now*.
 *
 * RemotePlugin starts the client with QProcess from this process, so a live
 * remote client is a child process of this process: that is the whole
 * difference between "in-process" and "separate process", and reading it out
 * of /proc is what makes the toggle's effect a fact rather than an inference
 * from the plugin's own state.
 *
 * @return the /proc PIDs whose executable basename is `name` and whose parent
 *         is this process; empty when no such client is running
 */
auto runningClientPids(const QString& name) -> QList<long>
{
	QList<long> pids;
#ifdef Q_OS_WIN
	Q_UNUSED(name);
#else
	auto* proc = opendir("/proc");
	if (proc == nullptr) { return pids; }

	const auto self = static_cast<long>(getpid());
	while (auto* entry = readdir(proc))
	{
		const QByteArray pid{entry->d_name};
		if (pid.isEmpty() || pid.at(0) < '0' || pid.at(0) > '9') { continue; }

		char target[4096];
		const auto length = ::readlink("/proc/" + pid + "/exe", target, sizeof(target) - 1);
		if (length <= 0) { continue; }
		target[length] = '\0';
		if (QFileInfo{QString::fromLocal8Bit(target)}.fileName() != name) { continue; }

		QFile status{"/proc/" + pid + "/status"};
		if (!status.open(QIODevice::ReadOnly)) { continue; }
		const QByteArray text = status.readAll();
		const auto marker = text.indexOf("PPid:");
		if (marker < 0) { continue; }
		// "PPid:\t<pid>\nName:..." - take the value up to the end of the line
		const auto ppid = text.mid(marker + 5, 32).split('\n').first().trimmed().toLong();
		if (ppid == self) { pids.append(pid.toLong()); }
	}
	closedir(proc);
#endif
	return pids;
}

//! How many clients are running under this process right now
auto runningClients(const QString& name) -> int
{
	return runningClientPids(name).size();
}

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
		QSKIP("cannot load a plugin module from a Windows test host (its import "
			"descriptor names lmms.exe): see AudioPluginTest.cpp");
#endif

		Engine::init(true);
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
		Engine::destroy();
	}

	//! No attribute (every project saved before the toggle existed): unchanged
	void defaultIsInProcess()
	{
		InstrumentTrack track{Engine::getSong()};
		auto plugin = instantiate(track);

		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(InProcessState));
		QCOMPARE(runningClients(QString::fromLatin1(RemoteClientName)), 0);
	}

	//! The stored choice starts a client that is a child process of this one
	void separateProcessChoiceSpawnsAClient()
	{
		InstrumentTrack track{Engine::getSong()};
		auto plugin = instantiate(track);

		loadChoice(*plugin, QStringLiteral("1"));

		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(SeparateProcessState));
		// The client is started asynchronously by RemotePlugin::init(), so it
		// takes a moment to appear in /proc.
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
		QTRY_COMPARE_WITH_TIMEOUT(runningClients(QString::fromLatin1(RemoteClientName)), 1, 10000);
	}

	//! A dead client must not take the host with it
	void hostSurvivesAndNoticesTheClientExit()
	{
		InstrumentTrack track{Engine::getSong()};
		auto plugin = instantiate(track);
		loadChoice(*plugin, QStringLiteral("1"));
		QCOMPARE(hostingState(plugin.get()), QString::fromLatin1(SeparateProcessState));

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
};

QTEST_GUILESS_MAIN(ZynSeparateProcessTest)

#include "ZynSeparateProcessTest.moc"
