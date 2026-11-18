/*
 * ShortcutsPage.cpp - M3 item 4: every keyboard shortcut, from the menus that own them
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

#include "ShortcutsPage.h"

#include <algorithm>

#include <QHeaderView>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

#include "CommandForm.h"
#include "CommandMetadata.h"
#include "HelpTasks.h"
#include "ListTable.h"
#include "ShortcutPresets.h"

namespace lmms::gui
{

namespace
{

//! The task a shortcut belongs to, as its position among the manual's sections (for the order) - its command's,
//! else its menu's.
int taskRank(const PaletteEntry& entry)
{
	return static_cast<int>(help::topics().indexOf(help::menuTopic(entry.command, entry.path)));
}

ListRow shortcutRow(const PaletteEntry& entry, const QString& task)
{
	QString label = entry.label;
	label.remove(QLatin1Char('&'));
	const QString command = entry.command.isEmpty() ? QString() : commandName(entry.command);
	ListRow row;
	row.cells = QStringList{task, entry.shortcut, label, entry.path, command};
	row.tips = QStringList{QString(), QString(), QString(), QString(), entry.command};
	// The portable spelling too ("Ctrl+S"): on macOS the shown text is native ("⌘S"), and a filter typed the way the
	// rest of the product writes shortcuts must still find it; the command's id finds it as well.
	row.hiddenWords = QKeySequence::fromString(entry.shortcut, QKeySequence::NativeText).toString(QKeySequence::PortableText)
		+ QLatin1Char(' ') + entry.command;
	return row;
}

} // namespace

QList<PaletteEntry> shortcutEntries(QMenuBar* bar)
{
	QList<PaletteEntry> entries;
	for (const PaletteEntry& entry : paletteMenuEntries(bar))
	{
		if (!entry.shortcut.isEmpty()) { entries << entry; }
	}
	std::stable_sort(entries.begin(), entries.end(), [](const PaletteEntry& a, const PaletteEntry& b) {
		return a.path != b.path ? a.path < b.path : a.label < b.label;
	});
	return entries;
}


ShortcutsPage::ShortcutsPage(QMenuBar* bar, QWidget* parent) :
	QDialog(parent),
	m_entries(shortcutEntries(bar))
{
	setWindowTitle(tr("Keyboard Shortcuts"));
	setObjectName(QStringLiteral("ShortcutsPage"));
	auto* layout = new QVBoxLayout(this);
	m_filter = new QLineEdit(this);
	m_filter->setPlaceholderText(tr("Filter by task, action, menu, key or command"));
	m_filter->setAccessibleName(tr("Filter shortcuts"));
	m_filter->setClearButtonEnabled(true);
	layout->addWidget(m_filter);
	m_presets = new QPushButton(tr("Presets..."), this);
	m_presets->setObjectName(QStringLiteral("shortcutsPresetButton"));
	m_presets->setAccessibleName(tr("Apply a shipped keyboard map"));
	m_presets->setToolTip(tr("Apply the accessibility map or a migration map for Ableton Live, FL Studio or Logic Pro; "
		"the map it replaces is saved as a backup first"));
	m_presetMenu = new QMenu(m_presets);
	m_presetMenu->setObjectName(QStringLiteral("shortcutsPresetMenu"));
	for (const ShortcutPreset& preset : shortcutPresets())
	{
		QAction* action = m_presetMenu->addAction(preset.name);
		action->setObjectName(QStringLiteral("shortcutPreset_%1").arg(preset.id));
		action->setToolTip(preset.description);
		action->setProperty("controlCommand", QStringLiteral("shortcuts.preset_apply"));
		connect(action, &QAction::triggered, this, [this, preset] {
			runCommand(this, CommandCall{QStringLiteral("shortcuts.preset_apply"),
				{{QStringLiteral("preset"), preset.id}}, {}, QString(), false});
		});
	}
	m_presets->setMenu(m_presetMenu);
	layout->addWidget(m_presets, 0, Qt::AlignLeft);

	// Theme 21: by task - the manual's sections in its order - and within a task by menu, as shortcutEntries orders.
	std::stable_sort(m_entries.begin(), m_entries.end(), [](const PaletteEntry& a, const PaletteEntry& b) {
		return taskRank(a) < taskRank(b);
	});
	QList<ListRow> rows;
	for (const PaletteEntry& entry : m_entries)
	{
		m_tasks.append(help::topicTitle(help::menuTopic(entry.command, entry.path)));
		rows.append(shortcutRow(entry, m_tasks.last()));
	}
	m_table = new ListTable(this);
	m_table->setAccessibleName(tr("Keyboard shortcuts, by task"));
	m_table->setAccessibleDescription(tr("Enter runs the highlighted action; the Menu key or Shift+F10 offers its "
		"Command Reference entry; a column title sorts by that column"));
	m_table->verticalHeader()->hide();
	m_table->setRows({tr("Task"), tr("Shortcut"), tr("Action"), tr("Menu"), tr("Command")}, rows);
	m_table->resizeColumnsToContents();
	layout->addWidget(m_table);
	connect(m_filter, &QLineEdit::textChanged, this, &ShortcutsPage::setFilter);
	connect(m_filter, &QLineEdit::returnPressed, m_table, qOverload<>(&QWidget::setFocus));
	connect(m_table, &ListTable::rowActivated, this, &ShortcutsPage::runRow);
	connect(m_table, &ListTable::rowMenuRequested, this, &ShortcutsPage::openRowMenu);
	resize(760, 480);
}


int ShortcutsPage::rowCount() const
{
	return m_table->rowCount();
}


int ShortcutsPage::visibleRowCount() const
{
	return m_table->visibleRowCount();
}


void ShortcutsPage::setFilter(const QString& text)
{
	m_table->setFilter(text);
}


QString ShortcutsPage::taskOf(int row) const
{
	return m_tasks.value(row);
}


bool ShortcutsPage::runRow(int row)
{
	if (row < 0 || row >= m_entries.size() || m_entries.at(row).action.isNull()) { return false; }
	paletteActivate(m_entries.at(row), parentWidget());
	return true;
}


void ShortcutsPage::openRowMenu(int row, const QPoint& at)
{
	QMenu menu(this);
	menu.setAccessibleName(tr("Shortcut actions"));
	const QString command = row >= 0 ? m_entries.at(row).command : QString();
	QAction* run = menu.addAction(tr("Run"));
	run->setEnabled(row >= 0);
	menu.setDefaultAction(run);
	QAction* reference = menu.addAction(tr("Show in the Command Reference"));
	reference->setProperty("controlCommand", QStringLiteral("app.help"));
	reference->setEnabled(!command.isEmpty());
	m_table->addSortMenu(&menu);
	const QAction* chosen = menu.exec(at);
	if (chosen == run) { runRow(row); }
	else if (chosen == reference)
	{
		runCommand(this, CommandCall{QStringLiteral("app.help"), {{QStringLiteral("reference"), true}, {QStringLiteral("query"), command}},
			{}, QString(), false});
	}
}

} // namespace lmms::gui
