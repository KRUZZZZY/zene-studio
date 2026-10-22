/*
 * LivecodePanelTest.cpp - Edit > Live Code schedules, cancels, loads and undoes live-code scripts
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

/*! The live-code panel over the REAL application, every edit through its registered command:
 *  the Edit > Live Code menu item declares window.livecode and opens the panel; Schedule runs
 *  livecode.schedule (the clock then holds the script), a source that does not compile is refused
 *  IN the panel and the schedule that was active stays exactly as it was, Cancel Schedule runs
 *  livecode.unschedule, one Undo re-arms it, Load Source reads livecode.get_source, and one Undo
 *  after a replacement restores the previous script's text, hook and budget. The help text the
 *  panel shows states the fresh-Lua-state rule and the source, count, poll and memory bounds. */

#include <QtTest>

#include <QAction>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "LivecodePanel.h"
#include "MainWindow.h"
#include "ScriptClock.h"
#include "ScriptEngine.h"
#include "ShellToolsMenu.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject scheduleState(const QString& id)
{
	return run(QStringLiteral("livecode.get_source"), {{QStringLiteral("id"), id}}).result;
}

QJsonArray schedules()
{
	return run(QStringLiteral("livecode.get_state"), {}).result
		.value(QStringLiteral("schedules")).toArray();
}

QJsonObject scheduleEntry(const QString& id)
{
	for (const QJsonValue& value : schedules())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("id")).toString() == id) { return entry; }
	}
	return {};
}

//! The main window's action that declares @a command (the "controlCommand" property declares() sets).
QAction* actionFor(QMainWindow* window, const QString& command)
{
	for (QAction* action : window->findChildren<QAction*>())
	{
		if (action->property("controlCommand").toString() == command) { return action; }
	}
	return nullptr;
}

QPushButton* buttonFor(LivecodePanel& panel, const char* command)
{
	for (QPushButton* button : panel.findChildren<QPushButton*>())
	{
		if (button->property("controlCommand").toString() == QLatin1String(command)) { return button; }
	}
	return nullptr;
}

//! The panel's editor, located the way a person does: by the widget's own accessible name.
QPlainTextEdit* editorOf(LivecodePanel& panel)
{
	for (QPlainTextEdit* edit : panel.findChildren<QPlainTextEdit*>())
	{
		if (edit->objectName() == QLatin1String("livecode_source")) { return edit; }
	}
	return nullptr;
}

} // namespace

class LivecodePanelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		getGUI()->mainWindow()->show();
		QCoreApplication::processEvents();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		// The clock is a process singleton: leave it as a fresh process would find it, and put
		// the memory budget back so the next test in this executable starts from the default.
		for (const QJsonValue& value : schedules())
		{
			run(QStringLiteral("livecode.unschedule"),
				{{QStringLiteral("id"), value.toObject().value(QStringLiteral("id")).toString()}});
		}
		ScriptEngine::instance()->setMemoryBudget(ScriptEngine::DefaultMemoryBudgetBytes);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The user-facing entry point: Edit > Live Code declares window.livecode, and triggering it
	//! (as a click does) opens the panel. This is the GUI path, not the command handler alone.
	void theEditMenuOpensThePanelThroughItsCommand()
	{
		QVERIFY(ControlRegistry::instance()->hasCommand(QStringLiteral("window.livecode")));
		QMainWindow* window = getGUI()->mainWindow();
		QAction* action = actionFor(window, QStringLiteral("window.livecode"));
		QVERIFY2(action != nullptr, "no menu action declares window.livecode");
		QCOMPARE(action->text(), QStringLiteral("Live Code..."));

		// ...and the footer's Tools home lists the same command, so both places are the one entry point.
		bool inTools = false;
		for (const shelltools::Tool& tool : shelltools::tools())
		{
			if (tool.command != QLatin1String("window.livecode")) { continue; }
			inTools = true;
			QVERIFY2(tool.name.contains(QLatin1String("Live Code")), qPrintable(tool.name));
		}
		QVERIFY2(inTools, "the footer's Tools menu does not list window.livecode");

		action->trigger();
		QCoreApplication::processEvents();
		LivecodePanel* panel = window->findChild<LivecodePanel*>();
		QVERIFY2(panel != nullptr, "triggering Edit > Live Code opened no panel");
		QVERIFY(panel->isVisible());
		QCOMPARE(panel->scheduleCount(), 0);
		const ControlResult opened = run(QStringLiteral("window.livecode"));
		QVERIFY(opened.ok);
		QCOMPARE(opened.result.value(QStringLiteral("shown")).toBool(), true);
		for (LivecodePanel* open : window->findChildren<LivecodePanel*>()) { open->close(); }
	}

	//! The panel schedules through livecode.schedule and cancels through livecode.unschedule by
	//! clicking its own buttons; one Undo (the panel's Undo button) re-arms the cancelled script.
	void thePanelSchedulesAndCancelsThroughTheCommands()
	{
		LivecodePanel panel;
		panel.setScheduleId(QStringLiteral("lc-panel-a"));
		panel.setHook(QStringLiteral("beat"));
		panel.setBudget(4000);
		panel.setSource(QStringLiteral("zene.log():info('panel-a')\n"));
		QPushButton* scheduleButton = buttonFor(panel, "livecode.schedule");
		QVERIFY(scheduleButton != nullptr);
		QTest::mouseClick(scheduleButton, Qt::LeftButton);

		QVERIFY2(panel.statusText().contains(QStringLiteral("Scheduled")), qPrintable(panel.statusText()));
		QCOMPARE(panel.scheduleCount(), 1);
		const QJsonObject entry = scheduleEntry(QStringLiteral("lc-panel-a"));
		QCOMPARE(entry.value(QStringLiteral("hook")).toString(), QStringLiteral("beat"));
		QCOMPARE(entry.value(QStringLiteral("budget")).toInt(), 4000);
		QCOMPARE(entry.value(QStringLiteral("source_bytes")).toInt(),
			QStringLiteral("zene.log():info('panel-a')\n").toUtf8().size());

		QPushButton* cancelButton = buttonFor(panel, "livecode.unschedule");
		QVERIFY(cancelButton != nullptr);
		QTest::mouseClick(cancelButton, Qt::LeftButton);
		QCOMPARE(panel.scheduleCount(), 0);
		QVERIFY(scheduleEntry(QStringLiteral("lc-panel-a")).isEmpty());

		QPushButton* undoButton = buttonFor(panel, "control.undo");
		QVERIFY(undoButton != nullptr);
		QTest::mouseClick(undoButton, Qt::LeftButton);
		QCOMPARE(panel.scheduleCount(), 1);
		QCOMPARE(scheduleState(QStringLiteral("lc-panel-a")).value(QStringLiteral("source")).toString(),
			QStringLiteral("zene.log():info('panel-a')\n"));
	}

	//! Scheduling with no id lets the clock choose lc-<n>, and the panel then names that id in its
	//! box so the next Cancel addresses the schedule it just made rather than an empty field.
	void aGeneratedIdIsReportedBackAndCancellable()
	{
		LivecodePanel panel;
		panel.setScheduleId(QString());
		panel.setHook(QStringLiteral("transport"));
		panel.setSource(QStringLiteral("return 'generated'\n"));
		QTest::mouseClick(buttonFor(panel, "livecode.schedule"), Qt::LeftButton);
		const QString generated = panel.scheduleId();
		QVERIFY2(generated.startsWith(QStringLiteral("lc-")), qPrintable(generated));
		QVERIFY2(!scheduleEntry(generated).isEmpty(), qPrintable(generated));
		QTest::mouseClick(buttonFor(panel, "livecode.unschedule"), Qt::LeftButton);
		QVERIFY2(scheduleEntry(generated).isEmpty(), qPrintable(panel.statusText()));
	}

	//! A source that does not compile is refused by the command, shown in the panel, and the
	//! schedule that was active keeps firing the script it already had.
	void aCompileErrorKeepsTheActiveSchedule()
	{
		LivecodePanel panel;
		panel.setScheduleId(QStringLiteral("lc-panel-b"));
		panel.setHook(QStringLiteral("bar"));
		panel.setSource(QStringLiteral("return 'first'\n"));
		QTest::mouseClick(buttonFor(panel, "livecode.schedule"), Qt::LeftButton);
		QCOMPARE(scheduleState(QStringLiteral("lc-panel-b")).value(QStringLiteral("source")).toString(),
			QStringLiteral("return 'first'\n"));

		panel.setSource(QStringLiteral("if then end"));
		QTest::mouseClick(buttonFor(panel, "livecode.schedule"), Qt::LeftButton);
		QVERIFY2(panel.statusText().contains(QStringLiteral("does not compile")),
			qPrintable(panel.statusText()));
		// The editor keeps the text the musician was working on...
		QCOMPARE(panel.source(), QStringLiteral("if then end"));
		// ...and the clock still holds the definition that compiled.
		const QJsonObject state = scheduleState(QStringLiteral("lc-panel-b"));
		QCOMPARE(state.value(QStringLiteral("source")).toString(), QStringLiteral("return 'first'\n"));
		QCOMPARE(state.value(QStringLiteral("hook")).toString(), QStringLiteral("bar"));
		QCOMPARE(scheduleEntry(QStringLiteral("lc-panel-b")).value(QStringLiteral("errors")).toInt(), 0);
	}

	//! Scheduling the same id again replaces the definition in place; one Undo restores the
	//! previous script's text, hook and budget - the runtime counters are not part of a snapshot.
	void oneUndoRestoresTheReplacedScript()
	{
		LivecodePanel panel;
		panel.setScheduleId(QStringLiteral("lc-panel-c"));
		panel.setHook(QStringLiteral("bar"));
		panel.setBudget(0);
		panel.setSource(QStringLiteral("return 'draft-1'\n"));
		QTest::mouseClick(buttonFor(panel, "livecode.schedule"), Qt::LeftButton);
		const int firesBefore = scheduleEntry(QStringLiteral("lc-panel-c"))
			.value(QStringLiteral("fires")).toInt();

		panel.setHook(QStringLiteral("beat"));
		panel.setBudget(9000);
		panel.setSource(QStringLiteral("return 'draft-22'\n"));
		QTest::mouseClick(buttonFor(panel, "livecode.schedule"), Qt::LeftButton);
		// Replacement is in place: the id still names exactly ONE slot (other tests' schedules
		// are still on the process-wide clock, so the count is per id, not global).
		int matching = 0;
		for (const QJsonValue& value : schedules())
		{
			if (value.toObject().value(QStringLiteral("id")).toString() == QLatin1String("lc-panel-c")) { ++matching; }
		}
		QCOMPARE(matching, 1);
		QCOMPARE(scheduleState(QStringLiteral("lc-panel-c")).value(QStringLiteral("source")).toString(),
			QStringLiteral("return 'draft-22'\n"));

		panel.setScheduleId(QStringLiteral("lc-panel-c"));
		QTest::mouseClick(buttonFor(panel, "control.undo"), Qt::LeftButton);
		const QJsonObject restored = scheduleState(QStringLiteral("lc-panel-c"));
		QCOMPARE(restored.value(QStringLiteral("source")).toString(), QStringLiteral("return 'draft-1'\n"));
		QCOMPARE(restored.value(QStringLiteral("hook")).toString(), QStringLiteral("bar"));
		QCOMPARE(restored.value(QStringLiteral("budget")).toInt(), 0);
		// The undo restored the DEFINITION; the fire counter is runtime history and is not rewound.
		QVERIFY(scheduleEntry(QStringLiteral("lc-panel-c")).value(QStringLiteral("fires")).toInt() >= firesBefore);
		QCOMPARE(panel.source(), QStringLiteral("return 'draft-1'\n"));
	}

	//! Load Source puts an agent-made schedule into the editor: the panel reads the text back
	//! through livecode.get_source, which get_state deliberately does not carry.
	void thePanelLoadsAnAgentMadeSchedule()
	{
		LivecodePanel panel;
		QVERIFY2(editorOf(panel) != nullptr, "the panel has no source editor");
		const QString source = QStringLiteral("-- written over the socket é\nreturn 'agent'\n");
		QVERIFY(run(QStringLiteral("livecode.schedule"), {{QStringLiteral("id"), QStringLiteral("lc-panel-agent")},
			{QStringLiteral("hook"), QStringLiteral("transport")},
			{QStringLiteral("budget"), 2500}, {QStringLiteral("source"), source}}).ok);
		QCOMPARE(panel.loadSource(QStringLiteral("lc-panel-agent")), QString());
		QCOMPARE(panel.source(), source);
		QCOMPARE(panel.scheduleId(), QStringLiteral("lc-panel-agent"));
		auto* hook = panel.findChild<QComboBox*>(QStringLiteral("livecode_hook"));
		QVERIFY(hook != nullptr);
		QCOMPARE(hook->currentData().toString(), QStringLiteral("transport"));
		auto* budget = panel.findChild<QSpinBox*>(QStringLiteral("livecode_budget"));
		QVERIFY(budget != nullptr);
		QCOMPARE(budget->value(), 2500);
		// Unknown id: the command's typed refusal is reported, the editor is untouched.
		QVERIFY(!panel.loadSource(QStringLiteral("lc-panel-missing")).isEmpty());
		QCOMPARE(panel.source(), source);
	}

	//! What the panel documents: a fresh Lua state per fire, and the source/count/poll/memory
	//! bounds read from the implementation's own constants rather than retyped prose.
	void thePanelDocumentsScriptMemoryAndScheduleBounds()
	{
		LivecodePanel panel;
		const QString help = panel.helpText();
		QVERIFY2(help.contains(QStringLiteral("fresh Lua state")), qPrintable(help));
		QVERIFY2(help.contains(QStringLiteral("do not persist")), qPrintable(help));
		QVERIFY2(help.contains(QString::number(ScriptClock::SourceByteCap)), qPrintable(help));
		QVERIFY2(help.contains(QString::number(ScriptClock::MaxScheduleCount)), qPrintable(help));
		QVERIFY2(help.contains(QString::number(ScriptClock::PollMs)), qPrintable(help));
		QVERIFY2(help.contains(QString::number(ScriptClockGrid::CrossCap)), qPrintable(help));
		QVERIFY2(help.contains(QString::number(ScriptEngine::DefaultMemoryBudgetBytes / (1024 * 1024))),
			qPrintable(help));
		QVERIFY2(help.contains(QStringLiteral("Script Memory Budget")), qPrintable(help));
		QVERIFY2(help.contains(QStringLiteral("not saved in the project")), qPrintable(help));
		QVERIFY2(help.contains(QStringLiteral("audio thread")), qPrintable(help));
		// And it is the text the widget actually shows.
		QLabel* visible = panel.findChild<QLabel*>(QStringLiteral("livecode_help"));
		QVERIFY(visible != nullptr);
		QCOMPARE(visible->text(), help);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(LivecodePanelTest)
#include "LivecodePanelTest.moc"
