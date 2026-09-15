/*
 * ScriptMemoryBudgetTest.cpp - CODE-6: the Lua memory budget beside the
 *                               instruction budget.
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
 *
 * WHAT THIS PROVES, and how it could be wrong. The instruction budget is a
 * count hook: it bounds TIME. ScriptEngine.cpp:96 used to open its Lua state
 * with luaL_newstate(), whose allocator is plain realloc() with no accounting
 * at all, so nothing bounded SPACE: a script could allocate until the machine
 * started swapping or the OOM killer arrived. The budget added here is a second,
 * independent bound, and the test is a PAIR for that reason - the same script is
 * run twice, once under the shipping budget (it completes and its peak is
 * measured) and once under a budget it cannot fit in (the run is refused, typed,
 * with the refused-allocation count non-zero). A refusal on its own proves
 * nothing: "the script failed" is also what a broken sandbox, a missing library
 * or an engine that never started looks like.
 */

#include <QtTest>

#include <QJsonObject>
#include <QStringList>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ScriptEngine.h"

using namespace lmms;

namespace
{

//! A script that holds well over the 1 MiB budget under test, and well under
//! the shipping 64 MiB one: sixteen 64 KiB strings fill 1 MiB, and the loop
//! goes to two hundred times that. String.rep is a C function, so the loop
//! costs almost no VM instructions - if this were refused by the INSTRUCTION
//! budget the test would be measuring the wrong cap, which is why the negative
//! control below is the same source.
const char* const MemoryHogSource =
	"local held = {}\n"
	"for i = 1, 200 do held[i] = string.rep('x', 65536) end\n"
	"print('held ' .. tostring(#held) .. ' blocks')\n";

constexpr quint64 OneMiB = 1024ull * 1024ull;

} // namespace

class ScriptMemoryBudgetTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		// The registry only dispatches handlers once the application says the
		// model is fully up (the ControlRegistryTest idiom).
		ControlRegistry::setReady(true);
		ScriptEngine::instance()->setMemoryBudget(ScriptEngine::DefaultMemoryBudgetBytes);
	}

	void cleanupTestCase()
	{
		ScriptEngine::instance()->setMemoryBudget(ScriptEngine::DefaultMemoryBudgetBytes);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The budget is real, and it is a MEMORY budget: the same script that
	//! completes under the shipping cap is refused under a cap it cannot fit in.
	void aScriptThatExceedsTheMemoryBudgetIsRefused()
	{
		ScriptEngine* engine = ScriptEngine::instance();

		// --- the negative control: the shipping budget runs it -------------
		engine->setMemoryBudget(ScriptEngine::DefaultMemoryBudgetBytes);
		QString okError;
		const auto ran = engine->runString(QString::fromUtf8(MemoryHogSource), &okError);
		QVERIFY2(ran == ScriptEngine::RunResult::Ok, qPrintable(okError));
		const quint64 okPeak = engine->lastRunMemoryPeakBytes();
		QVERIFY2(okPeak > OneMiB,
			qPrintable(QStringLiteral("the control run peaked at %1 bytes, so it never held the "
				"memory this test is about").arg(okPeak)));
		QVERIFY2(okPeak < ScriptEngine::DefaultMemoryBudgetBytes,
			qPrintable(QStringLiteral("the control run peaked at %1 bytes, at or over the "
				"shipping budget %2 - the control is not inside the budget")
				.arg(okPeak).arg(ScriptEngine::DefaultMemoryBudgetBytes)));
		QCOMPARE(engine->lastRunMemoryRefusals(), quint64(0));

		// --- the budget under test: 1 MiB cannot hold it -------------------
		engine->setMemoryBudget(OneMiB);
		QString error;
		const auto refused = engine->runString(QString::fromUtf8(MemoryHogSource), &error);
		QCOMPARE(int(refused), int(ScriptEngine::RunResult::ScriptError));
		QVERIFY2(error.contains(QStringLiteral("memory budget exceeded")), qPrintable(error));
		QVERIFY2(!error.contains(QStringLiteral("instruction budget exceeded")),
			qPrintable(QStringLiteral("the instruction budget answered, not the memory budget: %1")
				.arg(error)));
		QVERIFY2(engine->lastRunMemoryRefusals() > 0,
			"the run was refused without a single refused allocation");
		QVERIFY2(engine->lastRunMemoryPeakBytes() <= OneMiB,
			qPrintable(QStringLiteral("the state peaked at %1 bytes, over the %2-byte budget")
				.arg(engine->lastRunMemoryPeakBytes()).arg(OneMiB)));

		// The run never reached its own print: the refusal happened inside the
		// allocation, which is what "the script was stopped, not merely failed
		// afterwards" means.
		QVERIFY2(!engine->takeLogMessages().contains(QStringLiteral("held 200 blocks")),
			"the script finished its loop and failed later");
	}

	//! A refused run leaves the engine usable: the worker thread, the apply
	//! side and the next Lua state are all still there (the failure is a Lua
	//! memory error, not a crash or a stalled worker).
	void theEngineStillRunsAfterARefusedRun()
	{
		ScriptEngine* engine = ScriptEngine::instance();
		engine->setMemoryBudget(OneMiB);
		QString refusedError;
		engine->runString(QString::fromUtf8(MemoryHogSource), &refusedError);

		engine->setMemoryBudget(ScriptEngine::DefaultMemoryBudgetBytes);
		QString error;
		QCOMPARE(int(engine->runString(QStringLiteral("print('still alive')"), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY2(engine->takeLogMessages().contains(QStringLiteral("still alive")),
			"the engine did not survive the refused run");
	}

	//! The budget is drivable through the control surface, which is what makes
	//! it a 0.3.0 feature rather than a constant: the verb sets it, refuses
	//! values outside the declared range instead of clamping them, records the
	//! previous value for control.undo, and script.run reports both the budget
	//! and what the run measured against it.
	void theBudgetIsDrivableThroughTheControlSurface()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ScriptEngine* engine = ScriptEngine::instance();
		engine->setMemoryBudget(ScriptEngine::DefaultMemoryBudgetBytes);

		const ControlCommand* verb = registry->command(QStringLiteral("script.set_memory_budget"));
		QVERIFY2(verb != nullptr, "script.set_memory_budget is not registered");
		QVERIFY2(verb->mutating, "setting a budget is a write and must be recorded as one");

		// Below the floor: refused, typed, and NOT clamped - a client that asked
		// for something impossible must not be handed a different experiment.
		const ControlResult tooSmall = registry->invoke(QStringLiteral("script.set_memory_budget"),
			QJsonObject{{QStringLiteral("bytes"), 4096}});
		QVERIFY2(!tooSmall.ok, "a budget below the floor was accepted");
		QCOMPARE(tooSmall.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(controlErrorKindName(tooSmall.errorKind), QStringLiteral("invalid_args"));
		QCOMPARE(engine->memoryBudget(), ScriptEngine::DefaultMemoryBudgetBytes);

		// In range: accepted, reported, and the record names the previous value.
		const ControlResult set = registry->invoke(QStringLiteral("script.set_memory_budget"),
			QJsonObject{{QStringLiteral("bytes"), 2 * OneMiB}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("memory_budget")).toInt(), int(2 * OneMiB));
		QCOMPARE(engine->memoryBudget(), 2 * OneMiB);
		const QJsonObject transaction = set.result.value(QStringLiteral("__transaction")).toObject();
		QCOMPARE(transaction.value(QStringLiteral("reversible")).toBool(), true);
		QCOMPARE(transaction.value(QStringLiteral("before")).toObject()
				.value(QStringLiteral("memory_budget")).toInt(),
			int(ScriptEngine::DefaultMemoryBudgetBytes));

		// script.run reports the budget in force and the run's own measurement.
		const ControlResult report = registry->invoke(QStringLiteral("script.run"),
			QJsonObject{{QStringLiteral("source"),
				QStringLiteral("local held = {} for i = 1, 32 do held[i] = string.rep('y', 4096) end")}});
		QVERIFY2(report.ok, qPrintable(report.errorMessage));
		QCOMPARE(report.result.value(QStringLiteral("memory_budget")).toInt(), int(2 * OneMiB));
		QVERIFY2(report.result.value(QStringLiteral("memory_peak_bytes")).toInt() > 0,
			"script.run reported no memory measurement");
		QCOMPARE(report.result.value(QStringLiteral("memory_refusals")).toInt(), 0);

		// And the same report shows the refusal when the budget is too small.
		QVERIFY(registry->invoke(QStringLiteral("script.set_memory_budget"),
			QJsonObject{{QStringLiteral("bytes"), OneMiB}}).ok);
		const ControlResult refused = registry->invoke(QStringLiteral("script.run"),
			QJsonObject{{QStringLiteral("source"), QString::fromUtf8(MemoryHogSource)}});
		QVERIFY2(!refused.ok, "a script over the budget ran anyway");
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("memory budget exceeded")),
			qPrintable(refused.errorMessage));

		engine->setMemoryBudget(ScriptEngine::DefaultMemoryBudgetBytes);
	}
};

QTEST_GUILESS_MAIN(ScriptMemoryBudgetTest)
#include "ScriptMemoryBudgetTest.moc"
