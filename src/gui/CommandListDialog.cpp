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

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lmms::gui
{

namespace
{

constexpr int MaxCellChars = 120;

QString cellText(const QJsonValue& value)
{
	QString text;
	if (value.isString()) { text = value.toString(); }
	else if (value.isBool()) { text = value.toBool() ? QObject::tr("yes") : QObject::tr("no"); }
	else if (value.isDouble()) { text = QString::number(value.toDouble()); }
	else if (value.isArray()) { text = QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)); }
	else if (value.isObject()) { text = QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)); }
	return text.size() > MaxCellChars ? text.left(MaxCellChars - 1) + QStringLiteral("…") : text;
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
		connect(button, &QPushButton::clicked, this, [this, i] { runExtra(i); });
		buttons->addWidget(button);
	}
	auto* refreshButton = new QPushButton(tr("Refresh"), this);
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
	m_listed = listed.ok ? listed.result : QJsonObject();
	m_rows = listRows(m_spec, m_listed);
	if (!listed.ok) { m_status->setText(listed.errorMessage); }
	QStringList summary;
	for (const QString& key : m_spec.summaryKeys)
	{
		if (m_listed.contains(key)) { summary.append(QStringLiteral("%1: %2").arg(key, cellText(m_listed.value(key)))); }
	}
	m_summary->setText(summary.join(QStringLiteral("   ")));
	m_summary->setVisible(!summary.isEmpty());
	fill();
}

void CommandListDialog::fill()
{
	const int keep = m_table->currentRow();
	QStringList columns = m_spec.columns;
	if (columns.isEmpty() && !m_rows.isEmpty()) { columns = m_rows.first().toObject().keys(); }
	m_table->clear();
	m_table->setColumnCount(static_cast<int>(columns.size()));
	m_table->setHorizontalHeaderLabels(columns);
	m_table->setRowCount(static_cast<int>(m_rows.size()));
	for (int r = 0; r < m_rows.size(); ++r)
	{
		const QJsonObject item = m_rows.at(r).toObject();
		for (int c = 0; c < columns.size(); ++c)
		{
			m_table->setItem(r, c, new QTableWidgetItem(cellText(item.value(columns.at(c)))));
		}
	}
	if (keep >= 0 && keep < m_rows.size()) { m_table->selectRow(keep); }
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
	CommandCall call{spec.id, spec.fixed, {}, spec.label, spec.ask, spec.showResult};
	for (auto it = spec.fromRow.begin(); it != spec.fromRow.end(); ++it) { call.fixed.insert(it.key(), item.value(it.value())); }
	if (spec.confirm && !ControlRegistry::instance()->isHeadless()
		&& QMessageBox::question(this, spec.label, tr("%1 - are you sure?").arg(spec.label)) != QMessageBox::Yes)
	{
		return ControlResult::failure(ControlErrorKind::Refused, tr("cancelled"));
	}
	return dispatch(spec.label, call);
}

ControlResult CommandListDialog::runExtra(int index)
{
	if (index < 0 || index >= m_spec.extras.size())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, tr("no such button"));
	}
	return dispatch(m_spec.extras.at(index).label, m_spec.extras.at(index).call);
}

ControlResult CommandListDialog::dispatch(const QString& label, const CommandCall& call)
{
	CommandForm* form = nullptr;
	const ControlResult result = runCommand(this, call, &form);
	if (form != nullptr)
	{
		connect(form, &CommandForm::ran, this, [this, label](const ControlResult& ran) { report(label, ran); refresh(); });
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
	if (spec.nestedKey.isEmpty()) { return top; }
	QJsonArray rows;
	for (const QJsonValue& parentValue : top)
	{
		const QJsonObject parent = parentValue.toObject();
		for (const QJsonValue& childValue : parent.value(spec.nestedKey).toArray())
		{
			QJsonObject child = childValue.toObject();
			for (const QString& field : spec.parentFields) { child.insert(QStringLiteral("parent_") + field, parent.value(field)); }
			rows.append(child);
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
