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
 *    action maps the row's fields onto another command's arguments;
 *  - 0.5.0 themes 4 and 5: labels with units, bounds and blank guidance, worded choices, comma lists,
 *    raw JSON only under Advanced, input and output paths told apart, the answer shown, and a command
 *    that cannot be undone flagged. */

#include <QtTest>

#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

#include "CommandForm.h"
#include "CommandMetadata.h"
#include "CommandListDialog.h"
#include "ControlRegistry.h"
#include "ListTable.h"
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
	std::function<ControlResult(const QJsonObject&)> handler, bool mutating = false)
{
	ControlCommand cmd;
	cmd.mutating = mutating;
	cmd.id = QLatin1String(id);
	cmd.group = cmd.id.section(QLatin1Char('.'), 0, 0);
	cmd.verb = cmd.id.section(QLatin1Char('.'), 1);
	cmd.description = QStringLiteral("A fake for the command form test. It does nothing else.");
	cmd.argsSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), properties}, {QStringLiteral("required"), required}};
	cmd.resultSchema = prop("object");
	cmd.requiresEngine = false;
	cmd.handler = std::move(handler);
	if (!ControlRegistry::instance()->hasCommand(cmd.id)) { ControlRegistry::instance()->registerCommand(cmd); }  // a real one stays
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
	QJsonObject gain = prop("number");
	gain.insert(QStringLiteral("minimum"), -60);
	gain.insert(QStringLiteral("maximum"), 6);
	QJsonObject snap = prop("string");
	snap.insert(QStringLiteral("enum"), QJsonArray{QStringLiteral("to_grid"), QStringLiteral("off")});
	QJsonObject names = prop("array");
	names.insert(QStringLiteral("items"), prop("string"));
	add("formtest.units", {{QStringLiteral("level"), prop("integer")}, {QStringLiteral("snap"), snap},
		{QStringLiteral("names"), names}, {QStringLiteral("gain_db"), gain}}, {QStringLiteral("gain_db")},
		[](const QJsonObject& args) { g_lastArgs = args; return ControlResult::success({{QStringLiteral("gain_db"), -3}}); }, true);
	// A real id the reversibility table classes irreversible, registered as a fake: the flag comes from the table.
	add("safestart.clear", {{QStringLiteral("note"), prop("string")}}, {}, [](const QJsonObject&) { return ControlResult::success(); }, true);
}

//! Whether @a widget sits inside the form's Advanced section.
bool underAdvanced(const QWidget* widget)
{
	for (const QWidget* at = widget; at != nullptr; at = at->parentWidget())
	{
		if (at->objectName() == QStringLiteral("commandFormAdvanced")) { return true; }
	}
	return false;
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

	void aFailedRerunDropsTheAnswerThePreviousRunLeft()
	{
		CommandForm form(echo());
		form.setField(QStringLiteral("name"), QStringLiteral("good"));
		form.accept();
		QVERIFY(form.result().ok);
		const QString firstAnswer = form.resultText();
		QVERIFY2(!firstAnswer.isEmpty(), "the successful run showed no answer to go stale");

		QPushButton* copy = nullptr;
		for (QPushButton* button : form.findChildren<QPushButton*>())
		{
			if (button->text().contains(QStringLiteral("Copy"), Qt::CaseInsensitive)) { copy = button; }
		}
		QVERIFY(copy != nullptr);
		QVERIFY(!copy->isHidden());

		form.setField(QStringLiteral("name"), QStringLiteral("bad"));
		form.accept();
		QVERIFY(!form.result().ok);
		QVERIFY(!form.errorText().isEmpty());
		QVERIFY2(form.resultText().isEmpty(), "the failed rerun left the previous answer on screen");
		QVERIFY2(copy->isHidden(), "the failed rerun left Copy Result enabled to copy the failed result");
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

	//! 0.5.0 theme 5: a label in words with its unit, the bounds, what a blank means; required fields first.
	void eachFieldSaysWhatItWants()
	{
		QCOMPARE(fieldLabel(QStringLiteral("gain_db")), QStringLiteral("Gain (dB)"));
		QCOMPARE(fieldLabel(QStringLiteral("track_id")), QStringLiteral("Track"));
		QCOMPARE(fieldLabel(QStringLiteral("cpu_percent")), QStringLiteral("CPU (%)"));
		QCOMPARE(fieldLabel(QStringLiteral("missing_count")), QStringLiteral("Missing count"));
		QCOMPARE(fieldUnit(QStringLiteral("length_ticks"), prop("integer")), QStringLiteral("ticks"));
		QVERIFY2(fieldUnit(QStringLiteral("name_db"), prop("string")).isEmpty(), "a text field was given a unit");
		CommandForm form(CommandCall{QStringLiteral("formtest.units"), {}, {}, QString(), true});
		QCOMPARE(form.windowTitle(), commandName(QStringLiteral("formtest.units")));
		QCOMPARE(form.fields().first(), QStringLiteral("gain_db"));
		const auto* gainHelp = form.findChild<QLabel*>(QStringLiteral("help:gain_db"));
		QVERIFY(gainHelp != nullptr);
		QVERIFY2(gainHelp->text().contains(QStringLiteral("-60 to 6 dB")), qPrintable(gainHelp->text()));
		QVERIFY2(gainHelp->text().contains(QStringLiteral("Required")), qPrintable(gainHelp->text()));
		QVERIFY(form.findChild<QLabel*>(QStringLiteral("help:level"))->text().contains(QStringLiteral("blank")));
		QVERIFY(form.findChild<QWidget*>(QStringLiteral("field:gain_db"))->accessibleName().contains(QStringLiteral("required")));
		// Out of its bounds: refused before running, naming the field and the range.
		QVERIFY(form.setField(QStringLiteral("gain_db"), 99));
		QJsonObject args;
		QString error;
		QVERIFY(!form.collect(&args, &error));
		QVERIFY2(error.contains(QStringLiteral("Gain (dB)")) && error.contains(QStringLiteral("-60 to 6")), qPrintable(error));
		QVERIFY(form.setField(QStringLiteral("gain_db"), -6));
		QVERIFY2(form.collect(&args, &error), qPrintable(error));
		QCOMPARE(args.value(QStringLiteral("gain_db")).toDouble(), -6.0);
	}

	//! 0.5.0 theme 5: an enum offers its values in words (the blank entry says "(Default)"), a list of plain
	//! values is typed as "a, b", and raw JSON waits under Advanced.
	void choicesListsAndRawJsonHaveTheirPlaces()
	{
		CommandForm form(CommandCall{QStringLiteral("formtest.units"), {}, {}, QString(), true});
		auto* snap = form.findChild<QComboBox*>(QStringLiteral("field:snap"));
		QVERIFY(snap != nullptr);
		QCOMPARE(snap->itemText(0), QStringLiteral("(Default)"));
		QVERIFY(form.setField(QStringLiteral("snap"), QStringLiteral("to_grid")));
		QCOMPARE(snap->currentText(), QStringLiteral("To grid"));
		QVERIFY(form.setField(QStringLiteral("names"), QJsonArray{QStringLiteral("a"), QStringLiteral("b")}));
		QCOMPARE(form.findChild<QLineEdit*>(QStringLiteral("field:names"))->text(), QStringLiteral("a, b"));
		form.findChild<QLineEdit*>(QStringLiteral("field:names"))->setText(QStringLiteral("x, y"));
		QVERIFY(form.setField(QStringLiteral("gain_db"), 0));
		QJsonObject args;
		QString error;
		QVERIFY2(form.collect(&args, &error), qPrintable(error));
		QCOMPARE(args.value(QStringLiteral("snap")).toString(), QStringLiteral("to_grid"));
		QCOMPARE(args.value(QStringLiteral("names")).toArray(), (QJsonArray{QStringLiteral("x"), QStringLiteral("y")}));
		QVERIFY2(!underAdvanced(form.findChild<QWidget*>(QStringLiteral("field:names"))), "a list of plain values was hidden as JSON");
		CommandForm echoForm(echo());
		QVERIFY2(underAdvanced(echoForm.findChild<QWidget*>(QStringLiteral("field:items"))), "a raw JSON field was not under Advanced");
		QVERIFY(!underAdvanced(echoForm.findChild<QWidget*>(QStringLiteral("field:name"))));
		QVERIFY(echoForm.findChild<QWidget*>(QStringLiteral("commandFormAdvanced"))->isHidden());
	}

	//! 0.5.0 theme 5: a path says whether the command reads it or writes it, and its picker matches.
	void pathsAreToldApartAsInputOrOutput()
	{
		QCOMPARE(pathRole(QStringLiteral("formtest.editors"), QStringLiteral("file")), PathRole::OpenFile);
		QCOMPARE(pathRole(QStringLiteral("theme.export"), QStringLiteral("file")), PathRole::SaveFile);
		QCOMPARE(pathRole(QStringLiteral("project.merge"), QStringLiteral("report")), PathRole::SaveFile);
		QCOMPARE(pathRole(QStringLiteral("project.merge"), QStringLiteral("base")), PathRole::OpenFile);
		QCOMPARE(pathRole(QStringLiteral("formtest.editors"), QStringLiteral("out_dir")), PathRole::SaveFolder);
		QCOMPARE(pathRole(QStringLiteral("formtest.editors"), QStringLiteral("sample_dir")), PathRole::OpenFolder);
		QCOMPARE(pathRole(QStringLiteral("formtest.editors"), QStringLiteral("color")), PathRole::None);
		QVERIFY(pathRoleText(PathRole::OpenFile).startsWith(QStringLiteral("Input")));
		QVERIFY(pathRoleText(PathRole::SaveFile).startsWith(QStringLiteral("Output")));
		CommandForm form(CommandCall{QStringLiteral("formtest.editors"), {}, {}, QString(), true});
		const auto pickerOf = [&form](const char* key) {
			return form.findChild<QWidget*>(QStringLiteral("field:") + QLatin1String(key))->parentWidget()->findChild<QToolButton*>()->text();
		};
		QCOMPARE(pickerOf("file"), QStringLiteral("Open…"));
		QCOMPARE(pickerOf("out_dir"), QStringLiteral("Choose Folder…"));
		QVERIFY(form.findChild<QLabel*>(QStringLiteral("help:file"))->text().contains(QStringLiteral("Input")));
		QVERIFY(form.findChild<QLabel*>(QStringLiteral("help:out_dir"))->text().contains(QStringLiteral("Output")));
	}

	//! 0.5.0 themes 4 and 5: a read keeps its answer on screen (and Close is not Cancel), a change closes, and a
	//! command that cannot be undone says so; the id is shown, secondary.
	void theResultIsShownAndAnIrreversibleCommandWarns()
	{
		CommandForm read(echo());
		QSignalSpy accepted(&read, &QDialog::accepted);
		QSignalSpy rejected(&read, &QDialog::rejected);
		read.setField(QStringLiteral("name"), QStringLiteral("good"));
		read.accept();
		QVERIFY2(read.resultText().contains(QStringLiteral("good")), qPrintable(read.resultText()));
		read.reject();
		QCOMPARE(accepted.count(), 1);
		QCOMPARE(rejected.count(), 0);

		CommandForm change(CommandCall{QStringLiteral("formtest.units"), {}, {}, QString(), true});
		QSignalSpy closed(&change, &QDialog::accepted);
		change.setField(QStringLiteral("gain_db"), -3);
		change.accept();
		QVERIFY(change.result().ok);
		QCOMPARE(closed.count(), 1);
		QVERIFY2(change.resultText().isEmpty(), "a change kept the form open");
		QVERIFY(change.warningText().isEmpty());
		QVERIFY(change.findChild<QLabel*>(QStringLiteral("commandFormId"))->text().contains(QStringLiteral("formtest.units")));

		CommandForm risky(CommandCall{QStringLiteral("safestart.clear"), {}, {}, QString(), true});
		QVERIFY2(risky.warningText().contains(QStringLiteral("cannot be undone")), qPrintable(risky.warningText()));
		QVERIFY(resultSummary({{QStringLiteral("gain_db"), -3}, {QStringLiteral("rows"), QJsonArray{QJsonObject{}}}})
			.contains(QStringLiteral("Gain (dB): -3 dB")));
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

	//! Design theme 18: the shared list finds, sorts by a column (numbers by value, a third click back to the list's
	//! order), runs its primary action on Enter - on the row chosen, wherever the sort put it - offers every action
	//! from the Menu key or Shift+F10, and keeps each outcome in its Results log.
	void theListFindsSortsAndActsFromTheKeyboard()
	{
		ListSpec spec;
		spec.title = QStringLiteral("Rows");
		spec.listId = QStringLiteral("formtest.list");
		spec.arrayKey = QStringLiteral("rows");
		spec.columns = {QStringLiteral("id"), QStringLiteral("n")};
		RowAction take;
		take.label = QStringLiteral("Take");
		take.id = QStringLiteral("formtest.take");
		take.fromRow = {{QStringLiteral("item"), QStringLiteral("id")}};
		spec.actions = {take};
		CommandListDialog dialog(spec);
		QCOMPARE(dialog.find(QStringLiteral("r2")), 1);
		QCOMPARE(dialog.find(QString()), 2);
		ListTable* table = dialog.table();
		table->sortRows(1, false);  // by n, highest first
		QCOMPARE(table->item(0, 0)->text(), QStringLiteral("r2"));
		table->selectDataRow(0);  // r1, shown second now
		QCOMPARE(table->currentRow(), 1);
		g_lastArgs = {};
		QTest::keyClick(table, Qt::Key_Return);
		QCOMPARE(g_lastArgs.value(QStringLiteral("item")).toString(), QStringLiteral("r1"));
		QCOMPARE(table->currentDataRow(), 0);  // the refresh after the action kept the row and the sort
		QCOMPARE(table->item(0, 0)->text(), QStringLiteral("r2"));
		QVERIFY2(dialog.resultsText().contains(QStringLiteral("Take: done")), qPrintable(dialog.resultsText()));
		QObject::disconnect(table, &ListTable::rowMenuRequested, &dialog, nullptr);  // no menu to block the test
		QSignalSpy menus(table, &ListTable::rowMenuRequested);
		QTest::keyClick(table, Qt::Key_F10, Qt::ShiftModifier);
		QCOMPARE(menus.count(), 1);
		QCOMPARE(menus.first().first().toInt(), 0);
		QMenu menu;
		dialog.buildRowMenu(&menu, 0);
		QVERIFY(menu.defaultAction() != nullptr && menu.defaultAction()->text() == QStringLiteral("Take"));
		table->sortRows(-1);
		QCOMPARE(table->item(0, 0)->text(), QStringLiteral("r1"));
	}
};

QTEST_MAIN(CommandFormTest)
#include "CommandFormTest.moc"
