/*
 * MidiCommandBindingsGuiTest.cpp - the Launcher > Controller entries that bind a command to a control
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! Q050-217.1 over the REAL application: the GUI entry point for a command binding is the command
 *  itself, reached from Launcher > Controller. The test drives the built menu action (so the
 *  surface is proven present and wired), fills the command's own schema-built form, runs it, and
 *  then checks the same store the agent's command reads - and undoes it through Edit > Undo, the
 *  GUI's own action, so the one-step guarantee is the interface's and not a test-only path. */

#include <QtTest>

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "LaunchBindings.h"
#include "MainWindow.h"
#include "MenuTestSupport.h"
#include "Mixer.h"
#include "MidiEvent.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The action under Launcher > Controller carrying @a label.
QAction* controllerEntry(const QString& label)
{
	auto* launcher = getGUI()->mainWindow()->findChild<QMenu*>(QStringLiteral("launcherMenu"));
	if (launcher == nullptr) { return nullptr; }
	return menuAction(launcher, {QStringLiteral("Controller"), label});
}

} // namespace

class MidiCommandBindingsGuiTest : public QObject
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
		while (run(QStringLiteral("control.undo")).result.value(QStringLiteral("undone")).toBool()) {}
	}

	//! The interface has the entries, and each declares the command it runs (A15: an action names
	//! its command, so the surface can be reflected and swept).
	void theMenuCarriesTheCommandBindingEntries()
	{
		QAction* bind = controllerEntry(QStringLiteral("Bind a Command to a Note or Control Change..."));
		QVERIFY2(bind != nullptr, "Launcher > Controller has no command-binding entry");
		QCOMPARE(bind->property("controlCommand").toString(), QStringLiteral("session.bind_command"));
		QVERIFY(bind->isEnabled());
		QAction* unbind = controllerEntry(QStringLiteral("Unbind a Command..."));
		QVERIFY2(unbind != nullptr, "Launcher > Controller has no command-unbinding entry");
		QCOMPARE(unbind->property("controlCommand").toString(), QStringLiteral("session.unbind_command"));
		QAction* listed = controllerEntry(QStringLiteral("Command Bindings"));
		QVERIFY2(listed != nullptr, "Launcher > Controller has no command-bindings list");
		QCOMPARE(listed->property("controlCommand").toString(), QStringLiteral("session.command_bindings"));
		// The launcher's own entries are still there, unchanged.
		QVERIFY(controllerEntry(QStringLiteral("Bind a Note...")) != nullptr);
		QCOMPARE(controllerEntry(QStringLiteral("Bind a Note..."))->property("controlCommand").toString(), QStringLiteral("session.bind"));
	}

	//! Criterion 1 through the interface: the menu entry opens the command's form, the form's own
	//! Run dispatches it, and the bound control then runs the command when the note is pressed -
	//! once per press - through the same registry the agent's command uses.
	void theMenuBindsANoteThatThenRunsTheCommand()
	{
		QAction* entry = controllerEntry(QStringLiteral("Bind a Command to a Note or Control Change..."));
		QVERIFY(entry != nullptr);

		// The menu action opens the command's schema-built form (the one way into a command, A11).
		CommandForm* form = nullptr;
		for (int attempt = 0; attempt < 50 && form == nullptr; ++attempt)
		{
			entry->trigger();
			form = getGUI()->mainWindow()->findChild<CommandForm*>();
			if (form == nullptr) { QTest::qWait(20); }
		}
		QVERIFY2(form != nullptr, "the bind entry opened no form");
		QCOMPARE(form->fields().contains(QStringLiteral("command")), true);
		QVERIFY(form->setField(QStringLiteral("trigger"), QStringLiteral("note")));
		QVERIFY(form->setField(QStringLiteral("key"), 67));
		QVERIFY(form->setField(QStringLiteral("channel"), 0));
		QVERIFY(form->setField(QStringLiteral("command"), QStringLiteral("mixer.add_channel")));
		const int channelsBefore = Engine::mixer()->numChannels();
		form->accept();  // the form's own Run

		QCOMPARE(LaunchBindings::commandBindings().size(), 1);
		const QJsonObject stored = LaunchBindings::commandBindings().first().toObject();
		QCOMPARE(stored.value(QStringLiteral("trigger")).toString(), QStringLiteral("note"));
		QCOMPARE(stored.value(QStringLiteral("key")).toInt(), 67);
		QCOMPARE(stored.value(QStringLiteral("channel")).toInt(), 0);
		QCOMPARE(stored.value(QStringLiteral("command")).toString(), QStringLiteral("mixer.add_channel"));

		// The controller the form was not asked about still has to exist for notes to arrive: the
		// user's next step is Connect, and the binding is already stored either way.
		QVERIFY(run(QStringLiteral("session.bindings_connect"), {{QStringLiteral("input"), QString()}}).ok);
		LaunchBindings* live = LaunchBindings::instance();
		QVERIFY(live != nullptr);
		live->processInEvent(MidiEvent(MidiNoteOn, 0, 67, 100));
		live->drainPending();
		QCOMPARE(Engine::mixer()->numChannels(), channelsBefore + 1);
		live->processInEvent(MidiEvent(MidiNoteOff, 0, 67, 0));
		live->drainPending();
		QCOMPARE(Engine::mixer()->numChannels(), channelsBefore + 1);  // the release runs nothing
	}

	//! Criterion 2 through the interface: the binding reads back through its own list command, a
	//! second bind on the same control replaces it, and Edit > Undo - the GUI's action - puts the
	//! previous binding back in ONE step.
	void theMenuReadsBackReplacesAndUndoesOneStep()
	{
		QVERIFY(run(QStringLiteral("session.bind_command"),
			{{QStringLiteral("trigger"), QStringLiteral("cc")}, {QStringLiteral("controller"), 74},
				{QStringLiteral("channel"), 0}, {QStringLiteral("command"), QStringLiteral("mixer.add_channel")}}).ok);

		// Read back through the list command the "Command Bindings" entry shows.
		QAction* listed = controllerEntry(QStringLiteral("Command Bindings"));
		QVERIFY(listed != nullptr);
		QCOMPARE(listed->property("controlCommand").toString(), QStringLiteral("session.command_bindings"));
		const ControlResult read = run(QStringLiteral("session.command_bindings"));
		QVERIFY2(read.ok, qPrintable(read.errorMessage));
		QCOMPARE(read.result.value(QStringLiteral("bindings")).toArray().size(), 1);

		// Replace it from the menu's form.
		QAction* entry = controllerEntry(QStringLiteral("Bind a Command to a Note or Control Change..."));
		QVERIFY(entry != nullptr);
		CommandForm* form = nullptr;
		for (int attempt = 0; attempt < 50 && form == nullptr; ++attempt)
		{
			entry->trigger();
			form = getGUI()->mainWindow()->findChild<CommandForm*>();
			if (form == nullptr) { QTest::qWait(20); }
		}
		QVERIFY(form != nullptr);
		QVERIFY(form->setField(QStringLiteral("trigger"), QStringLiteral("cc")));
		QVERIFY(form->setField(QStringLiteral("controller"), 74));
		QVERIFY(form->setField(QStringLiteral("channel"), 0));
		QVERIFY(form->setField(QStringLiteral("command"), QStringLiteral("transport.stop")));
		form->accept();
		QCOMPARE(LaunchBindings::commandBindings().size(), 1);
		QCOMPARE(LaunchBindings::commandBindings().first().toObject().value(QStringLiteral("command")).toString(),
			QStringLiteral("transport.stop"));

		// Edit > Undo, the interface's own action: the binding the replacement displaced is back. The
		// action's enabled state is refreshed when the Edit menu is opened (MainWindow's aboutToShow),
		// so the menu is shown the way a user opens it, then its Undo is triggered.
		QMenu* edit = nullptr;
		for (QMenu* menu : getGUI()->mainWindow()->findChildren<QMenu*>())
		{
			if (menu->title().remove(QLatin1Char('&')) == QLatin1String("Edit")) { edit = menu; }
		}
		QVERIFY2(edit != nullptr, "the main window has no Edit menu");
		edit->popup(QPoint(0, 0));
		QTest::qWait(20);
		QAction* undo = nullptr;
		for (QAction* action : edit->actions())
		{
			if (action->data().toString() == QLatin1String("control.undo")) { undo = action; }
		}
		QVERIFY2(undo != nullptr, "Edit has no Undo action declaring control.undo");
		QVERIFY2(undo->isEnabled(), "Edit > Undo is disabled although a binding edit is on the stack");
		undo->trigger();
		edit->hide();
		QCOMPARE(LaunchBindings::commandBindings().size(), 1);
		QCOMPARE(LaunchBindings::commandBindings().first().toObject().value(QStringLiteral("command")).toString(),
			QStringLiteral("mixer.add_channel"));
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(MidiCommandBindingsGuiTest)
#include "MidiCommandBindingsGuiTest.moc"
