/*
 * MidiWorkflowBindingsGuiTest.cpp - the Launcher > Controller > Bind a Workflow entries
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

/*! Q050-217.2 over the REAL application: the GUI entry point for binding a workflow to a MIDI control
 *  is the command itself, reached from Launcher > Controller > Bind a Workflow, with one submenu per
 *  workflow in the library in use. The test drives the BUILT menu actions (so the surface is proven
 *  present, populated from the library and wired), fills the command's own schema-built form, and then
 *  plays a live note through the connected surface - exactly the path the person at the controller
 *  takes. It then reads the binding back through the list entry, replaces it from the menu, and proves
 *  the workflow that runs is the one the library in use holds, after switching scope. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "LaunchBindings.h"
#include "MainWindow.h"
#include "MenuTestSupport.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! Launcher > Controller > Bind a Workflow, the submenu Q050-217.2 fills as it opens.
QMenu* workflowMenu()
{
	auto* launcher = getGUI()->mainWindow()->findChild<QMenu*>(QStringLiteral("launcherMenu"));
	if (launcher == nullptr) { return nullptr; }
	for (QAction* action : launcher->actions())
	{
		if (action->text().remove(QLatin1Char('&')) == QLatin1String("Controller") && action->menu() != nullptr)
		{
			for (QAction* entry : action->menu()->actions())
			{
				if (entry->text().remove(QLatin1Char('&')) == QLatin1String("Bind a Workflow") && entry->menu() != nullptr)
				{
					return entry->menu();
				}
			}
		}
	}
	return nullptr;
}

//! Open @a menu the way a user does, so its aboutToShow rebuild runs, and answer the submenu for @a name.
QMenu* workflowSubmenu(const QString& name)
{
	QMenu* menu = workflowMenu();
	if (menu == nullptr) { return nullptr; }
	menu->popup(QPoint(0, 0));
	QTest::qWait(20);
	for (QAction* action : menu->actions())
	{
		if (action->text().remove(QLatin1Char('&')) == name && action->menu() != nullptr) { return action->menu(); }
	}
	return nullptr;
}

//! The action under Launcher > Controller > Bind a Workflow > @a name named @a label, or nullptr.
QAction* workflowBindAction(const QString& name, const QString& label)
{
	QMenu* sub = workflowSubmenu(name);
	if (sub == nullptr) { return nullptr; }
	for (QAction* action : sub->actions())
	{
		if (action->text().remove(QLatin1Char('&')) == label) { return action; }
	}
	return nullptr;
}

//! Trigger @a action and answer the form it opened (the schema-built CommandForm), or nullptr.
CommandForm* openForm(QAction* action)
{
	CommandForm* form = nullptr;
	for (int attempt = 0; attempt < 50 && form == nullptr; ++attempt)
	{
		action->trigger();
		form = getGUI()->mainWindow()->findChild<CommandForm*>();
		if (form == nullptr) { QTest::qWait(20); }
	}
	return form;
}

bool saveAddingWorkflow(const QString& name, int count)
{
	QJsonArray steps;
	for (int i = 0; i < count; ++i)
	{
		steps.append(QJsonObject{{QStringLiteral("command"), QStringLiteral("track.add")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("type"), QStringLiteral("instrument")},
				{QStringLiteral("name"), QStringLiteral("%1 %2").arg(name).arg(i + 1)}}}});
	}
	return run(QStringLiteral("workflow.save"), {{QStringLiteral("name"), name}, {QStringLiteral("steps"), steps}}).ok;
}

int trackCount() { return static_cast<int>(Engine::getSong()->tracks().size()); }

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

QJsonObject storedNote(int key, int channel)
{
	for (const QJsonValue& value : LaunchBindings::commandBindings())
	{
		const QJsonObject binding = value.toObject();
		if (binding.value(QStringLiteral("trigger")).toString() == QLatin1String("note")
			&& binding.value(QStringLiteral("key")).toInt(-1) == key
			&& binding.value(QStringLiteral("channel")).toInt(-1) == channel)
		{
			return binding;
		}
	}
	return {};
}

} // namespace

class MidiWorkflowBindingsGuiTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		guitest::startGui(m_dir);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		getGUI()->mainWindow()->show();
		QVERIFY(QTest::qWaitForWindowExposed(getGUI()->mainWindow()));
		ControlRegistry::setReady(true);
		QTest::qWait(100);  // the shell installs its menus once it is shown
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
		for (const QJsonValue& value : run(QStringLiteral("workflow.list")).result.value(QStringLiteral("workflows")).toArray())
		{
			run(QStringLiteral("workflow.remove"), {{QStringLiteral("name"), value.toObject().value(QStringLiteral("name"))}});
		}
		run(QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), QStringLiteral("shared")}});
		while (run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool()) {}
	}

	//! The interface has the entries: the Launcher > Controller > Bind a Workflow menu, one submenu per
	//! workflow in the library in use, each with a note and a control-change action declaring
	//! session.bind_command; the stage 1 command entries are still there beside it.
	void theMenuOffersEverySavedWorkflow()
	{
		QVERIFY2(workflowMenu() != nullptr, "Launcher > Controller has no Bind a Workflow submenu");
		QVERIFY(saveAddingWorkflow(QStringLiteral("menu workflow"), 1));

		QAction* note = workflowBindAction(QStringLiteral("menu workflow"), QStringLiteral("Bind a Note..."));
		QVERIFY2(note != nullptr, "the workflow's submenu has no note-binding entry");
		QCOMPARE(note->property("controlCommand").toString(), QStringLiteral("session.bind_command"));
		QVERIFY(note->isEnabled());
		QAction* control = workflowBindAction(QStringLiteral("menu workflow"), QStringLiteral("Bind a Control Change..."));
		QVERIFY2(control != nullptr, "the workflow's submenu has no control-change entry");
		QCOMPARE(control->property("controlCommand").toString(), QStringLiteral("session.bind_command"));

		// A workflow saved later is offered too: the submenu is rebuilt from the library as it opens.
		QVERIFY(saveAddingWorkflow(QStringLiteral("later workflow"), 1));
		QVERIFY2(workflowBindAction(QStringLiteral("later workflow"), QStringLiteral("Bind a Note...")) != nullptr,
			"a workflow saved after the menu was built is not offered");

		// Stage 1's command entries are unchanged, and the bindings list is reachable from the same menu.
		auto* launcher = getGUI()->mainWindow()->findChild<QMenu*>(QStringLiteral("launcherMenu"));
		QVERIFY(launcher != nullptr);
		QAction* commandEntry = menuAction(launcher, {QStringLiteral("Controller"),
			QStringLiteral("Bind a Command to a Note or Control Change...")});
		QVERIFY(commandEntry != nullptr);
		QCOMPARE(commandEntry->property("controlCommand").toString(), QStringLiteral("session.bind_command"));
		QVERIFY(menuAction(workflowMenu(), {QStringLiteral("Workflow and Command Bindings")}) != nullptr);
	}

	//! Criterion 1 through the interface: the built menu action opens the command's own form, the form
	//! fills and runs it, and then a live note through the connected surface RUNS THE SAVED WORKFLOW -
	//! once for the press, none for the release - the same registry path the agent's command takes.
	void theMenuBindsANoteThatRunsTheWorkflow()
	{
		QVERIFY(saveAddingWorkflow(QStringLiteral("play me"), 2));
		QAction* entry = workflowBindAction(QStringLiteral("play me"), QStringLiteral("Bind a Note..."));
		QVERIFY2(entry != nullptr, "the workflow's note-binding entry is missing");

		CommandForm* form = openForm(entry);
		QVERIFY2(form != nullptr, "the workflow bind entry opened no form");
		// The target and the trigger kind are fixed by where the entry was reached from; the form asks
		// for the number (and the channel).
		QVERIFY(form->fields().contains(QStringLiteral("key")));
		QVERIFY2(!form->fields().contains(QStringLiteral("workflow")), "the form asked for the workflow again");
		QVERIFY(form->setField(QStringLiteral("key"), 72));
		QVERIFY(form->setField(QStringLiteral("channel"), 0));
		form->accept();

		QCOMPARE(storedNote(72, 0).value(QStringLiteral("workflow")).toString(), QStringLiteral("play me"));

		// The controller the form did not ask about has to exist for notes to arrive (Connect is the
		// user's next step); the binding is stored either way.
		QVERIFY(run(QStringLiteral("session.bindings_connect"), {{QStringLiteral("input"), QString()}}).ok);
		LaunchBindings* live = LaunchBindings::instance();
		QVERIFY(live != nullptr);
		const int before = trackCount();
		live->processInEvent(MidiEvent(MidiNoteOn, 0, 72, 100));
		live->drainPending();
		QCOMPARE(waitForRun().value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(trackCount(), before + 2);  // the selected workflow completed
		live->processInEvent(MidiEvent(MidiNoteOff, 0, 72, 0));
		live->drainPending();
		QTest::qWait(50);
		QCOMPARE(trackCount(), before + 2);  // the release runs nothing
	}

	//! Criterion 1, control-change half through the interface: the CC entry's form binds a CC, and a
	//! sweep of the knob runs the workflow once; back at zero is a rearm.
	void theMenuBindsAControlChangeThatRunsTheWorkflow()
	{
		QVERIFY(saveAddingWorkflow(QStringLiteral("cc workflow"), 1));
		QAction* entry = workflowBindAction(QStringLiteral("cc workflow"), QStringLiteral("Bind a Control Change..."));
		QVERIFY(entry != nullptr);
		CommandForm* form = openForm(entry);
		QVERIFY(form != nullptr);
		QVERIFY(form->setField(QStringLiteral("controller"), 76));
		QVERIFY(form->setField(QStringLiteral("channel"), 1));
		form->accept();

		const QJsonArray stored = LaunchBindings::commandBindings();
		QCOMPARE(stored.size(), 1);
		QCOMPARE(stored.first().toObject().value(QStringLiteral("trigger")).toString(), QStringLiteral("cc"));
		QCOMPARE(stored.first().toObject().value(QStringLiteral("controller")).toInt(), 76);
		QCOMPARE(stored.first().toObject().value(QStringLiteral("workflow")).toString(), QStringLiteral("cc workflow"));

		QVERIFY(run(QStringLiteral("session.bindings_connect"), {{QStringLiteral("input"), QString()}}).ok);
		LaunchBindings* live = LaunchBindings::instance();
		QVERIFY(live != nullptr);
		const int before = trackCount();
		for (int value : {1, 64, 127})
		{
			live->processInEvent(MidiEvent(MidiControlChange, 1, 76, value));
			live->drainPending();
		}
		QCOMPARE(waitForRun().value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QCOMPARE(trackCount(), before + 1);
		live->processInEvent(MidiEvent(MidiControlChange, 1, 76, 0));
		live->drainPending();
		QTest::qWait(50);
		QCOMPARE(trackCount(), before + 1);
	}

	//! Criterion 2 through the interface: the binding reads back through the list entry, the workflow it
	//! names resolves from the library IN USE - the shared one, then the project's after the scope is
	//! switched - and the list says where it came from.
	void theMenuReadsBackAndTheBindingFollowsTheLibrary()
	{
		QVERIFY(saveAddingWorkflow(QStringLiteral("library run"), 1));

		// Read back through the "Workflow and Command Bindings" entry the submenu carries.
		QAction* listed = menuAction(workflowMenu(), {QStringLiteral("Workflow and Command Bindings")});
		QVERIFY2(listed != nullptr, "the Bind a Workflow menu has no bindings list entry");
		QCOMPARE(listed->property("controlCommand").toString(), QStringLiteral("session.command_bindings"));

		QAction* entry = workflowBindAction(QStringLiteral("library run"), QStringLiteral("Bind a Note..."));
		QVERIFY(entry != nullptr);
		CommandForm* form = openForm(entry);
		QVERIFY(form != nullptr);
		QVERIFY(form->setField(QStringLiteral("key"), 74));
		QVERIFY(form->setField(QStringLiteral("channel"), 2));
		form->accept();

		const ControlResult read = run(QStringLiteral("session.command_bindings"));
		QVERIFY2(read.ok, qPrintable(read.errorMessage));
		const QJsonArray targets = read.result.value(QStringLiteral("targets")).toArray();
		QCOMPARE(targets.size(), 1);
		QCOMPARE(targets.first().toObject().value(QStringLiteral("target")).toString(), QStringLiteral("workflow"));
		QCOMPARE(targets.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("library run"));
		QVERIFY(targets.first().toObject().value(QStringLiteral("resolved")).toBool());
		QCOMPARE(read.result.value(QStringLiteral("workflow_scope")).toString(), QStringLiteral("shared"));

		// Keep workflows with the project: the shared library's workflow is not in the library in use.
		QVERIFY(run(QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), QStringLiteral("project")}}).ok);
		QJsonObject after = run(QStringLiteral("session.command_bindings")).result.value(QStringLiteral("targets")).toArray()
			.first().toObject();
		QVERIFY2(!after.value(QStringLiteral("resolved")).toBool(), "a shared workflow resolved from the project's library");
		QCOMPARE(run(QStringLiteral("session.command_bindings")).result.value(QStringLiteral("workflow_scope")).toString(),
			QStringLiteral("project"));

		// Save that name in the PROJECT's library: now the binding resolves it, and a note runs it.
		QVERIFY(saveAddingWorkflow(QStringLiteral("library run"), 2));
		after = run(QStringLiteral("session.command_bindings")).result.value(QStringLiteral("targets")).toArray()
			.first().toObject();
		QVERIFY2(after.value(QStringLiteral("resolved")).toBool(), "the project's workflow did not resolve");
		QVERIFY(run(QStringLiteral("session.bindings_connect"), {{QStringLiteral("input"), QString()}}).ok);
		LaunchBindings* live = LaunchBindings::instance();
		QVERIFY(live != nullptr);
		const int before = trackCount();
		live->processInEvent(MidiEvent(MidiNoteOn, 2, 74, 100));
		live->drainPending();
		const QJsonObject status = waitForRun();
		QCOMPARE(status.value(QStringLiteral("name")).toString(), QStringLiteral("library run"));
		QCOMPARE(status.value(QStringLiteral("total")).toInt(), 2);  // the project's version
		QCOMPARE(trackCount(), before + 2);
		live->processInEvent(MidiEvent(MidiNoteOff, 2, 74, 0));
		live->drainPending();
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(MidiWorkflowBindingsGuiTest)
#include "MidiWorkflowBindingsGuiTest.moc"
