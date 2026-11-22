/*
 * MidiWorkflowBindingsTest.cpp - a MIDI note or control-change runs a SAVED WORKFLOW
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
 * Boston, MA 02110-1301, USA.
 *
 */

/*! Q050-217.2, stage 2: MIDI bindings for saved workflows.
 *
 *  The harness is stage 1's, unchanged: synthetic MidiEvent values go into the surface the way the
 *  MIDI client's thread hands them over (MidiSurfaceInput::processInEvent -> decode), the queue is
 *  drained on this (UI) thread with drainPending() instead of waiting for the 15 ms timer, and the
 *  workflow's own effect is what a run is measured by - the tracks its steps add, counted from the
 *  project, and workflow.status's outcome of the run the binding queued.
 *
 *  Criterion 1: a note and a control-change bound to a saved workflow each run it - once per
 *  activation, through workflow.run, so the steps apply and the selected workflow completes. The
 *  first activation of each kind proves note-off / CC 0 rearms without firing.
 *  Criterion 2: the binding reads back with its target named, survives a re-read of the config (a
 *  restart), and keeps resolving the workflow from the library IN USE: a workflow kept with the
 *  project runs from the project's library, and switching scope back and forth answers with
 *  whichever library now holds it. A binding whose workflow is gone does nothing rather than
 *  dispatching a stale name. */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "LaunchBindings.h"
#include "MidiEvent.h"
#include "ProjectWorkspace.h"
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

QJsonObject step(const char* command, const QJsonObject& args = QJsonObject())
{
	return {{QStringLiteral("command"), QLatin1String(command)}, {QStringLiteral("args"), args}};
}

//! Save workflow @a name adding @a count tracks, so a completed run is countable from the project.
bool saveAddingWorkflow(const QString& name, int count)
{
	QJsonArray steps;
	for (int i = 0; i < count; ++i)
	{
		steps.append(step("track.add", {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), QStringLiteral("%1 %2").arg(name).arg(i + 1)}}));
	}
	return run(QStringLiteral("workflow.save"), {{QStringLiteral("name"), name}, {QStringLiteral("steps"), steps}}).ok;
}

int trackCount() { return static_cast<int>(Engine::getSong()->tracks().size()); }

//! Wait (bounded) for the workflow run the binding queued to say it is done; answers its status.
QJsonObject waitForRun()
{
	QJsonObject status;
	for (int attempt = 0; attempt < 100; ++attempt)
	{
		status = run(QStringLiteral("workflow.status")).result;
		if (status.value(QStringLiteral("state")).toString() == QLatin1String("done")) { break; }
		QTest::qWait(20);
	}
	return status;
}

QJsonObject noteBinding(const QString& workflow, int key, int channel)
{
	return {{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("key"), key},
		{QStringLiteral("channel"), channel}, {QStringLiteral("workflow"), workflow}};
}

QJsonObject controlBinding(const QString& workflow, int controller, int channel)
{
	return {{QStringLiteral("trigger"), QStringLiteral("cc")}, {QStringLiteral("controller"), controller},
		{QStringLiteral("channel"), channel}, {QStringLiteral("workflow"), workflow}};
}

//! The stored binding for a note, or an empty object.
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

//! The stored binding for a control change, or an empty object.
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

class MidiWorkflowBindingsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_config.isValid());
		// A throwaway config: the binding store lives in it, and the shared workflow library does too.
		// ConfigManager::loadConfigFile(path) is the product's own --config override; without it a
		// development build would read and rewrite the developer's .zenestudio.xml.
		qputenv("HOME", m_config.path().toLocal8Bit());
		ConfigManager* config = ConfigManager::inst();
		config->loadConfigFile(m_config.filePath(QStringLiteral("zene.xml")));
		config->setWorkingDir(m_config.filePath(QStringLiteral("work")));
		config->createWorkingDir();
		Engine::init(true);
		ControlRegistry::setReady(true);
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
		// Nothing this test saves or binds is left for the next one: the shared library is emptied,
		// the project's library cleared and the scope put back to shared.
		for (const QJsonValue& value : run(QStringLiteral("workflow.list")).result.value(QStringLiteral("workflows")).toArray())
		{
			run(QStringLiteral("workflow.remove"), {{QStringLiteral("name"), value.toObject().value(QStringLiteral("name"))}});
		}
		run(QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), QStringLiteral("shared")}});
		ProjectWorkspace::instance().clear();
		while (run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool()) {}
	}

	//! Criterion 1, note half: a note bound to a saved workflow runs that workflow - once - and the
	//! run completes; its release rearms and fires nothing.
	void aBoundNoteRunsTheSavedWorkflow()
	{
		QVERIFY(saveAddingWorkflow(QStringLiteral("add two"), 2));
		QVERIFY2(run(QStringLiteral("session.bind_command"), noteBinding(QStringLiteral("add two"), 60, 0)).ok,
			"a note could not be bound to a saved workflow");
		const QJsonObject stored = storedNote(60, 0);
		QCOMPARE(stored.value(QStringLiteral("workflow")).toString(), QStringLiteral("add two"));
		QVERIFY2(!stored.contains(QStringLiteral("command")), "a workflow binding carries no command id");

		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);
		const int before = trackCount();

		// ONE press: the workflow is queued, acknowledged, then its steps run on the event loop.
		bindings->processInEvent(noteOn(0, 60, 100));
		bindings->drainPending();
		const QJsonObject status = waitForRun();
		QCOMPARE(status.value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(status.value(QStringLiteral("name")).toString(), QStringLiteral("add two"));
		QCOMPARE(status.value(QStringLiteral("stopped_at")).toInt(), -1);
		QCOMPARE(status.value(QStringLiteral("total")).toInt(), 2);
		QCOMPARE(trackCount(), before + 2);  // the selected workflow actually completed

		// Its release: a rearm, NOT a second run.
		bindings->processInEvent(noteOff(0, 60));
		bindings->drainPending();
		QTest::qWait(50);
		QCOMPARE(trackCount(), before + 2);
		QCOMPARE(run(QStringLiteral("workflow.status")).result.value(QStringLiteral("total")).toInt(), 2);

		// A note-on at velocity 0 is a release in every MIDI source, not a press.
		bindings->processInEvent(noteOn(0, 60, 0));
		bindings->drainPending();
		QTest::qWait(50);
		QCOMPARE(trackCount(), before + 2);

		// The next press is the next run of the same workflow.
		bindings->processInEvent(noteOn(0, 60, 100));
		bindings->drainPending();
		QCOMPARE(waitForRun().value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(trackCount(), before + 4);
		bindings->processInEvent(noteOff(0, 60));
		bindings->drainPending();
	}

	//! Criterion 1, control-change half: a CC bound to a saved workflow runs it once as the knob
	//! leaves zero, 0 rearms without firing, and the CC value is not passed on as an argument.
	void aBoundControlChangeRunsTheSavedWorkflowOncePerSweep()
	{
		QVERIFY(saveAddingWorkflow(QStringLiteral("add one"), 1));
		QVERIFY2(run(QStringLiteral("session.bind_command"), controlBinding(QStringLiteral("add one"), 74, 1)).ok,
			"a control change could not be bound to a saved workflow");
		QCOMPARE(storedControl(74, 1).value(QStringLiteral("workflow")).toString(), QStringLiteral("add one"));

		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);
		const int before = trackCount();

		// A sweep 1..127 is ONE activation: one run, and the run's steps are its own commands.
		for (int value : {1, 2, 40, 127})
		{
			bindings->processInEvent(controlChange(1, 74, value));
			bindings->drainPending();
		}
		QCOMPARE(waitForRun().value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(trackCount(), before + 1);

		// Back to zero: rearm, no run.
		bindings->processInEvent(controlChange(1, 74, 0));
		bindings->drainPending();
		QTest::qWait(50);
		QCOMPARE(trackCount(), before + 1);

		// The next sweep is the next run. A workflow's steps take no arguments from the CC, so the
		// run is the SAVED one - its total proves it was the workflow, not a stray command.
		bindings->processInEvent(controlChange(1, 74, 9));
		bindings->drainPending();
		const QJsonObject status = waitForRun();
		QCOMPARE(status.value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(status.value(QStringLiteral("name")).toString(), QStringLiteral("add one"));
		QCOMPARE(status.value(QStringLiteral("total")).toInt(), 1);
		QCOMPARE(trackCount(), before + 2);

		// Another controller, another channel: not this binding.
		bindings->processInEvent(controlChange(1, 75, 9));
		bindings->drainPending();
		bindings->processInEvent(controlChange(2, 74, 9));
		bindings->drainPending();
		QTest::qWait(50);
		QCOMPARE(trackCount(), before + 2);
	}

	//! Criterion 1: the note/CC rules are stage 1's unchanged, now for both target kinds - channel
	//! specificity, a command and a workflow alike, and a note moving between them in one undo step.
	void aWorkflowBindingObeysTheSameRulesAsACommand()
	{
		QVERIFY(saveAddingWorkflow(QStringLiteral("scoped"), 1));
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(QStringLiteral("scoped"), 70, 3)).ok);
		QCOMPARE(storedNote(70, 3).value(QStringLiteral("workflow")).toString(), QStringLiteral("scoped"));
		QVERIFY(storedNote(70, -1).isEmpty());  // an exact channel, not a wildcard

		// The exact channel answers; a different channel does not.
		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);
		const int before = trackCount();
		bindings->processInEvent(noteOn(4, 70, 100));
		bindings->drainPending();
		QTest::qWait(50);
		QCOMPARE(trackCount(), before);
		bindings->processInEvent(noteOff(4, 70));
		bindings->drainPending();
		bindings->processInEvent(noteOn(3, 70, 100));
		bindings->drainPending();
		QCOMPARE(waitForRun().value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(trackCount(), before + 1);
		bindings->processInEvent(noteOff(3, 70));
		bindings->drainPending();

		// A command on the same note at the same channel replaces the workflow in ONE step, and one
		// undo puts the workflow binding back - the store and its ordering are stage 1's.
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(QStringLiteral("scoped"), 70, 3)).ok);
		QVERIFY(run(QStringLiteral("session.bind_command"),
			{{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("key"), 70},
				{QStringLiteral("channel"), 3}, {QStringLiteral("command"), QStringLiteral("transport.stop")}}).ok);
		QCOMPARE(storedNote(70, 3).value(QStringLiteral("command")).toString(), QStringLiteral("transport.stop"));
		QVERIFY2(storedNote(70, 3).value(QStringLiteral("workflow")).isUndefined(), "the old workflow target was left beside the command");
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(storedNote(70, 3).value(QStringLiteral("workflow")).toString(), QStringLiteral("scoped"));
	}

	//! Criterion 2: the binding reads back with its target named, and the readback says whether the
	//! workflow still resolves from the library in use.
	void aBindingReadsBackWithItsTargetAndWhetherItResolves()
	{
		QVERIFY(saveAddingWorkflow(QStringLiteral("readback"), 1));
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(QStringLiteral("readback"), 61, -1)).ok);
		const ControlResult listed = run(QStringLiteral("session.command_bindings"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		const QJsonArray targets = listed.result.value(QStringLiteral("targets")).toArray();
		QCOMPARE(targets.size(), 1);
		const QJsonObject read = targets.first().toObject();
		QCOMPARE(read.value(QStringLiteral("trigger")).toString(), QStringLiteral("note"));
		QCOMPARE(read.value(QStringLiteral("key")).toInt(), 61);
		QCOMPARE(read.value(QStringLiteral("channel")).toInt(), -1);
		QCOMPARE(read.value(QStringLiteral("target")).toString(), QStringLiteral("workflow"));
		QCOMPARE(read.value(QStringLiteral("name")).toString(), QStringLiteral("readback"));
		QVERIFY2(read.value(QStringLiteral("resolved")).toBool(), "the workflow that was just saved did not resolve");
		// The library in use is named too, so a reader can tell where it came from.
		QCOMPARE(listed.result.value(QStringLiteral("workflow_scope")).toString(), QStringLiteral("shared"));
		QCOMPARE(listed.result.value(QStringLiteral("workflows")).toArray(), QJsonArray{QStringLiteral("readback")});
	}

	//! Criterion 2: the binding survives readback through the CONFIG FILE itself - the store a restart
	//! re-reads - and still names the same workflow, not a copy of its steps. The steps can change and
	//! the binding follows them, which is what "resolving the selected workflow" means.
	void theBindingSurvivesAConfigReloadAndFollowsTheWorkflow()
	{
		QVERIFY(saveAddingWorkflow(QStringLiteral("follow me"), 1));
		QVERIFY(run(QStringLiteral("session.bind_command"), controlBinding(QStringLiteral("follow me"), 80, 2)).ok);

		// A restart re-reads the same config file: write it out, empty the in-memory copy, load it.
		ConfigManager* config = ConfigManager::inst();
		config->saveConfigFile();
		const QString path = m_config.filePath(QStringLiteral("zene.xml"));
		QVERIFY2(QFile::exists(path), "the config file was not written");

		// The workflow's steps change AFTER the binding was made, and the binding still names it: the
		// stored target is the NAME, so the run is the workflow as it is now (two tracks, not the one
		// it had when the binding was made).
		QVERIFY(saveAddingWorkflow(QStringLiteral("follow me"), 2));
		LaunchBindings::disconnect();
		config->loadConfigFile(path);  // exactly what starting the application does with --config
		QCOMPARE(storedControl(80, 2).value(QStringLiteral("workflow")).toString(), QStringLiteral("follow me"));
		QVERIFY2(!storedControl(80, 2).contains(QStringLiteral("steps")), "the binding copied the workflow's steps");

		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);
		const int before = trackCount();
		bindings->processInEvent(controlChange(2, 80, 1));
		bindings->drainPending();
		const QJsonObject status = waitForRun();
		QCOMPARE(status.value(QStringLiteral("name")).toString(), QStringLiteral("follow me"));
		QCOMPARE(status.value(QStringLiteral("total")).toInt(), 2);  // the workflow as it is now
		QCOMPARE(trackCount(), before + 2);
	}

	//! Criterion 2: the workflow is resolved from the library IN USE at the moment it fires. A
	//! workflow kept with the project runs from the project's library; switching back to the shared
	//! library resolves the shared one instead - the same binding, a different selected workflow.
	void theBindingFollowsTheActiveWorkflowLibrary()
	{
		// The shared library holds "shared run"; the project's holds "project run".
		QVERIFY(saveAddingWorkflow(QStringLiteral("shared run"), 1));
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(QStringLiteral("shared run"), 62, 0)).ok);
		QVERIFY(run(QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), QStringLiteral("project")}}).ok);
		QVERIFY2(run(QStringLiteral("workflow.list")).result.value(QStringLiteral("workflows")).toArray().isEmpty(),
			"the project's library started non-empty");
		QVERIFY(saveAddingWorkflow(QStringLiteral("project run"), 3));
		QCOMPARE(run(QStringLiteral("workflow.list")).result.value(QStringLiteral("scope")).toString(), QStringLiteral("project"));

		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);
		int before = trackCount();
		// workflow.status reports the LAST run, which belongs to an earlier slot: the proof that this
		// activation ran nothing is that the answer is unchanged, not that it says "idle".
		const QJsonObject statusBefore = run(QStringLiteral("workflow.status")).result;

		// The shared library's workflow is not in the library in use: nothing runs, and nothing fails
		// into the MIDI drain - the binding is kept (the library may come back) and says so.
		bindings->processInEvent(noteOn(0, 62, 100));
		bindings->drainPending();
		QTest::qWait(50);
		QCOMPARE(trackCount(), before);
		QCOMPARE(run(QStringLiteral("workflow.status")).result, statusBefore);
		QJsonObject read = run(QStringLiteral("session.command_bindings")).result.value(QStringLiteral("targets")).toArray()
			.first().toObject();
		QVERIFY(!read.value(QStringLiteral("resolved")).toBool());
		QVERIFY2(read.value(QStringLiteral("reason")).toString().contains(QStringLiteral("shared run")),
			qPrintable(read.value(QStringLiteral("reason")).toString()));
		bindings->processInEvent(noteOff(0, 62));
		bindings->drainPending();

		// A workflow in the PROJECT's library under the SAME name: now the binding resolves to it, so
		// the selected workflow is the project's, not a name frozen at bind time.
		QVERIFY(saveAddingWorkflow(QStringLiteral("shared run"), 2));  // the project's own "shared run"
		bindings->processInEvent(noteOn(0, 62, 100));
		bindings->drainPending();
		QJsonObject status = waitForRun();
		QCOMPARE(status.value(QStringLiteral("name")).toString(), QStringLiteral("shared run"));
		QCOMPARE(status.value(QStringLiteral("total")).toInt(), 2);  // the project's version
		before = trackCount();
		bindings->processInEvent(noteOff(0, 62));
		bindings->drainPending();

		// Back to the shared library: the binding resolves the SHARED one again (its one step).
		QVERIFY(run(QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), QStringLiteral("shared")}}).ok);
		bindings->processInEvent(noteOn(0, 62, 100));
		bindings->drainPending();
		status = waitForRun();
		QCOMPARE(status.value(QStringLiteral("name")).toString(), QStringLiteral("shared run"));
		QCOMPARE(status.value(QStringLiteral("total")).toInt(), 1);  // the shared version
		bindings->processInEvent(noteOff(0, 62));
		bindings->drainPending();
	}

	//! Criterion 2's project half: with workflows kept with the project, the workflow a binding names is
	//! saved in the project's <workspace> and comes back with it - so the binding resolves it again after
	//! the project has been saved, cleared and reopened, and a project that never used it is unchanged.
	void aProjectScopedWorkflowSurvivesTheProject()
	{
		QVERIFY(run(QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), QStringLiteral("project")}}).ok);
		QVERIFY(saveAddingWorkflow(QStringLiteral("with the project"), 1));
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(QStringLiteral("with the project"), 65, 0)).ok);

		const QString path = m_config.filePath(QStringLiteral("project.mmp"));
		QVERIFY2(Engine::getSong()->saveProjectFile(path), "the project could not be saved");
		QFile project(path);
		QVERIFY(project.open(QIODevice::ReadOnly | QIODevice::Text));
		const QString saved = QString::fromUtf8(project.readAll());
		project.close();
		QVERIFY2(saved.contains(QStringLiteral("<workspace")) && saved.contains(QStringLiteral("with the project")),
			"the project-scoped workflow was not saved with the project");
		QVERIFY2(!saved.contains(QStringLiteral("midi\"")), "a controller binding travelled in the project");

		// A cleared project: the project's library is empty, so the binding no longer resolves...
		Engine::getSong()->clearProject();
		QVERIFY2(!run(QStringLiteral("session.command_bindings")).result.value(QStringLiteral("targets")).toArray()
			.first().toObject().value(QStringLiteral("resolved")).toBool(), "the cleared project still resolved the workflow");

		// ...and reopening the file puts it back, so the binding resolves and runs the project's workflow.
		Engine::getSong()->loadProject(path);
		QCOMPARE(run(QStringLiteral("session.command_bindings")).result.value(QStringLiteral("workflow_scope")).toString(),
			QStringLiteral("project"));
		QVERIFY2(run(QStringLiteral("session.command_bindings")).result.value(QStringLiteral("targets")).toArray()
			.first().toObject().value(QStringLiteral("resolved")).toBool(), "the reopened project did not resolve the workflow");

		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		QVERIFY(bindings != nullptr);
		const int before = trackCount();
		bindings->processInEvent(noteOn(0, 65, 100));
		bindings->drainPending();
		QCOMPARE(waitForRun().value(QStringLiteral("name")).toString(), QStringLiteral("with the project"));
		QCOMPARE(trackCount(), before + 1);
		bindings->processInEvent(noteOff(0, 65));
		bindings->drainPending();

		// A project that never used the workspace re-saves byte for byte: the shared library is what is in
		// use again, and no <workspace> element is written for it.
		QVERIFY(run(QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), QStringLiteral("shared")}}).ok);
		Engine::getSong()->clearProject();
		const QString plain = m_config.filePath(QStringLiteral("plain.mmp"));
		QVERIFY2(Engine::getSong()->saveProjectFile(plain), "the plain project could not be saved");
		QFile reopened(plain);
		QVERIFY(reopened.open(QIODevice::ReadOnly | QIODevice::Text));
		const QString plainText = QString::fromUtf8(reopened.readAll());
		reopened.close();
		QVERIFY2(!plainText.contains(QStringLiteral("<workspace")),
			"a project with no project-scoped data wrote a <workspace> element");
	}

	//! Criterion 1's guard: a binding must NAME a target, and a workflow it names must exist in the
	//! library in use - a typo is refused where the user can see it, not a binding that never fires.
	void aMissingOrAmbiguousTargetIsRefused()
	{
		// No workflow of that name: refused, with the library's names in the reason, and nothing stored.
		const ControlResult missing = run(QStringLiteral("session.bind_command"), noteBinding(QStringLiteral("no such workflow"), 63, 0));
		QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);
		QVERIFY2(missing.errorMessage.contains(QStringLiteral("no workflow 'no such workflow'")), qPrintable(missing.errorMessage));
		QVERIFY(LaunchBindings::commandBindings().isEmpty());

		// Both targets at once is ambiguous, not "prefer the command".
		const ControlResult both = run(QStringLiteral("session.bind_command"),
			{{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("key"), 63},
				{QStringLiteral("command"), QStringLiteral("transport.stop")}, {QStringLiteral("workflow"), QStringLiteral("x")}});
		QCOMPARE(both.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(both.errorMessage.contains(QStringLiteral("one target")), qPrintable(both.errorMessage));
		QVERIFY(LaunchBindings::commandBindings().isEmpty());

		// Neither target is refused too.
		QCOMPARE(run(QStringLiteral("session.bind_command"),
			{{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("key"), 63}}).errorKind, ControlErrorKind::InvalidArgs);
		// A workflow's blank name is refused.
		QCOMPARE(run(QStringLiteral("session.bind_command"),
			{{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("key"), 63},
				{QStringLiteral("workflow"), QStringLiteral("  ")}}).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(LaunchBindings::commandBindings().isEmpty());
	}

	//! Criterion 2: one undo step per edit, and the A16 contract knows the new shape of the two
	//! commands (their class did not change: they still write the one config list).
	void aWorkflowBindingIsOneUndoStep()
	{
		QVERIFY(saveAddingWorkflow(QStringLiteral("undo me"), 1));
		QVERIFY(run(QStringLiteral("session.bind_command"), noteBinding(QStringLiteral("undo me"), 64, 0)).ok);
		QCOMPARE(LaunchBindings::commandBindings().size(), 1);

		// A second bind on the same note, to a workflow this time, replaces it in ONE step.
		QVERIFY(saveAddingWorkflow(QStringLiteral("replace me"), 1));
		QVERIFY(run(QStringLiteral("session.bind_command"), controlBinding(QStringLiteral("replace me"), 90, 0)).ok);
		QVERIFY(run(QStringLiteral("session.unbind_command"),
			{{QStringLiteral("trigger"), QStringLiteral("note")}, {QStringLiteral("key"), 64}, {QStringLiteral("channel"), 0}}).ok);
		QCOMPARE(LaunchBindings::commandBindings().size(), 1);  // the CC binding only
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(LaunchBindings::commandBindings().size(), 2);  // the unbind came back, alone
		QVERIFY(!storedNote(64, 0).isEmpty());

		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		const control::ReversibilityEntry* bind = table.lookup(QStringLiteral("session.bind_command"));
		QVERIFY2(bind != nullptr, "session.bind_command has no reversibility row");
		QCOMPARE(bind->cls, control::ReversibilityClass::TrueInverse);
		const control::ReversibilityEntry* listed = table.lookup(QStringLiteral("session.command_bindings"));
		QVERIFY(listed != nullptr);
		QCOMPARE(listed->cls, control::ReversibilityClass::NotMutating);
	}

private:
	QTemporaryDir m_config;
};

QTEST_GUILESS_MAIN(MidiWorkflowBindingsTest)
#include "MidiWorkflowBindingsTest.moc"
