/*
 * PluginShortcutsTest.cpp - Q050-055.2 stage 2: keys that open the selected track's editor and load a chosen plug-in
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

/*! Over the REAL application: every binding is made through the shortcuts page's own controls and every press is a
 *  real key delivery to the focused arranger, so it passes the router's ShortcutOverride phase as a real key does.
 *
 *  1. THE EDITOR KEY acts on the selected track's device: the instrument window of THAT track opens and the other
 *     track's stays shut; a track with no such device refuses and nothing else opens. (This platform cannot embed a
 *     hosted editor, so the host's view is what opens; PluginNativeEditorTest owns the hosted window itself.)
 *  2. THE PLUG-IN KEY loads the chosen plug-in onto the selected track only, with plugin.load's result and undo: an
 *     effect is one undo step, an instrument replacement is recorded reversible:false, an incompatible target refuses
 *     and shows it, and the binding survives re-reading the config. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "AutomationTrack.h"
#include "CommandForm.h"
#include "CommandListDialog.h"
#include "CommandLists.h"
#include "ConfigManager.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "EffectChain.h"
#include "EffectControlDialog.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "InstrumentTrackWindow.h"
#include "ListTable.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "PanelShortcuts.h"
#include "Shell.h"
#include "ShellCommandMenus.h"
#include "ShellSelection.h"
#include "Song.h"
#include "SongEditor.h"
#include "TextFloat.h"
#include "TrackContainerView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

const QString Arranger = QStringLiteral("arranger");
const QString EditorAction = QStringLiteral("Arranger ▸ Open Selected Track's Editor");
const QString EffectAction = QStringLiteral("Arranger ▸ Open Selected Track's First Effect");

//! The dev-<n> of the loadable catalogue entry @a name, or empty.
QString deviceNamed(const QString& name)
{
	const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
	for (int i = 0; i < catalogue.size(); ++i)
	{
		if (catalogue[i].name == name && catalogue[i].loadable) { return control::deviceId(i); }
	}
	return {};
}

QPushButton* button(CommandListDialog& dialog, const QString& text)
{
	for (QPushButton* candidate : dialog.findChildren<QPushButton*>())
	{
		if (candidate->text() == text) { return candidate; }
	}
	return nullptr;
}

//! The form a click on @a dialog's button just opened, or nullptr.
CommandForm* shownForm(CommandListDialog& dialog)
{
	CommandForm* shown = nullptr;
	for (CommandForm* candidate : dialog.findChildren<CommandForm*>())
	{
		if (candidate->isVisible()) { shown = candidate; }
	}
	return shown;
}

//! Select @a id's row in the page, click "Set Keys...", fill the key and run: what the user does.
ControlResult bindRow(CommandListDialog& dialog, const QString& id, const QString& keys)
{
	for (int index = 0; index < dialog.rowCount(); ++index)
	{
		const QJsonObject row = dialog.row(index);
		if (row.value(QStringLiteral("scope")).toString() != Arranger || row.value(QStringLiteral("id")).toString() != id) { continue; }
		dialog.table()->selectDataRow(index);
		QTest::mouseClick(button(dialog, QStringLiteral("Set Keys...")), Qt::LeftButton);
		CommandForm* form = shownForm(dialog);
		if (form == nullptr || !form->setField(QStringLiteral("keys"), keys))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the key form did not open"));
		}
		form->accept();
		return form->result();
	}
	return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no row %1").arg(id));
}

//! Click "Bind a Plug-in...", choose the device and the key in its form and run.
ControlResult bindPluginThroughThePage(const QString& device, const QString& keys, bool force = false)
{
	CommandListDialog dialog(CommandLists::shortcuts());
	QPushButton* bind = button(dialog, QStringLiteral("Bind a Plug-in..."));
	if (bind == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no Bind a Plug-in button")); }
	QTest::mouseClick(bind, Qt::LeftButton);
	CommandForm* form = shownForm(dialog);
	if (form == nullptr || !form->setField(QStringLiteral("scope"), Arranger) || !form->setField(QStringLiteral("device"), device)
		|| !form->setField(QStringLiteral("keys"), keys) || (force && !form->setField(QStringLiteral("force"), true)))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the bind form refused a field"));
	}
	form->accept();
	return form->result();
}

//! The live row @a id of the arranger scope, or {}.
QJsonObject listed(const QString& id)
{
	for (const QJsonValue& value : run(QStringLiteral("shortcuts.list"), {{QStringLiteral("scope"), Arranger}})
			.result.value(QStringLiteral("bindings")).toArray())
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == id) { return value.toObject(); }
	}
	return {};
}

int effectsOn(const QString& target)
{
	ControlTarget resolved;
	ControlResult error;
	return resolveControlTarget(target, &resolved, &error) && resolved.chain != nullptr
		? static_cast<int>(resolved.chain->effects().size()) : -1;
}

//! The newest transaction record: the one a key's load left.
QJsonObject lastTransaction()
{
	const QJsonArray records = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("transactions")).toArray();
	return records.isEmpty() ? QJsonObject() : records.last().toObject();
}

InstrumentTrackView* viewOf(const Track* track)
{
	for (TrackView* view : getGUI()->songEditor()->m_editor->trackViews())
	{
		if (view->getTrack() == track) { return qobject_cast<InstrumentTrackView*>(view); }
	}
	return nullptr;
}

bool windowShown(const Track* track)
{
	return viewOf(track)->getInstrumentTrackWindow()->isVisible();  // a window of its own in the shell
}

void hideWindow(const Track* track)
{
	viewOf(track)->getInstrumentTrackWindow()->toggleVisibility(false);
}

QList<QWidget*> effectDialogs()
{
	QList<QWidget*> out;
	for (QWidget* child : getGUI()->mainWindow()->findChildren<QWidget*>())
	{
		if (dynamic_cast<EffectControlDialog*>(child) != nullptr) { out.append(child); }
	}
	return out;
}

int shownEffectDialogs()
{
	int shown = 0;
	for (QWidget* dialog : effectDialogs()) { shown += dialog->isVisible() ? 1 : 0; }
	return shown;
}

//! The floating messages on screen (a TextFloat is a top-level window of its own): what a refused key shows.
QString floated()
{
	QString text;
	for (QWidget* window : QApplication::topLevelWidgets())
	{
		if (dynamic_cast<TextFloat*>(window) == nullptr) { continue; }
		for (QLabel* label : window->findChildren<QLabel*>()) { text += label->text() + QLatin1Char(' '); }
	}
	return text;
}

} // namespace

class PluginShortcutsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
		guitest::startGui(m_home);
		getGUI()->mainWindow()->show();
		QVERIFY(QTest::qWaitForWindowExposed(getGUI()->mainWindow()));
		ControlRegistry::setReady(true);
		ControlRegistry::instance()->setHeadless(false);  // the offscreen platform is a display: a refusal is shown
		QTest::qWait(50);  // the shell's singleShot pass: ShortcutMap::apply and panelshortcuts::install
		m_triple = deviceNamed(QStringLiteral("tripleoscillator"));
		m_amplifier = deviceNamed(QStringLiteral("amplifier"));
		QVERIFY2(!m_triple.isEmpty() && !m_amplifier.isEmpty(), "the test's plug-in modules were not built");
		m_first = addInstrumentTrack(QStringLiteral("First"));
		m_second = addInstrumentTrack(QStringLiteral("Second"));
		for (const Track* track : {static_cast<const Track*>(m_first), static_cast<const Track*>(m_second)})
		{
			const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), control::trackIdOf(track)},
				{QStringLiteral("device"), m_triple}});
			QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		}
		const ControlResult sample = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")},
			{QStringLiteral("name"), QStringLiteral("Drums")}});
		QVERIFY2(sample.ok, qPrintable(sample.errorMessage));
		m_sampleId = sample.result.value(QStringLiteral("track")).toString();
		auto* automation = new AutomationTrack(Engine::getSong());
		m_automationId = control::trackIdOf(automation);
		QTest::qWait(50);
		QVERIFY(getGUI()->mainWindow()->shell()->revealPanel(Arranger));
		m_arranger = getGUI()->songEditor()->m_editor;
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("shortcuts.reset"));
		QTest::qWait(50);
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! CRITERION 1: the bound key opens the selected track's instrument editor, and no other track's.
	void theEditorKeyOpensTheSelectedTracksDevice()
	{
		CommandListDialog dialog(CommandLists::shortcuts());
		const ControlResult bound = bindRow(dialog, EditorAction, QStringLiteral("Ctrl+Alt+E"));
		QVERIFY2(bound.ok, qPrintable(bound.errorMessage));
		QVERIFY(bindRow(dialog, EffectAction, QStringLiteral("Ctrl+Alt+F")).ok);
		hideWindow(m_first);
		hideWindow(m_second);

		select(control::trackIdOf(m_first));
		press(Qt::Key_E);
		QVERIFY2(windowShown(m_first), "the selected track's instrument window did not open");
		QVERIFY2(!windowShown(m_second), "the key opened another track's instrument");

		// The selection moved: the key follows it when pressed, never the track it last acted on.
		hideWindow(m_first);
		select(control::trackIdOf(m_second));
		press(Qt::Key_E);
		QVERIFY(windowShown(m_second));
		QVERIFY2(!windowShown(m_first), "the key acted on the previously selected track");
		hideWindow(m_second);

		// A track without an instrument: its first effect's editor. Not the instrument tracks'.
		QVERIFY(run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), m_sampleId}, {QStringLiteral("device"), m_amplifier}}).ok);
		select(m_sampleId);
		const int before = shownEffectDialogs();
		press(Qt::Key_E);
		QCOMPARE(shownEffectDialogs(), before + 1);
		QVERIFY(!windowShown(m_first) && !windowShown(m_second));
		for (QWidget* opened : effectDialogs()) { opened->hide(); }

		// The first-effect key on a track with no effect refuses, shows why and opens nothing elsewhere.
		select(control::trackIdOf(m_second));
		const int none = shownEffectDialogs();
		press(Qt::Key_F);
		QCOMPARE(shownEffectDialogs(), none);
		QVERIFY(!windowShown(m_second));
		QVERIFY2(floated().contains(QStringLiteral("no effect")), qPrintable(floated()));

		// No device at all, and no selection: a typed refusal from the command the key runs.
		select(m_automationId);
		const ControlResult automation = run(QStringLiteral("plugin.selected_editor_open"));
		QVERIFY2(!automation.ok && automation.errorKind != ControlErrorKind::NotFound, qPrintable(automation.errorMessage));
		QVERIFY(run(QStringLiteral("window.select"), {{QStringLiteral("track"), QString()}}).ok);
		QCOMPARE(run(QStringLiteral("plugin.selected_editor_open")).errorKind, ControlErrorKind::Refused);
		QVERIFY(!windowShown(m_first) && !windowShown(m_second));

		// The same command from the menu bar's entry and from an agent.
		select(control::trackIdOf(m_first));
		QMenu menu;
		ShellCommandMenus::populateProject(&menu);
		QAction* entry = nullptr;
		for (QAction* action : menu.actions())
		{
			if (action->menu() != nullptr && action->text() == QStringLiteral("Selected Track")) { entry = action->menu()->actions().first(); }
		}
		QVERIFY2(entry != nullptr, "no Selected Track menu");
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("plugin.selected_editor_open"));
		entry->trigger();
		QVERIFY(windowShown(m_first));
		hideWindow(m_first);
		const ControlResult agent = run(QStringLiteral("plugin.selected_editor_open"));
		QVERIFY2(agent.ok, qPrintable(agent.errorMessage));
		QCOMPARE(agent.result.value(QStringLiteral("target")).toString(), control::trackIdOf(m_first));
		QCOMPARE(agent.result.value(QStringLiteral("plugin")).toString(), QStringLiteral("inst"));
		QCOMPARE(agent.result.value(QStringLiteral("surface")).toString(), QStringLiteral("generic"));
		hideWindow(m_first);
	}

	//! CRITERION 2: a chosen effect, bound through the page, loads onto the selected track only, as one undo step.
	void aBoundEffectLoadsOntoTheSelectionWithOneUndoStep()
	{
		const QString firstId = control::trackIdOf(m_first);
		const QString secondId = control::trackIdOf(m_second);
		const ControlResult bound = bindPluginThroughThePage(m_amplifier, QStringLiteral("Ctrl+Alt+A"));
		QVERIFY2(bound.ok, qPrintable(bound.errorMessage));
		const QString id = bound.result.value(QStringLiteral("id")).toString();
		// Binding is one undo step: it takes the row away, and the redo gives it back.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(listed(id).isEmpty(), "undoing the bind left its row");
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY2(id.startsWith(QStringLiteral("Arranger ▸ Load Plug-in ▸ ")), qPrintable(id));
		QVERIFY2(!id.contains(QStringLiteral("dev-")), "a binding must not name the catalogue position");
		QCOMPARE(listed(id).value(QStringLiteral("keys")).toString(), QStringLiteral("Ctrl+Alt+A"));
		QCOMPARE(listed(id).value(QStringLiteral("command")).toString(), QStringLiteral("plugin.selected_load"));

		// The same key on a second action of the same context is a clash, resolved by the same rule.
		QCOMPARE(bindPluginThroughThePage(m_triple, QStringLiteral("Ctrl+Alt+A")).errorKind, ControlErrorKind::Refused);

		const int firstBefore = effectsOn(firstId);
		const int secondBefore = effectsOn(secondId);
		select(firstId);
		press(Qt::Key_A);
		QCOMPARE(effectsOn(firstId), firstBefore + 1);
		QCOMPARE(effectsOn(secondId), secondBefore);
		const QJsonObject record = lastTransaction();
		QCOMPARE(record.value(QStringLiteral("command")).toString(), QStringLiteral("plugin.load"));
		QCOMPARE(record.value(QStringLiteral("reversible")).toBool(), true);

		// plugin.load's undo: ONE step takes it back, and the redo puts it on the same track.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(effectsOn(firstId), firstBefore);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(effectsOn(firstId), firstBefore + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);

		// The selection decides when the key is pressed.
		select(secondId);
		press(Qt::Key_A);
		QCOMPARE(effectsOn(secondId), secondBefore + 1);
		QCOMPARE(effectsOn(firstId), firstBefore);
		QVERIFY(run(QStringLiteral("control.undo")).ok);

		// A mixer channel selected on its own is a compatible target for an effect.
		const QString channel = control::channelIdOf(Engine::mixer()->mixerChannel(Engine::mixer()->createChannel()));
		QVERIFY(run(QStringLiteral("window.select"), {{QStringLiteral("channel"), channel}}).ok);
		press(Qt::Key_A);
		QCOMPARE(effectsOn(channel), 1);
		QCOMPARE(effectsOn(firstId), firstBefore);
		const ControlResult onChannel = run(QStringLiteral("plugin.selected_editor_open"));
		QVERIFY2(onChannel.ok, qPrintable(onChannel.errorMessage));
		QCOMPARE(onChannel.result.value(QStringLiteral("target")).toString(), channel);
		for (QWidget* opened : effectDialogs()) { opened->hide(); }
		QVERIFY(run(QStringLiteral("control.undo")).ok);

		// An incompatible target: the key shows plugin.load's own refusal and nothing moves anywhere.
		select(m_automationId);
		const int steps = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		const qsizetype shown = floated().size();
		press(Qt::Key_A);
		QVERIFY2(floated().size() > shown, "a refused load showed nothing");
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), steps);
		QCOMPARE(effectsOn(firstId), firstBefore);
		QCOMPARE(effectsOn(secondId), secondBefore);

		// The binding is config, not project state: re-read as the next start does, the key still loads.
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		QCOMPARE(listed(id).value(QStringLiteral("keys")).toString(), QStringLiteral("Ctrl+Alt+A"));
		select(firstId);
		press(Qt::Key_A);
		QCOMPARE(effectsOn(firstId), firstBefore + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);

		// A shortcut set carries it: this build knows the id, so an import does not call it unknown.
		const ControlResult imported = run(QStringLiteral("shortcuts.import"), {{QStringLiteral("overrides"), QJsonObject()},
			{QStringLiteral("panels"), panelshortcuts::overrides()}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QVERIFY(imported.result.value(QStringLiteral("unknown")).toArray().isEmpty());

		// Removing the key is one undo step too, and takes the row and the key's meaning with it.
		QVERIFY(run(QStringLiteral("shortcuts.set"), {{QStringLiteral("id"), id}, {QStringLiteral("scope"), Arranger}, {QStringLiteral("keys"), QString()}}).ok);
		QVERIFY2(listed(id).isEmpty(), "an unbound plug-in action keeps a row");
		press(Qt::Key_A);
		QCOMPARE(effectsOn(firstId), firstBefore);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(listed(id).value(QStringLiteral("keys")).toString(), QStringLiteral("Ctrl+Alt+A"));
	}

	//! CRITERION 2: a chosen instrument replaces the selected track's only, recorded as plugin.load records it.
	void aBoundInstrumentReplacesTheSelectedTracksAndIsReportedIrreversible()
	{
		const ControlResult bound = bindPluginThroughThePage(m_triple, QStringLiteral("Ctrl+Alt+I"));
		QVERIFY2(bound.ok, qPrintable(bound.errorMessage));
		const Instrument* other = m_second->instrument();
		const ControlResult kicker = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), control::trackIdOf(m_first)},
			{QStringLiteral("device"), deviceNamed(QStringLiteral("kicker"))}});
		QVERIFY2(kicker.ok, qPrintable(kicker.errorMessage));
		const int steps = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		select(control::trackIdOf(m_first));
		press(Qt::Key_I);
		QCOMPARE(QString::fromUtf8(m_first->instrument()->descriptor()->name), QStringLiteral("tripleoscillator"));
		QCOMPARE(m_second->instrument(), other);
		const QJsonObject record = lastTransaction();
		QCOMPARE(record.value(QStringLiteral("command")).toString(), QStringLiteral("plugin.load"));
		QCOMPARE(record.value(QStringLiteral("reversible")).toBool(), false);
		QCOMPARE(record.value(QStringLiteral("before")).toObject().value(QStringLiteral("plugin")).toString(), QStringLiteral("kicker"));
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), steps);

		// An instrument has no place on a sample track: refused, and shown.
		select(m_sampleId);
		const int effects = effectsOn(m_sampleId);
		press(Qt::Key_I);
		QCOMPARE(effectsOn(m_sampleId), effects);
		QVERIFY2(floated().contains(QStringLiteral("an instrument loads onto an instrument track")), qPrintable(floated()));
		const ControlResult agent = run(QStringLiteral("plugin.selected_load"), {{QStringLiteral("device"), m_triple}});
		QVERIFY(!agent.ok);
		QCOMPARE(agent.errorKind, ControlErrorKind::Refused);
	}

private:
	InstrumentTrack* addInstrumentTrack(const QString& name)
	{
		const ControlResult added = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), name}});
		const QString id = added.result.value(QStringLiteral("track")).toString();
		ControlResult error;
		auto* track = dynamic_cast<InstrumentTrack*>(control::resolveTrack(id, &error));
		return track;
	}

	void select(const QString& track)
	{
		const ControlResult selected = run(QStringLiteral("window.select"), {{QStringLiteral("track"), track}});
		QVERIFY2(selected.ok, qPrintable(selected.errorMessage));
	}

	//! Ctrl+Alt+@a key to the focused arranger.
	void press(Qt::Key key)
	{
		getGUI()->mainWindow()->activateWindow();
		m_arranger->setFocus();
		QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(m_arranger));
		QTest::keyClick(m_arranger, key, Qt::ControlModifier | Qt::AltModifier);
	}

	QTemporaryDir m_home;
	QString m_triple;
	QString m_amplifier;
	QString m_sampleId;
	QString m_automationId;
	InstrumentTrack* m_first = nullptr;
	InstrumentTrack* m_second = nullptr;
	SongEditor* m_arranger = nullptr;
};

QTEST_MAIN(PluginShortcutsTest)
#include "PluginShortcutsTest.moc"
