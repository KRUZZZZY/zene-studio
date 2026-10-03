/*
 * FollowActionEditor.cpp - a cell's or a scene's follow-action chain, edited as rows
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
#include "FollowActionEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "CommandForm.h"

namespace lmms::gui
{

namespace
{

const QStringList& actionNames()
{
	static const QStringList names{QStringLiteral("none"), QStringLiteral("stop"), QStringLiteral("play_again"), QStringLiteral("previous"),
		QStringLiteral("next"), QStringLiteral("first"), QStringLiteral("last"), QStringLiteral("any"), QStringLiteral("other"),
		QStringLiteral("jump")};
	return names;
}

enum Column { Type, Chance, Linked, Bars, Jump, ColumnCount };

} // namespace

FollowActionEditor::FollowActionEditor(const QString& command, const QJsonObject& where, const QJsonArray& chain,
	const QString& title, QWidget* parent) :
	QDialog(parent),
	m_command(command),
	m_where(where),
	m_rows(new QTableWidget(0, ColumnCount, this))
{
	setWindowTitle(QObject::tr("Follow Actions - %1").arg(title));
	setAttribute(Qt::WA_DeleteOnClose);
	setProperty("controlCommand", command);
	m_rows->setHorizontalHeaderLabels({QObject::tr("Action"), QObject::tr("Chance"), QObject::tr("After the clip"),
		QObject::tr("Bars"), QObject::tr("Jump to scene")});
	m_rows->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	m_rows->setAccessibleName(QObject::tr("Follow action chain"));
	for (const QJsonValue& action : chain) { addRow(action.toObject()); }
	auto* add = new QPushButton(QObject::tr("Add"), this);
	auto* remove = new QPushButton(QObject::tr("Remove"), this);
	connect(add, &QPushButton::clicked, this, [this] { addRow({{QStringLiteral("type"), QStringLiteral("next")}}); });
	connect(remove, &QPushButton::clicked, this, [this] { m_rows->removeRow(m_rows->currentRow()); });
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, [this] { apply(); });
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* rowButtons = new QHBoxLayout;
	rowButtons->addWidget(add);
	rowButtons->addWidget(remove);
	rowButtons->addStretch();
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_rows, 1);
	layout->addLayout(rowButtons);
	layout->addWidget(buttons);
	resize(560, 260);
}

void FollowActionEditor::addRow(const QJsonObject& action)
{
	const int row = m_rows->rowCount();
	m_rows->insertRow(row);
	auto* type = new QComboBox(m_rows);
	type->addItems(actionNames());
	type->setCurrentText(action.value(QStringLiteral("type")).toString(QStringLiteral("next")));
	auto* chance = new QDoubleSpinBox(m_rows);
	chance->setRange(0.0, 1.0);
	chance->setSingleStep(0.1);
	chance->setValue(action.value(QStringLiteral("chance")).toDouble(1.0));
	auto* linked = new QCheckBox(m_rows);
	linked->setChecked(action.value(QStringLiteral("linked")).toBool(true));
	auto* bars = new QDoubleSpinBox(m_rows);
	bars->setRange(0.25, 256.0);
	bars->setValue(action.value(QStringLiteral("time_bars")).toDouble(1.0));
	auto* jump = new QSpinBox(m_rows);
	jump->setRange(1, 512);
	jump->setValue(action.value(QStringLiteral("jump_to")).toInt(0) + 1);
	m_rows->setCellWidget(row, Type, type);
	m_rows->setCellWidget(row, Chance, chance);
	m_rows->setCellWidget(row, Linked, linked);
	m_rows->setCellWidget(row, Bars, bars);
	m_rows->setCellWidget(row, Jump, jump);
}

QJsonArray FollowActionEditor::chain() const
{
	QJsonArray out;
	for (int row = 0; row < m_rows->rowCount(); ++row)
	{
		out.append(QJsonObject{{QStringLiteral("type"), qobject_cast<QComboBox*>(m_rows->cellWidget(row, Type))->currentText()},
			{QStringLiteral("chance"), qobject_cast<QDoubleSpinBox*>(m_rows->cellWidget(row, Chance))->value()},
			{QStringLiteral("linked"), qobject_cast<QCheckBox*>(m_rows->cellWidget(row, Linked))->isChecked()},
			{QStringLiteral("time_bars"), qobject_cast<QDoubleSpinBox*>(m_rows->cellWidget(row, Bars))->value()},
			{QStringLiteral("jump_to"), qobject_cast<QSpinBox*>(m_rows->cellWidget(row, Jump))->value() - 1}});
	}
	return out;
}

void FollowActionEditor::apply()
{
	QJsonObject args = m_where;
	args.insert(QStringLiteral("follow_actions"), chain());
	if (runCommand(this, CommandCall{m_command, args, {}, {}, false}).ok) { accept(); }
}

} // namespace lmms::gui
