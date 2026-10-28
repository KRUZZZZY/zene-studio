/*
 * ShortcutsCommandsTest.cpp - shortcuts.*: what the commands do to real bindings
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! WF-110/111 (0.5.0 L13) through the commands themselves, against the REAL main window's menus:
 *  an imported value that is not key text leaves the action alone instead of unbinding it, an
 *  undo puts back every binding the call was not about, and resetting an action no menu has is
 *  refused rather than reported as a successful reset of nothing. */

#include <QtTest>

#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "ShortcutMap.h"
#include "UiConfigStore.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

struct Row
{
	QString id;
	QString keys;
	QString defaults;
	bool overridden = false;
};

//! One binding of the real menus, as shortcuts.list reports it.
Row binding(const QString& id)
{
	const ControlResult listed = run(QStringLiteral("shortcuts.list"));
	for (const QJsonValue& value : listed.result.value(QStringLiteral("bindings")).toArray())
	{
		const QJsonObject row = value.toObject();
		if (row.value(QStringLiteral("id")).toString() == id) { return {id, row.value(QStringLiteral("keys")).toString(),
			row.value(QStringLiteral("default")).toString(), row.value(QStringLiteral("overridden")).toBool()}; }
	}
	return {};
}

//! The first binding of @a command, as shortcuts.list reports it (the command, not a menu path, is stable).
Row commandBinding(const QString& command)
{
	for (const QJsonValue& value : run(QStringLiteral("shortcuts.list")).result.value(QStringLiteral("bindings")).toArray())
	{
		const QJsonObject row = value.toObject();
		if (row.value(QStringLiteral("command")).toString() == command)
		{
			return {row.value(QStringLiteral("id")).toString(), row.value(QStringLiteral("keys")).toString(),
				row.value(QStringLiteral("default")).toString(), row.value(QStringLiteral("overridden")).toBool()};
		}
	}
	return {};
}

//! Two real actions that carry a key of their own - the ones these cases rebind.
QPair<Row, Row> twoBoundActions()
{
	QJsonArray ids;
	for (const QJsonValue& value : run(QStringLiteral("shortcuts.list")).result.value(QStringLiteral("bindings")).toArray())
	{
		const QJsonObject row = value.toObject();
		if (!row.value(QStringLiteral("default")).toString().isEmpty())
		{
			ids.append(row.value(QStringLiteral("id")));
			if (ids.size() == 2) { break; }
		}
	}
	if (ids.size() != 2) { return {}; }
	return {binding(ids.at(0).toString()), binding(ids.at(1).toString())};
}

bool listed(QJsonArray array, const QString& id)
{
	for (const QJsonValue& value : array) { if (value.toString() == id) { return true; } }
	return false;
}

} // namespace

class ShortcutsCommandsTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_home;

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(gui::getGUI() != nullptr);
		ControlRegistry::setReady(true);
		gui::getGUI()->mainWindow()->show();
		static_cast<void>(QTest::qWaitForWindowExposed(gui::getGUI()->mainWindow())); // a wait, not a check (Qt 5.15 marks it nodiscard)
		QTest::qWait(100);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Every case starts from the code's own defaults: no override left by another case.
	void init()
	{
		QVERIFY(run(QStringLiteral("shortcuts.reset")).ok);
	}

	//! BUG 1 (cluster 4959): a value that is not key text is left out and named under 'invalid'.
	//! Read as an empty sequence it would be a valid UNBIND, so {"File ▸ Save": null} silently
	//! took the Save shortcut away instead of reporting the value it could not read.
	void anImportedValueThatIsNotKeyTextLeavesTheActionBound()
	{
		const QPair<Row, Row> two = twoBoundActions();
		QVERIFY(!two.first.id.isEmpty());
		QJsonObject overrides;
		overrides.insert(two.first.id, QJsonValue());  // null: not a sequence, and not an unbind either
		const ControlResult imported = run(QStringLiteral("shortcuts.import"), {{QStringLiteral("overrides"), overrides}});
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		QVERIFY2(listed(imported.result.value(QStringLiteral("invalid")).toArray(), two.first.id),
			"a null value was applied as an unbind instead of reported invalid");
		const Row after = binding(two.first.id);
		QVERIFY2(after.keys == two.first.defaults, qPrintable(QStringLiteral("keys are now '%1'").arg(after.keys)));
		QVERIFY(!after.overridden);
	}

	//! The same for a value that is text but not a key sequence - and a genuine "" is still an unbind.
	void anImportedEmptyStringStillUnbinds()
	{
		const QPair<Row, Row> two = twoBoundActions();
		QVERIFY(!two.first.id.isEmpty());
		const ControlResult cleared = run(QStringLiteral("shortcuts.import"),
			{{QStringLiteral("overrides"), QJsonObject{{two.first.id, QString()}}},
				{QStringLiteral("replace"), true}});
		QVERIFY(cleared.ok);
		QVERIFY(cleared.result.value(QStringLiteral("invalid")).toArray().isEmpty());
		QVERIFY2(binding(two.first.id).keys.isEmpty(), "an empty string is the documented way to unbind");
	}

	//! BUG 2 (cluster 4960): undoing a set puts back EVERY binding the call was not about. An import
	//! may leave two actions on one key; setting a third action to an unused key did not touch them,
	//! so its undo must not clear them either.
	void undoingASetLeavesTheOtherBindingsAlone()
	{
		const QPair<Row, Row> two = twoBoundActions();
		QVERIFY(!two.first.id.isEmpty());
		// Two actions on one key: what an import is allowed to leave behind.
		QVERIFY(run(QStringLiteral("shortcuts.import"), {{QStringLiteral("overrides"),
			QJsonObject{{two.first.id, QStringLiteral("Ctrl+Shift+F9")}, {two.second.id, QStringLiteral("Ctrl+Shift+F9")}}},
			{QStringLiteral("replace"), true}}).ok);
		QCOMPARE(binding(two.second.id).keys, QStringLiteral("Ctrl+Shift+F9"));
		// A third, unused key: nothing clashes, so the call only moved one action.
		const Row target = two.second;
		QVERIFY(run(QStringLiteral("shortcuts.set"), {{QStringLiteral("id"), two.first.id},
			{QStringLiteral("keys"), QStringLiteral("Ctrl+Shift+F10")}}).ok);
		QCOMPARE(binding(two.first.id).keys, QStringLiteral("Ctrl+Shift+F10"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(binding(target.id).keys == QStringLiteral("Ctrl+Shift+F9"),
			qPrintable(QStringLiteral("undo left '%1' with no key, though the call never touched it").arg(binding(target.id).keys)));
		QCOMPARE(binding(two.first.id).keys, QStringLiteral("Ctrl+Shift+F9"));
	}

	

	//! BUG 6 (cluster 5373): an override in the CONFIG FILE that is not a string unbinds the action.
	//! shortcuts.import checks what it is handed and refuses a value that is not key text, but a
	//! hand-edited config is read straight back by ShortcutMap::applyTo, where the non-string became
	//! an empty string through toString() - and an empty string is the documented UNBIND, so
	//! `42` quietly took the action's key away. Only a string may be read as an override; anything
	//! else keeps the action on its default.
	void aNonStringOverrideInTheConfigKeepsTheDefault()
	{
		const QPair<Row, Row> two = twoBoundActions();
		QVERIFY(!two.first.id.isEmpty());
		const QString before = binding(two.first.id).keys;
		QVERIFY(!before.isEmpty());
		QVERIFY(run(QStringLiteral("shortcuts.reset")).ok);

		// The config as a hand edit leaves it: the override's value is a number, not key text.
		QJsonObject store = gui::uistore::read(QLatin1String("shortcuts"));
		store.insert(QStringLiteral("overrides"), QJsonObject{{two.first.id, 42}});
		QVERIFY(gui::uistore::write(QLatin1String("shortcuts"), store, {}, gui::uistore::Inverse{},
			[] { gui::ShortcutMap::apply(gui::getGUI()->mainWindow()->menuBar()); }).ok);

		const Row after = binding(two.first.id);
		QVERIFY2(after.keys == after.defaults, qPrintable(QStringLiteral(
			"a non-string override left the action on '%1', not its default '%2'").arg(after.keys, after.defaults)));
		QVERIFY2(!after.overridden, "a value that is not key text was counted as an override");
	}

	//! Q050-202: the four shipped maps apply to real menu actions, each leaves a recoverable
	//! safety copy, and export/import round-trips every one.
	void theShippedMapsApplyRoundTripAndLeaveABackup()
	{
		const ControlResult presets = run(QStringLiteral("shortcuts.preset_list"));
		QVERIFY2(presets.ok, qPrintable(presets.errorMessage));
		QCOMPARE(presets.result.value(QStringLiteral("count")).toInt(), 4);
		QStringList ids;
		for (const QJsonValue& value : presets.result.value(QStringLiteral("presets")).toArray())
		{
			ids << value.toObject().value(QStringLiteral("preset")).toString();
		}
		QCOMPARE(ids, (QStringList{QStringLiteral("accessibility"), QStringLiteral("ableton-live"),
			QStringLiteral("fl-studio"), QStringLiteral("logic-pro")}));

		for (const QString& id : ids)
		{
			QVERIFY(run(QStringLiteral("shortcuts.reset")).ok);
			const ControlResult applied = run(QStringLiteral("shortcuts.preset_apply"), {{QStringLiteral("preset"), id}});
			QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
			const QString backup = applied.result.value(QStringLiteral("backup")).toString();
			QVERIFY2(!backup.isEmpty() && QFileInfo::exists(backup), qPrintable(backup));
			QVERIFY(applied.result.value(QStringLiteral("imported")).toInt() > 0);
			QVERIFY(applied.result.value(QStringLiteral("invalid")).toArray().isEmpty());
			const QMap<QString, QString> expectedKeys{{QStringLiteral("accessibility"), QStringLiteral("Ctrl+R")},
				{QStringLiteral("ableton-live"), QStringLiteral("F9")}, {QStringLiteral("fl-studio"), QStringLiteral("R")},
				{QStringLiteral("logic-pro"), QStringLiteral("R")}};
			QCOMPARE(commandBinding(QStringLiteral("transport.record")).keys, expectedKeys.value(id));

			// The safety copy restores the map the preset replaced.
			QVERIFY(run(QStringLiteral("shortcuts.import"), {{QStringLiteral("file"), backup},
				{QStringLiteral("replace"), true}}).ok);
			QCOMPARE(commandBinding(QStringLiteral("transport.record")).keys, commandBinding(QStringLiteral("transport.record")).defaults);

			// Export and re-import the preset itself: the bindings survive unchanged.
			QVERIFY(run(QStringLiteral("shortcuts.preset_apply"), {{QStringLiteral("preset"), id}}).ok);
			const QString exported = m_home.filePath(QStringLiteral("%1.json").arg(id));
			QVERIFY(run(QStringLiteral("shortcuts.export"), {{QStringLiteral("file"), exported}}).ok);
			QVERIFY(run(QStringLiteral("shortcuts.reset")).ok);
			QVERIFY(run(QStringLiteral("shortcuts.import"), {{QStringLiteral("file"), exported},
				{QStringLiteral("replace"), true}}).ok);
			QCOMPARE(commandBinding(QStringLiteral("transport.record")).keys, expectedKeys.value(id));
		}
		QVERIFY(run(QStringLiteral("shortcuts.preset_apply"), {{QStringLiteral("preset"), QStringLiteral("no-such-map")}}).errorKind
			== ControlErrorKind::NotFound);
	}

	//! BUG 7 (cluster 4014): an id no menu has is refused, not reported as a reset of nothing.
	void resettingAnActionNoMenuHasIsRefused()
	{
		const ControlResult reset = run(QStringLiteral("shortcuts.reset"),
			{{QStringLiteral("id"), QStringLiteral("No Such Menu ▸ No Such Action")}});
		QVERIFY2(!reset.ok, "a misspelled action id was reported as a successful reset");
		QCOMPARE(reset.errorKind, ControlErrorKind::NotFound);
	}

	//! The known id still resets, and without one every action goes back to its own default.
	void aKnownActionAndEveryActionStillReset()
	{
		const QPair<Row, Row> two = twoBoundActions();
		QVERIFY(!two.first.id.isEmpty());
		QVERIFY(run(QStringLiteral("shortcuts.set"), {{QStringLiteral("id"), two.first.id},
			{QStringLiteral("keys"), QStringLiteral("Ctrl+Shift+F11")}}).ok);
		QVERIFY(binding(two.first.id).overridden);
		const ControlResult one = run(QStringLiteral("shortcuts.reset"), {{QStringLiteral("id"), two.first.id}});
		QVERIFY(one.ok);
		QCOMPARE(one.result.value(QStringLiteral("reset")).toInt(), 1);
		QCOMPARE(binding(two.first.id).keys, two.first.defaults);

		QVERIFY(run(QStringLiteral("shortcuts.set"), {{QStringLiteral("id"), two.first.id},
			{QStringLiteral("keys"), QStringLiteral("Ctrl+Shift+F12")}}).ok);
		QVERIFY(run(QStringLiteral("shortcuts.set"), {{QStringLiteral("id"), two.second.id},
			{QStringLiteral("keys"), QStringLiteral("Ctrl+Shift+F12")}, {QStringLiteral("force"), true}}).ok);
		QVERIFY(run(QStringLiteral("shortcuts.reset")).ok);
		QVERIFY(!binding(two.first.id).overridden);
		QVERIFY(!binding(two.second.id).overridden);
		QCOMPARE(binding(two.first.id).keys, two.first.defaults);
		QCOMPARE(binding(two.second.id).keys, two.second.defaults);
	}

	//! Menu rows pass an explicit empty scope; the command schema must still accept the menu map.
	void anExplicitEmptyScopeCanRebindAMenuAction()
	{
		const QPair<Row, Row> two = twoBoundActions();
		QVERIFY(!two.first.id.isEmpty());
		const QString chosen = QStringLiteral("Ctrl+Alt+F8");
		const ControlResult set = run(QStringLiteral("shortcuts.set"), {{QStringLiteral("id"), two.first.id},
			{QStringLiteral("scope"), QString()}, {QStringLiteral("keys"), chosen}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(binding(two.first.id).keys, chosen);
		const ControlResult reset = run(QStringLiteral("shortcuts.reset"), {{QStringLiteral("id"), two.first.id},
			{QStringLiteral("scope"), QString()}});
		QVERIFY2(reset.ok, qPrintable(reset.errorMessage));
		QCOMPARE(binding(two.first.id).keys, two.first.defaults);
	}

	//! Q050-055.1 x Q050-202: a shipped preset maps menu actions only, so a panel binding survives
	//! it and its undo, and the preset's safety copy carries the panel map so it restores that too.
	void aPresetKeepsPanelBindingsAndItsBackupCarriesThem()
	{
		const QJsonArray arranger = run(QStringLiteral("shortcuts.list"), {{QStringLiteral("scope"), QStringLiteral("arranger")}})
			.result.value(QStringLiteral("bindings")).toArray();
		QVERIFY(!arranger.isEmpty());
		const QString id = arranger.first().toObject().value(QStringLiteral("id")).toString();
		const QString chosen = QStringLiteral("Ctrl+Alt+F7");
		const auto panelKeys = [&id] {
			for (const QJsonValue& value : run(QStringLiteral("shortcuts.list"), {{QStringLiteral("scope"), QStringLiteral("arranger")}})
				.result.value(QStringLiteral("bindings")).toArray())
			{
				if (value.toObject().value(QStringLiteral("id")).toString() == id) { return value.toObject().value(QStringLiteral("keys")).toString(); }
			}
			return QString();
		};
		QVERIFY(run(QStringLiteral("shortcuts.set"), {{QStringLiteral("id"), id}, {QStringLiteral("scope"), QStringLiteral("arranger")},
			{QStringLiteral("keys"), chosen}}).ok);

		const ControlResult applied = run(QStringLiteral("shortcuts.preset_apply"), {{QStringLiteral("preset"), QStringLiteral("ableton-live")}});
		QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
		QVERIFY2(panelKeys() == chosen, qPrintable(QStringLiteral("the preset left the arranger binding on '%1'").arg(panelKeys())));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(panelKeys(), chosen);

		QVERIFY(run(QStringLiteral("shortcuts.reset")).ok);
		QVERIFY(panelKeys() != chosen);
		QVERIFY(run(QStringLiteral("shortcuts.import"), {{QStringLiteral("file"), applied.result.value(QStringLiteral("backup")).toString()},
			{QStringLiteral("replace"), true}}).ok);
		QVERIFY2(panelKeys() == chosen, "the preset's safety copy did not restore the panel binding");
	}
};

QTEST_MAIN(ShortcutsCommandsTest)
#include "ShortcutsCommandsTest.moc"
