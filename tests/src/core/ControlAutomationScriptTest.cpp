/*
 * ControlAutomationScriptTest.cpp - unit tests for the automation.* and
 *                                   script.* command groups
 *                                   (SPEC-zene-studio.md A11-A16).
 *
 * The end-to-end behaviour (rendered audio, control.undo, the wire) lives in
 * tests/control-socket-integration.py; this file pins the registry surface those
 * flows rely on: the ids, the schemas, the typed refusal each error path must
 * produce, and the fact that script.run drives the embedded ScriptEngine rather
 * than a child process.
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

#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPair>
#include <QThread>
#include <QVector>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ScriptEngine.h"

using namespace lmms;

//! The shipped Lua examples; tests/CMakeLists.txt injects the absolute path.
#ifndef LUA_SCRIPT_DIR
#define LUA_SCRIPT_DIR "data/scripts"
#endif

class ControlAutomationScriptTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Every command of the group is registered, under a group.verb id, with
	//! both schemas, and the mutating ones say so.
	void automationCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QVector<QPair<QString, bool>> required = {
			{qstr("automation.get_state"), false},
			{qstr("automation.add_point"), true},
			{qstr("automation.remove_point"), true},
			{qstr("automation.clear"), true},
			{qstr("automation.mode_set"), true},
		};
		for (const auto& entry : required)
		{
			const ControlCommand* command = registry->command(entry.first);
			QVERIFY2(command != nullptr, qPrintable(entry.first + " is not registered"));
			QCOMPARE(command->id, entry.first);
			QCOMPARE(command->group + QStringLiteral(".") + command->verb, entry.first);
			QCOMPARE(command->mutating, entry.second);
			QVERIFY(!command->description.isEmpty());
			QVERIFY2(!command->argsSchema.isEmpty(), qPrintable(entry.first + " has no args schema"));
			QVERIFY2(!command->resultSchema.isEmpty(), qPrintable(entry.first + " has no result schema"));
			QVERIFY(command->requiresEngine);
		}
	}

	void scriptCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlCommand* run = registry->command(QStringLiteral("script.run"));
		QVERIFY(run != nullptr);
		QCOMPARE(run->group, QStringLiteral("script"));
		QCOMPARE(run->verb, QStringLiteral("run"));
		QVERIFY(run->mutating);
		QVERIFY(run->requiresEngine);
		QVERIFY(!run->argsSchema.isEmpty());
		QVERIFY(!run->resultSchema.isEmpty());

		const ControlCommand* list = registry->command(QStringLiteral("script.list"));
		QVERIFY(list != nullptr);
		QCOMPARE(list->group, QStringLiteral("script"));
		QCOMPARE(list->verb, QStringLiteral("list"));
		QVERIFY(!list->mutating);
		// Listing the shipped files needs no song and no mixer.
		QVERIFY(!list->requiresEngine);
	}

	//! A fresh song has no user tracks, so the read-back must say that rather
	//! than inventing one.
	void automationGetStateOnAFreshSongIsHonest()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult state = registry->invoke(QStringLiteral("automation.get_state"));
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		// Whatever the fresh song holds, every track it has is reported and
		// nothing is reported as automated.
		const int trackCount = state.result.value(QStringLiteral("track_count")).toInt();
		QCOMPARE(state.result.value(QStringLiteral("count")).toInt(), trackCount);
		QCOMPARE(state.result.value(QStringLiteral("automated_parameter_count")).toInt(), 0);
		QCOMPARE(static_cast<int>(state.result.value(QStringLiteral("tracks")).toArray().size()), trackCount);

		const ControlResult missing = registry->invoke(QStringLiteral("automation.get_state"),
			QJsonObject{{QStringLiteral("track"), QStringLiteral("trk-9999")}});
		QVERIFY(!missing.ok);
		QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);
	}

	//! Each refusing path returns the closed-set kind it promises.
	void automationTypedErrors()
	{
		ControlRegistry* registry = ControlRegistry::instance();

		struct Case
		{
			QString id;
			QJsonObject args;
			ControlErrorKind kind;
		};
		const QVector<Case> cases = {
			// no such track
			{qstr("automation.add_point"), {{qstr("track"), qstr("trk-9999")},
				{qstr("parameter"), qstr("inst/0")}, {qstr("ticks"), 0}, {qstr("value"), 1}},
				ControlErrorKind::NotFound},
			{qstr("automation.remove_point"), {{qstr("track"), qstr("trk-9999")},
				{qstr("parameter"), qstr("inst/0")}, {qstr("ticks"), 0}},
				ControlErrorKind::NotFound},
			{qstr("automation.clear"), {{qstr("track"), qstr("trk-9999")},
				{qstr("parameter"), qstr("inst/0")}}, ControlErrorKind::NotFound},
			// the args schema bites before the handler runs
			{qstr("automation.add_point"), {{qstr("track"), qstr("trk-9999")},
				{qstr("parameter"), qstr("inst/0")}, {qstr("ticks"), 0}},
				ControlErrorKind::InvalidArgs},
			{qstr("automation.add_point"), {{qstr("track"), qstr("trk-9999")},
				{qstr("parameter"), qstr("inst/0")}, {qstr("ticks"), -4}, {qstr("value"), 1}},
				ControlErrorKind::InvalidArgs},
			{qstr("automation.remove_point"), {{qstr("track"), qstr("trk-9999")},
				{qstr("parameter"), qstr("inst/0")}}, ControlErrorKind::InvalidArgs},
			// mode_set's closed enum is real, so a bogus mode never reaches the handler
			{qstr("automation.mode_set"), {{qstr("track"), qstr("trk-9999")},
				{qstr("parameter"), qstr("inst/0")}, {qstr("mode"), qstr("scribble")}},
				ControlErrorKind::InvalidArgs},
			// ... and a valid mode on a track that does not exist resolves the target first
			{qstr("automation.mode_set"), {{qstr("track"), qstr("trk-9999")},
				{qstr("parameter"), qstr("inst/0")}, {qstr("mode"), qstr("write")}},
				ControlErrorKind::NotFound},
			// an unknown command is not_found too
			{qstr("automation.no_such_verb"), {}, ControlErrorKind::NotFound},
		};
		for (const Case& testCase : cases)
		{
			const ControlResult result = registry->invoke(testCase.id, testCase.args);
			QVERIFY2(!result.ok, qPrintable(testCase.id + " unexpectedly succeeded"));
			QVERIFY2(result.errorKind == testCase.kind,
				qPrintable(QStringLiteral("%1: expected %2, got %3 (%4)")
					.arg(testCase.id, controlErrorKindName(testCase.kind),
						controlErrorKindName(result.errorKind), result.errorMessage)));
			QVERIFY(!result.errorMessage.isEmpty());
		}
	}

	void scriptListShowsTheShippedScripts()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult result = registry->invoke(QStringLiteral("script.list"));
		QVERIFY2(result.ok, qPrintable(result.errorMessage));

		const QString dir = result.result.value(QStringLiteral("dir")).toString();
		QVERIFY2(!dir.isEmpty(), "script.list resolved no scripts directory");
		QVERIFY(QFileInfo(dir).isDir());

		QStringList names;
		for (const QJsonValue& value : result.result.value(QStringLiteral("scripts")).toArray())
		{
			const QJsonObject entry = value.toObject();
			names.append(entry.value(QStringLiteral("name")).toString());
			QVERIFY(!entry.value(QStringLiteral("sha256")).toString().isEmpty());
			QVERIFY(entry.value(QStringLiteral("bytes")).toInt() > 0);
			QVERIFY(QFileInfo(entry.value(QStringLiteral("path")).toString()).exists());
		}
		for (const QString& shipped : {qstr("hello.lua"), qstr("create-pattern.lua"),
				qstr("generative-bass.lua"), qstr("midi-router.lua")})
		{
			QVERIFY2(names.contains(shipped),
				qPrintable(QStringLiteral("%1 is not listed (saw %2)").arg(shipped, names.join(QLatin1Char(',')))));
		}
		QCOMPARE(result.result.value(QStringLiteral("count")).toInt(), static_cast<int>(names.size()));
	}

	//! script.run drives the embedded engine on its own worker thread; the log
	//! the script wrote comes back, and the run is not a second process.
	void scriptRunsInProcessAndReturnsItsLog()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QString path = QStringLiteral(LUA_SCRIPT_DIR) + QStringLiteral("/hello.lua");
		QVERIFY2(QFileInfo::exists(path), qPrintable(path));

		const ControlResult result = registry->invoke(QStringLiteral("script.run"),
			QJsonObject{{QStringLiteral("path"), path}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("ran")).toBool(), true);
		QCOMPARE(result.result.value(QStringLiteral("path")).toString(), path);

		const QJsonArray log = result.result.value(QStringLiteral("log")).toArray();
		QVERIFY2(log.size() >= 3, qPrintable(QStringLiteral("log has %1 line(s)").arg(log.size())));
		QCOMPARE(result.result.value(QStringLiteral("log_lines")).toInt(), static_cast<int>(log.size()));
		QString joined;
		for (const QJsonValue& line : log) { joined += line.toString() + QLatin1Char('\n'); }
		QVERIFY2(joined.contains(QStringLiteral("Hello from Lua")), qPrintable(joined));
		QVERIFY2(joined.contains(QStringLiteral("hello.lua finished")), qPrintable(joined));

		// The engine ran the script on its dedicated worker, not on this thread
		// and not in a child process.
		ScriptEngine* engine = ScriptEngine::instance();
		QVERIFY(engine->workerThread() != nullptr);
		QVERIFY(engine->workerThread() != QThread::currentThread());
		QVERIFY(!engine->lastRunFailed());
	}

	void scriptRefusesAMissingFileAndTheBudgetExhaustion()
	{
		ControlRegistry* registry = ControlRegistry::instance();

		const ControlResult missing = registry->invoke(QStringLiteral("script.run"),
			QJsonObject{{QStringLiteral("path"), QStringLiteral("/nonexistent/no-such.lua")}});
		QVERIFY(!missing.ok);
		QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);

		const ControlResult both = registry->invoke(QStringLiteral("script.run"),
			QJsonObject{{QStringLiteral("path"), QStringLiteral("a.lua")},
				{QStringLiteral("source"), QStringLiteral("return 1")}});
		QVERIFY(!both.ok);
		QCOMPARE(both.errorKind, ControlErrorKind::InvalidArgs);

		const ControlResult neither = registry->invoke(QStringLiteral("script.run"));
		QVERIFY(!neither.ok);
		QCOMPARE(neither.errorKind, ControlErrorKind::InvalidArgs);

		// A runaway script is stopped by the engine's own instruction hook, and
		// the refusal names the budget rather than swallowing it.
		const ControlResult runaway = registry->invoke(QStringLiteral("script.run"),
			QJsonObject{{QStringLiteral("source"),
					QStringLiteral("lmms.log():info('runaway')\nwhile true do end\n")},
				{QStringLiteral("budget"), 20000}});
		QVERIFY2(!runaway.ok, "the runaway script was not stopped");
		QCOMPARE(runaway.errorKind, ControlErrorKind::Refused);
		QVERIFY2(runaway.errorMessage.contains(QStringLiteral("instruction budget exceeded")),
			qPrintable(runaway.errorMessage));
		// The log line the script did write travels with the refusal.
		QVERIFY2(runaway.errorMessage.contains(QStringLiteral("runaway")),
			qPrintable(runaway.errorMessage));
	}

	//! script.run is mutating, and records the only honest answer: a Lua script's
	//! edits have no registry inverse (SPEC A16).
	void scriptRunRecordsAnHonestTransaction()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult result = registry->invoke(QStringLiteral("script.run"),
			QJsonObject{{QStringLiteral("source"), QStringLiteral("lmms.log():info('tx')\n")}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("instruction_budget")).toInt(),
			static_cast<int>(ScriptEngine::instance()->instructionBudget()));

		bool found = false;
		for (const QJsonValue& value : registry->transactions())
		{
			const QJsonObject transaction = value.toObject();
			if (transaction.value(QStringLiteral("command")).toString() != QStringLiteral("script.run"))
			{
				continue;
			}
			found = true;
			QCOMPARE(transaction.value(QStringLiteral("reversible")).toBool(), false);
			QVERIFY(transaction.value(QStringLiteral("mechanism")).toString()
				.contains(QStringLiteral("script")));
		}
		QVERIFY2(found, "script.run recorded no transaction");
	}

private:
	static QString qstr(const char* text) { return QString::fromLatin1(text); }
};

QTEST_GUILESS_MAIN(ControlAutomationScriptTest)
#include "ControlAutomationScriptTest.moc"
