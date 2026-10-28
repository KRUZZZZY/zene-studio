/*
 * ControllerMappingEditorTest.cpp - the MIDI mapping editor and the controller-script menu (Q050-081)
 *                                       reversibility contract (0.5.0 F6)
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

/*! Q050-081's editor half, headless (offscreen), over the real menus:
 *   - Customise > MIDI Mappings... lists a binding, its actions are the stable command ids, an edit made from
 *     the list (Soft Takeover On) is applied, and the binding and its flag survive project.save / project.open;
 *   - Customise > Controller Scripts > Start Script... runs controller.script_start from its form, Script State
 *     reads it, Stop Script... stops it. */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>

#include "CommandForm.h"
#include "CommandListDialog.h"
#include "CommandLists.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControllerScript.h"
#include "Engine.h"
#include "MenuTestSupport.h"
#include "ShellCommandMenus.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

template<class T>
T* visible()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (auto* found = qobject_cast<T*>(widget); found != nullptr && found->isVisible()) { return found; }
	}
	return nullptr;
}

void clickRun(CommandForm* form)
{
	for (QPushButton* button : form->findChildren<QPushButton*>())
	{
		if (button->text().remove(QLatin1Char('&')) == QStringLiteral("Run")) { QTest::mouseClick(button, Qt::LeftButton); }
	}
}

int rowFor(CommandListDialog* dialog, const QString& control)
{
	for (int i = 0; i < dialog->rowCount(); ++i)
	{
		if (dialog->row(i).value(QStringLiteral("control")).toString() == control) { return i; }
	}
	return -1;
}

QJsonObject boundControl(const QString& control)
{
	for (const QJsonValue& value : run(QStringLiteral("controller.surface_state")).result.value(QStringLiteral("controls")).toArray())
	{
		if (value.toObject().value(QStringLiteral("control")).toString() == control) { return value.toObject(); }
	}
	return {};
}

} // namespace

class ControllerMappingEditorTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControllerScript::stopAll();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theMappingEditorListsEditsAndPersistsBindings()
	{
		const QString trackId = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}})
			.result.value(QStringLiteral("track")).toString();
		ControlResult ignored;
		Track* track = control::resolveTrack(trackId, &ignored);
		QVERIFY(track != nullptr);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), trackId}, {QStringLiteral("name"), QStringLiteral("Mapped Lead")}}).ok);
		const QString control = track->findChildren<FloatModel*>().first()->fullDisplayName();
		QVERIFY(run(QStringLiteral("controller.map"), {{QStringLiteral("target"), control}, {QStringLiteral("channel"), 4},
			{QStringLiteral("controller"), 11}}).ok);

		// The editor's actions are the registered commands, by id.
		const ListSpec spec = CommandLists::controllerMappings();
		QCOMPARE(spec.listId, QStringLiteral("controller.surface_state"));
		QStringList ids;
		for (const RowAction& action : spec.actions) { ids << action.id; }
		QCOMPARE(ids, (QStringList{QStringLiteral("controller.soft_takeover"), QStringLiteral("controller.soft_takeover"),
			QStringLiteral("controller.feedback"), QStringLiteral("controller.feedback"), QStringLiteral("controller.unmap")}));
		QCOMPARE(spec.extras.first().call.id, QStringLiteral("controller.map"));

		QMenu menu;
		ShellCommandMenus::populateCustomise(&menu);
		QAction* mappings = menuAction(&menu, {QStringLiteral("MIDI Mappings...")});
		QVERIFY(mappings != nullptr);
		QCOMPARE(mappings->property("controlCommand").toString(), QStringLiteral("controller.surface_state"));
		mappings->trigger();
		CommandListDialog* dialog = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((dialog = visible<CommandListDialog>()) != nullptr, 3000);
		const int row = rowFor(dialog, control);
		QVERIFY2(row >= 0, "the editor does not list the binding");
		QCOMPARE(dialog->row(row).value(QStringLiteral("channel")).toInt(), 4);
		QCOMPARE(dialog->row(row).value(QStringLiteral("controller")).toInt(), 11);
		QCOMPARE(dialog->row(row).value(QStringLiteral("soft_takeover")).toBool(), false);

		// Edit from the list: Soft Takeover On (action 0).
		QVERIFY(dialog->runAction(row, 0).ok);
		dialog->refresh();
		QCOMPARE(dialog->row(rowFor(dialog, control)).value(QStringLiteral("soft_takeover")).toBool(), true);
		dialog->close();

		// Persist: save and reopen the project; the binding and its flag come back.
		const QString path = m_dir.filePath(QStringLiteral("mapped.mmpz"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), path}}).ok);
		QVERIFY(run(QStringLiteral("controller.unmap"), {{QStringLiteral("target"), control}}).ok);
		QVERIFY(boundControl(control).isEmpty());
		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), path}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		const QJsonObject restored = boundControl(control);
		QVERIFY2(!restored.isEmpty(), "the binding did not survive save and reopen");
		QCOMPARE(restored.value(QStringLiteral("channel")).toInt(), 4);
		QCOMPARE(restored.value(QStringLiteral("controller")).toInt(), 11);
		QCOMPARE(restored.value(QStringLiteral("soft_takeover")).toBool(), true);
		CommandListDialog reread(CommandLists::controllerMappings());
		QVERIFY(rowFor(&reread, control) >= 0);
	}

	void theScriptMenuStartsReadsAndStopsAScript()
	{
		const QString path = m_dir.filePath(QStringLiteral("menu.lua"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("function on_midi(e) end\n");
		file.close();

		QMenu menu;
		ShellCommandMenus::populateCustomise(&menu);
		const QStringList base{QStringLiteral("Controller Scripts")};
		QAction* start = menuAction(&menu, base + QStringList{QStringLiteral("Start Script...")});
		QAction* state = menuAction(&menu, base + QStringList{QStringLiteral("Script State")});
		QAction* stop = menuAction(&menu, base + QStringList{QStringLiteral("Stop Script...")});
		QVERIFY(start != nullptr && state != nullptr && stop != nullptr);
		QCOMPARE(start->property("controlCommand").toString(), QStringLiteral("controller.script_start"));

		start->trigger();
		CommandForm* form = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((form = visible<CommandForm>()) != nullptr, 3000);
		QVERIFY(form->setField(QStringLiteral("name"), QStringLiteral("menu")));
		QVERIFY(form->setField(QStringLiteral("path"), path));
		clickRun(form);
		QTRY_VERIFY_WITH_TIMEOUT(ControllerScript::find(QStringLiteral("menu")) != nullptr, 3000);
		form->close();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

		state->trigger();
		QVERIFY(ControllerScript::find(QStringLiteral("menu")) != nullptr);
		stop->trigger();
		form = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((form = visible<CommandForm>()) != nullptr, 3000);
		QVERIFY(form->setField(QStringLiteral("name"), QStringLiteral("menu")));
		clickRun(form);
		QTRY_VERIFY_WITH_TIMEOUT(ControllerScript::find(QStringLiteral("menu")) == nullptr, 3000);
		form->close();
	}
};

QTEST_MAIN(ControllerMappingEditorTest)
#include "ControllerMappingEditorTest.moc"
