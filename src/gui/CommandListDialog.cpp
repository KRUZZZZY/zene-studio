/*
 * CommandListDialog.cpp - a list a command answers, with row actions that run other commands
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

#include "CommandListDialog.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTime>
#include <QVBoxLayout>

#include "AccessibleWidgets.h"
#include "CommandMetadata.h"
#include "ListTable.h"

namespace lmms::gui
{

namespace
{

constexpr int MaxCellChars = 120;
constexpr int MaxResultLines = 8;   //!< a reply's "Label: value" lines kept in the Results log per action
constexpr int MaxResultBlocks = 500;

QString clipped(const QString& text)
{
	return text.size() > MaxCellChars ? text.left(MaxCellChars - 1) + QStringLiteral("…") : text;
}

//! A cell as the table shows it: valueText's reading (a whole number in full - a size in bytes, a tick, never
//! "1.23457e+06"; a list of plain values as "a, b"), cut at MaxCellChars.
QString cellText(const QJsonValue& value)
{
	return clipped(valueText(value));
}

//! What the "are you sure" question calls @a item: its name (or label, or path), else its first column.
QString rowName(const QJsonObject& item, const QStringList& columns)
{
	for (const char* field : {"name", "label", "display_name", "path"})
	{
		const QString text = item.value(QLatin1String(field)).toString();
		if (!text.isEmpty()) { return text; }
	}
	return columns.isEmpty() ? QString() : cellText(item.value(columns.first()));
}

/*! Whether to ask before running @a id: when the list declares it, or (theme 4) when the command cannot be
 *  undone - with what is lost in @a caution, the declared caution first. */
bool needsConfirm(bool declared, const QString& id, QString* caution)
{
	const QString warning = commandUndoWarning(id);
	if (caution->isEmpty()) { *caution = warning; }
	return declared || !warning.isEmpty();
}

//! Where @a row is in @a rows, or -1.
int indexOfRow(const QJsonArray& rows, const QJsonValue& row)
{
	for (int i = 0; i < rows.size(); ++i)
	{
		if (rows.at(i) == row) { return i; }
	}
	return -1;
}

//! The list command's reply, read under a busy cursor: a long reading shows that it is under way.
ControlResult readList(const QString& id, const QJsonObject& args)
{
	QGuiApplication::setOverrideCursor(Qt::BusyCursor);
	const ControlResult listed = ControlRegistry::instance()->invoke(id, args);
	QGuiApplication::restoreOverrideCursor();
	return listed;
}

//! @a result's reply as indented "Label: value" lines under its outcome, for the Results log; empty for none.
QString replyLines(const ControlResult& result)
{
	if (!result.ok) { return QString(); }
	QString lines = resultSummary(result.result, MaxResultLines);
	return lines.isEmpty() ? lines : QStringLiteral("\n    ") + lines.replace(QLatin1Char('\n'), QStringLiteral("\n    "));
}

} // namespace

CommandListDialog::CommandListDialog(const ListSpec& spec, QWidget* parent) :
	QDialog(parent),
	m_spec(spec)
{
	setWindowTitle(spec.title);
	setAccessibleName(spec.title);
	setObjectName(QStringLiteral("commandList"));
	auto* layout = new QVBoxLayout(this);
	m_summary = new QLabel(this);
	m_summary->setWordWrap(true);
	m_summary->setObjectName(QStringLiteral("commandListSummary"));
	layout->addWidget(m_summary);
	m_table = new ListTable(this);
	m_table->setObjectName(QStringLiteral("commandListTable"));
	m_table->setAccessibleName(spec.title);
	if (!spec.actions.isEmpty())
	{
		m_table->setAccessibleDescription(tr("Enter runs %1; the Menu key or Shift+F10 opens the row's actions; a column "
			"title sorts by that column").arg(spec.actions.first().label));
	}
	addFilter(layout);
	layout->addWidget(m_table, 3);

	auto* buttons = new QHBoxLayout;
	for (int i = 0; i < spec.actions.size(); ++i)
	{
		auto* button = new QPushButton(spec.actions.at(i).label, this);
		button->setProperty("controlCommand", spec.actions.at(i).id);
		connect(button, &QPushButton::clicked, this, [this, i] { runAction(m_table->currentDataRow(), i); });
		buttons->addWidget(button);
		m_actionButtons.append(button);
	}
	buttons->addStretch();
	for (int i = 0; i < spec.extras.size(); ++i)
	{
		auto* button = new QPushButton(spec.extras.at(i).label, this);
		button->setProperty("controlCommand", spec.extras.at(i).call.id);
		// As syncButtons() does for the row actions: a command this build lacks is a button that cannot be pressed.
		button->setEnabled(ControlRegistry::instance()->hasCommand(spec.extras.at(i).call.id));
		connect(button, &QPushButton::clicked, this, [this, i] { runExtra(i); });
		buttons->addWidget(button);
	}
	auto* refreshButton = new QPushButton(tr("Refresh"), this);
	refreshButton->setProperty("controlCommand", spec.listId);  // it re-runs the list command
	connect(refreshButton, &QPushButton::clicked, this, &CommandListDialog::refresh);
	buttons->addWidget(refreshButton);
	layout->addLayout(buttons);
	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("commandListStatus"));
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("Last outcome"));
	layout->addWidget(m_status);
	m_results = new QPlainTextEdit(this);
	m_results->setObjectName(QStringLiteral("commandListResults"));
	m_results->setReadOnly(true);
	m_results->setMaximumBlockCount(MaxResultBlocks);
	m_results->setAccessibleName(tr("Results"));
	m_results->setAccessibleDescription(tr("Every action's outcome in this list, with its reply, oldest first"));
	m_results->setPlaceholderText(tr("Results of the actions you run here stay in this log."));
	m_results->hide();  // shown with the first outcome, so an untouched list stays compact
	layout->addWidget(m_results, 1);

	connect(m_table, &QTableWidget::itemSelectionChanged, this, &CommandListDialog::syncButtons);
	connect(m_table, &ListTable::rowActivated, this, &CommandListDialog::activateRow);
	connect(m_table, &ListTable::rowMenuRequested, this, &CommandListDialog::openRowMenu);
	resize(640, 480);
	refresh();
}

void CommandListDialog::refresh()
{
	const int was = m_table->currentDataRow();
	const QJsonValue kept = was >= 0 && was < m_rows.size() ? m_rows.at(was) : QJsonValue();
	const QString before = m_status->text();
	m_status->setText(tr("Reading %1...").arg(m_spec.title));
	if (m_status->isVisible()) { m_status->repaint(); }  // painted before the reading, which holds the event loop
	const ControlResult listed = readList(m_spec.listId, m_spec.listArgs);
	m_listed = listed.ok ? listed.result : QJsonObject();
	m_rows = listRows(m_spec, m_listed);
	m_status->setText(listed.ok ? before : listed.errorMessage);
	fill(kept);
}

void CommandListDialog::showSummary()
{
	const int shown = m_table->visibleRowCount();
	QStringList summary{shown == m_rows.size() ? tr("%n item(s)", "", shown)
		: tr("%1 of %n item(s) shown", "", static_cast<int>(m_rows.size())).arg(shown)};
	for (const QString& key : m_spec.summaryKeys)
	{
		if (m_listed.contains(key)) { summary.append(QStringLiteral("%1: %2").arg(fieldLabel(key), cellText(m_listed.value(key)))); }
	}
	m_summary->setText(summary.join(QStringLiteral("   ")));
}

int CommandListDialog::find(const QString& text)
{
	m_table->setFilter(text);
	showSummary();
	syncButtons();
	return m_table->visibleRowCount();
}

void CommandListDialog::fill(const QJsonValue& kept)
{
	QStringList columns = m_spec.columns;
	if (columns.isEmpty() && !m_rows.isEmpty()) { columns = m_rows.first().toObject().keys(); }
	QStringList titles;
	for (const QString& column : columns) { titles.append(fieldLabel(column)); }
	QList<ListRow> rows;
	for (const QJsonValue& value : m_rows)
	{
		const QJsonObject item = value.toObject();
		ListRow row;
		for (const QString& column : columns)
		{
			const QString text = valueText(item.value(column));
			row.cells.append(clipped(text));
			row.tips.append(text.size() > MaxCellChars ? text : QString());  // a cut path or error stays readable
		}
		rows.append(row);
	}
	// The selection follows its row, not its index: after a Remove the next row must not slide under it; a row
	// that is gone leaves nothing selected, so no action is aimed at another row.
	m_table->setRows(titles, rows, indexOfRow(m_rows, kept));
	showSummary();
	syncButtons();
}

void CommandListDialog::syncButtons()
{
	const bool chosen = m_table->currentDataRow() >= 0;
	for (int i = 0; i < m_actionButtons.size(); ++i)
	{
		m_actionButtons.at(i)->setEnabled(chosen && ControlRegistry::instance()->hasCommand(m_spec.actions.at(i).id));
	}
}

ControlResult CommandListDialog::runAction(int index, int action)
{
	if (index < 0 || index >= m_rows.size() || action < 0 || action >= m_spec.actions.size())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, tr("no row or no such action"));
	}
	const RowAction& spec = m_spec.actions.at(action);
	const QJsonObject item = row(index);
	// A form asking for a track or a clip starts from the selected one.
	CommandCall call{spec.id, spec.fixed, selectionContext(spec.id), spec.label, spec.ask, spec.showResult};
	for (auto it = spec.fromRow.begin(); it != spec.fromRow.end(); ++it) { call.fixed.insert(it.key(), item.value(it.value())); }
	QString caution = spec.caution;
	if (needsConfirm(spec.confirm, spec.id, &caution) && !confirmAction(this, spec.label, rowName(item, m_spec.columns), caution))
	{
		return cancelled(spec.label);
	}
	return dispatch(spec.label, call);
}

ControlResult CommandListDialog::activateRow(int index)
{
	if (m_spec.actions.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, tr("this list has no row action")); }
	return runAction(index, 0);
}

void CommandListDialog::buildRowMenu(QMenu* menu, int index)
{
	for (int i = 0; i < m_spec.actions.size(); ++i)
	{
		QAction* action = menu->addAction(m_spec.actions.at(i).label);
		action->setData(i);
		action->setProperty("controlCommand", m_spec.actions.at(i).id);
		action->setEnabled(index >= 0 && ControlRegistry::instance()->hasCommand(m_spec.actions.at(i).id));
		if (i == 0) { menu->setDefaultAction(action); }  // shown bold: what Enter does
	}
	if (!m_spec.actions.isEmpty()) { menu->addSeparator(); }
	m_table->addSortMenu(menu);
	QAction* again = menu->addAction(tr("Refresh"));
	again->setData(-1);
	again->setProperty("controlCommand", m_spec.listId);
}

void CommandListDialog::openRowMenu(int index, const QPoint& at)
{
	QMenu menu(this);
	menu.setAccessibleName(tr("Row actions"));
	buildRowMenu(&menu, index);
	const QAction* chosen = menu.exec(at);
	// Sort By's entries act by themselves; a row action or Refresh runs once the menu is closed.
	if (chosen == nullptr || !chosen->data().isValid()) { return; }
	if (chosen->data().toInt() < 0) { refresh(); }
	else { runAction(index, chosen->data().toInt()); }
}

ControlResult CommandListDialog::runExtra(int index)
{
	if (index < 0 || index >= m_spec.extras.size())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, tr("no such button"));
	}
	const ListExtra& extra = m_spec.extras.at(index);
	QString caution;
	if (needsConfirm(extra.confirm, extra.call.id, &caution) && !confirmAction(this, extra.label, QString(), caution))
	{
		return cancelled(extra.label);
	}
	return dispatch(extra.label, extra.call);
}

ControlResult CommandListDialog::cancelled(const QString& label)
{
	const ControlResult refused = ControlResult::failure(ControlErrorKind::Refused, tr("cancelled"));
	report(label, refused);  // "<label>: cancelled", as a dismissed form says
	return refused;
}

void CommandListDialog::addFilter(QBoxLayout* layout)
{
	if (m_spec.filterArg.isEmpty())
	{
		// Theme 18: a list without a find argument of its own is found in as shown.
		auto* local = new QLineEdit(this);
		local->setObjectName(QStringLiteral("commandListFind"));
		local->setPlaceholderText(tr("Find in this list"));
		local->setAccessibleName(tr("Find in %1").arg(m_spec.title));
		local->setClearButtonEnabled(true);
		local->setProperty("controlCommand", m_spec.listId);  // it narrows that command's rows
		connect(local, &QLineEdit::textChanged, this, &CommandListDialog::find);
		connect(local, &QLineEdit::returnPressed, m_table, qOverload<>(&QWidget::setFocus));
		layout->addWidget(local);
		return;
	}
	auto* find = new QLineEdit(m_spec.listArgs.value(m_spec.filterArg).toString(), this);
	find->setPlaceholderText(m_spec.filterLabel);
	find->setAccessibleName(m_spec.filterLabel);
	find->setClearButtonEnabled(true);
	find->setProperty("controlCommand", m_spec.listId);
	connect(find, &QLineEdit::textChanged, this, [this](const QString& text) {
		m_spec.listArgs.insert(m_spec.filterArg, text);
		refresh();
	});
	connect(find, &QLineEdit::returnPressed, m_table, qOverload<>(&QWidget::setFocus));
	layout->addWidget(find);
}

ControlResult CommandListDialog::dispatch(const QString& label, const CommandCall& call)
{
	CommandForm* form = nullptr;
	const ControlResult result = runCommand(this, call, &form);
	if (form != nullptr)
	{
		connect(form, &CommandForm::ran, this, [this, label](const ControlResult& ran) { report(label, ran); refresh(); });
		connect(form, &QDialog::rejected, this, [this, label] { cancelled(label); });
		m_status->setText(tr("%1: fill in the form").arg(label));
		return result;
	}
	report(label, result);
	refresh();
	return result;
}

void CommandListDialog::report(const QString& label, const ControlResult& result)
{
	const QString line = result.ok ? tr("%1: done").arg(label) : tr("%1: %2").arg(label, result.errorMessage);
	m_status->setText(line);
	a11y::announceStatus(m_status, line, result.ok ? a11y::Politeness::Polite : a11y::Politeness::Assertive);
	m_results->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss  ")) + line + replyLines(result));
	m_results->show();
}

QString CommandListDialog::statusText() const
{
	return m_status->text();
}

QString CommandListDialog::resultsText() const
{
	return m_results->toPlainText();
}

QJsonArray listRows(const ListSpec& spec, const QJsonObject& listed)
{
	const QJsonArray top = listed.value(spec.arrayKey).toArray();
	if (spec.nestedKeys.isEmpty()) { return top; }
	QJsonArray rows;
	for (const QJsonValue& parentValue : top)
	{
		const QJsonObject parent = parentValue.toObject();
		for (const QString& key : spec.nestedKeys)
		{
			const QJsonValue nested = parent.value(key);
			const QJsonArray children = nested.isObject() ? QJsonArray{nested} : nested.toArray();
			for (const QJsonValue& childValue : children)
			{
				QJsonObject child = childValue.toObject();
				for (const QString& field : spec.parentFields) { child.insert(QStringLiteral("parent_") + field, parent.value(field)); }
				rows.append(child);
			}
		}
	}
	return rows;
}

CommandListDialog* showCommandList(QWidget* parent, const ListSpec& spec)
{
	auto* dialog = new CommandListDialog(spec, parent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setModal(false);
	dialog->show();
	return dialog;
}

} // namespace lmms::gui
