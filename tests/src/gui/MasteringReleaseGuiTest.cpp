/*
 * MasteringReleaseGuiTest.cpp - the mastering release page and the agent surface are one list
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
/*! 0.5.0 L12 (Q050-317.1, stage 1 of 3): the interface half of the mastering release sequence.
 *
 *  The claim: the release page is not a second implementation. It is a CommandListDialog over
 *  mastering.sequence_list whose every entry runs a mastering.song_* command, so what a person
 *  clicks and what an agent calls are the same edit - and a change made on one side is what the
 *  other side reads back.
 *
 *  This drives the REAL Project Tools menu (ShellCommandMenus::populateProject), triggers
 *  "Mastering Release...", and works the dialog the way a person does: pick a row, run the
 *  action button, fill the command's own form, press Run. Then it asks the agent surface, calls
 *  the agent surface, refreshes the page, and compares. The page's list command is asserted to be
 *  the id the menu entry declares, so a menu entry that showed something else would fail here. */

#include <QtTest>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>

#include "CommandForm.h"
#include "CommandListDialog.h"
#include "CommandLists.h"
#include "ListTable.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiTestApplication.h"
#include "MasteringSequence.h"
#include "ProjectJournal.h"
#include "MenuTestSupport.h"
#include "ProjectInfo.h"
#include "ShellCommandMenus.h"
#include "Song.h"

#include "../core/TakeLaneTestSupport.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonArray songs() { return run(QStringLiteral("mastering.sequence_list")).result.value(QStringLiteral("songs")).toArray(); }

QString sourceAt(int index) { return songs().at(index).toObject().value(QStringLiteral("source")).toString(); }

QString idAt(int index) { return songs().at(index).toObject().value(QStringLiteral("song")).toString(); }

int fieldOf(int index, const char* field) { return songs().at(index).toObject().value(QLatin1String(field)).toInt(); }

QString processingAt(int index) { return songs().at(index).toObject().value(QStringLiteral("processing")).toString(); }

template<class T>
T* visible()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (auto* found = qobject_cast<T*>(widget); found != nullptr && found->isVisible()) { return found; }
	}
	return nullptr;
}

//! The row of @a dialog whose `song` is @a id, or -1.
int rowOf(CommandListDialog* dialog, const QString& id)
{
	for (int row = 0; row < dialog->rowCount(); ++row)
	{
		if (dialog->row(row).value(QStringLiteral("song")).toString() == id) { return row; }
	}
	return -1;
}

//! The index of the action labelled @a label in @a spec.
int actionOf(const ListSpec& spec, const QString& label)
{
	for (int i = 0; i < spec.actions.size(); ++i)
	{
		if (spec.actions.at(i).label == label) { return i; }
	}
	return -1;
}

/*! Put the session into a mastering document with an EMPTY release, without replacing the
 *  session: project.new_document is the agent path the core test drives, and replacing the
 *  document under a live GuiApplication tears down every track view while this test's posted
 *  events are still in flight (a pre-existing GUI-test limitation, not this feature's). The
 *  document type is set through its own registered command, and the songs are removed through
 *  the release sequence's own, so every step stays on the surface under test. */
void emptyMasteringRelease()
{
	QVERIFY(run(QStringLiteral("project.settings"), {{QStringLiteral("document_type"), QStringLiteral("mastering")}}).ok);
	while (!songs().isEmpty())
	{
		QVERIFY(run(QStringLiteral("mastering.song_remove"), {{QStringLiteral("song"),
			songs().first().toObject().value(QStringLiteral("song")).toString()}}).ok);
	}
}

void clickRun(CommandForm* form)
{
	for (QPushButton* button : form->findChildren<QPushButton*>())
	{
		if (button->text().remove(QLatin1Char('&')) == QStringLiteral("Run")) { QTest::mouseClick(button, Qt::LeftButton); }
	}
}

} // namespace

class MasteringReleaseGuiTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_home;
	QString m_first;
	QString m_second;

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the build's effects for the mastering template
#endif
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		// The offscreen platform IS a display (a message can be shown over it, nothing can be clicked), so the
		// registry is told there is somebody to show a refusal to - the convention GrooveMenuTest documents.
		ControlRegistry::instance()->setHeadless(false);
		m_first = taketest::writeToneWav(m_home.filePath(QStringLiteral("one.wav")), 220.0);
		m_second = taketest::writeToneWav(m_home.filePath(QStringLiteral("two.wav")), 660.0);
		QVERIFY(!m_first.isEmpty());
		QVERIFY(!m_second.isEmpty());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The menu entry opens the release page, and the page is the registered list command.
	void theProjectMenuOpensTheReleasePage()
	{
		emptyMasteringRelease();
		QMenu menu;
		ShellCommandMenus::populateProject(&menu);
		QAction* entry = lmms::test::menuAction(&menu, {QStringLiteral("Mastering Release...")});
		QVERIFY2(entry != nullptr, "Project Tools has no Mastering Release entry");
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("mastering.sequence_list"));
		QCOMPARE(CommandLists::masteringSequence().listId, QStringLiteral("mastering.sequence_list"));

		entry->trigger();
		CommandListDialog* dialog = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((dialog = visible<CommandListDialog>()) != nullptr, 3000);
		QCOMPARE(dialog->windowTitle(), QStringLiteral("Mastering Release"));
		QCOMPARE(dialog->rowCount(), 0);  // a new mastering document has no songs yet
		dialog->close();
	}

	//! A song added through the page's own "Add Song..." form is what the agent surface then reports.
	void addingASongThroughThePageReachesTheAgentSurface()
	{
		emptyMasteringRelease();
		const ListSpec spec = CommandLists::masteringSequence();
		CommandListDialog dialog(spec);
		QCOMPARE(dialog.rowCount(), 0);

		// The Add Song... extra asks (no fixed arguments), so it opens the command's form.
		QCOMPARE(dialog.runExtra(0).errorKind, ControlErrorKind::Busy);
		CommandForm* form = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((form = visible<CommandForm>()) != nullptr, 3000);
		QCOMPARE(form->findChild<QLabel*>(QStringLiteral("commandFormId"))->text().contains(QStringLiteral("mastering.song_add")), true);
		QVERIFY(form->setField(QStringLiteral("source"), m_first));
		QVERIFY(form->setField(QStringLiteral("title"), QStringLiteral("Opening")));
		QVERIFY(form->setField(QStringLiteral("processing"), QStringLiteral("streaming-14")));
		QVERIFY(form->setField(QStringLiteral("fade_in_ms"), 1200));
		QVERIFY(form->setField(QStringLiteral("fade_out_ms"), 800));
		QVERIFY(form->setField(QStringLiteral("gap_ms"), 2000));
		clickRun(form);
		QTRY_COMPARE(form->result().ok, true);

		dialog.refresh();
		QCOMPARE(dialog.rowCount(), 1);
		QCOMPARE(dialog.row(0).value(QStringLiteral("source")).toString(), QFileInfo(m_first).absoluteFilePath());
		// The same state, read through the agent surface.
		QCOMPARE(songs().size(), 1);
		QCOMPARE(sourceAt(0), QFileInfo(m_first).absoluteFilePath());
		QCOMPARE(processingAt(0), QStringLiteral("streaming-14"));
		QCOMPARE(fieldOf(0, "fade_in_ms"), 1200);
		QCOMPARE(fieldOf(0, "fade_out_ms"), 800);
		QCOMPARE(fieldOf(0, "gap_ms"), 2000);
	}

	//! A song edited through the page's row form is what the agent surface then reports.
	void editingThroughThePageReachesTheAgentSurface()
	{
		emptyMasteringRelease();
		QVERIFY(run(QStringLiteral("mastering.song_add"), {{QStringLiteral("source"), m_first}, {QStringLiteral("title"), QStringLiteral("One")}}).ok);
		const QString id = idAt(0);
		const ListSpec spec = CommandLists::masteringSequence();
		CommandListDialog dialog(spec);
		QCOMPARE(dialog.rowCount(), 1);
		const int row = rowOf(&dialog, id);
		QVERIFY(row >= 0);
		QCOMPARE(dialog.runAction(row, actionOf(spec, QStringLiteral("Edit..."))).errorKind, ControlErrorKind::Busy);
		CommandForm* form = nullptr;
		QTRY_VERIFY_WITH_TIMEOUT((form = visible<CommandForm>()) != nullptr, 3000);
		// The row's song is fixed in the form; the fields it edits are the song's own settings.
		QVERIFY(!form->fields().contains(QStringLiteral("song")));
		QVERIFY(form->setField(QStringLiteral("processing"), QStringLiteral("ebu-r128")));
		QVERIFY(form->setField(QStringLiteral("gap_ms"), 3500));
		QVERIFY(form->setField(QStringLiteral("fade_out_ms"), 1750));
		clickRun(form);
		QTRY_COMPARE(form->result().ok, true);
		QCOMPARE(processingAt(0), QStringLiteral("ebu-r128"));
		QCOMPARE(fieldOf(0, "gap_ms"), 3500);
		QCOMPARE(fieldOf(0, "fade_out_ms"), 1750);
		QCOMPARE(idAt(0), id);
	}

	//! An edit made on the agent surface is what the page shows after a refresh, in the same order.
	void theAgentSurfaceDrivesThePage()
	{
		emptyMasteringRelease();
		QVERIFY(run(QStringLiteral("mastering.song_add"), {{QStringLiteral("source"), m_first}, {QStringLiteral("title"), QStringLiteral("One")}}).ok);
		QVERIFY(run(QStringLiteral("mastering.song_add"), {{QStringLiteral("source"), m_second}, {QStringLiteral("title"), QStringLiteral("Two")}}).ok);
		const QString first = idAt(0);
		const QString second = idAt(1);
		QVERIFY(run(QStringLiteral("mastering.song_set"), {{QStringLiteral("song"), first}, {QStringLiteral("processing"), QStringLiteral("streaming-16")},
			{QStringLiteral("fade_in_ms"), 400}, {QStringLiteral("gap_ms"), 1100}}).ok);
		QVERIFY(run(QStringLiteral("mastering.song_move"), {{QStringLiteral("song"), second}, {QStringLiteral("to"), 0}}).ok);

		CommandListDialog dialog(CommandLists::masteringSequence());
		QCOMPARE(dialog.rowCount(), 2);
		QCOMPARE(dialog.row(0).value(QStringLiteral("song")).toString(), second);
		QCOMPARE(dialog.row(1).value(QStringLiteral("song")).toString(), first);
		QCOMPARE(dialog.row(1).value(QStringLiteral("processing")).toString(), QStringLiteral("streaming-16"));
		QCOMPARE(dialog.row(1).value(QStringLiteral("gap_ms")).toInt(), 1100);
		// The last song's gap is not heard between songs; the page says so rather than promising silence.
		QCOMPARE(dialog.row(0).value(QStringLiteral("gap_applies")).toBool(), true);
		QCOMPARE(dialog.row(1).value(QStringLiteral("gap_applies")).toBool(), false);

		// And the page's own Move to Top action is the same command the agent uses, keeping one step.
		const ListSpec spec = CommandLists::masteringSequence();
		const int row = rowOf(&dialog, first);
		QVERIFY(row >= 0);
		dialog.table()->selectDataRow(row);
		QPushButton* move = nullptr;
		for (QPushButton* button : dialog.findChildren<QPushButton*>())
		{
			if (button->text() == QStringLiteral("Move to Top")) { move = button; }
		}
		QVERIFY2(move != nullptr, "the release page has no Move to Top button");
		QCOMPARE(move->property("controlCommand").toString(), QStringLiteral("mastering.song_move"));
		const int before = Engine::projectJournal()->undoDepth();
		QTest::mouseClick(move, Qt::LeftButton);
		QCOMPARE(dialog.row(0).value(QStringLiteral("song")).toString(), first);
		QCOMPARE(Engine::projectJournal()->undoDepth(), before + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(idAt(0), second);
	}
};

QTEST_MAIN(MasteringReleaseGuiTest)
#include "MasteringReleaseGuiTest.moc"
