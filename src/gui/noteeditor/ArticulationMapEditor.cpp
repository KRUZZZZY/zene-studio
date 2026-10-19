/*
 * ArticulationMapEditor.cpp - Track > Articulations: the articulation map, edited through track.articulations_set
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

#include "ArticulationMapEditor.h"

#include <algorithm>
#include <utility>

#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "NoteConditions.h"

namespace lmms::gui
{

namespace
{

ControlResult run(const char* command, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), args);
}

//! The text of a number cell: blank for "not that".
QString numberText(int value) { return value >= 0 ? QString::number(value) : QString(); }

//! 1, 2, ... @a count: every entry where it stood, the 'from' of an edit that moves none.
std::vector<int> identityOrigins(std::size_t count)
{
	std::vector<int> from(count);
	for (std::size_t i = 0; i < count; ++i) { from[i] = static_cast<int>(i) + 1; }
	return from;
}

//! @a value into the number @a field of @a entry; false (and @a why) when a value has no controller to belong to.
bool applyNumber(Articulation* entry, ArticulationMapEditor::Field field, int value, QString* why)
{
	switch (field)
	{
		case ArticulationMapEditor::Field::Keyswitch: entry->keyswitch = value; return true;
		case ArticulationMapEditor::Field::Program: entry->program = value; return true;
		case ArticulationMapEditor::Field::Controller:
			entry->controller = value;
			if (value < 0) { entry->value = 0; }
			return true;
		case ArticulationMapEditor::Field::Value:
			if (entry->controller < 0)
			{
				*why = ArticulationMapEditor::tr("Choose a controller first: the value is what it is set to.");
				return false;
			}
			entry->value = std::max(0, value);
			return true;
	}
	return false;
}

//! The cells of one row: number, name, keyswitch, program, controller, value.
QStringList rowCells(int row, const Articulation& entry)
{
	return {QString::number(row + 1), entry.name, numberText(entry.keyswitch), numberText(entry.program),
		numberText(entry.controller), entry.controller >= 0 ? QString::number(entry.value) : QString()};
}

} // namespace

ArticulationMapEditor::ArticulationMapEditor(const QString& trackId, QWidget* parent) :
	QDialog(parent),
	m_track(trackId)
{
	setObjectName(QStringLiteral("articulationMapEditor-") + trackId);
	setWindowTitle(tr("Articulations"));
	setAccessibleName(tr("Articulation map"));
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Articulation map status"));
	m_table = new QTableWidget(0, 6, this);
	m_table->setHorizontalHeaderLabels({tr("#"), tr("Name"), tr("Keyswitch"), tr("Program"), tr("Controller"), tr("Value")});
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setAccessibleName(tr("Articulations"));
	m_table->setToolTip(tr("A note asks for an articulation by its number here. Keyswitch is a key (0-127) played just before the "
		"note, Program a program change, Controller and Value a control change; leave a cell blank to send nothing of that kind."));

	m_add = new QPushButton(tr("&New"), this);
	m_add->setToolTip(tr("Add an articulation at the end"));
	m_duplicate = new QPushButton(tr("&Duplicate"), this);
	m_duplicate->setToolTip(tr("Copy the selected articulation, right after it"));
	m_up = new QPushButton(tr("Move &Up"), this);
	m_down = new QPushButton(tr("Move D&own"), this);
	m_remove = new QPushButton(tr("&Remove"), this);
	m_remove->setToolTip(tr("Remove the selected articulation; notes that asked for it then ask for none"));
	auto* close = new QPushButton(tr("Close"), this);
	// Each button is the registered command it runs (the agent-surface sweep reads this).
	for (QPushButton* button : {m_add, m_duplicate, m_up, m_down, m_remove})
	{
		button->setProperty("controlCommand", QStringLiteral("track.articulations_set"));
	}

	connect(m_add, &QPushButton::clicked, this, [this] { addEntry(); });
	connect(m_duplicate, &QPushButton::clicked, this, [this] { duplicateEntry(m_table->currentRow()); });
	connect(m_up, &QPushButton::clicked, this, [this] { moveEntry(m_table->currentRow(), -1); });
	connect(m_down, &QPushButton::clicked, this, [this] { moveEntry(m_table->currentRow(), 1); });
	connect(m_remove, &QPushButton::clicked, this, [this] { removeEntry(m_table->currentRow()); });
	connect(close, &QPushButton::clicked, this, &QDialog::close);
	connect(m_table, &QTableWidget::currentCellChanged, this, [this] { followRow(); });
	connect(m_table, &QTableWidget::cellChanged, this, [this](int row, int column) { cellEdited(row, column); });

	auto* buttons = new QHBoxLayout;
	for (QPushButton* button : {m_add, m_duplicate, m_up, m_down, m_remove}) { buttons->addWidget(button); }
	buttons->addStretch();
	buttons->addWidget(close);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_status);
	layout->addWidget(m_table, 1);
	layout->addLayout(buttons);
	resize(560, 360);
	refresh();
}

ArticulationMapEditor* ArticulationMapEditor::open(const QString& trackId, QWidget* parent)
{
	auto* editor = parent != nullptr ? parent->findChild<ArticulationMapEditor*>(QStringLiteral("articulationMapEditor-") + trackId) : nullptr;
	if (editor == nullptr)
	{
		editor = new ArticulationMapEditor(trackId, parent);
		editor->setAttribute(Qt::WA_DeleteOnClose);
		editor->setWindowFlag(Qt::Window);
	}
	editor->refresh();
	editor->show();
	editor->raise();
	editor->activateWindow();
	return editor;
}

int ArticulationMapEditor::rowCount() const
{
	return m_table->rowCount();
}

void ArticulationMapEditor::refresh()
{
	reload(-1);
}

void ArticulationMapEditor::reload(int selectRow)
{
	const ControlResult listed = run("track.articulations", {{QStringLiteral("track"), m_track}});
	QString why;
	std::vector<Articulation> entries;
	if (!listed.ok || !ArticulationMap::fromJson(listed.result.value(QStringLiteral("articulations")).toArray(), &entries, &why))
	{
		m_entries.clear();
		m_table->setRowCount(0);
		m_status->setText(listed.ok ? why : listed.errorMessage);
		followRow();
		return;
	}
	m_entries = std::move(entries);
	fillRows(selectRow >= 0 ? selectRow : m_table->currentRow());
	m_status->setText(m_message.isEmpty() ? tr("%1 of %2 articulations. A note asks for one by its number (note.edit 'articulation').")
		.arg(m_entries.size()).arg(NoteConditions::MaxArticulations) : m_message);
	followRow();
}

void ArticulationMapEditor::fillRows(int current)
{
	m_refreshing = true;
	m_table->setRowCount(static_cast<int>(m_entries.size()));
	for (int row = 0; row < static_cast<int>(m_entries.size()); ++row)
	{
		const Articulation& entry = m_entries[static_cast<std::size_t>(row)];
		const QStringList cells = rowCells(row, entry);
		for (int column = 0; column < cells.size(); ++column)
		{
			auto* item = new QTableWidgetItem(cells.at(column));
			// The number is not editable, and the value only means something with a controller.
			const bool fixed = column == 0 || (column == 5 && entry.controller < 0);
			if (fixed) { item->setFlags(item->flags() & ~Qt::ItemIsEditable); }
			m_table->setItem(row, column, item);
		}
	}
	m_refreshing = false;
	if (current >= 0 && current < m_table->rowCount()) { m_table->setCurrentCell(current, 1); }
}

void ArticulationMapEditor::followRow()
{
	const int row = m_table->currentRow();
	const int count = static_cast<int>(m_entries.size());
	m_add->setEnabled(count < NoteConditions::MaxArticulations);
	m_duplicate->setEnabled(row >= 0 && count < NoteConditions::MaxArticulations);
	m_up->setEnabled(row > 0);
	m_down->setEnabled(row >= 0 && row + 1 < count);
	m_remove->setEnabled(row >= 0);
}

void ArticulationMapEditor::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	// Back from another window: an undo or an agent's edit may have changed the map meanwhile.
	if (event->type() == QEvent::ActivationChange && isActiveWindow()) { refresh(); }
}

bool ArticulationMapEditor::fail(const QString& why)
{
	m_message = why;
	refresh();
	return false;
}

bool ArticulationMapEditor::nameTaken(const QString& name, int exceptRow) const
{
	for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
	{
		if (i != exceptRow && m_entries[static_cast<std::size_t>(i)].name.compare(name, Qt::CaseInsensitive) == 0) { return true; }
	}
	return false;
}

QString ArticulationMapEditor::freeName(const QString& wanted) const
{
	if (!nameTaken(wanted, -1)) { return wanted; }
	for (int n = 2;; ++n)
	{
		const QString candidate = QStringLiteral("%1 %2").arg(wanted).arg(n);
		if (!nameTaken(candidate, -1)) { return candidate; }
	}
}

bool ArticulationMapEditor::commit(const std::vector<Articulation>& entries, const std::vector<int>& from, int selectRow)
{
	ArticulationMap map;
	map.set(entries);
	QJsonArray origins;
	for (const int origin : from) { origins.append(origin); }
	const ControlResult result = run("track.articulations_set", {{QStringLiteral("track"), m_track},
		{QStringLiteral("articulations"), map.toJson()}, {QStringLiteral("from"), origins}});
	if (!result.ok) { return fail(result.errorMessage); }
	m_message.clear();
	const int remapped = result.result.value(QStringLiteral("notes_remapped")).toInt();
	reload(selectRow);
	if (remapped > 0) { m_status->setText(tr("%1 notes now follow their articulation to its new place.").arg(remapped)); }
	return true;
}

bool ArticulationMapEditor::addEntry(const QString& name)
{
	if (static_cast<int>(m_entries.size()) >= NoteConditions::MaxArticulations)
	{
		return fail(tr("A track holds at most %1 articulations.").arg(NoteConditions::MaxArticulations));
	}
	const QString wanted = name.trimmed();
	if (!wanted.isEmpty() && nameTaken(wanted, -1)) { return fail(tr("There is already an articulation called \"%1\".").arg(wanted)); }
	std::vector<Articulation> entries = m_entries;
	std::vector<int> from = identityOrigins(entries.size());
	Articulation added;
	added.name = wanted.isEmpty() ? freeName(tr("Articulation %1").arg(entries.size() + 1)) : wanted;
	entries.push_back(added);
	from.push_back(0);
	return commit(entries, from, static_cast<int>(entries.size()) - 1);
}

bool ArticulationMapEditor::duplicateEntry(int row)
{
	if (row < 0 || row >= static_cast<int>(m_entries.size())) { return false; }
	if (static_cast<int>(m_entries.size()) >= NoteConditions::MaxArticulations)
	{
		return fail(tr("A track holds at most %1 articulations.").arg(NoteConditions::MaxArticulations));
	}
	std::vector<Articulation> entries = m_entries;
	std::vector<int> from = identityOrigins(entries.size());
	Articulation copy = entries[static_cast<std::size_t>(row)];
	copy.name = freeName(tr("%1 copy").arg(copy.name));
	entries.insert(entries.begin() + row + 1, copy);
	from.insert(from.begin() + row + 1, 0);
	return commit(entries, from, row + 1);
}

bool ArticulationMapEditor::renameEntry(int row, const QString& name)
{
	if (row < 0 || row >= static_cast<int>(m_entries.size())) { return false; }
	const QString wanted = name.trimmed();
	if (wanted.isEmpty()) { return fail(tr("An articulation needs a name.")); }
	if (nameTaken(wanted, row)) { return fail(tr("There is already an articulation called \"%1\".").arg(wanted)); }
	std::vector<Articulation> entries = m_entries;
	entries[static_cast<std::size_t>(row)].name = wanted;
	std::vector<int> from = identityOrigins(entries.size());
	return commit(entries, from, row);
}

bool ArticulationMapEditor::moveEntry(int row, int delta)
{
	const int target = row + delta;
	if (row < 0 || row >= static_cast<int>(m_entries.size()) || target < 0 || target >= static_cast<int>(m_entries.size()) || delta == 0)
	{
		return false;
	}
	std::vector<Articulation> entries = m_entries;
	std::vector<int> from = identityOrigins(entries.size());
	// Take the entry out and put it back where it is going, in the entries and in 'from' alike.
	const Articulation moved = entries[static_cast<std::size_t>(row)];
	const int origin = from[static_cast<std::size_t>(row)];
	entries.erase(entries.begin() + row);
	from.erase(from.begin() + row);
	entries.insert(entries.begin() + target, moved);
	from.insert(from.begin() + target, origin);
	return commit(entries, from, target);
}

bool ArticulationMapEditor::removeEntry(int row)
{
	if (row < 0 || row >= static_cast<int>(m_entries.size())) { return false; }
	std::vector<Articulation> entries = m_entries;
	std::vector<int> from = identityOrigins(entries.size());
	entries.erase(entries.begin() + row);
	from.erase(from.begin() + row);
	return commit(entries, from, std::min(row, static_cast<int>(entries.size()) - 1));
}

bool ArticulationMapEditor::setNumber(int row, Field field, int value)
{
	if (row < 0 || row >= static_cast<int>(m_entries.size())) { return false; }
	if (value < -1 || value > 127) { return fail(tr("A keyswitch, program, controller or value is 0 to 127, or blank.")); }
	std::vector<Articulation> entries = m_entries;
	QString why;
	if (!applyNumber(&entries[static_cast<std::size_t>(row)], field, value, &why)) { return fail(why); }
	return commit(entries, identityOrigins(entries.size()), row);
}

void ArticulationMapEditor::cellEdited(int row, int column)
{
	if (m_refreshing || row < 0 || row >= static_cast<int>(m_entries.size())) { return; }
	const QString text = m_table->item(row, column)->text().trimmed();
	if (column == 1) { renameEntry(row, text); return; }
	if (column < 2 || column > 5) { return; }
	bool ok = true;
	const int value = text.isEmpty() || text == QStringLiteral("-") ? -1 : text.toInt(&ok);
	if (!ok) { fail(tr("A keyswitch, program, controller or value is 0 to 127, or blank.")); return; }
	setNumber(row, static_cast<Field>(column), value);
}

} // namespace lmms::gui
