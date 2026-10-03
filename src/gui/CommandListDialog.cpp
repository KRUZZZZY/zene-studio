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

#include <cmath>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "GuiApplication.h"

namespace lmms::gui
{

namespace
{

constexpr int MaxCellChars = 120;
constexpr double MaxExactWhole = 9007199254740992.0;  // 2^53: every whole number below it is exact in a double

//! A number as it reads: a whole one in full (a size in bytes, a tick - never "1.23457e+06"), else as Qt writes it.
QString numberText(double number)
{
	const bool whole = std::abs(number) < MaxExactWhole && number == std::floor(number);
	return whole ? QString::number(static_cast<qint64>(number)) : QString::number(number);
}

//! The whole text of a cell (the table shows it cut at MaxCellChars, with the whole of it as the tooltip).
QString fullText(const QJsonValue& value)
{
	if (value.isString()) { return value.toString(); }
	if (value.isBool()) { return value.toBool() ? QObject::tr("yes") : QObject::tr("no"); }
	if (value.isDouble()) { return numberText(value.toDouble()); }
	if (value.isArray()) { return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)); }
	if (value.isObject()) { return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)); }
	return {};
}

QString clipped(const QString& text)
{
	return text.size() > MaxCellChars ? text.left(MaxCellChars - 1) + QStringLiteral("…") : text;
}

QString cellText(const QJsonValue& value)
{
	return clipped(fullText(value));
}

//! A result field as a heading: "missing_count" reads "Missing count".
QString titleOf(const QString& key)
{
	QString title = key;
	title.replace(QLatin1Char('_'), QLatin1Char(' '));
	if (!title.isEmpty()) { title[0] = title.at(0).toUpper(); }
	return title;
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

/*! Whether the person at the screen agrees to @a label on @a target, told @a caution. Nobody is asked
 *  headless or with no interface to ask over (a tool embedding the registry), as announce() floats nothing there. */
bool confirmed(QWidget* parent, const QString& label, const QString& target, const QString& caution)
{
	if (ControlRegistry::instance()->isHeadless() || getGUI() == nullptr) { return true; }
	const QString what = target.isEmpty() ? label : CommandListDialog::tr("%1 \"%2\"").arg(label, target);
	const QString question = caution.isEmpty() ? CommandListDialog::tr("%1 - are you sure?").arg(what)
		: CommandListDialog::tr("%1 - %2 Are you sure?").arg(what, caution);
	return QMessageBox::question(parent, label, question) == QMessageBox::Yes;
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
	addFilter(layout);
	m_table = new QTableWidget(this);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->setAccessibleName(spec.title);
	layout->addWidget(m_table, 1);

	auto* buttons = new QHBoxLayout;
	for (int i = 0; i < spec.actions.size(); ++i)
	{
		auto* button = new QPushButton(spec.actions.at(i).label, this);
		button->setProperty("controlCommand", spec.actions.at(i).id);
		connect(button, &QPushButton::clicked, this, [this, i] { runAction(m_table->currentRow(), i); });
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
	layout->addWidget(m_status);

	connect(m_table, &QTableWidget::itemSelectionChanged, this, &CommandListDialog::syncButtons);
	resize(640, 420);
	refresh();
}

void CommandListDialog::refresh()
{
	const ControlResult listed = ControlRegistry::instance()->invoke(m_spec.listId, m_spec.listArgs);
	const int was = m_table->currentRow();
	const QJsonValue kept = was >= 0 && was < m_rows.size() ? m_rows.at(was) : QJsonValue();
	m_listed = listed.ok ? listed.result : QJsonObject();
	m_rows = listRows(m_spec, m_listed);
	if (!listed.ok) { m_status->setText(listed.errorMessage); }
	QStringList summary;
	for (const QString& key : m_spec.summaryKeys)
	{
		if (m_listed.contains(key)) { summary.append(QStringLiteral("%1: %2").arg(titleOf(key), cellText(m_listed.value(key)))); }
	}
	m_summary->setText(summary.join(QStringLiteral("   ")));
	m_summary->setVisible(!summary.isEmpty());
	fill(kept);
}

void CommandListDialog::fill(const QJsonValue& kept)
{
	// The selection follows its row, not its index: after a Remove the next row must not slide under it.
	const int keep = indexOfRow(m_rows, kept);
	QStringList columns = m_spec.columns;
	if (columns.isEmpty() && !m_rows.isEmpty()) { columns = m_rows.first().toObject().keys(); }
	m_table->clear();
	m_table->setColumnCount(static_cast<int>(columns.size()));
	QStringList titles;
	for (const QString& column : columns) { titles.append(titleOf(column)); }
	m_table->setHorizontalHeaderLabels(titles);
	m_table->setRowCount(static_cast<int>(m_rows.size()));
	for (int r = 0; r < m_rows.size(); ++r)
	{
		const QJsonObject item = m_rows.at(r).toObject();
		for (int c = 0; c < columns.size(); ++c)
		{
			const QString text = fullText(item.value(columns.at(c)));
			auto* cell = new QTableWidgetItem(clipped(text));
			if (text.size() > MaxCellChars) { cell->setToolTip(text); }  // a cut path or error stays readable
			m_table->setItem(r, c, cell);
		}
	}
	if (keep >= 0) { m_table->selectRow(keep); }
	else { m_table->setCurrentItem(nullptr); }  // gone: nothing selected, so no action is aimed at another row
	syncButtons();
}

void CommandListDialog::syncButtons()
{
	const bool chosen = m_table->currentRow() >= 0 && m_table->currentRow() < m_rows.size();
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
	if (spec.confirm && !confirmed(this, spec.label, rowName(item, m_spec.columns), spec.caution)) { return cancelled(spec.label); }
	return dispatch(spec.label, call);
}

ControlResult CommandListDialog::runExtra(int index)
{
	if (index < 0 || index >= m_spec.extras.size())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, tr("no such button"));
	}
	const ListExtra& extra = m_spec.extras.at(index);
	if (extra.confirm && !confirmed(this, extra.label, QString(), QString())) { return cancelled(extra.label); }
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
	if (m_spec.filterArg.isEmpty()) { return; }
	auto* find = new QLineEdit(m_spec.listArgs.value(m_spec.filterArg).toString(), this);
	find->setPlaceholderText(m_spec.filterLabel);
	find->setAccessibleName(m_spec.filterLabel);
	find->setClearButtonEnabled(true);
	find->setProperty("controlCommand", m_spec.listId);
	connect(find, &QLineEdit::textChanged, this, [this](const QString& text) {
		m_spec.listArgs.insert(m_spec.filterArg, text);
		refresh();
	});
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
	m_status->setText(result.ok ? tr("%1: done").arg(label) : tr("%1: %2").arg(label, result.errorMessage));
}

QString CommandListDialog::statusText() const
{
	return m_status->text();
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
