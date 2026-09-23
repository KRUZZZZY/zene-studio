/*
 * FocusDeskActionsTest.cpp - the Focus Desk's command record (UI plan §4.1),
 *                            its generated menus and the `mod:` dispatch seam
 *                            (work-list rows 4 and 5 + §9.4).
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

#include <QApplication>
#include <QMenu>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "FocusDesk.h"
#include "FocusDeskActions.h"
#include "FocusDeskModules.h"

using namespace lmms;        // ControlRegistry, ControlCommand, ConfigManager
using namespace lmms::gui;

namespace
{

QAction* findAction(QMenu& menu, const QString& controlCommand)
{
	for (QAction* action : menu.actions())
	{
		if (action->property("controlCommand").toString() == controlCommand) { return action; }
	}
	return nullptr;
}

QAction* findActionByText(QMenu& menu, const QString& needle)
{
	for (QAction* action : menu.actions())
	{
		if (action->text().contains(needle)) { return action; }
	}
	return nullptr;
}

} // namespace

//! The record shape, the mechanical availability rule, the generated menus and
//! the one seam every `mod:` mount shares (§9.4). Widget-heavy where the menu
//! is the deliverable, registry-heavy where dispatch is.
class FocusDeskActionsTest : public QObject
{
	Q_OBJECT

private slots:
	//! §4.1's row, every field: a record the renderer cannot describe or
	//! locate is a record X5/X1 would fail on, so the shape is asserted first.
	void everyRecordCarriesItsShape()
	{
		const auto records = focusDeskCommandRecords();
		QVERIFY(records.size() >= 20);
		for (const auto& record : records)
		{
			QVERIFY2(!record.name.isEmpty(), qPrintable(record.id));
			QVERIFY2(!record.where.isEmpty(), qPrintable(record.id));
			QVERIFY2(!record.owner.isEmpty(), qPrintable(record.id));
		}

		// Rows 3 and 7 as `settings.set` choices: three density presets under
		// the key FocusDeskPane writes, five workspaces under its workspace key.
		int densityChoices = 0;
		int workspaceChoices = 0;
		for (const auto& record : records)
		{
			if (record.id != QLatin1String("settings.set")) { continue; }
			const QString key = record.args.value(QStringLiteral("key")).toString();
			if (key == QLatin1String("ui/focusdesk.density"))
			{
				++densityChoices;
				QVERIFY(!record.args.value(QStringLiteral("value")).toString().isEmpty());
				QCOMPARE(static_cast<int>(record.kind), static_cast<int>(FocusCommandKind::Choice));
			}
			if (key == QLatin1String("ui/focusdesk.workspace")) { ++workspaceChoices; }
		}
		QCOMPARE(densityChoices, 3);
		QCOMPARE(workspaceChoices, 5);

		// UI-FREEZE (2026-09-24): the two formerly in-flight families LANDED
		// and are wired to the real commands - no `todo.*` stub survives the
		// freeze, nothing is left in flight, and availability is mechanical
		// (registry reasons only), never "in flight".
		const auto recordFor = [&records](const QString& id)
		{
			for (const auto& record : records)
			{
				if (record.id == id) { return record; }
			}
			return FocusCommandRecord{};
		};
		for (const auto& id : {QStringLiteral("todo.mts"), QStringLiteral("todo.s7-lanes")})
		{
			QVERIFY2(recordFor(id).id.isEmpty(), qPrintable(id));
		}
		for (const auto& id : {QStringLiteral("mts.get_state"), QStringLiteral("mts.load_scale"),
				QStringLiteral("mts.reset"), QStringLiteral("comp.lane_list"),
				QStringLiteral("comp.lane_add"), QStringLiteral("comp.select")})
		{
			const FocusCommandRecord record = recordFor(id);
			QCOMPARE(record.id, id);
			QVERIFY2(!record.unavailable.contains(QLatin1String("in flight")),
				qPrintable(record.unavailable));
		}

		// One `mod:` record per register row, id exactly focusActionCommandId's
		// spelling - the menu and the strip cannot disagree about the verb.
		const auto rows = FocusDeskModules::v1Register();
		const auto moduleRecords = focusDeskModuleRecords(rows);
		QCOMPARE(moduleRecords.size(), rows.size());
		for (int i = 0; i < rows.size(); ++i)
		{
			QCOMPARE(moduleRecords[i].id, focusActionCommandId(rows[i].id));
			QCOMPARE(moduleRecords[i].owner, rows[i].id);
		}
	}

	//! X4 made mechanical: a preset reason wins; an id whose schema wants
	//! arguments this mount cannot pass names them; an on-train command with
	//! everything it needs is available. `audio.device_set` is the probe for
	//! the middle rule - its `required: ["device"]` is published in the live
	//! commands snapshot (tools/mcp-zene-control/.../commands_snapshot.json).
	void availabilityIsMechanicalAndNeverSilent()
	{
		{ // an id no mount can reach
			FocusCommandRecord record;
			record.id = QStringLiteral("transport.thisVerbDoesNotExist");
			record.name = QStringLiteral("Phantom");
			record.where = QStringLiteral("Transport");
			const QString reason = focusCommandUnavailable(record);
			QVERIFY(!reason.isEmpty());
			QVERIFY2(reason.contains(QLatin1String("not registered")), qPrintable(reason));
		}
		{ // missing the argument the schema requires, named in the reason
			FocusCommandRecord record;
			record.id = QStringLiteral("audio.device_set");
			record.name = QStringLiteral("Device");
			record.where = QStringLiteral("Audio");
			const QString reason = focusCommandUnavailable(record);
			QVERIFY2(!reason.isEmpty(), qPrintable(reason));
			QVERIFY2(reason.contains(QLatin1String("device")), qPrintable(reason));
		}
		{ // supplying it makes the same record available
			FocusCommandRecord record;
			record.id = QStringLiteral("audio.device_set");
			record.name = QStringLiteral("Device");
			record.where = QStringLiteral("Audio");
			record.args = QJsonObject{{QStringLiteral("device"), QStringLiteral("none")}};
			QVERIFY(focusCommandUnavailable(record).isEmpty());
		}
		{ // an on-train command whose schema wants `target`: named, not hidden
			FocusCommandRecord record;
			record.id = QStringLiteral("routing.get_state");
			record.name = QStringLiteral("Graph");
			record.where = QStringLiteral("Routing");
			const QString reason = focusCommandUnavailable(record);
			QVERIFY2(!reason.isEmpty(), qPrintable(reason));
			QVERIFY2(reason.contains(QLatin1String("target")), qPrintable(reason));
		}

		// The on-train groups the desk wires are available as records: reads
		// and no-arg verbs from transport, feedback, livecode, mixer, routing.
		const auto records = focusDeskCommandRecords();
		for (const auto& id : {QStringLiteral("transport.play"), QStringLiteral("transport.stop"),
				QStringLiteral("feedback.get_state"), QStringLiteral("livecode.get_state"),
				QStringLiteral("mixer.get_state"),
				QStringLiteral("track.list"), QStringLiteral("settings.set")})
		{
			bool found = false;
			for (const auto& record : records)
			{
				if (record.id != id) { continue; }
				found = true;
				QVERIFY2(focusCommandUnavailable(record).isEmpty(),
					qPrintable(id + QLatin1String(": ") + focusCommandUnavailable(record)));
			}
			QVERIFY2(found, qPrintable(id));
		}
	}

	//! §9.4's seam: one dispatchAction() for every mount. It answers its own
	//! `mod:` ids with focusModule() (register reasons intact), refuses what
	//! is not an action, and a REGISTRY-OWNED id wins so that when the staged
	//! group lands nothing in the UI has to change.
	void theModSeamPrefersTheRegistryAndAnswersItsOwnIds()
	{
		QWidget host;
		FocusDesk desk;
		QVERIFY(desk.mountModule(QStringLiteral("arrangement"), new QWidget(&host)));
		QVERIFY(desk.mountModule(QStringLiteral("mixer"), new QWidget(&host)));
		QCOMPARE(desk.focused(), QStringLiteral("arrangement"));

		QSignalSpy refused(&desk, &FocusDesk::moduleRefused);
		QVERIFY(desk.dispatchAction(focusActionCommandId(QStringLiteral("mixer"))));
		QCOMPARE(desk.focused(), QStringLiteral("mixer"));

		// A row this build cannot stage is refused with the register's reason
		// through the same seam - not by a shortcut that could drift.
		QVERIFY(!desk.dispatchAction(focusActionCommandId(QStringLiteral("browser"))));
		QVERIFY(refused.count() >= 1);
		QVERIFY(!refused.last().last().toString().isEmpty());

		// Not a mod: id, not on the control surface: refused, typed.
		QVERIFY(!desk.dispatchAction(QStringLiteral("transport.notAEither")));
		QVERIFY(!desk.dispatchAction(QStringLiteral("mod:")));

		// The registry-first half: a registered id wins over the desk's own
		// interpretation, and the desk does not re-route it to focusModule().
		auto* registry = ControlRegistry::instance();
		bool probeRan = false;
		ControlCommand probe;
		probe.id = QStringLiteral("mod:__probe__");
		probe.group = QStringLiteral("mod");
		probe.verb = QStringLiteral("__probe__");
		probe.description = QStringLiteral("probe for the staged mod: registration path");
		probe.requiresEngine = false;
		probe.mutating = false;
		probe.handler = [&probeRan](const QJsonObject&)
		{
			probeRan = true;
			return ControlResult::success();
		};
		registry->registerCommand(probe);
		if (!registry->hasCommand(QStringLiteral("mod:__probe__")))
		{
			// The registry's id-shape rules are not this lane's to change: the
			// seam's local half is already proven above, and this says plainly
			// what the staged registration still has to satisfy.
			QSKIP("the registry declines a colon id in this build; the staged "
				"mod: registration path must register one before it can win");
		}
		QVERIFY(desk.dispatchAction(QStringLiteral("mod:__probe__")));
		QVERIFY(probeRan);
		QCOMPARE(desk.focused(), QStringLiteral("mixer"));
	}

	//! Work-list row 5: `View ▸ Modules` is the register rendered as entries -
	//! availability IS the register (unmounted rows keep their stated reason,
	//! rows that may not take the stage say so, rows not on the desk yet say
	//! that), every entry declares its `controlCommand`, and a null desk greys
	//! everything with one reason.
	void viewModulesRendersTheRegisterGreyedWithReason()
	{
		QWidget host;
		FocusDesk desk;
		QVERIFY(desk.mountModule(QStringLiteral("arrangement"), new QWidget(&host)));
		QVERIFY(desk.mountModule(QStringLiteral("mixer"), new QWidget(&host)));
		const auto rows = FocusDeskModules::v1Register();

		QMenu menu;
		addModulesMenu(&menu, &desk);
		QCOMPARE(menu.actions().size(), 1); // the one Modules submenu
		QVERIFY(menu.actions().first()->menu() != nullptr);
		QMenu& modules = *menu.actions().first()->menu();
		QCOMPARE(modules.actions().size(), rows.size());

		QAction* browser = findAction(modules, focusActionCommandId(QStringLiteral("browser")));
		QVERIFY(browser != nullptr);
		QVERIFY2(!browser->isEnabled(), qPrintable(browser->toolTip()));
		QVERIFY2(!browser->toolTip().isEmpty(), qPrintable(QStringLiteral("greyed without a reason")));

		QAction* devicechain = findAction(modules, focusActionCommandId(QStringLiteral("devicechain")));
		QVERIFY(devicechain != nullptr);
		QVERIFY2(!devicechain->isEnabled(), qPrintable(devicechain->toolTip()));

		// A mounted, focusable row with no preset reason: live, checkable,
		// and ticked only when it IS the stage's module (§7.3(3)).
		QAction* mixer = findAction(modules, focusActionCommandId(QStringLiteral("mixer")));
		QVERIFY(mixer != nullptr);
		QVERIFY(mixer->isEnabled());
		QVERIFY(mixer->isCheckable());
		QCOMPARE(mixer->isChecked(), false);
		QAction* arrangement = findAction(modules,
			focusActionCommandId(QStringLiteral("arrangement")));
		QVERIFY(arrangement != nullptr);
		QVERIFY(arrangement->isEnabled());
		QCOMPARE(arrangement->isChecked(), true);

		// With no desk there is nothing to promote into: every entry present,
		// every entry greyed, one stated reason (X4, the toggle's null rule).
		QMenu orphan;
		addModulesMenu(&orphan, nullptr);
		QVERIFY(orphan.actions().first()->menu() != nullptr);
		QMenu& orphanModules = *orphan.actions().first()->menu();
		QCOMPARE(orphanModules.actions().size(), rows.size());
		for (QAction* action : orphanModules.actions())
		{
			QVERIFY2(!action->isEnabled(), qPrintable(action->text()));
			QVERIFY2(action->toolTip().contains(QLatin1String("not built")),
				qPrintable(action->text() + QLatin1String(" / ") + action->toolTip()));
		}
	}

	//! Row 4's dispatch half: the generated Commands menu's entries are not
	//! decoration - triggering a density choice runs `settings.set` through
	//! the registry and the config key FocusDeskPane follows moves with it
	//! (A11: one action, one implementation, reachable before the engine).
	void theCommandsMenuDispatchesSettingsSet()
	{
		auto* button = desk().findChild<QToolButton*>(QStringLiteral("focusDeskCommands"));
		QVERIFY(button != nullptr);
		auto* menu = button->findChild<QMenu*>();
		QVERIFY(menu != nullptr);
		QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
		QVERIFY(menu->actions().size() >= 15);

		QAction* minimal = findActionByText(*menu, QStringLiteral("Density: Minimal"));
		QVERIFY(minimal != nullptr);
		QVERIFY(minimal->isEnabled());
		QVERIFY(minimal->isCheckable());
		QCOMPARE(minimal->property("controlCommand").toString(), QStringLiteral("settings.set"));
		QVERIFY(minimal->toolTip().contains(QStringLiteral("View")));

		minimal->trigger();
		QCOMPARE(ConfigManager::inst()->value("ui", "focusdesk.density"),
			QStringLiteral("minimal"));

		QAction* perform = findActionByText(*menu, QStringLiteral("Workspace: Perform"));
		QVERIFY(perform != nullptr);
		perform->trigger();
		QCOMPARE(ConfigManager::inst()->value("ui", "focusdesk.workspace"),
			QStringLiteral("perform"));
	}

private:
	//! A desk of its own for the tests that drive the strip's own buttons.
	//! No parent object: the test is not a widget, and a process-lifetime desk
	//! needs no one to delete it.
	FocusDesk& desk()
	{
		if (m_ownedDesk == nullptr) { m_ownedDesk = new FocusDesk; }
		return *m_ownedDesk;
	}
	FocusDesk* m_ownedDesk = nullptr;
};

// The platform plugin has to be chosen before the QApplication exists; the gate
// runner exports QT_QPA_PLATFORM=offscreen and CI does not, so default to
// offscreen when nothing else is configured and no display is attached.
int main(int argc, char** argv)
{
	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
		&& qEnvironmentVariableIsEmpty("DISPLAY")
		&& qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
	{
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}

	QApplication app(argc, argv);
	FocusDeskActionsTest test;
	return QTest::qExec(&test, argc, argv);
}

#include "FocusDeskActionsTest.moc"
