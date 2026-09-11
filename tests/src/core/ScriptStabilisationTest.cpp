/*
 * ScriptStabilisationTest.cpp - the Lua API stabilisation contract (task #613)
 *
 * Copyright (c) 2026 LMMS contributors
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

/*!
 * Three promises, each proved rather than asserted in prose:
 *
 *  1. the version a script reads (lmms.version()/lmms.apiVersion()) and the
 *     version the loader enforces (`--! lmms-api <major>.<minor>`) both come
 *     from the build, so neither can drift from the other;
 *  2. script output reaches the DAW's logging path - the Qt message handler a
 *     build installs - not merely ScriptEngine's in-memory capture buffer;
 *  3. a script that raises a Lua error (or calls a name that does not exist)
 *     leaves the DAW alive and the engine usable, and says why on the console.
 *
 * docs/LUA-COMPATIBILITY-POLICY.md is the prose half of this contract.
 */

#include <QThread>
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>

#include "Engine.h"
#include "ScriptApiVersion.h"
#include "ScriptConsole.h"
#include "ScriptEngine.h"
#include "ScriptPackage.h"
#include "Song.h"

//! The version the *test* binary was compiled with. Deliberately taken from the
//! build instead of a literal: the engine's copy comes from the lmmsobjs
//! compile line, and comparing the two is what catches a hardcoded version.
static const char* const kExpectedApiVersion = ZENE_LUA_API_MAJOR_MINOR_STRING;
static const char* const kExpectedApiFullVersion = ZENE_LUA_API_VERSION_STRING;

namespace
{

/*! \brief Captures Qt log lines for its lifetime.
 *
 * The console streams from the script worker thread, so the handler can run
 * while this thread waits inside runString(); the mutex is for that, not for
 * style. Removal is in the destructor so an early QVERIFY return cannot leave a
 * dangling handler installed for the next slot.
 */
class LogCapture
{
public:
	LogCapture()
	{
		s_active = this;
		m_previous = qInstallMessageHandler(&LogCapture::handler);
	}

	~LogCapture()
	{
		qInstallMessageHandler(m_previous);
		s_active = nullptr;
	}

	LogCapture(const LogCapture&) = delete;
	LogCapture& operator=(const LogCapture&) = delete;

	QStringList messages() const
	{
		QMutexLocker locker(&m_mutex);
		return m_messages;
	}

private:
	static void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
	{
		Q_UNUSED(type);
		Q_UNUSED(context);
		if (s_active == nullptr)
		{
			return;
		}
		QMutexLocker locker(&s_active->m_mutex);
		s_active->m_messages.append(message);
	}

	QStringList m_messages;
	mutable QMutex m_mutex;
	QtMessageHandler m_previous{nullptr};

	static LogCapture* s_active;
};

LogCapture* LogCapture::s_active = nullptr;

//! Write \a source to \a path, failing the test when the file cannot be opened.
void writeScript(const QString& path, const QString& source)
{
	QFile file(path);
	QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
	file.write(source.toUtf8());
	file.close();
}

} // namespace

class ScriptStabilisationTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		using namespace lmms;
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		using namespace lmms;
		Engine::destroy();
	}

	void init()
	{
		using namespace lmms;
		Engine::getSong()->clearProject();
		ScriptEngine::instance()->takeLogMessages();
		ScriptEngine::instance()->setInstructionBudget(5000000);
		ScriptEngine::instance()->setAutoApply(true);
		ScriptConsole::setEnabled(true);
	}

	// -----------------------------------------------------------------------
	// 1. Versioning: the entry point, the header gate, and the build
	// -----------------------------------------------------------------------

	void versionEntryPointReportsTheBuiltVersion()
	{
		using namespace lmms;

		// C++ side: the values come from include/ScriptApiVersion.h, which is
		// fed by the top-level CMakeLists.txt variables in both compilation
		// units (see tests/CMakeLists.txt).
		QCOMPARE(ScriptApi::version(), QString::fromLatin1(kExpectedApiVersion));
		QCOMPARE(ScriptApi::fullVersion(), QString::fromLatin1(kExpectedApiFullVersion));
		QCOMPARE(ScriptApi::major(), int(ZENE_LUA_API_VERSION_MAJOR));
		QCOMPARE(ScriptApi::minor(), int(ZENE_LUA_API_VERSION_MINOR));
		QVERIFY2(!ScriptApi::stability().isEmpty(), "the API must state where it stands");

		// Lua side: the entry point a script actually calls.
		const QString source = QStringLiteral(R"(
assert(lmms.version() == '%1', 'lmms.version() is ' .. tostring(lmms.version()) .. ', expected %1')
assert(lmms.apiVersion() == '%2', 'lmms.apiVersion() is ' .. tostring(lmms.apiVersion()) .. ', expected %2')
assert(lmms.apiVersionMajor() == %3, 'major mismatch')
assert(lmms.apiVersionMinor() == %4, 'minor mismatch')
assert(type(lmms.apiStability()) == 'string' and #lmms.apiStability() > 0, 'no stability string')
assert(string.match(lmms.version(), '^%3%.%4$') ~= nil, 'version is not major.minor')
)").arg(QString::fromLatin1(kExpectedApiVersion),
		QString::fromLatin1(kExpectedApiFullVersion))
		.arg(ZENE_LUA_API_VERSION_MAJOR)
		.arg(ZENE_LUA_API_VERSION_MINOR);

		QString error;
		QCOMPARE(int(ScriptEngine::instance()->runString(source, &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
	}

	void versionHeaderGateFollowsTheBuild()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		// A script declaring exactly the version this build implements runs.
		const QString currentPath = dir.filePath(QStringLiteral("current.lua"));
		writeScript(currentPath, QStringLiteral("--! lmms-api %1\nprint('current ran')\n")
				.arg(QString::fromLatin1(kExpectedApiVersion)));
		QString error;
		QCOMPARE(int(engine->runFile(currentPath, &error)), int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));

		// One minor ahead is refused, and the refusal names the version the
		// engine actually implements - a stale number here sends the author
		// chasing the wrong build.
		const QString aheadPath = dir.filePath(QStringLiteral("ahead.lua"));
		writeScript(aheadPath, QStringLiteral("--! lmms-api %1.%2\nprint('ahead ran')\n")
				.arg(ZENE_LUA_API_VERSION_MAJOR)
				.arg(ZENE_LUA_API_VERSION_MINOR + 1));
		error.clear();
		QCOMPARE(int(engine->runFile(aheadPath, &error)), int(ScriptEngine::RunResult::VersionError));
		QVERIFY2(error.contains(ScriptApi::version()), qPrintable(error));
		QVERIFY2(!engine->takeLogMessages().contains(QStringLiteral("ahead ran")),
			"a script that failed the version gate must not run");

		// A major bump is a break, so it is refused even though the minor is 0.
		const QString majorPath = dir.filePath(QStringLiteral("major.lua"));
		writeScript(majorPath, QStringLiteral("--! lmms-api %1.0\nprint('major ran')\n")
				.arg(ZENE_LUA_API_VERSION_MAJOR + 1));
		error.clear();
		QCOMPARE(int(engine->runFile(majorPath, &error)), int(ScriptEngine::RunResult::VersionError));
		QVERIFY2(!engine->takeLogMessages().contains(QStringLiteral("major ran")),
			"a script that failed the version gate must not run");
	}

	// -----------------------------------------------------------------------
	// 2. Console: script output reaches the DAW's logging path
	// -----------------------------------------------------------------------

	void consoleOutputReachesTheLoggingPath()
	{
		using namespace lmms;
		LogCapture capture;

		QString error;
		const auto result = ScriptEngine::instance()->runString(
			QStringLiteral("print('console-proof-line')\n"
					"lmms.log():info('logged-line')\n"
					"lmms.log():warn('warned-line')"), &error);

		const QStringList streamed = capture.messages();

		QCOMPARE(int(result), int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));

		// The proof is the message handler installed above - the same Qt logging
		// path every other qInfo()/qWarning() in the product goes through - not
		// the mere fact that LuaLog::info() was called.
		QVERIFY2(streamed.filter(QStringLiteral("lua: console-proof-line")).size() == 1,
			qPrintable(streamed.join('|')));
		QVERIFY2(streamed.filter(QStringLiteral("lua: [info] logged-line")).size() == 1,
			qPrintable(streamed.join('|')));
		QVERIFY2(streamed.filter(QStringLiteral("lua: [warn] warned-line")).size() == 1,
			qPrintable(streamed.join('|')));

		// The capture buffer still holds the raw lines: the console is an
		// addition to the existing surface, not a replacement for it.
		const QStringList captured = ScriptEngine::instance()->takeLogMessages();
		QVERIFY2(captured.contains(QStringLiteral("console-proof-line")), qPrintable(captured.join('|')));
		QVERIFY2(captured.contains(QStringLiteral("[info] logged-line")), qPrintable(captured.join('|')));
	}

	void consoleStreamingIsSwitchable()
	{
		using namespace lmms;
		LogCapture capture;

		ScriptConsole::setEnabled(false);
		QVERIFY(!ScriptConsole::enabled());
		QString error;
		QCOMPARE(int(ScriptEngine::instance()->runString(QStringLiteral("print('muted-line')"), &error)),
			int(ScriptEngine::RunResult::Ok));

		const QStringList streamed = capture.messages();

		QCOMPARE(static_cast<int>(streamed.filter(QStringLiteral("muted-line")).size()), 0);
		// Turning the console off loses nothing but the streamed copy.
		const QStringList captured = ScriptEngine::instance()->takeLogMessages();
		QVERIFY2(captured.contains(QStringLiteral("muted-line")), qPrintable(captured.join('|')));
	}

	// -----------------------------------------------------------------------
	// 3. Negative control: a failing script never takes the DAW with it
	// -----------------------------------------------------------------------

	void luaErrorIsSurvivedAndTheEngineStaysUsable()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();

		LogCapture capture;

		// (a) an explicit error() in the script
		QString error;
		QCOMPARE(int(engine->runString(QStringLiteral("print('before the error')\n"
				"error('deliberate failure')"), &error)),
			int(ScriptEngine::RunResult::ScriptError));
		QVERIFY2(error.contains(QStringLiteral("deliberate failure")), qPrintable(error));
		QVERIFY(engine->lastRunFailed());

		// (b) an undefined entry point on the API namespace
		error.clear();
		QCOMPARE(int(engine->runString(QStringLiteral("lmms.thisEntryPointDoesNotExist()"), &error)),
			int(ScriptEngine::RunResult::ScriptError));
		QVERIFY2(error.contains(QStringLiteral("thisEntryPointDoesNotExist")), qPrintable(error));

		// (c) an undefined method on a bound class
		error.clear();
		QCOMPARE(int(engine->runString(QStringLiteral("lmms.patternStore():noSuchMethod()"), &error)),
			int(ScriptEngine::RunResult::ScriptError));
		QVERIFY2(error.contains(QStringLiteral("noSuchMethod")), qPrintable(error));

		const QStringList streamed = capture.messages();

		// The failures said so on the console: an author debugging by prints
		// must not see output stop with no reason.
		QVERIFY2(!streamed.filter(QStringLiteral("deliberate failure")).isEmpty(),
			qPrintable(streamed.join('|')));
		QVERIFY2(!streamed.filter(QStringLiteral("noSuchMethod")).isEmpty(),
			qPrintable(streamed.join('|')));

		// The engine is still usable ...
		error.clear();
		QCOMPARE(int(engine->runString(QStringLiteral("print('engine still usable')"), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(!engine->lastRunFailed());
		QVERIFY(engine->takeLogMessages().contains(QStringLiteral("engine still usable")));

		// ... and the worker thread and the apply-side contract survived all
		// three failures: scripts still run off the calling thread, and engine
		// state is still mutated by the apply side.
		QVERIFY2(engine->workerThread() != QThread::currentThread(),
			"scripts must still run on the dedicated worker thread");
		QVERIFY2(engine->workerThread()->isRunning(), "the script worker thread died");
		engine->setAutoApply(false);
		error.clear();
		// addNoteAt() is queued and never read back, so with autoApply off it
		// stays pending - proof the apply side, not the worker, still owns the
		// queue after the three failures above. (addPattern() alone would prove
		// nothing: it reads the new pattern back, which flushes the queue.)
		QCOMPARE(int(engine->runString(QStringLiteral(
				"local clip = lmms.song():patternStore():addPattern()\n"
				"clip:addNoteAt(60, 0, 10, 100)"), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY2(engine->pendingCommandCount() > 0, "the command queue stopped accepting work");
		QVERIFY2(engine->processCommands() > 0, "the queued command could not be applied");
		QCOMPARE(engine->lastApplyThread(), QThread::currentThread());
		engine->setAutoApply(true);
	}

	void failedScriptOutputStopsAtTheFailure()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();

		// The script prints, then fails. Both facts must be observable: the
		// output before the error is kept, and the run is reported as failed.
		QString error;
		QCOMPARE(int(engine->runString(QStringLiteral("print('reached before failure')\n"
				"local x = nil\nprint(x.field)"), &error)),
			int(ScriptEngine::RunResult::ScriptError));
		QVERIFY2(!error.isEmpty(), "a failed run must explain itself");

		const QStringList captured = engine->takeLogMessages();
		QVERIFY2(captured.contains(QStringLiteral("reached before failure")), qPrintable(captured.join('|')));
		QVERIFY2(captured.contains(error), qPrintable(captured.join('|')));
	}

	// -----------------------------------------------------------------------
	// 4. Package format: naming, discovery, and the entry script
	// -----------------------------------------------------------------------

	void packageNamesAreValidated()
	{
		using namespace lmms;
		QVERIFY(ScriptPackages::isValidName(QStringLiteral("arp")));
		QVERIFY(ScriptPackages::isValidName(QStringLiteral("note-gen_2")));
		QVERIFY(ScriptPackages::isValidName(QString(64, QLatin1Char('a'))));
		QVERIFY(!ScriptPackages::isValidName(QString()));
		QVERIFY2(!ScriptPackages::isValidName(QStringLiteral("Arp")), "names are lowercase");
		QVERIFY2(!ScriptPackages::isValidName(QStringLiteral("2arp")), "a name may not start with a digit");
		QVERIFY2(!ScriptPackages::isValidName(QStringLiteral("..")), "a name is a path component");
		QVERIFY2(!ScriptPackages::isValidName(QStringLiteral("arp/../etc")), "no separators in a name");
		QVERIFY2(!ScriptPackages::isValidName(QStringLiteral("note gen")), "no spaces in a name");
		QVERIFY2(!ScriptPackages::isValidName(QString(65, QLatin1Char('a'))), "65 characters is too long");
	}

	void packagesAreDiscoveredAndTheirEntryScriptsRun()
	{
		using namespace lmms;
		QTemporaryDir root;
		QVERIFY(root.isValid());
		QDir rootDir(root.path());

		// A valid package with a manifest.
		QVERIFY(rootDir.mkpath(QStringLiteral("arp")));
		writeScript(rootDir.absoluteFilePath(QStringLiteral("arp/package.lua")),
			QStringLiteral("--! lmms-api %1\n--! lmms-package arp 0.2\nprint('arp package ran')\n")
				.arg(QString::fromLatin1(kExpectedApiVersion)));

		// A valid package without a manifest: the version stays empty rather
		// than being invented.
		QVERIFY(rootDir.mkpath(QStringLiteral("drums")));
		writeScript(rootDir.absoluteFilePath(QStringLiteral("drums/package.lua")),
			QStringLiteral("--! lmms-api %1\nprint('drums package ran')\n")
				.arg(QString::fromLatin1(kExpectedApiVersion)));

		// Skipped, not reported as broken: an invalid directory name, a
		// directory with no entry script, and a loose script file (the product
		// ships loose example scripts beside packages).
		QVERIFY(rootDir.mkpath(QStringLiteral("Bad Name")));
		QVERIFY(rootDir.mkpath(QStringLiteral("empty")));
		writeScript(rootDir.absoluteFilePath(QStringLiteral("hello.lua")),
			QStringLiteral("--! lmms-api %1\nprint('loose script')\n")
				.arg(QString::fromLatin1(kExpectedApiVersion)));

		const QVector<ScriptPackage> packages = ScriptPackages::scan(root.path());
		QCOMPARE(static_cast<int>(packages.size()), 2);
		QCOMPARE(packages[0].name, QStringLiteral("arp"));
		QCOMPARE(packages[0].version, QStringLiteral("0.2"));
		QVERIFY(packages[0].isValid());
		QCOMPARE(packages[0].entryPath, rootDir.absoluteFilePath(QStringLiteral("arp/package.lua")));
		QCOMPARE(packages[1].name, QStringLiteral("drums"));
		QVERIFY2(packages[1].version.isEmpty(), "an absent manifest must not invent a version");

		// A discovered entry is an ordinary v0 script: the engine runs it.
		QString error;
		QCOMPARE(int(ScriptEngine::instance()->runFile(packages[0].entryPath, &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY(ScriptEngine::instance()->takeLogMessages().contains(QStringLiteral("arp package ran")));

		// The package format adds no way around the version gate.
		QVERIFY(rootDir.mkpath(QStringLiteral("future")));
		writeScript(rootDir.absoluteFilePath(QStringLiteral("future/package.lua")),
			QStringLiteral("--! lmms-api %1.0\nprint('future ran')\n")
				.arg(ZENE_LUA_API_VERSION_MAJOR + 1));
		QCOMPARE(static_cast<int>(ScriptPackages::scan(root.path()).size()), 3);
		error.clear();
		QCOMPARE(int(ScriptEngine::instance()->runFile(
				rootDir.absoluteFilePath(QStringLiteral("future/package.lua")), &error)),
			int(ScriptEngine::RunResult::VersionError));
		QVERIFY2(!ScriptEngine::instance()->takeLogMessages().contains(QStringLiteral("future ran")),
			"a package must not bypass the version gate");
	}

	void manifestParsingIsStrict()
	{
		using namespace lmms;
		const ScriptPackage parsed = ScriptPackages::parseManifest(QStringLiteral(
			"--! lmms-api 0.1\n--! lmms-package my-fx 1.2.3\nprint('x')\n"));
		QCOMPARE(parsed.name, QStringLiteral("my-fx"));
		QCOMPARE(parsed.version, QStringLiteral("1.2.3"));

		// A name-only manifest is legal: the version is optional.
		QCOMPARE(ScriptPackages::parseManifest(QStringLiteral("--! lmms-package solo")).name,
			QStringLiteral("solo"));
		// Malformed or invalid declarations yield no package at all.
		QVERIFY(ScriptPackages::parseManifest(QStringLiteral("print('x')")).name.isEmpty());
		QVERIFY(ScriptPackages::parseManifest(QStringLiteral("--! lmms-package Bad Name")).name.isEmpty());
		QVERIFY(ScriptPackages::parseManifest(
			QStringLiteral("--! lmms-package ../escape 1.0")).name.isEmpty());
	}
};

QTEST_GUILESS_MAIN(ScriptStabilisationTest)
#include "ScriptStabilisationTest.moc"
