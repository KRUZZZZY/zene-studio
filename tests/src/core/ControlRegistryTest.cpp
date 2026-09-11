/*
 * ControlRegistryTest.cpp - unit tests for the in-app command registry
 *                           (SPEC-zene-studio.md A11-A16).
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

#include "ControlRegistry.h"
#include "Engine.h"

using namespace lmms;

class ControlRegistryTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		// The registry only dispatches handlers once the application says the
		// model is fully up; the unit test sets that flag itself.
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Runs after every slot, so a slot that flips the readiness flag (and fails
	//! before flipping it back) cannot poison the ones after it.
	void cleanup()
	{
		ControlRegistry::setReady(true);
		ControlRegistry::setQuitPromptAnswer(ControlRegistry::QuitPromptAnswer::Ask);
	}

	//! Every command of the first slice is registered under its stable group.verb id.
	void requiredCommandsAreRegistered()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList required = {
			QStringLiteral("control.ping"),
			QStringLiteral("control.version"),
			QStringLiteral("control.commands_list"),
			QStringLiteral("transport.play"),
			QStringLiteral("transport.stop"),
			QStringLiteral("transport.seek"),
			QStringLiteral("transport.set_tempo"),
			QStringLiteral("transport.get_state"),
			QStringLiteral("track.list"),
			QStringLiteral("track.get_state"),
			QStringLiteral("mixer.get_state"),
			QStringLiteral("mixer.set_volume"),
			QStringLiteral("mixer.set_pan"),
			QStringLiteral("mixer.add_channel"),
			QStringLiteral("mixer.remove_channel"),
			QStringLiteral("project.open"),
			QStringLiteral("project.save"),
			QStringLiteral("project.get_state"),
			QStringLiteral("render.render"),
		};
		for (const QString& id : required)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing command %1").arg(id)));
		}
	}

	//! The registry reports its own surface with schemas and requires declarations.
	void describeAllCarriesSchemasAndRequires()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QJsonObject described = registry->describeAll();
		QCOMPARE(described.value(QStringLiteral("count")).toInt(), registry->commandCount());
		QCOMPARE(described.value(QStringLiteral("proto")).toInt(), 1);

		const QJsonArray commands = described.value(QStringLiteral("commands")).toArray();
		bool sawPing = false;
		for (const QJsonValue& value : commands)
		{
			const QJsonObject entry = value.toObject();
			const QString id = entry.value(QStringLiteral("id")).toString();
			QVERIFY(!id.isEmpty());
			QVERIFY(entry.contains(QStringLiteral("args_schema")));
			QVERIFY(entry.contains(QStringLiteral("result_schema")));
			QVERIFY(entry.contains(QStringLiteral("requires")));
			if (id == QLatin1String("control.ping")) { sawPing = true; }
		}
		QVERIFY(sawPing);
	}

	//! An unknown command id is the typed not_found error, never an empty success.
	void unknownCommandIsNotFound()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult result = registry->invoke(QStringLiteral("does.not_exist"));
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::NotFound);
		QCOMPARE(controlErrorKindName(result.errorKind), QStringLiteral("not_found"));
		QVERIFY(!result.errorMessage.isEmpty());
	}

	void missingRequiredArgumentIsInvalidArgs()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult result = registry->invoke(QStringLiteral("transport.seek"));
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(controlErrorKindName(result.errorKind), QStringLiteral("invalid_args"));
	}

	void wrongArgumentTypeIsInvalidArgs()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QJsonObject args;
		args.insert(QStringLiteral("ticks"), QStringLiteral("not a number"));
		const ControlResult result = registry->invoke(QStringLiteral("transport.seek"), args);
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	}

	void outOfRangeArgumentIsInvalidArgs()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QJsonObject args;
		args.insert(QStringLiteral("bpm"), 100000);
		const ControlResult result = registry->invoke(QStringLiteral("transport.set_tempo"), args);
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	}

	void unexpectedArgumentIsInvalidArgs()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QJsonObject args;
		args.insert(QStringLiteral("oops"), 1);
		const ControlResult result = registry->invoke(QStringLiteral("transport.get_state"), args);
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	}

	void malformedTrackIdIsInvalidArgs()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QJsonObject args;
		args.insert(QStringLiteral("track"), QStringLiteral("track-one"));
		const ControlResult result = registry->invoke(QStringLiteral("track.get_state"), args);
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! A command that needs a human is refused, typed, with a reason.
	void humanRequirementIsRefused()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ControlCommand cmd;
		cmd.id = QStringLiteral("test.needs_human");
		cmd.group = QStringLiteral("test");
		cmd.verb = QStringLiteral("needs_human");
		cmd.requiresDecl = {QStringLiteral("human")};
		cmd.handler = [](const QJsonObject&) { return ControlResult::success(); };
		registry->registerCommand(cmd);

		const ControlResult result = registry->invoke(QStringLiteral("test.needs_human"));
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::Requires);
		QCOMPARE(controlErrorKindName(result.errorKind), QStringLiteral("requires"));
	}

	//! A display requirement is honoured in a headless instance and clears once
	//! the same command is asked on a display-ful instance.
	void displayRequirementFollowsHeadlessState()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ControlCommand cmd;
		cmd.id = QStringLiteral("test.needs_display");
		cmd.group = QStringLiteral("test");
		cmd.verb = QStringLiteral("needs_display");
		cmd.requiresDecl = {QStringLiteral("display")};
		cmd.handler = [](const QJsonObject&) { return ControlResult::success(QJsonObject{{QStringLiteral("ran"), true}}); };
		registry->registerCommand(cmd);

		registry->setHeadless(true);
		const ControlResult headless = registry->invoke(QStringLiteral("test.needs_display"));
		QVERIFY(!headless.ok);
		QCOMPARE(headless.errorKind, ControlErrorKind::Requires);

		registry->setHeadless(false);
		const ControlResult onDisplay = registry->invoke(QStringLiteral("test.needs_display"));
		QVERIFY(onDisplay.ok);
		QVERIFY(onDisplay.result.value(QStringLiteral("ran")).toBool());
	}

	//! A schema-valid command runs its handler and returns the handler's result.
	void validCommandRunsHandler()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ControlCommand cmd;
		cmd.id = QStringLiteral("test.echo");
		cmd.group = QStringLiteral("test");
		cmd.verb = QStringLiteral("echo");
		cmd.argsSchema = QJsonObject{
			{QStringLiteral("type"), QStringLiteral("object")},
			{QStringLiteral("properties"),
				QJsonObject{{QStringLiteral("value"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
			{QStringLiteral("required"), QJsonArray{QStringLiteral("value")}},
			{QStringLiteral("additionalProperties"), false},
		};
		cmd.handler = [](const QJsonObject& args) {
			return ControlResult::success(QJsonObject{{QStringLiteral("echoed"), args.value(QStringLiteral("value")).toString()}});
		};
		registry->registerCommand(cmd);

		QJsonObject args;
		args.insert(QStringLiteral("value"), QStringLiteral("hello"));
		const ControlResult result = registry->invoke(QStringLiteral("test.echo"), args);
		QVERIFY(result.ok);
		QCOMPARE(result.result.value(QStringLiteral("echoed")).toString(), QStringLiteral("hello"));
	}

	//! The quit prompt answer defaults to Ask: the GUI keeps asking a human, and
	//! only a control-surface quit (control.quit) states an intent instead (#626).
	void quitPromptAnswerDefaultsToAskingAHuman()
	{
		QCOMPARE(ControlRegistry::quitPromptAnswer(), ControlRegistry::QuitPromptAnswer::Ask);
		QVERIFY(!ControlRegistry::quitPending());
		ControlRegistry::setQuitPromptAnswer(ControlRegistry::QuitPromptAnswer::Discard);
		QCOMPARE(ControlRegistry::quitPromptAnswer(), ControlRegistry::QuitPromptAnswer::Discard);
		ControlRegistry::setQuitPromptAnswer(ControlRegistry::QuitPromptAnswer::Ask);
	}

	//! Task #626: a ping that is not ready says WHY. A bare engine_ready=false left
	//! a client with nothing to act on, which is what made the defect expensive.
	void notReadyPingCarriesAReason()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ControlRegistry::setReady(false);
		const ControlResult result = registry->invoke(QStringLiteral("control.ping"));
		QVERIFY(result.ok);
		QCOMPARE(result.result.value(QStringLiteral("engine_ready")).toBool(), false);

		const QJsonObject reason = result.result.value(QStringLiteral("reason")).toObject();
		QVERIFY2(!reason.isEmpty(), "engine_ready=false with no reason object");
		QVERIFY2(!reason.value(QStringLiteral("code")).toString().isEmpty(), "reason has no code");
		QVERIFY2(!reason.value(QStringLiteral("message")).toString().isEmpty(), "reason has no message");

		// The audio report is present whether or not the engine is ready, so a client
		// can tell "still starting up" from "your sound card never opened".
		const QJsonObject audio = result.result.value(QStringLiteral("audio")).toObject();
		QVERIFY(!audio.isEmpty());
		QVERIFY(audio.contains(QStringLiteral("state")));
		QVERIFY(audio.contains(QStringLiteral("start_failed")));

		ControlRegistry::setReady(true);
		const ControlResult ready = registry->invoke(QStringLiteral("control.ping"));
		QVERIFY(ready.ok);
		QCOMPARE(ready.result.value(QStringLiteral("engine_ready")).toBool(), true);
	}

	//! ...and an engine command issued in that window repeats the same reason.
	void engineCommandBeforeReadyCarriesTheReason()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		ControlRegistry::setReady(false);
		const ControlResult result = registry->invoke(QStringLiteral("mixer.get_state"));
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::Busy);
		const QString message = result.errorMessage;
		QVERIFY2(!message.isEmpty(), "the busy error carries no message");
		// The message must name the machine-readable reason, not just say "busy".
		QVERIFY2(message.contains(QStringLiteral("engine_starting")), qPrintable(message));
		ControlRegistry::setReady(true);
	}

	//! Every mutating command leaves a transaction behind (SPEC A16 hook).
	void mutatingCommandRecordsTransaction()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		registry->clearTransactions();

		ControlCommand cmd;
		cmd.id = QStringLiteral("test.mutator");
		cmd.group = QStringLiteral("test");
		cmd.verb = QStringLiteral("mutator");
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject&) {
			QJsonObject transaction;
			transaction.insert(QStringLiteral("before"), QJsonObject{{QStringLiteral("value"), 1}});
			transaction.insert(QStringLiteral("inverse"),
				QJsonObject{{QStringLiteral("op"), QStringLiteral("test.mutator")},
					{QStringLiteral("args"), QJsonObject{{QStringLiteral("value"), 1}}}});
			transaction.insert(QStringLiteral("reversible"), true);
			transaction.insert(QStringLiteral("mechanism"), QStringLiteral("test"));
			QJsonObject result;
			result.insert(QStringLiteral("value"), 2);
			result.insert(QStringLiteral("__transaction"), transaction);
			return ControlResult::success(result);
		};
		registry->registerCommand(cmd);

		const ControlResult result = registry->invoke(QStringLiteral("test.mutator"));
		QVERIFY(result.ok);
		// the private bookkeeping key must not leak into the wire result
		QVERIFY(!result.result.contains(QStringLiteral("__transaction")));

		const QJsonArray transactions = registry->transactions();
		QCOMPARE(transactions.size(), 1);
		const QJsonObject recorded = transactions.last().toObject();
		QCOMPARE(recorded.value(QStringLiteral("command")).toString(), QStringLiteral("test.mutator"));
		QCOMPARE(recorded.value(QStringLiteral("before")).toObject().value(QStringLiteral("value")).toInt(), 1);
		QVERIFY(recorded.value(QStringLiteral("reversible")).toBool());
	}

	//! A mutating command that records nothing is still visible as irreversible.
	void mutatingCommandWithoutRecordStillLeavesATransaction()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		registry->clearTransactions();

		ControlCommand cmd;
		cmd.id = QStringLiteral("test.silent_mutator");
		cmd.group = QStringLiteral("test");
		cmd.verb = QStringLiteral("silent_mutator");
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject&) { return ControlResult::success(QJsonObject{{QStringLiteral("done"), true}}); };
		registry->registerCommand(cmd);

		QVERIFY(registry->invoke(QStringLiteral("test.silent_mutator")).ok);
		const QJsonArray transactions = registry->transactions();
		QCOMPARE(transactions.size(), 1);
		QVERIFY(!transactions.last().toObject().value(QStringLiteral("reversible")).toBool());
	}

	//! The undo/redo commands share the engine's own ProjectJournal.
	void undoRedoUseTheProjectJournal()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult undo = registry->invoke(QStringLiteral("control.undo"));
		QVERIFY(undo.ok);
		QCOMPARE(undo.result.value(QStringLiteral("mechanism")).toString(), QStringLiteral("lmms::ProjectJournal"));

		const ControlResult redo = registry->invoke(QStringLiteral("control.redo"));
		QVERIFY(redo.ok);
	}

	//! Stable string ids are formatted and parsed by one place.
	void stableIdHelpers()
	{
		QCOMPARE(control::trackId(0), QStringLiteral("trk-0"));
		QCOMPARE(control::trackId(12), QStringLiteral("trk-12"));
		QCOMPARE(control::channelId(3), QStringLiteral("ch-3"));
		QCOMPARE(control::idToIndex(QStringLiteral("trk-7"), QStringLiteral("trk-")), 7);
		QCOMPARE(control::idToIndex(QStringLiteral("ch-0"), QStringLiteral("ch-")), 0);
		QCOMPARE(control::idToIndex(QStringLiteral("ch-x"), QStringLiteral("ch-")), -1);
		QCOMPARE(control::idToIndex(QStringLiteral("trk-"), QStringLiteral("trk-")), -1);
	}

	//! mixer.set_pan is an honest typed refusal: no pan exists on a mixer channel.
	void mixerSetPanRefusesTyped()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		QJsonObject args;
		args.insert(QStringLiteral("channel"), QStringLiteral("ch-0"));
		args.insert(QStringLiteral("pan"), 0.5);
		const ControlResult result = registry->invoke(QStringLiteral("mixer.set_pan"), args);
		QVERIFY(!result.ok);
		QCOMPARE(result.errorKind, ControlErrorKind::Refused);
		QCOMPARE(controlErrorKindName(result.errorKind), QStringLiteral("refused"));
	}
};

QTEST_GUILESS_MAIN(ControlRegistryTest)
#include "ControlRegistryTest.moc"
