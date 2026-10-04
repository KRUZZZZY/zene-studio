/*
 * CommandPaletteTest.cpp - M3.4's command palette: what it lists, how it ranks, what it runs
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

/*! The palette is a way to REACH the menus' and the registry's own implementations, so what
 *  is held here is exactly that: every enabled menu action appears with its menu path, its
 *  shortcut and the command it declares; separators, disabled and hidden actions do not; a
 *  registry command appears only when it needs no argument, declares no `requires` and no
 *  menu already carries it; ranking puts a prefix first; and running an entry triggers the
 *  REAL action (or dispatches the command) - never a copy of either.
 *  0.5.0 theme 4: a registry entry reads by its name with its id secondary, a command with
 *  arguments left to give configures first (Run with defaults second), and one that cannot be
 *  undone is flagged.
 */

#include <QtTest>

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QSignalSpy>

#include "DeprecationHelper.h"
#include "CommandForm.h"
#include "CommandMetadata.h"
#include "CommandPalette.h"
#include "ControlRegistry.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

int g_noopRuns = 0;

void registerFakes()
{
	ControlRegistry* registry = ControlRegistry::instance();
	auto add = [registry](const char* id, QJsonObject args, QStringList needs, bool counts) {
		ControlCommand cmd;
		cmd.id = QLatin1String(id);
		cmd.group = cmd.id.section(QLatin1Char('.'), 0, 0);
		cmd.verb = cmd.id.section(QLatin1Char('.'), 1);
		cmd.description = QStringLiteral("A fake for the palette test. It does nothing else.");
		cmd.argsSchema = args;
		cmd.resultSchema = QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}};
		cmd.requiresDecl = needs;
		cmd.requiresEngine = false;
		cmd.handler = [counts](const QJsonObject&) {
			if (counts) { ++g_noopRuns; }
			return ControlResult::success(QJsonObject());
		};
		if (!registry->hasCommand(cmd.id)) { registry->registerCommand(cmd); }  // a real command is used as it is
	};
	const QJsonObject none{{QStringLiteral("type"), QStringLiteral("object")}};
	add("palettetest.noop", none, {}, true);
	add("palettetest.needs_arg", QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), QJsonObject{{QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
		{QStringLiteral("required"), QJsonArray{QStringLiteral("x")}}}, {}, false);
	add("palettetest.needs_human", none, {QStringLiteral("human")}, false);
	add("palettetest.in_a_menu", none, {}, false);
	add("palettetest.optional", QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
		{QStringLiteral("properties"), QJsonObject{{QStringLiteral("level"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}}}},
		{}, true);
	// A real id the reversibility table classes irreversible (the registry already has it): the flag comes from the table.
	add("safestart.clear", none, {}, false);
}

bool hasLabel(const QList<PaletteEntry>& entries, const QString& label)
{
	return std::any_of(entries.begin(), entries.end(), [&](const PaletteEntry& e) { return e.label == label; });
}

bool hasCommand(const QList<PaletteEntry>& entries, const QString& id)
{
	return std::any_of(entries.begin(), entries.end(), [&](const PaletteEntry& e) { return e.command == id; });
}

PaletteEntry entryFor(const QList<PaletteEntry>& entries, const QString& id)
{
	const auto found = std::find_if(entries.begin(), entries.end(), [&](const PaletteEntry& e) { return e.command == id; });
	return found != entries.end() ? *found : PaletteEntry();
}

} // namespace


class CommandPaletteTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		registerFakes();
		QMenu* file = m_bar.addMenu(QStringLiteral("&File"));
		m_save = file->addAction(QStringLiteral("&Save"));
		m_save->setShortcut(keySequence(Qt::CTRL, Qt::Key_S));
		m_save->setProperty("controlCommand", QStringLiteral("project.save"));
		file->addAction(QStringLiteral("Save &As..."));
		file->addSeparator();
		QMenu* recent = file->addMenu(QStringLiteral("&Recent"));
		recent->addAction(QStringLiteral("song.mmp"));
		QAction* disabled = file->addAction(QStringLiteral("Disabled thing"));
		disabled->setEnabled(false);
		QAction* hidden = file->addAction(QStringLiteral("Hidden thing"));
		hidden->setVisible(false);
		QMenu* view = m_bar.addMenu(QStringLiteral("&View"));
		QAction* inMenu = view->addAction(QStringLiteral("Export"));
		inMenu->setProperty("controlCommand", QStringLiteral("palettetest.in_a_menu"));
	}

	void menuEntriesCarryTheirPathShortcutAndCommand()
	{
		const QList<PaletteEntry> entries = paletteMenuEntries(&m_bar);
		QVERIFY(hasLabel(entries, QStringLiteral("Save")));
		QVERIFY(hasLabel(entries, QStringLiteral("Save As...")));
		QVERIFY(hasLabel(entries, QStringLiteral("song.mmp")));
		QVERIFY2(!hasLabel(entries, QStringLiteral("Disabled thing")), "a disabled action was listed");
		QVERIFY2(!hasLabel(entries, QStringLiteral("Hidden thing")), "a hidden action was listed");
		for (const PaletteEntry& entry : entries)
		{
			if (entry.label == QStringLiteral("Save"))
			{
				QCOMPARE(entry.path, QStringLiteral("File"));
				QCOMPARE(entry.command, QStringLiteral("project.save"));
				QVERIFY(!entry.shortcut.isEmpty());
			}
			if (entry.label == QStringLiteral("song.mmp")) { QCOMPARE(entry.path, QStringLiteral("File ▸ Recent")); }
		}
	}

	void registryEntriesAreTheArgumentFreeUnclaimedOnes()
	{
		const QList<PaletteEntry> entries = paletteRegistryEntries(paletteMenuEntries(&m_bar));
		QVERIFY(hasCommand(entries, QStringLiteral("palettetest.noop")));
		QVERIFY2(!hasCommand(entries, QStringLiteral("palettetest.needs_arg")), "a command with a required argument was listed");
		QVERIFY2(!hasCommand(entries, QStringLiteral("palettetest.needs_human")), "a command that requires a human was listed");
		QVERIFY2(!hasCommand(entries, QStringLiteral("palettetest.in_a_menu")), "a command a menu already carries was listed twice");
	}

	//! 0.5.0 W1: a command that takes required arguments is listed too, and runs through its form.
	void aCommandWithRequiredArgumentsOpensItsForm()
	{
		const QList<PaletteEntry> forms = paletteFormEntries(paletteMenuEntries(&m_bar));
		QVERIFY(hasCommand(forms, QStringLiteral("palettetest.needs_arg")));
		QVERIFY2(!hasCommand(forms, QStringLiteral("palettetest.noop")), "an argument-free command was listed as a form");
		const PaletteEntry entry = entryFor(forms, QStringLiteral("palettetest.needs_arg"));
		QVERIFY(entry.form);
		QWidget host;
		QVERIFY(paletteActivate(entry, &host).contains(QStringLiteral("form")));
		auto* form = host.findChild<CommandForm*>();
		QVERIFY(form != nullptr);
		QCOMPARE(form->fields(), QStringList{QStringLiteral("x")});
	}

	void aPrefixRanksFirstAndNothingMatchesNothing()
	{
		const QList<PaletteEntry> entries = paletteMenuEntries(&m_bar);
		const QList<PaletteEntry> sav = paletteMatches(entries, QStringLiteral("sav"));
		QVERIFY(sav.size() >= 2);
		QVERIFY(sav.at(0).label.startsWith(QStringLiteral("Save")));
		QVERIFY(sav.at(1).label.startsWith(QStringLiteral("Save")));
		QCOMPARE(paletteMatches(entries, QStringLiteral("exp")).first().label, QStringLiteral("Export"));
		QVERIFY(paletteMatches(entries, QStringLiteral("zzqq")).isEmpty());
		// The path matches too: "recent" finds the entry filed under File > Recent.
		QCOMPARE(paletteMatches(entries, QStringLiteral("recent")).first().label, QStringLiteral("song.mmp"));
		QCOMPARE(paletteMatches(entries, QString()).size(), entries.size());
	}

	void runningAnEntryTriggersTheRealActionOrCommand()
	{
		QSignalSpy triggered(m_save, &QAction::triggered);
		CommandPalette palette(&m_bar);
		palette.setQuery(QStringLiteral("save"));
		QCOMPARE(palette.shown().first().label, QStringLiteral("Save"));
		QVERIFY(palette.activateRow(0));
		QCOMPARE(triggered.count(), 1);

		const int before = g_noopRuns;
		PaletteEntry command;
		command.label = command.command = QStringLiteral("palettetest.noop");
		const QString outcome = paletteActivate(command);
		QCOMPARE(g_noopRuns, before + 1);
		QVERIFY2(outcome.contains(QStringLiteral("done")), qPrintable(outcome));
	}

	//! 0.5.0 theme 4: a registry command reads by its name, filed under its group, with its id kept secondary.
	void aRegistryCommandReadsByItsNameWithItsIdSecondary()
	{
		const PaletteEntry noop = entryFor(paletteRegistryEntries(paletteMenuEntries(&m_bar)), QStringLiteral("palettetest.noop"));
		QCOMPARE(noop.label, QStringLiteral("Noop"));
		QCOMPARE(noop.path, QStringLiteral("Palettetest"));
		QCOMPARE(noop.command, QStringLiteral("palettetest.noop"));
		QVERIFY2(noop.detail.contains(QStringLiteral("It does nothing else.")), "the whole description, not its first sentence");
		QCOMPARE(commandName(QStringLiteral("transport.set_tempo")), QStringLiteral("Set Tempo"));
		QCOMPARE(commandName(QStringLiteral("track.remove")), QStringLiteral("Remove Track"));
		QCOMPARE(commandName(QStringLiteral("transport.tempo_map_add")), QStringLiteral("Add Tempo Map"));
		QCOMPARE(commandName(QStringLiteral("transport.get_state")), QStringLiteral("Show Transport State"));
		QCOMPARE(commandName(QStringLiteral("interchange.smf_export")), QStringLiteral("Export MIDI File"));
		QCOMPARE(commandCategory(QStringLiteral("safestart.clear")), QStringLiteral("Safe Start"));
		// The id still finds it: a script writer types what they know.
		QCOMPARE(paletteMatches({noop}, QStringLiteral("palettetest.noop")).size(), 1);
		CommandPalette palette(&m_bar);
		palette.setQuery(QStringLiteral("Noop"));
		QCOMPARE(palette.shown().first().command, QStringLiteral("palettetest.noop"));
		QVERIFY2(palette.detailText().contains(QStringLiteral("palettetest.noop")), qPrintable(palette.detailText()));
	}

	//! 0.5.0 theme 4: arguments left to give make Configure the primary action and "Run with defaults" the secondary.
	void unresolvedArgumentsMakeConfigurePrimary()
	{
		const QList<PaletteEntry> entries = paletteRegistryEntries(paletteMenuEntries(&m_bar));
		const PaletteEntry optional = entryFor(entries, QStringLiteral("palettetest.optional"));
		QVERIFY2(optional.form, "a command with optional arguments ran without asking");
		QVERIFY(!entryFor(entries, QStringLiteral("palettetest.noop")).form);
		QWidget host;
		const int before = g_noopRuns;
		QVERIFY(paletteActivate(optional, &host).contains(QStringLiteral("form")));
		QCOMPARE(g_noopRuns, before);
		auto* form = host.findChild<CommandForm*>();
		QVERIFY(form != nullptr);
		QCOMPARE(form->fields(), QStringList{QStringLiteral("level")});
		QVERIFY(paletteCanRunWithDefaults(optional));
		const QString ran = paletteRunWithDefaults(optional, &host);
		QVERIFY2(ran.contains(QStringLiteral("done")), qPrintable(ran));
		QCOMPARE(g_noopRuns, before + 1);
		// A required argument the selection does not fill: no "Run with defaults", and asking for it says why.
		const PaletteEntry needs = entryFor(paletteFormEntries(paletteMenuEntries(&m_bar)), QStringLiteral("palettetest.needs_arg"));
		QVERIFY(!paletteCanRunWithDefaults(needs));
		QVERIFY(paletteRunWithDefaults(needs, &host).contains(QStringLiteral("Configure")));
		QVERIFY2(!paletteCanRunWithDefaults(entryFor(entries, QStringLiteral("palettetest.noop"))), "a command with nothing to configure");
	}

	//! 0.5.0 theme 4: a command the reversibility table says cannot be undone is flagged (and, on a screen, asked).
	void anIrreversibleCommandIsFlagged()
	{
		const QList<PaletteEntry> entries = paletteRegistryEntries(paletteMenuEntries(&m_bar));
		QVERIFY(entryFor(entries, QStringLiteral("safestart.clear")).irreversible);
		QVERIFY(!entryFor(entries, QStringLiteral("palettetest.noop")).irreversible);
		QVERIFY(commandIsIrreversible(QStringLiteral("project.new")));
		QVERIFY(!commandIsIrreversible(QStringLiteral("palettetest.noop")));
		QVERIFY(commandUndoWarning(QStringLiteral("project.new")).contains(QStringLiteral("cannot be undone")));
		QVERIFY(commandUndoWarning(QStringLiteral("palettetest.noop")).isEmpty());
		// Headless there is nobody to ask, so the run goes ahead (confirmAction answers yes): the command itself answers -
		// here refusing, as there is no crash marker to clear - rather than the question cancelling it.
		const QString outcome = paletteActivate(entryFor(entries, QStringLiteral("safestart.clear")));
		QVERIFY2(!outcome.isEmpty() && !outcome.contains(QStringLiteral("cancelled")), qPrintable(outcome));
	}

private:
	QMenuBar m_bar;
	QAction* m_save = nullptr;
};

QTEST_MAIN(CommandPaletteTest)
#include "CommandPaletteTest.moc"
