/*
 * MidiCommandBindingsTest.cpp - a MIDI note or control-change runs a built-in command
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
 */

/*! Q050-217.1, stage 1: MIDI bindings for built-in commands.
 *
 *  The harness is synthetic and exact: MidiEvent values go into the surface the way the MIDI
 *  client's thread hands them over (MidiSurfaceInput::processInEvent -> decode), and the queue is
 *  drained on the test's (UI) thread with drainPending() instead of waiting for the 15 ms timer.
 *  What is counted is dispatches through ControlRegistry, heard from its own command observer (the
 *  macro recorder's hook), so "one dispatch per activation" is measured where the product
 *  dispatches and not from a side effect.
 *
 *  Criterion 1: a bound note and a bound control-change each dispatch once per activation - a press
 *  is one, the release is none, a sweep 1..127 is one, the return to 0 rearms - and the bound
 *  command runs through the registry (a handler with no arguments, which is the class of command
 *  this stage binds).
 *  Criterion 2: a binding reads back, is replaced by a second bind on the same control, is one undo
 *  step that puts the previous binding back, and the launcher's own bindings behave exactly as they
 *  did (bind, learn, unblind, dispatch through session.launch_scene). */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "LaunchBindings.h"
#include "MidiEvent.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

//! A synthetic MIDI event, built as the MIDI clients build one (see MidiClientRaw::parseData).
MidiEvent noteOn(int channel, int key, int velocity) { return MidiEvent(MidiNoteOn, channel, key, velocity); }
MidiEvent noteOff(int channel, int key) { return MidiEvent(MidiNoteOff, channel, key, 0); }
MidiEvent controlChange(int channel, int controller, int value) { return MidiEvent(MidiControlChange, channel, controller, value); }

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A command that needs no arguments, so a MIDI control can run it: it only says it ran.
class Counter
{
public:
	static void install(ControlRegistry* registry)
	{
		s_count = 0;
		if (registry->hasCommand(QStringLiteral("miditest.count"))) { return; }
		ControlCommand command;
		command.id = QStringLiteral("miditest.count");
		command.group = QStringLiteral("miditest");
		command.verb = QStringLiteral("count");
		command.description = QStringLiteral("Counts its dispatches; takes no arguments, so a MIDI control can run it.");
		command.argsSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
			{QStringLiteral("properties"), QJsonObject{}}, {QStringLiteral("required"), QJsonArray{}}};
		command.resultSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
		command.mutating = false;
		command.requiresEngine = false;
		command.handler = [](const QJsonObject&) {
			++s_count;
			return ControlResult::success({{QStringLiteral("count"), s_count}});
		};
		registry->registerCommand(command);
	}
	static int count() { return s_count; }
	static void reset() { s_count = 0; }

private:
	static inline int s_count = 0;
};

//! A command with an OPTIONAL argument, so a CC can be bound; it remembers the arguments it got.
struct Optional
{
	static inline QJsonObject seen;
};

//! The trigger objects the commands take, spelled once so the tests read as intentions.
QJsonObject noteBinding(int key, int channel, const QString& command)
{
	return {{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("key"), key},
		{QStringLiteral("channel"), channel}, {QStringLiteral("command"), command}};
}

QJsonObject controlBinding(int controller, int channel, const QString& command)
{
	return {{QStringLiteral("trigger"), QStringLiteral("cc")}, {QStringLiteral("controller"), controller},
		{QStringLiteral("channel"), channel}, {QStringLiteral("command"), command}};
}

//! The stored command binding for a note, or an empty object.
QJsonObject storedNote(int key, int channel)
{
	for (const QJsonValue& value : LaunchBindings::commandBindings())
	{
		const QJsonObject binding = value.toObject();
		if (binding.value(QStringLiteral("trigger")).toString() == QLatin1String("note")
			&& binding.value(QStringLiteral("key")).toInt(-1) == key && binding.value(QStringLiteral("channel")).toInt(-1) == channel)
		{
			return binding;
		}
	}
	return {};
}

//! The stored command binding for a control change, or an empty object.
QJsonObject storedControl(int controller, int channel)
{
	for (const QJsonValue& value : LaunchBindings::commandBindings())
	{
		const QJsonObject binding = value.toObject();
		if (binding.value(QStringLiteral("trigger")).toString() == QLatin1String("cc")
			&& binding.value(QStringLiteral("controller")).toInt(-1) == controller
			&& binding.value(QStringLiteral("channel")).toInt(-1) == channel)
		{
			return binding;
		}
	}
	return {};
}

} // namespace

class MidiCommandBindingsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		Counter::install(ControlRegistry::instance());
	}

	void cleanupTestCase()
	{
		LaunchBindings::disconnect();
		LaunchBindings::setBindings({});
		LaunchBindings::setCommandBindings({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void cleanup()
	{
		LaunchBindings::disconnect();
		LaunchBindings::setBindings({});
		LaunchBindings::setCommandBindings({});
		Counter::reset();
		// Nothing this test bound is left on the undo stack for the next one to trip over.
		while (run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool()) {}
	}

	//! Criterion 1, note half: one dispatch per press, none on the release, and the dispatch is a
	//! registry invoke (the observer hears exactly the bound command, once).
	void aBoundNoteDispatchesOncePerPress()
	{
		QVERIFY2(run(QStringLiteral("session.bind_command"), noteBinding(60, 0, QStringLiteral("miditest.count"))).ok,
			"a note could not be bound to an argument-free command");
		QCOMPARE(storedNote(60, 0).value(QStringLiteral("command")).toString(), QStringLiteral("miditest.count"));

		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);

		// One press: exactly one run of the command - the counter lives in the command's handler,
		// so it counts DISPATCHES through the registry, not side effects somewhere else.
		bindings->processInEvent(noteOn(0, 60, 100));
		bindings->drainPending();
		QCOMPARE(Counter::count(), 1);

		// Its release: a rearm, and NOT a second dispatch.
		bindings->processInEvent(noteOff(0, 60));
		bindings->drainPending();
		QCOMPARE(Counter::count(), 1);

		// A note-on at velocity 0 is a release in every MIDI source, not a press.
		bindings->processInEvent(noteOn(0, 60, 0));
		bindings->drainPending();
		QCOMPARE(Counter::count(), 1);

		// The next press is the next activation.
		bindings->processInEvent(noteOn(0, 60, 1));
		bindings->drainPending();
		QCOMPARE(Counter::count(), 2);
	}

	//! Criterion 1, control-change half: a sweep 1..127 is ONE dispatch, 0 rearms, and the CC's
	//! value is never passed on as an argument (the command takes none and still succeeds).
	void aBoundControlChangeDispatchesOncePerSweep()
	{
		QVERIFY2(run(QStringLiteral("session.bind_command"), controlBinding(74, 1, QStringLiteral("miditest.count"))).ok,
			"a control change could not be bound to an argument-free command");
		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);

		for (int value : {1, 2, 40, 127})
		{
			bindings->processInEvent(controlChange(1, 74, value));
			bindings->drainPending();
		}
		QCOMPARE(Counter::count(), 1);  // the whole sweep is one activation

		bindings->processInEvent(controlChange(1, 74, 0));  // rearm, and no dispatch
		bindings->drainPending();
		QCOMPARE(Counter::count(), 1);

		bindings->processInEvent(controlChange(1, 74, 5));  // a new sweep: the next activation
		bindings->drainPending();
		QCOMPARE(Counter::count(), 2);

		// A different controller on the same channel and a different channel are not this binding.
		bindings->processInEvent(controlChange(1, 75, 5));
		bindings->drainPending();
		bindings->processInEvent(controlChange(2, 74, 5));
		bindings->drainPending();
		QCOMPARE(Counter::count(), 2);
	}

	//! Criterion 1: the channel rule is the launcher's - an exact channel wins, -1 is any channel,
	//! and a malformed channel is skipped rather than read as "any".
	void theChannelRuleIsTheLaunchers()
	{
		LaunchBindings::setCommandBindings(QJsonArray{
			controlBinding(20, -1, QStringLiteral("miditest.count")),      // any channel
			controlBinding(21, 3, QStringLiteral("miditest.count")),       // channel 3
			QJsonObject{{QStringLiteral("trigger"), QStringLiteral("cc")}, {QStringLiteral("controller"), 22},
				{QStringLiteral("channel"), -2}, {QStringLiteral("command"), QStringLiteral("miditest.count")}},
			QJsonObject{{QStringLiteral("trigger"), QStringLiteral("cc")}, {QStringLiteral("controller"), 23},
				{QStringLiteral("command"), QStringLiteral("miditest.count")}}});
		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);
		const auto fire = [&](int channel, int controller, int value) {
			bindings->processInEvent(controlChange(channel, controller, value));
			bindings->drainPending();
			bindings->processInEvent(controlChange(channel, controller, 0));
			bindings->drainPending();
		};
		fire(9, 20, 64);
		QCOMPARE(Counter::count(), 1);  // -1: any channel
		fire(3, 21, 64);
		QCOMPARE(Counter::count(), 2);  // an exact match
		fire(4, 21, 64);
		QCOMPARE(Counter::count(), 2);  // a different channel: not this binding
		fire(3, 22, 64);
		QCOMPARE(Counter::count(), 2);  // channel -2: malformed, skipped
		fire(3, 23, 64);
		QCOMPARE(Counter::count(), 2);  // no channel field: malformed, not any channel
	}

	//! Criterion 1's guard: a command that requires an argument cannot be bound, because an
	//! activation supplies none (the same rule window.header_set applies to a header button).
	void aCommandThatNeedsArgumentsIsRefused()
	{
		const ControlResult refused = run(QStringLiteral("session.bind_command"),
			noteBinding(61, 0, QStringLiteral("session.launch_scene")));
		QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("needs arguments")), qPrintable(refused.errorMessage));
		QVERIFY(LaunchBindings::commandBindings().isEmpty());

		QCOMPARE(run(QStringLiteral("session.bind_command"), noteBinding(61, 0, QStringLiteral("no.such_command"))).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("session.bind_command"),
			QJsonObject{{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("command"), QStringLiteral("miditest.count")}}).errorKind,
			ControlErrorKind::InvalidArgs);  // a note binding needs its key
		QVERIFY(LaunchBindings::commandBindings().isEmpty());
	}

	//! Criterion 1: CC values are NOT arguments. A command whose schema takes an optional argument
	//! is bindable and runs with NO argument at all - the sweep's value is not smuggled in.
	void theControlChangeValueIsNotPassedAsAnArgument()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		Optional::seen = QJsonObject();
		ControlCommand command;
		command.id = QStringLiteral("miditest.optional");
		command.group = QStringLiteral("miditest");
		command.verb = QStringLiteral("optional");
		command.description = QStringLiteral("Takes an OPTIONAL argument and records what it was given.");
		command.argsSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
			{QStringLiteral("properties"), QJsonObject{{QStringLiteral("value"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}}},
			{QStringLiteral("required"), QJsonArray{}}};
		command.resultSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
		command.mutating = false;
		command.requiresEngine = false;
		// A static target, not a lambda over a local: the command stays registered for the rest of
		// the run, so a captured local would dangle the moment this slot returned.
		command.handler = [](const QJsonObject& args) { Optional::seen = args; return ControlResult::success({}); };
		if (!registry->hasCommand(command.id)) { registry->registerCommand(command); }

		QVERIFY(run(QStringLiteral("session.bind_command"), controlBinding(30, 0, QStringLiteral("miditest.optional"))).ok);
		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);
		bindings->processInEvent(controlChange(0, 30, 99));
		bindings->drainPending();
		QVERIFY2(Optional::seen.isEmpty(), qPrintable(QStringLiteral("the CC value reached the command: %1")
			.arg(QString::fromUtf8(QJsonDocument(Optional::seen).toJson(QJsonDocument::Compact)))));
	}

	//! Criterion 2: a binding reads back, a second bind replaces it in place, and one undo step
	//! restores the binding the replacement displaced.
	void aBindingReadsBackIsReplacedAndUndoes()
	{
		QVERIFY(run(QStringLiteral("session.bind_command"), controlBinding(74, 0, QStringLiteral("miditest.count"))).ok);

		const ControlResult listed = run(QStringLiteral("session.command_bindings"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		const QJsonArray read = listed.result.value(QStringLiteral("bindings")).toArray();
		QCOMPARE(read.size(), 1);
		QCOMPARE(read.first().toObject().value(QStringLiteral("controller")).toInt(), 74);
		QCOMPARE(read.first().toObject().value(QStringLiteral("channel")).toInt(), 0);
		QCOMPARE(read.first().toObject().value(QStringLiteral("command")).toString(), QStringLiteral("miditest.count"));

		// Replace: the second bind on the same control leaves ONE binding naming the new command.
		QVERIFY(run(QStringLiteral("session.bind_command"), controlBinding(74, 0, QStringLiteral("transport.stop"))).ok);
		QCOMPARE(LaunchBindings::commandBindings().size(), 1);
		QCOMPARE(storedControl(74, 0).value(QStringLiteral("command")).toString(), QStringLiteral("transport.stop"));

		// One undo step: the FIRST binding is back (and only one step was taken for the replacement).
		QVERIFY2(run(QStringLiteral("control.undo")).ok, "the replacement had no undo step");
		QCOMPARE(LaunchBindings::commandBindings().size(), 1);
		QCOMPARE(storedControl(74, 0).value(QStringLiteral("command")).toString(), QStringLiteral("miditest.count"));

		// The undo of the ORIGINAL bind removes it.
		QVERIFY2(run(QStringLiteral("control.undo")).ok, "the bind had no undo step");
		QVERIFY(LaunchBindings::commandBindings().isEmpty());
	}

	//! Criterion 2: unbind removes just that control, is one undo step, and a missing binding is a
	//! typed NotFound rather than a silent success.
	void unbindRemovesOneAndUndoes()
	{
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(62, 0, QStringLiteral("miditest.count"))).ok);
		QVERIFY(run(QStringLiteral("session.bind_command"), controlBinding(75, 0, QStringLiteral("miditest.count"))).ok);
		QCOMPARE(LaunchBindings::commandBindings().size(), 2);

		QVERIFY(run(QStringLiteral("session.unbind_command"),
			QJsonObject{{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("key"), 62}, {QStringLiteral("channel"), 0}}).ok);
		QCOMPARE(LaunchBindings::commandBindings().size(), 1);
		QVERIFY(!storedControl(75, 0).isEmpty());
		QVERIFY(storedNote(62, 0).isEmpty());

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!storedNote(62, 0).isEmpty());  // the removal came back, alone

		QCOMPARE(run(QStringLiteral("session.unbind_command"),
			QJsonObject{{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("key"), 99}, {QStringLiteral("channel"), 0}}).errorKind,
			ControlErrorKind::NotFound);
	}

	//! Criterion 2: the launcher's bindings still work, unchanged - written, read, dispatched and
	//! undone by the commands that always did it - and a command binding on the same note takes the
	//! note over in ONE edit that undo puts back.
	void theLaunchersBindingsStillWork()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(Track::create(Track::Type::Instrument, song) != nullptr);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 2}}).ok);
		for (int scene = 0; scene < 2; ++scene)
		{
			QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), scene},
				{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 0},
				{QStringLiteral("mode"), QStringLiteral("gate")}, {QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		}

		// The launcher's own store and commands: unchanged.
		QVERIFY(run(QStringLiteral("session.bind"), {{QStringLiteral("key"), 60}, {QStringLiteral("action"), QStringLiteral("scene")},
			{QStringLiteral("scene"), 1}}).ok);
		QCOMPARE(LaunchBindings::bindings().size(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(LaunchBindings::bindings().isEmpty());
		QVERIFY(run(QStringLiteral("session.bind"), {{QStringLiteral("key"), 60}, {QStringLiteral("action"), QStringLiteral("scene")},
			{QStringLiteral("scene"), 1}}).ok);

		// It still launches through the registry when its note is pressed.
		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);
		bindings->applyNote(LaunchNote{0, 60, true});
		QTest::qWait(150);  // the scheduler's launch is asynchronous
		const ControlResult state = run(QStringLiteral("session.get_state"));
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		bindings->processInEvent(noteOff(0, 60));
		bindings->drainPending();
		bindings->applyNote(LaunchNote{0, 60, false});
		QVERIFY(run(QStringLiteral("session.stop_all")).ok);

		// Now bind that same note to a command: ONE edit moves it, and one undo puts the launcher
		// binding back and removes the command binding - both stores, one step.
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(60, 0, QStringLiteral("miditest.count"))).ok);
		QCOMPARE(LaunchBindings::bindings().size(), 1);  // the any-channel launcher still serves other channels
		QCOMPARE(LaunchBindings::bindings().first().toObject().value(QStringLiteral("channel")).toInt(), -1);
		QCOMPARE(storedNote(60, 0).value(QStringLiteral("command")).toString(), QStringLiteral("miditest.count"));

		// The exact-channel command takes channel 0; the preserved wildcard launcher still launches on channel 1.
		bindings->processInEvent(noteOn(1, 60, 100));
		bindings->drainPending();
		QTest::qWait(150);
		const QJsonArray columns = run(QStringLiteral("session.get_state")).result.value(QStringLiteral("columns")).toArray();
		bool launcherRanOnOtherChannel = false;
		for (const QJsonValue& value : columns)
		{
			const QJsonObject column = value.toObject();
			launcherRanOnOtherChannel = launcherRanOnOtherChannel || (column.value(QStringLiteral("track")).toInt() == 0
				&& column.value(QStringLiteral("phase")).toString() == QLatin1String("playing")
				&& column.value(QStringLiteral("scene")).toInt() == 1);
		}
		QVERIFY(launcherRanOnOtherChannel);
		QCOMPARE(Counter::count(), 0);
		bindings->processInEvent(noteOff(1, 60));
		bindings->drainPending();
		bindings->processInEvent(noteOn(0, 60, 100));
		bindings->drainPending();
		QCOMPARE(Counter::count(), 1);
		bindings->processInEvent(noteOff(0, 60));
		bindings->drainPending();
		QCOMPARE(Counter::count(), 1);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(LaunchBindings::bindings().size(), 1);
		QVERIFY(LaunchBindings::commandBindings().isEmpty());

		// An exact launcher binding wins over a wildcard command on the same note; the wildcard still
		// answers on the other channels. This makes both stores obey one channel-specificity rule.
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(60, -1, QStringLiteral("miditest.count"))).ok);
		QVERIFY(run(QStringLiteral("session.bind"), {{QStringLiteral("key"), 60}, {QStringLiteral("channel"), 0},
			{QStringLiteral("action"), QStringLiteral("scene")}, {QStringLiteral("scene"), 0}}).ok);
		Counter::reset();
		bindings->processInEvent(noteOn(0, 60, 100));
		bindings->drainPending();
		QCOMPARE(Counter::count(), 0);
		QTest::qWait(150);
		bool exactLauncherRan = false;
		for (const QJsonValue& value : run(QStringLiteral("session.get_state")).result.value(QStringLiteral("columns")).toArray())
		{
			const QJsonObject column = value.toObject();
			exactLauncherRan = exactLauncherRan || (column.value(QStringLiteral("track")).toInt() == 0
				&& column.value(QStringLiteral("phase")).toString() == QLatin1String("playing")
				&& column.value(QStringLiteral("scene")).toInt() == 0);
		}
		QVERIFY(exactLauncherRan);
		bindings->processInEvent(noteOff(0, 60));
		bindings->drainPending();

		// Learning a note still works, and takes that note off the command bindings in the same edit.
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(64, 0, QStringLiteral("miditest.count"))).ok);
		QVERIFY(run(QStringLiteral("session.bind_learn"), {{QStringLiteral("action"), QStringLiteral("stop_all")}}).ok);
		bindings->applyNote(LaunchNote{0, 64, true});
		QVERIFY(storedNote(64, 0).isEmpty());
		QVERIFY(run(QStringLiteral("session.unbind"), {{QStringLiteral("key"), 64}, {QStringLiteral("channel"), 0}}).ok);
	}

	//! A note with exact-channel launcher and command bindings runs the command once and does not launch.
	void anExactCommandWinsAnEqualSpecificityLauncher()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(Track::create(Track::Type::Instrument, song) != nullptr);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 1}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 0},
			{QStringLiteral("mode"), QStringLiteral("gate")}, {QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(60, 0, QStringLiteral("miditest.count"))).ok);
		QVERIFY(run(QStringLiteral("session.bind"), {{QStringLiteral("key"), 60}, {QStringLiteral("channel"), 0},
			{QStringLiteral("action"), QStringLiteral("scene")}, {QStringLiteral("scene"), 0}}).ok);

		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);
		Counter::reset();
		bindings->processInEvent(noteOn(0, 60, 100));
		bindings->drainPending();
		QCOMPARE(Counter::count(), 1);
		QTest::qWait(150);
		const ControlResult state = run(QStringLiteral("session.get_state"));
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		for (const QJsonValue& value : state.result.value(QStringLiteral("columns")).toArray())
		{
			const QJsonObject column = value.toObject();
			QVERIFY(!(column.value(QStringLiteral("track")).toInt() == 0
				&& column.value(QStringLiteral("phase")).toString() == QLatin1String("playing")
				&& column.value(QStringLiteral("scene")).toInt() == 0));
		}
		bindings->processInEvent(noteOff(0, 60));
		bindings->drainPending();
	}

	//! Criterion 1 + A16: the trigger is not a project edit, and the commands are classified in the
	//! contract so an undo step says what it is.
	void theContractClassifiesTheNewCommands()
	{
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		const control::ReversibilityEntry* bind = table.lookup(QStringLiteral("session.bind_command"));
		QVERIFY2(bind != nullptr, "session.bind_command has no reversibility row");
		QCOMPARE(bind->cls, control::ReversibilityClass::TrueInverse);
		const control::ReversibilityEntry* unbind = table.lookup(QStringLiteral("session.unbind_command"));
		QVERIFY2(unbind != nullptr, "session.unbind_command has no reversibility row");
		QCOMPARE(unbind->cls, control::ReversibilityClass::TrueInverse);
		const control::ReversibilityEntry* listed = table.lookup(QStringLiteral("session.command_bindings"));
		QVERIFY2(listed != nullptr, "session.command_bindings has no reversibility row");
		QCOMPARE(listed->cls, control::ReversibilityClass::NotMutating);
	}
};

QTEST_MAIN(MidiCommandBindingsTest)
#include "MidiCommandBindingsTest.moc"
