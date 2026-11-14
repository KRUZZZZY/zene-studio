/*
 * ItemListPanel.cpp - the editable item list every editing canvas can show beside itself
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

#include "ItemList.h"

#include <algorithm>

#include <QAbstractTableModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QKeyEvent>
#include <QAction>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include "Accessibility.h"
#include "AccessibleWidgets.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "MusicalTime.h"
#include "NoteGrid.h"
#include "ShellChrome.h"
#include "ShellIcons.h"

namespace lmms::gui
{

// ---------------------------------------------------------------------------------------------
// the source's shared part

QString ItemListSource::text(int row, int column) const
{
	const QList<ItemColumn> all = columns();
	const ItemColumn::Kind kind = column >= 0 && column < all.size() ? all.at(column).kind : ItemColumn::Kind::Number;
	const int v = value(row, column);
	switch (kind)
	{
		case ItemColumn::Kind::Position: return musicaltime::positionText(v);
		case ItemColumn::Kind::Length: return musicaltime::lengthText(v);
		case ItemColumn::Kind::Key: return NoteGrid::keyName(v);
		case ItemColumn::Kind::Percent: return QStringLiteral("%1 %").arg(v);
		default: return QString::number(v);
	}
}

int ItemListSource::step(int) const { return 1; }
bool ItemListSource::insertAfter(int) { return false; }
bool ItemListSource::remove(const QList<int>&) { return false; }
void ItemListSource::select(const QList<int>&) {}

namespace
{

bool keyFromText(const QString& text, int* value)
{
	for (int key = 0; key < 128; ++key)
	{
		if (NoteGrid::keyName(key).compare(text, Qt::CaseInsensitive) == 0) { *value = key; return true; }
	}
	return false;
}

bool numberFromText(QString text, int* value, bool allowPercent = false)
{
	text = text.trimmed();
	if (allowPercent && text.endsWith(QLatin1Char('%'))) { text.chop(1); }
	bool ok = false;
	const int number = text.trimmed().toInt(&ok);
	if (ok) { *value = number; }
	return ok;
}

} // namespace

bool ItemListSource::parse(ItemColumn::Kind kind, const QString& text, int* value)
{
	tick_t ticks = 0;
	switch (kind)
	{
		case ItemColumn::Kind::Position:
			if (!musicaltime::positionFromText(text, &ticks)) { return false; }
			*value = static_cast<int>(ticks);
			return true;
		case ItemColumn::Kind::Length:
			if (!musicaltime::lengthFromText(text, &ticks)) { return false; }
			*value = static_cast<int>(ticks);
			return true;
		case ItemColumn::Kind::Key: return numberFromText(text, value) || keyFromText(text.trimmed(), value);
		case ItemColumn::Kind::Percent: return numberFromText(text, value, true);
		case ItemColumn::Kind::Text: return false;
		default: return numberFromText(text, value);
	}
}

QJsonObject ItemListSource::describe(int maxRows) const
{
	const QList<ItemColumn> all = columns();
	QJsonArray cols;
	for (const ItemColumn& column : all)
	{
		cols.append(QJsonObject{{QStringLiteral("id"), column.id}, {QStringLiteral("title"), column.title},
			{QStringLiteral("editable"), column.editable}});
	}
	QJsonArray rows;
	for (int row = 0; row < std::min(rowCount(), maxRows); ++row)
	{
		QJsonObject entry{{QStringLiteral("id"), rowId(row)}};
		for (int column = 0; column < all.size(); ++column) { entry.insert(all.at(column).id, text(row, column)); }
		rows.append(entry);
	}
	return {{QStringLiteral("title"), title()}, {QStringLiteral("columns"), cols}, {QStringLiteral("rows"), rows},
		{QStringLiteral("count"), rowCount()}};
}

bool ItemListSource::run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (result.ok)
	{
		m_error.clear();
		emit changed();  // the list re-reads what its own edit did, whether or not the canvas signals it
		return true;
	}
	refuse(result.errorMessage);
	return false;
}

void ItemListSource::refuse(const QString& why)
{
	m_error = why;
}

// ---------------------------------------------------------------------------------------------
// the table model

class ItemListModel : public QAbstractTableModel
{
public:
	ItemListModel(ItemListSource* source, QObject* parent)
		: QAbstractTableModel(parent)
		, m_source(source)
	{
	}

	void reset()
	{
		beginResetModel();
		m_columns = m_source->columns();
		endResetModel();
	}

	const QList<ItemColumn>& columnsShown() const { return m_columns; }

	int rowCount(const QModelIndex& parent = QModelIndex()) const override
	{
		return parent.isValid() ? 0 : m_source->rowCount();
	}

	int columnCount(const QModelIndex& parent = QModelIndex()) const override
	{
		return parent.isValid() ? 0 : static_cast<int>(m_columns.size());
	}

	QVariant data(const QModelIndex& index, int role) const override
	{
		const bool shown = role == Qt::DisplayRole || role == Qt::EditRole || role == Qt::AccessibleTextRole;
		if (!index.isValid() || !shown || index.row() >= m_source->rowCount()) { return {}; }
		return m_source->text(index.row(), index.column());
	}

	QVariant headerData(int section, Qt::Orientation orientation, int role) const override
	{
		if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole) { return {}; }
		if (orientation == Qt::Vertical) { return section + 1; }
		return section < m_columns.size() ? m_columns.at(section).title : QString();
	}

	Qt::ItemFlags flags(const QModelIndex& index) const override
	{
		const Qt::ItemFlags base = QAbstractTableModel::flags(index);
		const bool editable = index.isValid() && index.column() < m_columns.size() && m_columns.at(index.column()).editable;
		return editable ? base | Qt::ItemIsEditable : base;
	}

	bool setData(const QModelIndex& index, const QVariant& value, int role) override
	{
		if (role != Qt::EditRole || !index.isValid() || index.column() >= m_columns.size()) { return false; }
		int parsed = 0;
		const bool read = ItemListSource::parse(m_columns.at(index.column()).kind, value.toString(), &parsed);
		m_badEntry = read ? QString() : value.toString();
		const bool ok = read && m_source->setValue(index.row(), index.column(), parsed);
		// Told after the editor has closed: the answer re-reads the list, which must not happen under the editor.
		if (edited) { QTimer::singleShot(0, this, [this, ok] { edited(ok); }); }
		return ok;
	}

	std::function<void(bool)> edited;

	//! What the last edit typed that did not read as a value (empty when it did).
	QString badEntry() const { return m_badEntry; }

private:
	ItemListSource* m_source;
	QList<ItemColumn> m_columns;
	QString m_badEntry;
};

// ---------------------------------------------------------------------------------------------
// the panel

namespace
{

QString panelName(const QString& kind) { return QStringLiteral("zeneItemList-") + kind; }

} // namespace

ItemListPanel::ItemListPanel(ItemListSource* source, QWidget* surface, const QString& kind)
	: QWidget(surface, Qt::Tool)
	, m_source(source)
	, m_table(new QTableView(this))
	, m_model(new ItemListModel(source, this))
	, m_status(new QLabel(this))
	, m_reload(new QTimer(this))
{
	setObjectName(panelName(kind));
	setAttribute(Qt::WA_DeleteOnClose);
	source->setParent(this);
	setWindowTitle(tr("List: %1").arg(source->title()));
	m_table->setModel(m_model);
	m_table->setAccessibleName(source->title());
	m_table->setAccessibleDescription(tr("Arrows move; F2 or typing edits a cell; + and - step it; Insert adds an item "
		"after this one, Delete removes the selected ones; Escape closes the list."));
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(kind == QStringLiteral("clips") ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed | QAbstractItemView::DoubleClicked);
	m_table->verticalHeader()->setDefaultSectionSize(tokens::targetSize());  // theme 23: the global zoom and accessible mode
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->installEventFilter(this);
	m_status->setAccessibleName(tr("List status"));
	m_status->setWordWrap(true);
	auto* insert = new QPushButton(tr("Insert"), this);
	auto* remove = new QPushButton(tr("Delete"), this);
	auto* close = new QPushButton(tr("Close"), this);
	insert->setEnabled(source->canInsert());
	remove->setEnabled(source->canRemove());
	insert->setToolTip(tr("Add an item after the current one (Insert)"));
	remove->setToolTip(tr("Remove the selected items (Delete)"));
	connect(insert, &QPushButton::clicked, this, [this] { insertItem(); });
	connect(remove, &QPushButton::clicked, this, [this] { removeItems(); });
	connect(close, &QPushButton::clicked, this, &QWidget::close);
	auto* buttons = new QHBoxLayout;
	for (QPushButton* button : {insert, remove, close}) { buttons->addWidget(button); }
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_table, 1);
	layout->addLayout(buttons);
	layout->addWidget(m_status);
	m_reload->setSingleShot(true);
	m_reload->setInterval(0);
	connect(m_reload, &QTimer::timeout, this, &ItemListPanel::reload);
	connect(source, &ItemListSource::changed, m_reload, qOverload<>(&QTimer::start));
	connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { selectionEdited(); });
	m_model->edited = [this](bool ok) { showResult(ok); };
	resize(tokens::targetSize() * 18, tokens::targetSize() * 14);
	reload();
}

ItemListPanel* ItemListPanel::find(QWidget* surface, const QString& kind)
{
	return surface != nullptr ? surface->findChild<ItemListPanel*>(panelName(kind), Qt::FindDirectChildrenOnly) : nullptr;
}

ItemListPanel* ItemListPanel::open(QWidget* surface, const QString& kind, const std::function<ItemListSource*(QObject*)>& make)
{
	if (surface == nullptr) { return nullptr; }  // no canvas to list or to be placed beside: nothing is made
	if (ItemListPanel* shown = find(surface, kind)) { return shown; }
	auto* panel = new ItemListPanel(make(nullptr), surface, kind);
	// Beside the canvas's window, on the right, where a docked list would be.
	const QRect frame = surface->window()->frameGeometry();
	panel->move(frame.right() - panel->width(), frame.top() + tokens::size::headerHeight);
	return panel;
}

QString ItemListPanel::statusText() const
{
	return m_status->text();
}

QList<int> ItemListPanel::chosenRows() const
{
	QList<int> rows;
	for (const QModelIndex& index : m_table->selectionModel()->selectedRows()) { rows.append(index.row()); }
	if (rows.isEmpty() && m_table->currentIndex().isValid()) { rows.append(m_table->currentIndex().row()); }
	std::sort(rows.begin(), rows.end());
	return rows;
}

void ItemListPanel::reload()
{
	m_reload->stop();
	const QModelIndex current = m_table->currentIndex();
	const QString currentId = current.isValid() && current.row() < m_source->rowCount() ? m_source->rowId(current.row()) : QString();
	QStringList selectedIds;
	for (const int row : chosenRows())
	{
		if (row < m_source->rowCount()) { selectedIds.append(m_source->rowId(row)); }
	}
	m_syncing = true;
	m_model->reset();
	restore(currentId, current.isValid() ? current.column() : 0, selectedIds);
	m_syncing = false;
	setWindowTitle(tr("List: %1").arg(m_source->title()));
	m_table->setAccessibleName(m_source->title());
}

void ItemListPanel::restore(const QString& currentId, int currentColumn, const QStringList& selectedIds)
{
	// The canvas's selection when it has one; else the rows that were selected, found again by their ids.
	QList<int> rows = m_source->tracksSelection() ? m_source->selectedRows() : QList<int>{};
	for (int row = 0; !m_source->tracksSelection() && row < m_source->rowCount(); ++row)
	{
		if (selectedIds.contains(m_source->rowId(row))) { rows.append(row); }
	}
	QItemSelection selection;
	for (const int row : rows) { selection.select(m_model->index(row, 0), m_model->index(row, std::max(0, m_model->columnCount() - 1))); }
	m_table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
	const int found = rowOf(currentId);
	const int currentRow = found >= 0 ? found : rows.isEmpty() ? -1 : rows.first();
	if (currentRow >= 0)
	{
		m_table->selectionModel()->setCurrentIndex(m_model->index(currentRow, std::min(currentColumn, m_model->columnCount() - 1)),
			QItemSelectionModel::NoUpdate);
	}
}

int ItemListPanel::rowOf(const QString& id) const
{
	for (int row = 0; !id.isEmpty() && row < m_source->rowCount(); ++row)
	{
		if (m_source->rowId(row) == id) { return row; }
	}
	return -1;
}

void ItemListPanel::selectionEdited()
{
	if (m_syncing || !m_source->tracksSelection()) { return; }
	QList<int> rows;
	for (const QModelIndex& index : m_table->selectionModel()->selectedRows()) { rows.append(index.row()); }
	std::sort(rows.begin(), rows.end());
	if (rows.size() > 1) { return; }  // the arranger has one current clip, while its list can select several to delete; an empty selection still clears it (new-p8-137)
	if (rows != m_source->selectedRows()) { m_source->select(rows); }
}

void ItemListPanel::showResult(bool ok)
{
	const QString bad = m_model->badEntry();
	const QString why = !bad.isEmpty() ? tr("\"%1\" is not a value for this column").arg(bad) : m_source->lastError();
	m_status->setText(ok ? QString() : tr("Not done: %1").arg(why));
	if (!ok) { a11y::announceTransition(m_status, a11y::Transition::Refused, why); }
	reload();
}

bool ItemListPanel::insertItem()
{
	if (!m_source->canInsert()) { return false; }
	const QModelIndex current = m_table->currentIndex();
	const bool ok = m_source->insertAfter(current.isValid() ? current.row() : -1);
	showResult(ok);
	return ok;
}

bool ItemListPanel::removeItems()
{
	const QList<int> rows = chosenRows();
	if (!m_source->canRemove() || rows.isEmpty()) { return false; }
	const bool ok = m_source->remove(rows);
	showResult(ok);
	return ok;
}

bool ItemListPanel::stepCurrent(int direction)
{
	const QModelIndex current = m_table->currentIndex();
	if (!current.isValid() || current.column() >= m_model->columnsShown().size() || !m_model->columnsShown().at(current.column()).editable)
	{
		return false;
	}
	const int column = current.column();
	const bool ok = m_source->setValue(current.row(), column, m_source->value(current.row(), column) + direction * m_source->step(column));
	showResult(ok);
	return ok;
}

bool ItemListPanel::eventFilter(QObject* watched, QEvent* event)
{
	// The list's own keys; the table keeps its moving and editing keys (an open editor has the focus, not the table).
	if (watched != m_table || event->type() != QEvent::KeyPress) { return false; }
	switch (static_cast<QKeyEvent*>(event)->key())
	{
		case Qt::Key_Insert: insertItem(); break;
		case Qt::Key_Delete:
		case Qt::Key_Backspace: removeItems(); break;
		case Qt::Key_Plus:
		case Qt::Key_Equal: stepCurrent(1); break;
		case Qt::Key_Minus: stepCurrent(-1); break;
		case Qt::Key_Escape: close(); break;
		default: return false;
	}
	return true;
}

ShellButton* itemlists::listButton(const QString& surface, QWidget* canvas, QWidget* parent)
{
	auto* button = new ShellButton(parent);
	const QString tip = QObject::tr("Show or hide these items as an editable list beside the canvas (Ctrl+Shift+L)");
	auto* action = new QAction(shellicons::icon(QStringLiteral("list")), QObject::tr("List"), button);
	action->setToolTip(tip);
	action->setProperty("controlCommand", QStringLiteral("window.item_list"));
	button->setDefaultAction(action);
	button->setObjectName(QStringLiteral("itemListButton-") + surface);
	button->setProperty("controlCommand", QStringLiteral("window.item_list"));
	button->setProperty("itemListSurface", surface);
	a11y::announce(button, action->text(), tip);
	QObject::connect(action, &QAction::triggered, button, [surface, target = QPointer<QWidget>(canvas), button] {
		if (target != nullptr) { target->setFocus(Qt::OtherFocusReason); }
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("window.item_list"),
			{{QStringLiteral("surface"), surface}});
		if (!result.ok) { a11y::announceTransition(button, a11y::Transition::Refused, result.errorMessage); }
	});
	return button;
}

} // namespace lmms::gui
