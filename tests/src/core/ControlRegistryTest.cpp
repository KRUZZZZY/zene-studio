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

#include "lmmsconfig.h"
#include "ControlRegistry.h"
#ifdef ZENE_TELEMETRY_ENABLED
#include "ConfigManager.h"
#include <QJsonDocument>
#endif
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
			// plugin.* / dsp.* (post-alpha/cmd-plugins).
			QStringLiteral("plugin.list"),
			QStringLiteral("plugin.load"),
			QStringLiteral("plugin.unload"),
			QStringLiteral("plugin.bypass"),
			QStringLiteral("plugin.param_get"),
			QStringLiteral("plugin.param_set"),
			QStringLiteral("plugin.state_save"),
			QStringLiteral("plugin.state_load"),
			QStringLiteral("plugin.preset_list"),
			QStringLiteral("plugin.preset_load"),
			QStringLiteral("plugin.preset_save"),
			QStringLiteral("dsp.get_state"),
			// settings.* / audio.* / midi.* / app.* (post-alpha/cmd-plugins).
			QStringLiteral("settings.get"),
			QStringLiteral("settings.set"),
			QStringLiteral("audio.device_list"),
			QStringLiteral("audio.device_set"),
			QStringLiteral("midi.device_list"),
			QStringLiteral("app.version"),
#ifdef ZENE_TELEMETRY_ENABLED
			// telemetry.* (SPEC A15). The Help menu's "Telemetry - what we
			// send..." action declares telemetry.consent through the dynamic
			// property "controlCommand", so this command existing is what makes
			// the agent-surface gate see that menu item as reachable at all.
			// With -DZENE_TELEMETRY=OFF the client is not compiled, the group
			// is not registered, and the two ids are required to be ABSENT -
			// that half is asserted by
			// telemetryCommandsAreAbsentWhenTheClientIsCompiledOut below.
			QStringLiteral("telemetry.consent"),
			QStringLiteral("telemetry.status"),
#endif
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

	//! Q050-343: sample.generate's help names BOTH routes - the command surface an
	//! agent uses and the sample track's More menu, where the same registered id is
	//! offered as Insert Test Signal... - and rejects the stale "Socket-only / no
	//! interface reaches this verb" claim the description carried while the menu
	//! action already existed. A description is what `control.commands_list`, the
	//! MCP tool text and the CommandForm's about line all show, so this is the one
	//! place the route can drift.
	void sampleGenerateHelpNamesItsTrackMenuRoute()
	{
		const ControlCommand* command =
			ControlRegistry::instance()->command(QStringLiteral("sample.generate"));
		QVERIFY(command != nullptr);
		const QString help = command->description;
		QVERIFY2(help.contains(QStringLiteral("Insert Test Signal...")) &&
			help.contains(QStringLiteral("More")) && help.contains(QStringLiteral("sample track")),
			qPrintable(QStringLiteral("sample.generate does not name its menu route: %1").arg(help)));
		QVERIFY2(help.contains(QStringLiteral("control socket")) && help.contains(QStringLiteral("MCP")),
			qPrintable(QStringLiteral("sample.generate dropped its command-surface route: %1").arg(help)));
		QVERIFY2(!help.contains(QStringLiteral("Socket-only"), Qt::CaseInsensitive) &&
			!help.contains(QStringLiteral("no interface reaches")),
			qPrintable(QStringLiteral("sample.generate still denies an interface: %1").arg(help)));
		QVERIFY2(help.contains(QStringLiteral("NEW audio clip")),
			qPrintable(QStringLiteral("sample.generate stopped describing the generated clip: %1").arg(help)));
		QCOMPARE(command->id, QStringLiteral("sample.generate"));  // the id the snapshot carries
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

	void requiredIsCheckedWithoutAPropertiesObject()
	{
		QJsonObject schema;
		schema.insert(QStringLiteral("type"), QStringLiteral("object"));
		schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("x")});
		QVERIFY(!ControlRegistry::validateArgs(schema, QJsonObject{}).isEmpty());
		QJsonObject given;
		given.insert(QStringLiteral("x"), 1);
		QVERIFY(ControlRegistry::validateArgs(schema, given).isEmpty());
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

	//! Luna cluster 5727: a quarantine entry is matched by the scan against the
	//! CANDIDATE's absolute path (PluginFactory.cpp: `isQuarantined(file.
	//! absoluteFilePath())`), and both verbs say so in their own error text. A
	//! relative path can never match that comparison, so accepting one stores an
	//! entry that reports success and hides nothing on the next scan. It must be
	//! refused as an argument error - before the scan cache is even consulted, so
	//! the refusal does not depend on whether this process has a writable cache.
	void quarantineRefusesARelativePath()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		// ONLY the declared arguments: an extra key would be refused by the
		// schema and this case would pass without ever reaching the handler.
		const QJsonObject relative{{QStringLiteral("path"), QStringLiteral("./bad-plugin.so")}};
		const QJsonObject relativeAdd{{QStringLiteral("path"), QStringLiteral("./bad-plugin.so")},
			{QStringLiteral("reason"), QStringLiteral("a relative path")}};
		for (const auto& call : {QPair<QString, QJsonObject>(
			 QStringLiteral("plugin.scan_cache_quarantine_add"), relativeAdd),
			QPair<QString, QJsonObject>(
				QStringLiteral("plugin.scan_cache_quarantine_remove"), relative)})
		{
			const ControlResult result = registry->invoke(call.first, call.second);
			QVERIFY2(!result.ok, qPrintable(QStringLiteral("%1 accepted a relative path")
				.arg(call.first)));
			QVERIFY2(result.errorKind == ControlErrorKind::InvalidArgs,
				qPrintable(QStringLiteral("%1 refused './bad-plugin.so' as kind %2: %3")
					.arg(call.first).arg(int(result.errorKind)).arg(result.errorMessage)));
		}
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
		cmd.requiresDecl = QStringList{QStringLiteral("human")};
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
		cmd.requiresDecl = QStringList{QStringLiteral("display")};
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
		const QJsonObject overflow = audio.value(QStringLiteral("play_handle_overflow")).toObject();
		QVERIFY(overflow.contains(QStringLiteral("engine_handles_dropped")));
		QVERIFY(overflow.contains(QStringLiteral("bus_edits_dropped")));
		QVERIFY(overflow.contains(QStringLiteral("worker_buffer_overflows")));
		QVERIFY(audio.contains(QStringLiteral("slicert_state_overflow")));
		QVERIFY(audio.contains(QStringLiteral("sample_voice_overflow")));
		QVERIFY(audio.contains(QStringLiteral("sample_voice_queue_invariant_failures")));

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
					{QStringLiteral("args"), QJsonObject{{QStringLiteral("value"), 1}}},
					// The inverse is the PAIRED COMMAND, and says so: a command
					// inverse needs no journal step of its own (new-p14-81), while
					// the default ("journal") means the inverse rests on a step this
					// call recorded - and this test command records none.
					{QStringLiteral("applies"), QStringLiteral("command")}});
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

	//! A mutating command that runs another mutating command inside itself
	//! (document.open, range.consolidate, the alternative switch) records ONE
	//! transaction: the nested call is part of the outer one, exactly as the
	//! macro-recorder observer already treats it.
	void nestedMutatingCommandRecordsOneTransaction()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		registry->clearTransactions();

		ControlCommand inner;
		inner.id = QStringLiteral("test.nested_inner");
		inner.group = QStringLiteral("test");
		inner.verb = QStringLiteral("nested_inner");
		inner.mutating = true;
		inner.handler = [](const QJsonObject&) {
			QJsonObject transaction;
			transaction.insert(QStringLiteral("before"), QJsonObject{{QStringLiteral("value"), 1}});
			transaction.insert(QStringLiteral("inverse"),
				QJsonObject{{QStringLiteral("op"), QStringLiteral("test.nested_inner")},
					{QStringLiteral("args"), QJsonObject{{QStringLiteral("value"), 1}}}});
			transaction.insert(QStringLiteral("reversible"), true);
			transaction.insert(QStringLiteral("mechanism"), QStringLiteral("test"));
			return ControlResult::success(QJsonObject{{QStringLiteral("value"), 2},
				{QStringLiteral("__transaction"), transaction}});
		};
		registry->registerCommand(inner);

		ControlCommand outer;
		outer.id = QStringLiteral("test.nested_outer");
		outer.group = QStringLiteral("test");
		outer.verb = QStringLiteral("nested_outer");
		outer.mutating = true;
		outer.handler = [inner](const QJsonObject& args) {
			return ControlRegistry::instance()->invoke(inner.id, args);
		};
		registry->registerCommand(outer);

		QVERIFY(registry->invoke(outer.id).ok);
		const QJsonArray transactions = registry->transactions();
		QCOMPARE(transactions.size(), 1);
		QCOMPARE(transactions.last().toObject().value(QStringLiteral("command")).toString(), outer.id);
	}

	//! The undo/redo commands share the engine's own ProjectJournal.
	void undoRedoUseTheProjectJournal()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		// The transaction record is cleared first because control.undo is
		// contract-aware since SPEC A16: it refuses, typed, when the LAST
		// recorded command has no inverse (the test above deliberately leaves
		// test.silent_mutator, reversible:false, on top). With no record it
		// falls through to the engine's own journal, which is what this test is
		// about.
		registry->clearTransactions();

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

	//! A stable id's suffix is DECIMAL DIGITS and nothing else (Luna cluster
	//! 5033). QString::toInt() also accepts a sign and surrounding whitespace, so
	//! "clip--0" used to parse its "-0" suffix as clip 0 and a mutating command
	//! addressed through the malformed id acted on the real clip. Every suffix
	//! that is not [0-9]+ must be -1, which is what "does not exist" means.
	void malformedIdSuffixesDoNotAliasRealObjects()
	{
		QCOMPARE(control::idToIndex(QStringLiteral("clip--0"), QStringLiteral("clip-")), -1);
		QCOMPARE(control::idToIndex(QStringLiteral("clip-+0"), QStringLiteral("clip-")), -1);
		QCOMPARE(control::idToIndex(QStringLiteral("clip- 0"), QStringLiteral("clip-")), -1);
		QCOMPARE(control::idToIndex(QStringLiteral("clip-0 "), QStringLiteral("clip-")), -1);
		QCOMPARE(control::idToIndex(QStringLiteral("clip-\t0"), QStringLiteral("clip-")), -1);
		QCOMPARE(control::idToIndex(QStringLiteral("trk--1"), QStringLiteral("trk-")), -1);
		// A leading zero is still digits and still names one object, so the
		// grammar keeps accepting it rather than growing a second rule.
		QCOMPARE(control::idToIndex(QStringLiteral("clip-007"), QStringLiteral("clip-")), 7);
	}

	//! mixer.set_pan sets the channel's pan model (it was a typed refusal until
	//! MixerChannel gained m_panModel) - on a channel that EXISTS: a ch-<n> id is a
	//! constructed id, not an index (SPEC-stable-ids.md slice 2). Out of range is
	//! invalid_args, the schema's own -1..+1 bound.
	void mixerSetPanSetsTheChannelPan()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const ControlResult added = registry->invoke(QStringLiteral("mixer.add_channel"));
		const ControlResult result = registry->invoke(QStringLiteral("mixer.set_pan"),
			QJsonObject{{QStringLiteral("channel"), added.result.value(QStringLiteral("channel"))},
				{QStringLiteral("pan"), 0.5}});
		QVERIFY2(added.ok && result.ok, qPrintable(added.errorMessage + result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("pan")).toDouble(), 0.5);
		QCOMPARE(result.result.value(QStringLiteral("previous_pan")).toDouble(), 0.0);
		const ControlResult wide = registry->invoke(QStringLiteral("mixer.set_pan"),
			QJsonObject{{QStringLiteral("channel"), added.result.value(QStringLiteral("channel"))},
				{QStringLiteral("pan"), 1.5}});
		QCOMPARE(controlErrorKindName(wide.errorKind), QStringLiteral("invalid_args"));
	}

#ifdef ZENE_TELEMETRY_ENABLED
	//! The telemetry command group (SPEC A15, the Help menu's "Telemetry - what
	//! we send..." action). Two properties the agent-surface gate relies on and
	//! cannot check by itself: that telemetry.consent declares `requires:
	//! human` (its tests/agent-surface-allowlist.txt entry only counts with a
	//! declared excuse) and that telemetry.status declares NONE (a command with
	//! no excuse has to be exercised by the headless sweep). The screen itself
	//! is not opened here - the registry refuses before the handler runs, which
	//! is the behaviour under test.
	void telemetryCommandsSplitConsentFromVisibility()
	{
		ControlRegistry* registry = ControlRegistry::instance();

		const ControlCommand* status = registry->command(QStringLiteral("telemetry.status"));
		QVERIFY2(status != nullptr, "telemetry.status is not registered");
		QVERIFY2(status->requiresDecl.isEmpty(),
			"telemetry.status must stay headless-safe: a declared requires is the only thing "
			"that can justify an allowlist entry, and the read-only report has no excuse");
		QVERIFY2(!status->mutating, "telemetry.status writes nothing");

		// It answers, typed, with no display and no human - the property that
		// lets the headless sweep account for it. The consent VALUES are not
		// asserted (they come from the machine's own config file); the shape is.
		const ControlResult report = registry->invoke(QStringLiteral("telemetry.status"));
		QVERIFY2(report.ok, qPrintable(report.errorMessage));
		QVERIFY2(report.result.contains(QStringLiteral("enabled")),
			"telemetry.status must report whether consent is on");
		QVERIFY2(report.result.contains(QStringLiteral("payload_json")),
			"telemetry.status must report the payload that would be sent");

		const ControlCommand* consent = registry->command(QStringLiteral("telemetry.consent"));
		QVERIFY2(consent != nullptr, "telemetry.consent is not registered");
		QVERIFY2(consent->requiresDecl.contains(QStringLiteral("human")),
			"telemetry.consent opens a modal screen: it must declare `requires: human`, because "
			"an agent must never be able to consent on the user's behalf");
		QVERIFY2(!consent->mutating, "telemetry.consent records no transaction");

		// The registry refuses it for every automated caller, before the handler,
		// so an unattended run can never block on the consent screen.
		const ControlResult refused = registry->invoke(QStringLiteral("telemetry.consent"));
		QVERIFY2(!refused.ok, "an automated caller reached the consent screen");
		QCOMPARE(refused.errorKind, ControlErrorKind::Requires);
		QCOMPARE(controlErrorKindName(refused.errorKind), QStringLiteral("requires"));
	}

	//! The read-only command may report the configured origin, but never any
	//! credential-bearing URL component.
	void telemetryStatusRedactsConfiguredEndpointCredentials()
	{
		ConfigManager* config = ConfigManager::inst();
		const QString originalEndpoint = config->value(QStringLiteral("telemetry"),
			QStringLiteral("endpoint"), QString());
		const QString configuredEndpoint = QStringLiteral(
			"https://alice:password-secret@ingest.example.org:8443/internal/path-secret"
			"?token=query-secret#fragment-secret");
		config->setValue(QStringLiteral("telemetry"), QStringLiteral("endpoint"), configuredEndpoint);

		const ControlResult report = ControlRegistry::instance()->invoke(
			QStringLiteral("telemetry.status"));
		config->setValue(QStringLiteral("telemetry"), QStringLiteral("endpoint"), originalEndpoint);

		QVERIFY2(report.ok, qPrintable(report.errorMessage));
		const QString response = QString::fromUtf8(
			QJsonDocument(report.result).toJson(QJsonDocument::Compact));
		QVERIFY2(!response.contains(QStringLiteral("alice")) &&
			!response.contains(QStringLiteral("password-secret")) &&
			!response.contains(QStringLiteral("path-secret")) &&
			!response.contains(QStringLiteral("query-secret")) &&
			!response.contains(QStringLiteral("fragment-secret")),
			"telemetry.status exposed a credential-bearing endpoint");
		QCOMPARE(report.result.value(QStringLiteral("transport_endpoint")).toString(),
			QStringLiteral("https://ingest.example.org:8443"));
	}

#else
	//! The same contract from the other side: with the packager kill switch off
	//! the client is not compiled, so the group must not be in the registry at
	//! all - 72 commands, not 74, and no telemetry.* id. An id that could only
	//! answer "not in this build" advertises a feature the binary does not
	//! contain (docs/TELEMETRY-KILL-SWITCH.md).
	void telemetryCommandsAreAbsentWhenTheClientIsCompiledOut()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		for (const QString& id : registry->commandIds())
		{
			QVERIFY2(!id.startsWith(QStringLiteral("telemetry.")), qPrintable(id));
		}
		// MEASUREMENT (Q050-156, round 6): the whole registry of a telemetry-OFF
		// build of the Q050-156 branch (`-DZENE_TELEMETRY=OFF`, ControlRegistryTest
		// run from its build tree) reported 842 commands while its live snapshot
		// held 837 ids. Integrated onto 050/m0-train-16 the live snapshot holds 853
		// ids (16 more train ids, none of them telemetry.*), so the same build
		// reports 842 + 16 = 858; the 4 record.sampler_capture* ids are in it. The
		// older hand-summed figure (85 + 7 + 5 + 5 + 5 + 3 + 3 + 7 + 4 = 124) had
		// stopped matching long before. A mismatch at the tip = finding: the merge
		// train must re-measure.
		QCOMPARE(registry->commandCount(), 858);
	}
#endif
};

QTEST_GUILESS_MAIN(ControlRegistryTest)
#include "ControlRegistryTest.moc"
