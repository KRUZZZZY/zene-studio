/*
 * CommandFormTest.cpp - the schema-built command form, its dispatcher and the list dialog
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

/*! The 0.5.0 W1 interface spine, over fake commands (no engine):
 *  - CommandForm builds one typed field per schema property, leaves fixed arguments out of the
 *    fields and in the call, omits an empty optional field, refuses a missing required one or
 *    text of the wrong type before running, and keeps a refusal on screen;
 *  - runCommand dispatches at once when nothing is left to ask, otherwise opens the form;
 *  - addCommandChoices offers every enum value (On/Off for a boolean) and each runs the command;
 *  - CommandListDialog shows the rows a list command answers - one level down too - and a row
 *    action maps the row's fields onto another command's arguments. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QMenu>
#include <QToolButton>

#include "CommandForm.h"
#include "CommandListDialog.h"
#include "ControlRegistry.h"
#include "MenuTestSupport.h"
#include "ShortcutMap.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

QJsonObject g_lastArgs;
int g_fixedRuns = 0;

QJsonObject prop(const char* type)
{
	return QJsonObject{{QStringLiteral("type"), QLatin1String(type)}};
}

void add(const char* id, const QJsonObject& properties, const QJsonArray& required,
	std::function<ControlResult(const QJsonObject&)> handler)
{
	ControlCommand cmd;
	cmd.id = QLatin1String(id);
	cmd.group = cmd.id.section(QLatin1Char('.'), 0, 0);
	cmd.verb = cmd.id.section(QLatin1Char('.'), 1);
	cmd.description = QStringLiteral("A fake for the command form test. It does nothing else.");
	cmd.argsSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), properties}, {QStringLiteral("required"), required}};
	cmd.resultSchema = prop("object");
	cmd.requiresEngine = false;
	cmd.handler = std::move(handler);
	ControlRegistry::instance()->registerCommand(cmd);
}

void registerFakes()
{
	QJsonObject count = prop("integer");
	count.insert(QStringLiteral("minimum"), 1);
	count.insert(QStringLiteral("maximum"), 9);
	QJsonObject mode = prop("string");
	mode.insert(QStringLiteral("enum"), QJsonArray{QStringLiteral("a"), QStringLiteral("b")});
	add("formtest.echo", {{QStringLiteral("name"), prop("string")}, {QStringLiteral("count"), count},
		{QStringLiteral("ratio"), prop("number")}, {QStringLiteral("loud"), prop("boolean")},
		{QStringLiteral("mode"), mode}, {QStringLiteral("items"), prop("array")}, {QStringLiteral("track"), prop("string")}},
		{QStringLiteral("name")}, [](const QJsonObject& args) {
			g_lastArgs = args;
			if (args.value(QStringLiteral("name")).toString() == QStringLiteral("bad"))
			{
				return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("bad name"));
			}
			return ControlResult::success(args);
		});
	add("formtest.fixed_only", {{QStringLiteral("track"), prop("string")}}, {QStringLiteral("track")},
		[](const QJsonObject&) { ++g_fixedRuns; return ControlResult::success(); });
	add("formtest.list", {}, {}, [](const QJsonObject&) {
		const QJsonArray rows{QJsonObject{{QStringLiteral("id"), QStringLiteral("r1")}, {QStringLiteral("n"), 1}},
			QJsonObject{{QStringLiteral("id"), QStringLiteral("r2")}, {QStringLiteral("n"), 2}}};
		const QJsonArray groups{QJsonObject{{QStringLiteral("id"), QStringLiteral("g")},
			{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("k"), 1}}, QJsonObject{{QStringLiteral("k"), 2}}}}}};
		return ControlResult::success({{QStringLiteral("rows"), rows}, {QStringLiteral("groups"), groups}, {QStringLiteral("total"), 2}});
	});
	QJsonObject keys = prop("string");
	keys.insert(QStringLiteral("format"), QStringLiteral("keys"));
	add("formtest.editors", {{QStringLiteral("keys"), keys}, {QStringLiteral("file"), prop("string")},
		{QStringLiteral("out_dir"), prop("string")}, {QStringLiteral("color"), prop("string")}}, {},
		[](const QJsonObject& args) { g_lastArgs = args; return ControlResult::success(); });
	add("formtest.take", {{QStringLiteral("item"), prop("string")}}, {QStringLiteral("item")}, [](const QJsonObject& args) {
		g_lastArgs = args;
		return ControlResult::success();
	});
}

CommandCall echo(QJsonObject fixed = {}, bool ask = true)
{
	return CommandCall{QStringLiteral("formtest.echo"), std::move(fixed), {}, QString(), ask};
}

} // namespace

class CommandFormTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { registerFakes(); }

	void fieldsFollowTheSchemaAndLeaveFixedOut()
	{
		CommandForm form(echo({{QStringLiteral("track"), QStringLiteral("trk-1")}}));
		const QStringList fields = form.fields();
		QCOMPARE(fields.size(), 6);
		QVERIFY(!fields.contains(QStringLiteral("track")));
		QVERIFY(fields.contains(QStringLiteral("name")) && fields.contains(QStringLiteral("items")));
		QVERIFY(form.findChild<QWidget*>(QStringLiteral("fixed:track")) != nullptr);
	}

	void collectTypesEachFieldAndOmitsTheEmptyOnes()
	{
		CommandForm form(echo({{QStringLiteral("track"), QStringLiteral("trk-1")}}));
		QVERIFY(form.setField(QStringLiteral("name"), QStringLiteral("x")));
		QJsonObject args;
		QString error;
		QVERIFY2(form.collect(&args, &error), qPrintable(error));
		QCOMPARE(args, (QJsonObject{{QStringLiteral("track"), QStringLiteral("trk-1")}, {QStringLiteral("name"), QStringLiteral("x")}}));

		QVERIFY(form.setField(QStringLiteral("count"), 3));
		QVERIFY(form.setField(QStringLiteral("ratio"), 0.5));
		QVERIFY(form.setField(QStringLiteral("loud"), true));
		QVERIFY(form.setField(QStringLiteral("mode"), QStringLiteral("b")));
		QVERIFY(form.setField(QStringLiteral("items"), QJsonArray{1, 2}));
		QVERIFY2(!form.setField(QStringLiteral("mode"), QStringLiteral("z")), "an enum field took a value outside it");
		QVERIFY(!form.setField(QStringLiteral("nope"), 1));
		QVERIFY2(form.collect(&args, &error), qPrintable(error));
		QCOMPARE(args.value(QStringLiteral("count")).toInt(), 3);
		QCOMPARE(args.value(QStringLiteral("ratio")).toDouble(), 0.5);
		QCOMPARE(args.value(QStringLiteral("loud")).toBool(), true);
		QCOMPARE(args.value(QStringLiteral("mode")).toString(), QStringLiteral("b"));
		QCOMPARE(args.value(QStringLiteral("items")).toArray(), (QJsonArray{1, 2}));
	}

	void aMissingRequiredOrMistypedFieldIsRefusedBeforeRunning()
	{
		CommandForm form(echo());
		QJsonObject args;
		QString error;
		QVERIFY(!form.collect(&args, &error));
		QVERIFY2(error.contains(QStringLiteral("Name")), qPrintable(error));
		form.setField(QStringLiteral("name"), QStringLiteral("x"));
		form.setField(QStringLiteral("count"), QStringLiteral("abc"));
		QVERIFY(!form.collect(&args, &error));
		QVERIFY2(error.contains(QStringLiteral("Count")), qPrintable(error));
		form.setField(QStringLiteral("count"), QString());
		form.setField(QStringLiteral("items"), QStringLiteral("{not json"));
		QVERIFY(!form.collect(&args, &error));
		g_lastArgs = {};
		form.accept();
		QVERIFY2(g_lastArgs.isEmpty(), "the command ran with an argument the form could not read");
		QVERIFY(!form.errorText().isEmpty());
	}

	void acceptRunsTheCommandAndKeepsARefusalOnScreen()
	{
		CommandForm form(echo());
		QSignalSpy ran(&form, &CommandForm::ran);
		form.setField(QStringLiteral("name"), QStringLiteral("bad"));
		form.accept();
		QVERIFY(!form.result().ok);
		QVERIFY(form.errorText().contains(QStringLiteral("bad name")));
		form.setField(QStringLiteral("name"), QStringLiteral("good"));
		form.accept();
		QVERIFY(form.result().ok);
		QCOMPARE(form.result().result.value(QStringLiteral("name")).toString(), QStringLiteral("good"));
		QCOMPARE(form.result().result.value(QStringLiteral("name")).toString(), g_lastArgs.value(QStringLiteral("name")).toString());
		QCOMPARE(ran.count(), 2);
	}

	void runCommandDispatchesWhenNothingIsLeftToAsk()
	{
		CommandForm* form = nullptr;
		g_fixedRuns = 0;
		const ControlResult fixed = runCommand(nullptr,
			CommandCall{QStringLiteral("formtest.fixed_only"), {{QStringLiteral("track"), QStringLiteral("trk-1")}}, {}, QString(), true}, &form);
		QVERIFY(fixed.ok);
		QCOMPARE(g_fixedRuns, 1);
		QVERIFY(form == nullptr);
		QVERIFY(runCommand(nullptr, echo({{QStringLiteral("name"), QStringLiteral("n")}}, false), &form).ok);
		QVERIFY(form == nullptr);
		const ControlResult asked = runCommand(nullptr, echo(), &form);
		QCOMPARE(asked.errorKind, ControlErrorKind::Busy);
		QVERIFY(form != nullptr);
		QVERIFY(form->isVisible());
		form->close();
	}

	void choicesOfferEveryEnumValueAndOnOffForABoolean()
	{
		QMenu menu;
		const auto make = [] { return echo({{QStringLiteral("name"), QStringLiteral("n")}}); };
		QMenu* modes = addCommandChoices(&menu, QStringLiteral("Mode"), QStringLiteral("formtest.echo"), QStringLiteral("mode"), make);
		QCOMPARE(modes->actions().size(), 2);
		menuAction(&menu, {QStringLiteral("Mode"), QStringLiteral("b")})->trigger();
		QCOMPARE(g_lastArgs.value(QStringLiteral("mode")).toString(), QStringLiteral("b"));
		addCommandChoices(&menu, QStringLiteral("Loud"), QStringLiteral("formtest.echo"), QStringLiteral("loud"), make);
		menuAction(&menu, {QStringLiteral("Loud"), QStringLiteral("Off")})->trigger();
		QCOMPARE(g_lastArgs.value(QStringLiteral("loud")), QJsonValue(false));
		QMenu* none = addCommandChoices(&menu, QStringLiteral("None"), QStringLiteral("formtest.echo"), QStringLiteral("name"), make);
		QVERIFY2(!none->isEnabled(), "a free string offered choices");
	}

	void anActionForAnUnknownCommandIsDisabled()
	{
		QMenu menu;
		QAction* known = addCommandAction(&menu, QStringLiteral("Known"), QStringLiteral("formtest.echo"), [] { return echo(); });
		QAction* unknown = addCommandAction(&menu, QStringLiteral("Unknown"), QStringLiteral("formtest.missing"), [] { return echo(); });
		QVERIFY(known->isEnabled());
		QVERIFY(!unknown->isEnabled());
		QCOMPARE(known->property("controlCommand").toString(), QStringLiteral("formtest.echo"));
		QVERIFY(selectionContext(QStringLiteral("formtest.echo")).isEmpty());
	}

	//! L13: a `format: keys` field captures a shortcut; a path or colour field has a picker.
	void keysPathsAndColoursGetTheirOwnEditors()
	{
		CommandForm form(CommandCall{QStringLiteral("formtest.editors"), {}, {}, QString(), true});
		QVERIFY(qobject_cast<QKeySequenceEdit*>(form.findChild<QWidget*>(QStringLiteral("field:keys"))) != nullptr);
		QVERIFY(form.setField(QStringLiteral("keys"), QStringLiteral("Ctrl+Shift+K")));
		QJsonObject args;
		QString error;
		QVERIFY2(form.collect(&args, &error), qPrintable(error));
		QCOMPARE(args.value(QStringLiteral("keys")).toString(), QStringLiteral("Ctrl+Shift+K"));
		for (const char* key : {"file", "out_dir", "color"})
		{
			QWidget* field = form.findChild<QWidget*>(QStringLiteral("field:") + QLatin1String(key));
			QVERIFY2(field != nullptr && field->parentWidget()->findChild<QToolButton*>() != nullptr,
				qPrintable(QStringLiteral("no picker beside ") + QLatin1String(key)));
		}
		QCOMPARE(ShortcutMap::portable(QStringLiteral("ctrl+k")), QStringLiteral("Ctrl+K"));
		ShortcutMap::Binding a;
		a.id = QStringLiteral("A");
		a.keys = QStringLiteral("Ctrl+K");
		ShortcutMap::Binding b;
		b.id = QStringLiteral("B");
		QCOMPARE(ShortcutMap::conflicts({a, b}, QStringLiteral("Ctrl+K"), QStringLiteral("B")), QStringList{QStringLiteral("A")});
		QVERIFY(ShortcutMap::conflicts({a, b}, QStringLiteral("Ctrl+K"), QStringLiteral("A")).isEmpty());
	}

	void theListDialogMapsARowOntoAnotherCommand()
	{
		ListSpec spec;
		spec.title = QStringLiteral("Rows");
		spec.listId = QStringLiteral("formtest.list");
		spec.arrayKey = QStringLiteral("rows");
		spec.columns = {QStringLiteral("id"), QStringLiteral("n")};
		spec.summaryKeys = {QStringLiteral("total")};
		RowAction take;
		take.label = QStringLiteral("Take");
		take.id = QStringLiteral("formtest.take");
		take.fromRow = {{QStringLiteral("item"), QStringLiteral("id")}};
		spec.actions = {take};
		CommandListDialog dialog(spec);
		QCOMPARE(dialog.rowCount(), 2);
		QCOMPARE(dialog.listResult().value(QStringLiteral("total")).toInt(), 2);
		QVERIFY(dialog.runAction(1, 0).ok);
		QCOMPARE(g_lastArgs.value(QStringLiteral("item")).toString(), QStringLiteral("r2"));
		QVERIFY(dialog.statusText().contains(QStringLiteral("done")));
		QCOMPARE(dialog.runAction(5, 0).errorKind, ControlErrorKind::InvalidArgs);

		spec.arrayKey = QStringLiteral("groups");
		spec.nestedKeys = {QStringLiteral("items")};
		spec.parentFields = {QStringLiteral("id")};
		const QJsonArray rows = listRows(spec, dialog.listResult());
		QCOMPARE(rows.size(), 2);
		QCOMPARE(rows.at(1).toObject().value(QStringLiteral("k")).toInt(), 2);
		QCOMPARE(rows.at(1).toObject().value(QStringLiteral("parent_id")).toString(), QStringLiteral("g"));
	}
};

QTEST_MAIN(CommandFormTest)
#include "CommandFormTest.moc"
