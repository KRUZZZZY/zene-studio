/*
 * PanelShortcutsTest.cpp - Q050-055.1 stage 1: one key, four contexts, one meaning each
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*! Q050-055.1 stage 1 (0.5.0 L13, panel-scoped shortcuts), over the REAL application and the
 *  shortcuts page the user works in:
 *
 *  1. THE SAME KEY IN ALL FOUR CONTEXTS - the arranger, the note editor, the automation editor
 *     and Perform are each given Ctrl+Alt+J, bound to four different actions through the GUI's
 *     own shortcuts list (CommandListDialog + the row's Set Keys form), and the key is then
 *     pressed with each panel focused: only the focused panel's action runs, and each of the four
 *     runs its own. A press with no panel focused runs none of them.
 *  2. A SAME-CONTEXT CONFLICT, resolved in the shortcuts UI, surviving a restart: two note-editor
 *     actions claim one key, the second set is refused, the GUI's form resolves it (force moves
 *     the key and unbinds the other, in that context only), and after the config file is read
 *     again - what the next start does - the page, the router and the other contexts' bindings
 *     are all still right.
 *
 *  The key presses go through QTest's key delivery, so they pass through Qt's ShortcutOverride
 *  phase exactly as a real key does (PanelShortcuts.cpp's router claims the key there), and what
 *  the keys did is read back through the commands: the arranger's edit mode, the note editor's
 *  reported tool, the automation editor's mode and Perform's scene/transport state. */

#include <QtTest>

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>

#include "AutomationClip.h"
#include "AutomationEditor.h"
#include "AutomationTrack.h"
#include "CommandForm.h"
#include "CommandListDialog.h"
#include "CommandLists.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "ListTable.h"
#include "MainWindow.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "PanelShortcuts.h"
#include "PerformView.h"
#include "Shell.h"
#include "Song.h"
#include "SongEditor.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The key all four contexts get: one no menu action carries.
const QString SharedKey = QStringLiteral("Ctrl+Alt+J");

//! One row of the shortcuts list, as the list command answers it.
QJsonObject listedRow(const QString& scope, const QString& id)
{
	for (const QJsonValue& value : run(QStringLiteral("shortcuts.list"), {{QStringLiteral("scope"), scope}})
			.result.value(QStringLiteral("bindings")).toArray())
	{
		const QJsonObject row = value.toObject();
		if (row.value(QStringLiteral("id")).toString() == id) { return row; }
	}
	return {};
}

//! The row index of @a id in @a scope in the dialog's own rows.
int rowOf(CommandListDialog& dialog, const QString& scope, const QString& id)
{
	for (int index = 0; index < dialog.rowCount(); ++index)
	{
		const QJsonObject row = dialog.row(index);
		if (row.value(QStringLiteral("scope")).toString() == scope && row.value(QStringLiteral("id")).toString() == id)
		{
			return index;
		}
	}
	return -1;
}

//! The dialog's button labelled @a text, or nullptr.
QPushButton* button(CommandListDialog& dialog, const QString& text)
{
	for (QPushButton* candidate : dialog.findChildren<QPushButton*>())
	{
		if (candidate->text() == text) { return candidate; }
	}
	return nullptr;
}

/*! Bind @a id in @a scope to @a keys the way the user does: select the row in the shortcuts page,
 *  click "Set Keys...", fill the key field (and the force switch when asked) and press Run.
 *  Returns the command's result; empty args on a form that never opened. */
ControlResult bindThroughThePage(CommandListDialog& dialog, const QString& scope, const QString& id,
	const QString& keys, bool force = false)
{
	const int row = rowOf(dialog, scope, id);
	if (row < 0) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no row for %1").arg(id)); }
	QPushButton* set = button(dialog, QStringLiteral("Set Keys..."));
	if (set == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no Set Keys button")); }
	// The button acts on the CURRENT row, as a click in the real dialog does.
	dialog.table()->selectDataRow(row);
	QTest::mouseClick(set, Qt::LeftButton);
	CommandForm* shown = nullptr;
	for (CommandForm* candidate : dialog.findChildren<CommandForm*>())
	{
		if (candidate->isVisible()) { shown = candidate; }
	}
	if (shown == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no key form opened")); }
	if (!shown->setField(QStringLiteral("keys"), keys))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the form refused the key"));
	}
	if (force && !shown->setField(QStringLiteral("force"), true))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the form refused force"));
	}
	shown->accept();
	return shown->result();
}

//! The action of @a scope bound to @a keys in the stored map, or empty.
QString actionOf(const QString& scope, const QString& keys)
{
	for (const panelshortcuts::Action& action : panelshortcuts::actions())
	{
		if (action.scope != scope) { continue; }
		const QJsonValue chosen = panelshortcuts::stored(panelshortcuts::overrides(), scope, action.id);
		if (chosen.isString() && ShortcutMap::portable(chosen.toString()) == keys) { return action.id; }
	}
	return {};
}

//! The scene the engine reports column @a track playing, or -1 when it is idle.
int playingScene(int track)
{
	for (const QJsonValue& value : run(QStringLiteral("session.get_state")).result.value(QStringLiteral("columns")).toArray())
	{
		const QJsonObject column = value.toObject();
		if (column.value(QStringLiteral("track")).toInt() == track
			&& column.value(QStringLiteral("phase")).toString() == QLatin1String("playing"))
		{
			return column.value(QStringLiteral("scene")).toInt();
		}
	}
	return -1;
}

//! The note editor's tool, as window.note_editor_set reports it.
QString noteTool()
{
	return run(QStringLiteral("window.get_state")).result.value(QStringLiteral("note_editor")).toObject()
		.value(QStringLiteral("tool")).toString();
}

} // namespace

class PanelShortcutsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		// Shown and settled: the shell installs the panels' router once the main window is on screen.
		getGUI()->mainWindow()->show();
		QVERIFY(QTest::qWaitForWindowExposed(getGUI()->mainWindow()));
		ControlRegistry::setReady(true);
		QTest::qWait(50);  // the shell's singleShot pass: ShortcutMap::apply and panelshortcuts::install

		// One clip of each kind the four contexts need, through the commands the product has.
		const ControlResult lead = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), QStringLiteral("Lead")}});
		QVERIFY2(lead.ok, qPrintable(lead.errorMessage));
		const QString leadTrack = lead.result.value(QStringLiteral("track")).toString();
		const ControlResult clip = run(QStringLiteral("clip.add"), {{QStringLiteral("track"), leadTrack},
			{QStringLiteral("position"), 0}, {QStringLiteral("length"), 4 * 192}});
		QVERIFY2(clip.ok, qPrintable(clip.errorMessage));
		m_clip = clip.result.value(QStringLiteral("clip")).toString();
		QVERIFY2(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")},
			{QStringLiteral("name"), QStringLiteral("Drums")}}).ok, "no sample track");
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), QStringLiteral("kick.wav")},
			{QStringLiteral("name"), QStringLiteral("kick")}}).ok);
		QVERIFY(run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 0},
			{QStringLiteral("name"), QStringLiteral("Intro")}}).ok);
		QVERIFY(run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 1},
			{QStringLiteral("name"), QStringLiteral("Drop")}}).ok);

		// An automation clip for the automation editor.
		auto* automationTrack = new AutomationTrack(Engine::getSong());
		m_automationClip = static_cast<AutomationClip*>(automationTrack->createClip(TimePos(0)));
		QVERIFY(m_automationClip != nullptr);
		m_automationClip->setProgressionType(AutomationClip::ProgressionType::CubicHermite);
		m_automationClip->putValue(TimePos(0), 0.0f, false, true);
		m_automationClip->putValue(TimePos(192), 1.0f, false, true);
		QTest::qWait(50);
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("session.stop_all"));
		run(QStringLiteral("transport.stop"));
		run(QStringLiteral("shortcuts.reset"));
		QTest::qWait(50);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! CRITERION 1: one key, a different action in each of the four contexts, and only the focused
	//! panel's action fires. Every binding is made through the shortcuts page's own controls, and
	//! every press is a real key delivery to the focused panel.
	void theSameKeyMeansOneThingPerPanel()
	{
		CommandListDialog dialog(CommandLists::shortcuts());
		// The list shows the context, so the four rows are distinguishable and a clash resolvable.
		QVERIFY2(dialog.table()->columnCount() >= 7, "the shortcuts list must show each row's context");
		const QJsonObject arrangerRow = listedRow(QStringLiteral("arranger"), QStringLiteral("Arranger ▸ Zoom In"));
		QVERIFY2(!arrangerRow.isEmpty(), "the arranger's actions are not in the shortcuts list");
		QCOMPARE(arrangerRow.value(QStringLiteral("scope")).toString(), QStringLiteral("arranger"));

		for (const auto& bind : {std::pair{QStringLiteral("arranger"), QStringLiteral("Arranger ▸ Knife Tool")},
				std::pair{QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Erase Tool")},
				std::pair{QStringLiteral("automation_editor"), QStringLiteral("Automation Editor ▸ Erase Mode")},
				std::pair{QStringLiteral("perform"), QStringLiteral("Perform ▸ Stop All")}})
		{
			const ControlResult bound = bindThroughThePage(dialog, bind.first, bind.second, SharedKey);
			QVERIFY2(bound.ok, qPrintable(QStringLiteral("%1: %2").arg(bind.second, bound.errorMessage)));
			QCOMPARE(listedRow(bind.first, bind.second).value(QStringLiteral("keys")).toString(), SharedKey);
		}
		dialog.refresh();
		// The four are one key in four contexts: the list reports no clash at all.
		QCOMPARE(dialog.listResult().value(QStringLiteral("conflict_count")).toInt(), 0);
		// One scoped set is ONE undo step, and the redo puts it back: the store's own inverse.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(storedKey(QStringLiteral("perform"), QStringLiteral("Perform ▸ Stop All")).isEmpty(),
			"one undo step did not take the last scoped binding back");
		QCOMPARE(storedKey(QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Erase Tool")), SharedKey);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(storedKey(QStringLiteral("perform"), QStringLiteral("Perform ▸ Stop All")), SharedKey);

		// 1a. The arranger: the key picks the knife tool.
		QVERIFY(run(QStringLiteral("window.set_view"), {{QStringLiteral("view"), QStringLiteral("arrange")}}).ok);
		Shell* shell = getGUI()->mainWindow()->shell();
		QVERIFY(shell->revealPanel(QStringLiteral("arranger")));
		SongEditor* arranger = getGUI()->songEditor()->m_editor;
		arranger->setEditMode(SongEditor::EditMode::Draw);
		arranger->setFocus();
		QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(arranger));
		QTest::keyClick(arranger, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
		QCOMPARE(arranger->editMode(), SongEditor::EditMode::Knife);

		// 1b. The note editor: the SAME key picks the erase tool.
		QVERIFY(shell->revealPanel(QStringLiteral("detail")));
		QVERIFY(run(QStringLiteral("window.note_editor_set"), {{QStringLiteral("clip"), m_clip},
			{QStringLiteral("tool"), QStringLiteral("draw")}}).ok);
		NoteCanvas* canvas = getGUI()->noteEditor()->canvas();
		canvas->setFocus();
		QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(canvas));
		QTest::keyClick(canvas, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
		QCOMPARE(noteTool(), QStringLiteral("erase"));

		// 1c. The automation editor: the key picks the erase MODE.
		QVERIFY(shell->revealPanel(QStringLiteral("automation")));
		getGUI()->automationEditor()->open(m_automationClip);
		QVERIFY(run(QStringLiteral("automation.editor_mode"), {{QStringLiteral("mode"), QStringLiteral("draw")}}).ok);
		AutomationEditor* automation = getGUI()->automationEditor()->m_editor;
		automation->setFocus();
		QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(automation));
		QTest::keyClick(automation, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
		QCOMPARE(getGUI()->automationEditor()->editorMode(), AutomationEditor::EditMode::Erase);

		// 1d. Perform: the key stops everything (a scene plays first, so stopping is visible).
		PerformView* perform = PerformView::instance(true);
		perform->show();
		QVERIFY(QTest::qWaitForWindowExposed(perform));
		perform->refresh();
		QTest::keyClick(perform, Qt::Key_1);  // the panel's built-in key still launches scene 1
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), 0, 3000);
		QTest::keyClick(perform, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), -1, 3000);

		// 1e. Each of the four kept its own meaning, and a widget outside every scope runs none.
		QCOMPARE(arranger->editMode(), SongEditor::EditMode::Knife);
		QCOMPARE(noteTool(), QStringLiteral("erase"));
		QCOMPARE(getGUI()->automationEditor()->editorMode(), AutomationEditor::EditMode::Erase);
		QMenuBar* menus = getGUI()->mainWindow()->menuBar();
		menus->setFocus();
		QTest::keyClick(menus, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
		QCOMPARE(arranger->editMode(), SongEditor::EditMode::Knife);
		QCOMPARE(noteTool(), QStringLiteral("erase"));
		QCOMPARE(getGUI()->automationEditor()->editorMode(), AutomationEditor::EditMode::Erase);

		// A dialog parented to a panel is still a separate window: its controls must not inherit
		// the panel's shortcut scope through QWidget::parentWidget().
		arranger->setEditMode(SongEditor::EditMode::Draw);
		QDialog childDialog(arranger);
		QPushButton dialogButton(&childDialog);
		childDialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&childDialog));
		dialogButton.setFocus();
		QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&dialogButton));
		QTest::keyClick(&dialogButton, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
		QCOMPARE(arranger->editMode(), SongEditor::EditMode::Draw);
		childDialog.hide();
		perform->hide();
	}

	//! CRITERION 2: a same-context clash, resolved in the shortcuts UI, surviving a restart - and
	//! the other context's binding of that key unchanged.
	void aScopedClashSurvivesARestart()
	{
		const QString clashKey = QStringLiteral("Ctrl+Alt+K");
		CommandListDialog dialog(CommandLists::shortcuts());
		// The arranger keeps Ctrl+Alt+K for itself: resolving the note editor's clash must not touch it.
		QVERIFY2(bindThroughThePage(dialog, QStringLiteral("arranger"),
			QStringLiteral("Arranger ▸ Select Tool"), clashKey).ok, "the arranger's own binding was refused");
		QVERIFY2(bindThroughThePage(dialog, QStringLiteral("note_editor"),
			QStringLiteral("Note Editor ▸ Mute Tool"), clashKey).ok, "the first note-editor binding was refused");

		// The clash: the second note-editor action on the same key is refused, typed, and unchanged.
		const ControlResult refused = bindThroughThePage(dialog, QStringLiteral("note_editor"),
			QStringLiteral("Note Editor ▸ Audition Tool"), clashKey);
		QVERIFY2(!refused.ok, "a same-context clash was accepted without being resolved");
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(listedRow(QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Audition Tool"))
			.value(QStringLiteral("keys")).toString().isEmpty(), "the refused set wrote the key anyway");

		// The resolution, in the same form the page opens: force moves it here and unbinds the other
		// action of THIS context only.
		const ControlResult resolved = bindThroughThePage(dialog, QStringLiteral("note_editor"),
			QStringLiteral("Note Editor ▸ Audition Tool"), clashKey, true);
		QVERIFY2(resolved.ok, qPrintable(resolved.errorMessage));
		QCOMPARE(listedRow(QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Audition Tool"))
			.value(QStringLiteral("keys")).toString(), clashKey);
		QVERIFY2(listedRow(QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Mute Tool"))
			.value(QStringLiteral("keys")).toString().isEmpty(), "the clash was not cleared in its own context");
		dialog.refresh();
		QCOMPARE(dialog.listResult().value(QStringLiteral("conflict_count")).toInt(), 0);
		// The arranger's binding of the same key is untouched - that is what "scoped" means.
		QCOMPARE(listedRow(QStringLiteral("arranger"), QStringLiteral("Arranger ▸ Select Tool"))
			.value(QStringLiteral("keys")).toString(), clashKey);
		QCOMPARE(listedRow(QStringLiteral("perform"), QStringLiteral("Perform ▸ Stop All"))
			.value(QStringLiteral("keys")).toString(), SharedKey);
		QVERIFY2(!ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("shortcuts"))
			.isEmpty(), "the set was not saved to the config file");

		// THE RESTART: read the config file again, as the next start does, and check the page and the
		// keys. loadConfigFile(path) is the product's own --config read path.
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		QCOMPARE(storedKey(QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Audition Tool")), clashKey);
		QVERIFY(storedKey(QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Mute Tool")).isEmpty());
		QCOMPARE(storedKey(QStringLiteral("arranger"), QStringLiteral("Arranger ▸ Select Tool")), clashKey);
		QCOMPARE(storedKey(QStringLiteral("perform"), QStringLiteral("Perform ▸ Stop All")), SharedKey);
		QCOMPARE(storedKey(QStringLiteral("note_editor"), QStringLiteral("Note Editor ▸ Erase Tool")), SharedKey);
		QCOMPARE(actionOf(QStringLiteral("note_editor"), clashKey), QStringLiteral("Note Editor ▸ Audition Tool"));
		QCOMPARE(actionOf(QStringLiteral("arranger"), clashKey), QStringLiteral("Arranger ▸ Select Tool"));
		QCOMPARE(actionOf(QStringLiteral("perform"), SharedKey), QStringLiteral("Perform ▸ Stop All"));

		// The router reads the reloaded map: the resolved note-editor key now picks the audition tool,
		// and the arranger's key of the same name still picks the select tool.
		Shell* shell = getGUI()->mainWindow()->shell();
		QVERIFY(shell->revealPanel(QStringLiteral("detail")));
		QVERIFY(run(QStringLiteral("window.note_editor_set"), {{QStringLiteral("tool"), QStringLiteral("draw")}}).ok);
		NoteCanvas* canvas = getGUI()->noteEditor()->canvas();
		getGUI()->mainWindow()->activateWindow();
		canvas->setFocus();
		QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(canvas));
		QTest::keyClick(canvas, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
		QCOMPARE(noteTool(), QStringLiteral("audition"));
		QVERIFY(shell->revealPanel(QStringLiteral("arranger")));
		SongEditor* arranger = getGUI()->songEditor()->m_editor;
		arranger->setEditMode(SongEditor::EditMode::Draw);
		arranger->setFocus();
		QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(arranger));
		QTest::keyClick(arranger, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
		QCOMPARE(arranger->editMode(), SongEditor::EditMode::Select);
	}

private:
	//! The stored keys of @a id in @a scope (empty = unbound), read from the live store.
	QString storedKey(const QString& scope, const QString& id) const
	{
		const QJsonValue chosen = panelshortcuts::stored(panelshortcuts::overrides(), scope, id);
		return chosen.isString() ? ShortcutMap::portable(chosen.toString()) : QString();
	}

	QTemporaryDir m_home;
	QString m_clip;
	AutomationClip* m_automationClip = nullptr;
};

QTEST_MAIN(PanelShortcutsTest)
#include "PanelShortcutsTest.moc"
