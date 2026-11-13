/*
 * MasteringChoicesGuiTest.cpp - the run choices a person makes are the ones the report shows
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
/*! Q050-188.1 (GUI readback): the genre and delivery target a person chooses are visible per candidate.
 *
 *  The claim: the Project Tools > Mastering Report... list is a CommandListDialog over mastering.get_state,
 *  the same command an agent reads, so its rows are last_run's own candidate rows and carry the run's
 *  `genre` and `delivery_target`. Auto-Master's form offers exactly the values mastering.list_candidates
 *  publishes, refuses a value off that list, and keeps both choices on screen when a run is refused.
 *
 *  The report test drives a COMPOSED last_run (control::setMasteringLastRun + setMasteringRunState), not a
 *  render: the list reads the same store a completed run writes, and a render with audio is the core test's
 *  job. Composing keeps the per-row choices exact and the test independent of rendering time. */

#include <QtTest>

#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>

#include "CommandForm.h"
#include "CommandListDialog.h"
#include "CommandLists.h"
#include "ControlMasteringSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiTestApplication.h"
#include "ListTable.h"
#include "MenuTestSupport.h"
#include "ShellCommandMenus.h"

using namespace lmms;
using namespace lmms::gui;

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

//! One candidate row as mastering.run's document carries it.
QJsonObject candidate(const QString& name, const QString& genre, const QString& target, const QString& file)
{
	QJsonObject row;
	row.insert(QStringLiteral("name"), name);
	row.insert(QStringLiteral("genre"), genre);
	row.insert(QStringLiteral("delivery_target"), target);
	row.insert(QStringLiteral("loudness_pass"), true);
	row.insert(QStringLiteral("true_peak_pass"), true);
	row.insert(QStringLiteral("file"), file);
	return row;
}

//! The value each item of @a box sends, in order (the display text is a label, not the value).
QStringList valuesOf(const QComboBox* box)
{
	QStringList values;
	for (int i = 0; i < box->count(); ++i) { values.append(box->itemData(i).value<QJsonValue>().toString()); }
	return values;
}

//! A string array from the registry's published value @a key.
QStringList publishedList(const QJsonObject& published, const char* key)
{
	QStringList out;
	for (const QJsonValue& value : published.value(QLatin1String(key)).toArray()) { out.append(value.toString()); }
	return out;
}

} // namespace

class MasteringChoicesGuiTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_home;

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		// A refusal has somebody to be shown to, as in MasteringReleaseGuiTest.
		ControlRegistry::instance()->setHeadless(false);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The menu entry opens the report, the report is mastering.get_state, and it has no rows before a run.
	void theProjectMenuOpensTheReportBeforeAnyRun()
	{
		QMenu menu;
		ShellCommandMenus::populateProject(&menu);
		QAction* entry = lmms::test::menuAction(&menu, {QStringLiteral("Mastering Report...")});
		QVERIFY2(entry != nullptr, "Project Tools has no Mastering Report entry");
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("mastering.get_state"));
		QCOMPARE(CommandLists::masteringReport().listId, QStringLiteral("mastering.get_state"));

		entry->trigger();
		CommandListDialog* dialog = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((dialog = visible<CommandListDialog>()) != nullptr, 3000);
		QCOMPARE(dialog->windowTitle(), QStringLiteral("Mastering Report"));
		QCOMPARE(dialog->rowCount(), 0);  // no run has completed in this process
		dialog->close();
	}

	//! A completed last_run's rows are the report's rows, and each row shows the genre and target it was run with.
	void theReportShowsEachRowsRetainedChoices()
	{
		QJsonObject report;
		report.insert(QStringLiteral("candidates"), QJsonArray{
			candidate(QStringLiteral("Loud"), QStringLiteral("electronic"), QStringLiteral("streaming-14"), QStringLiteral("/tmp/loud.wav")),
			candidate(QStringLiteral("Open"), QStringLiteral("electronic"), QStringLiteral("streaming-16"), QStringLiteral("/tmp/open.wav"))});
		control::setMasteringLastRun(report);
		control::setMasteringRunState(QStringLiteral("completed"));

		QMenu menu;
		ShellCommandMenus::populateProject(&menu);
		QAction* entry = lmms::test::menuAction(&menu, {QStringLiteral("Mastering Report...")});
		QVERIFY(entry != nullptr);
		entry->trigger();
		CommandListDialog* dialog = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((dialog = visible<CommandListDialog>()) != nullptr, 3000);
		QCOMPARE(dialog->rowCount(), 2);
		ListTable* table = dialog->table();
		// Assert the rendered table, not only its unfiltered source JSON: removing either visible
		// choice column must make this regression fail even though the command still returns it.
		QCOMPARE(table->columnCount(), 7);
		QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("Genre"));
		QCOMPARE(table->horizontalHeaderItem(2)->text(), QStringLiteral("Delivery target"));
		QCOMPARE(table->item(0, 1)->text(), QStringLiteral("electronic"));
		QCOMPARE(table->item(0, 2)->text(), QStringLiteral("streaming-14"));
		QCOMPARE(table->item(1, 1)->text(), QStringLiteral("electronic"));
		QCOMPARE(table->item(1, 2)->text(), QStringLiteral("streaming-16"));
		dialog->close();
	}

	//! Auto-Master's form offers both choices with the published lists, refuses an off-list genre, and keeps both on a refusal.
	void autoMasterFormOffersTheChoicesAndKeepsThemOnRefusal()
	{
		const ControlResult published = run(QStringLiteral("mastering.list_candidates"));
		QVERIFY(published.ok);
		const QStringList genres = publishedList(published.result, "genres");
		const QStringList targets = publishedList(published.result, "delivery_targets");
		QVERIFY(!genres.isEmpty());
		QVERIFY(!targets.isEmpty());

		QMenu menu;
		ShellCommandMenus::populateProject(&menu);
		QAction* entry = lmms::test::menuAction(&menu, {QStringLiteral("Auto-Master...")});
		QVERIFY2(entry != nullptr, "Project Tools has no Auto-Master entry");
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("mastering.run"));

		entry->trigger();
		CommandForm* form = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((form = visible<CommandForm>()) != nullptr, 3000);
		QVERIFY(form->fields().contains(QStringLiteral("genre")));
		QVERIFY(form->fields().contains(QStringLiteral("delivery_target")));

		auto* genreBox = form->findChild<QComboBox*>(QStringLiteral("field:genre"));
		auto* targetBox = form->findChild<QComboBox*>(QStringLiteral("field:delivery_target"));
		QVERIFY2(genreBox != nullptr, "the genre field is not a choice list");
		QVERIFY2(targetBox != nullptr, "the delivery_target field is not a choice list");
		// An optional choice carries the command's own default first, then exactly the published values.
		QCOMPARE(genreBox->count(), genres.size() + 1);
		QCOMPARE(targetBox->count(), targets.size() + 1);
		QCOMPARE(valuesOf(genreBox).mid(1), genres);
		QCOMPARE(valuesOf(targetBox).mid(1), targets);

		// The list is closed and lower-case: a case-folded value is refused, the exact one is taken.
		QVERIFY(!form->setField(QStringLiteral("genre"), QStringLiteral("Electronic")));
		QVERIFY(form->setField(QStringLiteral("genre"), QStringLiteral("electronic")));
		QVERIFY(form->setField(QStringLiteral("delivery_target"), QStringLiteral("streaming-14")));
		QVERIFY(form->setField(QStringLiteral("out_dir"), QStringLiteral(":/not-a-path")));

		// The run is refused on out_dir (checked first); the form stays open with the reason and both choices.
		clickRun(form);
		QTRY_VERIFY_WITH_TIMEOUT(!form->errorText().isEmpty(), 3000);
		QVERIFY(!form->result().ok);
		QVERIFY(form->errorText().contains(QStringLiteral("out_dir")));
		QVERIFY(form->isVisible());
		QVERIFY(form->fields().contains(QStringLiteral("genre")));
		QVERIFY(form->fields().contains(QStringLiteral("delivery_target")));
		// The selection is the value it was set to, not the display label ("Electronic").
		QCOMPARE(genreBox->currentData().value<QJsonValue>().toString(), QStringLiteral("electronic"));
		QCOMPARE(targetBox->currentData().value<QJsonValue>().toString(), QStringLiteral("streaming-14"));
		form->close();
	}
};

QTEST_MAIN(MasteringChoicesGuiTest)
#include "MasteringChoicesGuiTest.moc"
