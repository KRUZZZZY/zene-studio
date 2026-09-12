/*
 * ScriptEngineTest.cpp
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

#include <QThread>
#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <cmath>

#include "AutomatableModel.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ProjectJournal.h"
#include "ScriptEngine.h"
#include "Song.h"
#include "Track.h"

//! Path to data/scripts, injected by tests/CMakeLists.txt.
#ifndef LUA_SCRIPT_DIR
#define LUA_SCRIPT_DIR "data/scripts"
#endif

//! Build-tree plugin dir, injected by tests/CMakeLists.txt.
#ifndef LMMS_TEST_PLUGIN_DIR
#define LMMS_TEST_PLUGIN_DIR "plugins"
#endif

namespace
{

/*!
 * Windows: a test host cannot load a plugin MODULE library at runtime.
 *
 * Every plugin module links the zene executable, so an MSVC module's import
 * descriptor names zene.exe; the Windows loader then fails with
 * ERROR_MOD_NOT_FOUND (126) because the test host is not zene.exe. CI
 * (msvc-x64, QT_FORCE_STDERR_LOGGING=1) shows the loader error verbatim:
 *   QWARN : ... Cannot load library ...\plugins\tripleoscillator.dll:
 *           The specified module could not be found.
 * On Linux/macOS the module's undefined lmms symbols are bound from the
 * loading process's exported symbol table (the test target sets
 * ENABLE_EXPORTS), so the same load succeeds there. The product loads these
 * modules inside zene.exe, where the import resolves by construction: this
 * is a test-host limitation, not a product defect.
 */
constexpr auto testHostCanLoadPluginModules() -> bool
{
#ifdef Q_OS_WIN
	return false;
#else
	return true;
#endif
}

} // namespace

class ScriptEngineTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		using namespace lmms;
		// Tell PluginFactory where the build-tree plugins live *before* it is
		// first instantiated (it reads LMMS_PLUGIN_DIR in its constructor).
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
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
	}

	// --- G4: API versioning (spec section 6) ---

	void testVersionHeader_data()
	{
		QTest::addColumn<QString>("source");
		QTest::addColumn<int>("expected");

		using Result = lmms::ScriptEngine::RunResult;
		QTest::newRow("missing") << "print('x')" << int(Result::VersionError);
		QTest::newRow("too-new-major")
			<< "--! lmms-api 1.0\nprint('x')" << int(Result::VersionError);
		QTest::newRow("malformed")
			<< "--! lmms-api nonsense\nprint('x')" << int(Result::VersionError);
		QTest::newRow("v0.1-ok")
			<< "--! lmms-api 0.1\nprint('x')" << int(Result::Ok);
		QTest::newRow("v0.0-ok")
			<< "--! lmms-api 0.0\nprint('x')" << int(Result::Ok);
		QTest::newRow("v0.2-too-new")
			<< "--! lmms-api 0.2\nprint('x')" << int(Result::VersionError);
	}

	void testVersionHeader()
	{
		using namespace lmms;
		QFETCH(QString, source);
		QFETCH(int, expected);

		const QString path = QDir(QDir::tempPath()).absoluteFilePath("lmms-version-test.lua");
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		file.write(source.toUtf8());
		file.close();

		QString error;
		const auto result = ScriptEngine::instance()->runFile(path, &error);
		QFile::remove(path);
		QCOMPARE(int(result), expected);
		if (expected != int(ScriptEngine::RunResult::Ok))
		{
			QVERIFY2(!error.isEmpty(), "a rejected script must explain why");
		}
	}

	void testParseVersionHeader()
	{
		using namespace lmms;
		QCOMPARE(ScriptEngine::parseVersionHeader("--! lmms-api 0.1\n"), QStringLiteral("0.1"));
		QCOMPARE(ScriptEngine::parseVersionHeader("--! lmms-api 2.7\nprint(1)"), QStringLiteral("2.7"));
		QCOMPARE(ScriptEngine::parseVersionHeader("print(1)"), QString());
	}

	// --- G1: engine runs a script, output observed via LuaLog ---

	void testHelloScript()
	{
		using namespace lmms;
		QString error;
		const auto result = ScriptEngine::instance()->runFile(
			QStringLiteral(LUA_SCRIPT_DIR) + "/hello.lua", &error);
		QVERIFY2(result == ScriptEngine::RunResult::Ok, qPrintable(error));

		const QStringList log = ScriptEngine::instance()->takeLogMessages();
		for (const QString& line : log) { qInfo().noquote() << "lua:" << line; }
		QVERIFY2(log.filter("Hello from Lua Lua 5.4").size() == 1,
			qPrintable(log.join('|')));
		QVERIFY2(log.filter("Zene Studio Lua API 0.1").size() == 1,
			qPrintable(log.join('|')));
		QVERIFY2(log.contains("hello.lua finished"), qPrintable(log.join('|')));
	}

	// The scripting namespace and API header were renamed `zene`; both
	// pre-rename spellings must keep working, because that is the contract every
	// script written against the published alpha was written to.
	void testLegacyNamespaceAndHeaderStillRun()
	{
		using namespace lmms;
		QString error;

		const auto legacy = ScriptEngine::instance()->runString(
			"--! lmms-api 0.1\n"
			"lmms.log():info('legacy-namespace-ok ' .. lmms.ticksPerBar())\n",
			&error);
		QVERIFY2(legacy == ScriptEngine::RunResult::Ok, qPrintable(error));
		const QStringList legacyLog = ScriptEngine::instance()->takeLogMessages();
		QVERIFY2(legacyLog.filter("legacy-namespace-ok").size() == 1,
			qPrintable(legacyLog.join('|')));

		error.clear();
		const auto renamed = ScriptEngine::instance()->runString(
			"--! zene-api 0.1\n"
			"zene.log():info('new-namespace-ok ' .. zene.ticksPerBar())\n",
			&error);
		QVERIFY2(renamed == ScriptEngine::RunResult::Ok, qPrintable(error));
		const QStringList renamedLog = ScriptEngine::instance()->takeLogMessages();
		QVERIFY2(renamedLog.filter("new-namespace-ok").size() == 1,
			qPrintable(renamedLog.join('|')));
	}

	// --- G2: Transport/Song + Pattern/Note bound; example 1 on a real project ---

	void testCreatePatternScript()
	{
		using namespace lmms;
		auto* song = Engine::getSong();
		auto* store = Engine::patternStore();
		auto* engine = ScriptEngine::instance();

		QCOMPARE(store->tracks().size(), std::size_t(0));
		QCOMPARE(song->countTracks(Track::Type::Pattern), 0);

		QString error;
		const auto result = engine->runFile(
			QStringLiteral(LUA_SCRIPT_DIR) + "/create-pattern.lua", &error);
		QVERIFY2(result == ScriptEngine::RunResult::Ok, qPrintable(error));
		engine->processCommands();

		QCOMPARE(store->tracks().size(), std::size_t(1));
		QCOMPARE(song->countTracks(Track::Type::Pattern), 1);
		QCOMPARE(store->numOfPatterns(), 1);

		auto* clip = dynamic_cast<MidiClip*>(store->tracks()[0]->getClip(0));
		QVERIFY2(clip != nullptr, "pattern 0 has no MidiClip on track 0");
		QCOMPARE(clip->name(), QStringLiteral("Hi-Hat 16"));
		QCOMPARE(int(clip->notes().size()), 16);

		const int step = TimePos::ticksPerBar() / 16;
		for (int i = 0; i < 16; ++i)
		{
			const Note* note = clip->notes()[i];
			QCOMPARE(note->key(), 42);
			QCOMPARE(note->pos().getTicks(), i * step);
			QCOMPARE(note->length().getTicks(), step / 2);
			QCOMPARE(int(note->getVolume()), i % 4 == 0 ? 112 : 72);
		}
	}

	void testCreatePatternUndo()
	{
		using namespace lmms;
		auto* store = Engine::patternStore();
		auto* engine = ScriptEngine::instance();

		QString error;
		QVERIFY2(engine->runFile(QStringLiteral(LUA_SCRIPT_DIR) + "/create-pattern.lua",
			&error) == ScriptEngine::RunResult::Ok, qPrintable(error));
		engine->processCommands();

		auto* clip = dynamic_cast<MidiClip*>(store->tracks()[0]->getClip(0));
		QVERIFY(clip != nullptr);
		QCOMPARE(int(clip->notes().size()), 16);

		Engine::projectJournal()->undo();
		QCOMPARE(int(clip->notes().size()), 0);
	}

	// --- G4: example 2 and 3 (spec section 7) ---

	void testGenerativeBassScript()
	{
		using namespace lmms;
		auto* store = Engine::patternStore();
		auto* engine = ScriptEngine::instance();

		QString error;
		QVERIFY2(engine->runFile(QStringLiteral(LUA_SCRIPT_DIR) + "/generative-bass.lua",
			&error) == ScriptEngine::RunResult::Ok, qPrintable(error));
		engine->processCommands();

		auto* clip = dynamic_cast<MidiClip*>(store->tracks()[0]->getClip(0));
		QVERIFY(clip != nullptr);
		QCOMPARE(int(clip->notes().size()), 8 * 16);
		for (const Note* note : clip->notes())
		{
			QVERIFY(note->key() >= 36);
			QVERIFY(note->key() <= 36 + 10 + 12);
		}
	}

	void testMidiRouterScript()
	{
		using namespace lmms;
		auto* store = Engine::patternStore();
		auto* engine = ScriptEngine::instance();

		// Simulate the polled MIDI-in buffer: two note-ons plus a note-off
		// that the script must ignore.
		engine->pushMidiInEvent(0, 60, 100, true);
		engine->pushMidiInEvent(0, 64, 90, true);
		engine->pushMidiInEvent(0, 64, 0, false);

		QString error;
		QVERIFY2(engine->runFile(QStringLiteral(LUA_SCRIPT_DIR) + "/midi-router.lua",
			&error) == ScriptEngine::RunResult::Ok, qPrintable(error));
		engine->processCommands();

		QCOMPARE(engine->midiInPending(), 0);

		auto* clip = dynamic_cast<MidiClip*>(store->tracks()[1]->getClip(0));
		QVERIFY2(clip != nullptr, "transposed clip missing on the destination track");
		QCOMPARE(int(clip->notes().size()), 2);
		QCOMPARE(clip->notes()[0]->key(), 67);   // 60 + 7
		QCOMPARE(clip->notes()[1]->key(), 71);   // 64 + 7

		const QStringList log = engine->takeLogMessages();
		QVERIFY2(log.filter("routed 2 note(s)").size() == 1, qPrintable(log.join('|')));
		QVERIFY2(log.filter("MIDI out: note-on ch0 key67 vel100").size() == 1,
			qPrintable(log.join('|')));
	}

	// --- G3: sandbox ---

	void testSandboxStdlibRemoved()
	{
		using namespace lmms;
		const QString source = QStringLiteral(R"(
assert(os == nil, "os leaked")
assert(io == nil, "io leaked")
assert(require == nil, "require leaked")
assert(load == nil, "load leaked")
assert(loadstring == nil, "loadstring leaked")
assert(dofile == nil, "dofile leaked")
assert(package == nil, "package leaked")
assert(debug ~= nil, "restricted debug table expected")
assert(type(debug.sethook) == "function", "debug.sethook expected")
assert(string ~= nil and table ~= nil and math ~= nil, "safe stdlib missing")
)");
		QString error;
		QCOMPARE(int(ScriptEngine::instance()->runString(source, &error)), int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
	}

	void testSandboxHookCannotRaiseBudget()
	{
		using namespace lmms;
		// The restricted debug.sethook may only lower the budget.
		QString error;
		QCOMPARE(int(ScriptEngine::instance()->runString(
			QStringLiteral("debug.sethook(1000000)"), &error)),
			int(ScriptEngine::RunResult::Ok));
		QCOMPARE(ScriptEngine::instance()->instructionBudget(), quint64(5000000));
	}

	void testSandboxInstructionBudget()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		engine->setInstructionBudget(200000);

		QElapsedTimer timer;
		timer.start();
		QString error;
		const auto result = engine->runString(QStringLiteral("while true do end"), &error);
		const qint64 elapsed = timer.elapsed();

		QCOMPARE(int(result), int(ScriptEngine::RunResult::ScriptError));
		QVERIFY2(error.contains("instruction budget exceeded"), qPrintable(error));
		QVERIFY2(elapsed < 10000, qPrintable(QStringLiteral("took %1 ms").arg(elapsed)));
		qInfo() << "runaway script halted after" << elapsed << "ms:" << error;

		// The engine must still be usable afterwards (no stall, no deadlock).
		QString secondError;
		QCOMPARE(int(engine->runString(QStringLiteral("print('still alive')"), &secondError)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(engine->takeLogMessages().contains("still alive"), "engine stalled");

		engine->setInstructionBudget(5000000);
	}

	void testSandboxFileAccess()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		QTemporaryDir project;
		QVERIFY(project.isValid());
		engine->setProjectDir(project.path());

		// In-project write + read must work.
		QString error;
		QCOMPARE(int(engine->runString(QStringLiteral(
			"lmms.projectFile():write('note.txt', 'hello sandbox')\n"
			"assert(lmms.projectFile():read('note.txt') == 'hello sandbox')"), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));

		// Escaping the project directory must be rejected.
		error.clear();
		QCOMPARE(int(engine->runString(
			QStringLiteral("lmms.projectFile():read('/etc/passwd')"), &error)),
			int(ScriptEngine::RunResult::ScriptError));
		QVERIFY2(error.contains("escapes the project directory"), qPrintable(error));
		qInfo().noquote() << "out-of-project read rejected:" << error;

		error.clear();
		QCOMPARE(int(engine->runString(
			QStringLiteral("lmms.projectFile():write('../../escape.txt', 'x')"), &error)),
			int(ScriptEngine::RunResult::ScriptError));
		QVERIFY2(error.contains("escapes the project directory"), qPrintable(error));

		error.clear();
		QCOMPARE(int(engine->runString(
			QStringLiteral("lmms.projectFile():read('../../etc/passwd')"), &error)),
			int(ScriptEngine::RunResult::ScriptError));
		QVERIFY2(error.contains("escapes the project directory"), qPrintable(error));

		QVERIFY(!QFile::exists(QDir(project.path()).absoluteFilePath("../escape.txt")));
	}

	void testWorkerThreadNotAudioThread()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		QVERIFY(engine->workerThread() != nullptr);
		QCOMPARE(engine->workerThread()->objectName(), QStringLiteral("zene-lua-script-worker"));
		QVERIFY2(engine->workerThread() != QThread::currentThread(),
			"scripts must not run on the calling (UI/audio) thread");
		QVERIFY2(engine->workerThread() != Engine::audioEngine()->thread(),
			"scripts must never run on the audio thread");
	}

	// --- G3: engine state is only mutated by the apply side ---

	void testApplyRunsOnApplySideThread()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();

		// Regression: read-side flushes used to apply commands on the worker
		// thread, so Track::create() ran on the wrong thread (Qt warned about
		// children in a different thread). Engine state must only ever be
		// mutated by the apply side (spec section 4 / section 10).
		QString error;
		QVERIFY2(engine->runFile(QStringLiteral(LUA_SCRIPT_DIR) + "/create-pattern.lua",
			&error) == ScriptEngine::RunResult::Ok, qPrintable(error));

		QVERIFY2(engine->lastApplyThread() != nullptr, "no command was applied");
		QCOMPARE(engine->lastApplyThread(), QThread::currentThread());
		QVERIFY2(engine->lastApplyThread() != engine->workerThread(),
			"engine state must never be mutated on the script worker thread");
	}

	// --- G4: Instrument binding (spec section 3) ---

	void testInstrumentParameterReadWrite()
	{
		using namespace lmms;

		// This slot loads the real tripleoscillator module through the normal
		// InstrumentTrack path. On Windows no test host can do that (an MSVC
		// module's import descriptor names zene.exe - see the note on
		// testHostCanLoadPluginModules()), so skip here - and only here - so
		// the rest of this file keeps running on Windows.
		if (!testHostCanLoadPluginModules())
		{
			QSKIP("this slot loads the tripleoscillator module through the normal host path, "
				"and a Windows test host cannot load plugin modules: plugin modules link the "
				"zene executable, so on Windows their import descriptor names zene.exe and a "
				"test host cannot satisfy it; the product loads them inside zene.exe where "
				"that resolves by construction (CI msvc-x64: QLibrary::load -> "
				"ERROR_MOD_NOT_FOUND, 126)");
		}

		auto* engine = ScriptEngine::instance();

		// A real plugin, loaded through the normal InstrumentTrack path. The
		// test binary exports its symbols and knows the build-tree plugin dir
		// (tests/CMakeLists.txt), so plugin modules resolve core symbols the
		// same way they do inside the zene executable - on ELF platforms. On
		// Windows this cannot work at all, hence the skip above.
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::patternStore()));
		QVERIFY2(track != nullptr, "could not create an instrument track");
		Instrument* instrument = track->loadInstrument("tripleoscillator");
		QVERIFY(instrument != nullptr);
		QCOMPARE(instrument->displayName(), QStringLiteral("TripleOscillator"));

		const int count = instrument->parameterCount();
		QVERIFY2(count > 0, "instrument exposes no named parameters");
		QVERIFY2(!instrument->parameterName(0).isEmpty(), "parameter 0 has no name");
		qInfo() << "C++ side: instrument" << instrument->displayName()
			<< "parameterCount" << count << "parameterName(0)"
			<< instrument->parameterName(0);

		AutomatableModel* model = instrument->parameterModel(0);
		QVERIFY2(model != nullptr, "parameter 0 has no model");
		const float minimum = model->minValue<float>();
		const float maximum = model->maxValue<float>();
		const float before = model->value<float>();
		QVERIFY2(maximum > minimum, "parameter has no usable range");

		// Pick a target inside the range that differs from the current value.
		float target = 40.0f;
		if (target < minimum || target > maximum
			|| qFuzzyCompare(target + 1.0f, before + 1.0f))
		{
			target = (before + maximum) / 2.0f;
		}
		QVERIFY2(!qFuzzyCompare(target + 1.0f, before + 1.0f),
			"target value must differ from the current value");
		qInfo().noquote() << QString("C++ side: %1 = %2 (range %3..%4), target %5")
			.arg(instrument->parameterName(0))
			.arg(double(before), 0, 'f', 4)
			.arg(double(minimum), 0, 'f', 4)
			.arg(double(maximum), 0, 'f', 4)
			.arg(double(target), 0, 'f', 4);

		// Read AND write the same parameter from Lua. Every assert runs inside
		// the script, so a wrong read or a lost write fails the run itself.
		const QString source = QStringLiteral(R"(
local track = lmms.song():patternStore():track(0)
local instrument = track:asInstrumentTrack():instrument()
assert(instrument:isValid(), 'instrument wrapper invalid')
assert(instrument:name() == 'TripleOscillator', 'unexpected instrument: ' .. instrument:name())
local count = instrument:parameterCount()
assert(count > 0, 'instrument exposes no parameters')
local model = instrument:parameterModel(%1)
assert(model:isValid(), 'parameter model invalid')
local read = model:value()
assert(math.abs(read - %3) < 0.0001, string.format('read %.4f, expected %.4f', read, %3))
lmms.log():info(string.format('read  %s (%s) = %.4f range %.4f..%.4f',
    model:name(), model:type(), read, model:minValue(), model:maxValue()))
model:setValue(%2)
local written = model:value()
assert(math.abs(written - %2) < 0.0001, string.format('read-back %.4f, expected %.4f', written, %2))
lmms.log():info(string.format('wrote %s = %.4f', model:name(), written))
lmms.log():info(string.format('parameterCount=%d parameterName(0)=%s',
    count, instrument:parameterName(0)))
)").arg(0)
		.arg(QString::number(double(target), 'f', 4))
		.arg(QString::number(double(before), 'f', 4));

		QString error;
		const auto result = engine->runString(source, &error, QStringLiteral("=(instrument)"));
		QVERIFY2(result == ScriptEngine::RunResult::Ok, qPrintable(error));
		engine->processCommands();

		const QStringList log = engine->takeLogMessages();
		for (const QString& line : log) { qInfo().noquote() << "lua:" << line; }
		QVERIFY2(log.filter("parameterCount=").size() == 1, qPrintable(log.join('|')));

		// The C++ side observes the Lua write: the model really changed.
		const float after = model->value<float>();
		qInfo() << "C++ side: value after Lua write" << double(after);
		QVERIFY2(std::fabs(after - target) < 1e-3f,
			qPrintable(QString("model value %1 != target %2")
				.arg(double(after)).arg(double(target))));
	}

	void testModelWritesAreQueuedNotDirect()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();

		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::patternStore()));
		QVERIFY(track != nullptr);
		const int before = track->getVolume();

		// With autoApply off nothing drains the queue, so the write must stay
		// pending: proof the worker thread never touches the model directly
		// (spec section 4 / section 7 item 5).
		engine->setAutoApply(false);
		QString error;
		const auto result = engine->runString(QStringLiteral(R"(
local track = lmms.song():patternStore():track(0)
local model = track:volumeModel()
model:setValue(42)
lmms.log():info('queued a model write')
)"), &error, QStringLiteral("=(queue)"));
		QVERIFY2(result == ScriptEngine::RunResult::Ok, qPrintable(error));

		qInfo() << "pending commands after script:" << engine->pendingCommandCount()
			<< "track volume:" << track->getVolume();
		QVERIFY2(engine->pendingCommandCount() > 0, "model write must be queued");
		QCOMPARE(track->getVolume(), before);

		engine->processCommands();
		qInfo() << "track volume after apply:" << track->getVolume();
		QCOMPARE(track->getVolume(), 42);
		engine->setAutoApply(true);
	}

	// --- command queue ---

	void testCommandQueueOverflowDropsWithoutBlocking()
	{
		using namespace lmms;
		auto* engine = ScriptEngine::instance();
		engine->setAutoApply(false);

		// Flood the queue: every addNote becomes one command. With autoApply
		// off nothing drains it, so overflow must be counted, not fatal.
		QString error;
		const auto result = engine->runString(QStringLiteral(R"(
local clip = lmms.song():patternStore():addPattern()
for i = 1, 20000 do clip:addNoteAt(60, i, 10, 100) end
)"), &error, QStringLiteral("=(flood)"));
		QVERIFY2(result == ScriptEngine::RunResult::Ok, qPrintable(error));
		QVERIFY2(engine->droppedCommandCount() > 0,
			"expected the bounded SPSC queue to drop commands instead of blocking");
		qInfo() << "dropped commands:" << engine->droppedCommandCount()
			<< "pending:" << engine->pendingCommandCount();

		engine->processCommands();
		engine->setAutoApply(true);
	}
};

QTEST_GUILESS_MAIN(ScriptEngineTest)
#include "ScriptEngineTest.moc"
