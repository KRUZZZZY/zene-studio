/*
 * UndoHistoryPanel.cpp - Edit > Undo History: the undo stack and the command record, from the interface
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

#include "UndoHistoryPanel.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

QJsonObject read(const char* command)
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), QJsonObject{}).result;
}

QString depthText(const QJsonObject& depth)
{
	return UndoHistoryPanel::tr("%1 step(s) to undo, %2 to redo - kept within %3 steps and %4 MB "
		"(%5 KB held); %6 step(s) evicted by the bound")
		.arg(depth.value(QStringLiteral("depth")).toInt())
		.arg(depth.value(QStringLiteral("redo_depth")).toInt())
		.arg(depth.value(QStringLiteral("cap_steps")).toInt())
		.arg(depth.value(QStringLiteral("cap_bytes")).toDouble() / (1024.0 * 1024.0), 0, 'f', 0)
		.arg(depth.value(QStringLiteral("retained_bytes")).toDouble() / 1024.0, 0, 'f', 1)
		.arg(depth.value(QStringLiteral("evicted")).toInt());
}

QString transactionText(const QJsonObject& tx)
{
	const int commands = tx.value(QStringLiteral("commands")).toInt(1);
	QString text = tx.value(QStringLiteral("command")).toString();
	if (commands > 1) { text += UndoHistoryPanel::tr("  (x%1, one step)").arg(commands); }
	if (!tx.value(QStringLiteral("reversible")).toBool(true)) { text += UndoHistoryPanel::tr("  - cannot be undone"); }
	return text;
}

QPushButton* commandButton(const QString& label, const QString& command, QWidget* parent)
{
	auto* button = new QPushButton(label, parent);
	button->setProperty("controlCommand", command);
	return button;
}

} // namespace

UndoHistoryPanel::UndoHistoryPanel(QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Undo History"));
	setAccessibleName(tr("Undo history"));
	m_summary = new QLabel(this);
	m_summary->setWordWrap(true);
	m_list = new QListWidget(this);
	m_list->setAccessibleName(tr("Recorded commands, newest first"));
	auto* note = new QLabel(tr("The list shows edits made through commands - agents, scripts, the "
		"command palette and the menu items that run one. An edit made directly in an editor is "
		"undone by Undo too, but it is not listed here."), this);
	note->setWordWrap(true);

	m_undo = commandButton(tr("Undo"), QStringLiteral("control.undo"), this);
	m_redo = commandButton(tr("Redo"), QStringLiteral("control.redo"), this);
	auto* again = new QPushButton(tr("Refresh"), this);
	for (auto [button, command] : {std::pair{m_undo, "control.undo"}, std::pair{m_redo, "control.redo"}})
	{
		connect(button, &QPushButton::clicked, this, [this, command = command] {
			ControlRegistry::instance()->invoke(QString::fromLatin1(command), QJsonObject{});
			refresh();
		});
	}
	connect(again, &QPushButton::clicked, this, [this] { refresh(); });

	auto* buttons = new QHBoxLayout;
	buttons->addWidget(m_undo);
	buttons->addWidget(m_redo);
	buttons->addStretch();
	buttons->addWidget(again);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_summary);
	layout->addLayout(buttons);
	layout->addWidget(m_list, 1);
	layout->addWidget(note);
	resize(520, 420);
	refresh();
}

void UndoHistoryPanel::refresh()
{
	const QJsonObject depth = read("control.undo_depth");
	m_summary->setText(depthText(depth));
	m_undo->setEnabled(depth.value(QStringLiteral("can_undo")).toBool());
	m_redo->setEnabled(depth.value(QStringLiteral("can_redo")).toBool());
	m_list->clear();
	const QJsonArray transactions = read("control.transactions").value(QStringLiteral("transactions")).toArray();
	for (qsizetype i = transactions.size() - 1; i >= 0; --i)
	{
		const QJsonObject tx = transactions.at(i).toObject();
		auto* item = new QListWidgetItem(transactionText(tx), m_list);
		item->setToolTip(tx.value(QStringLiteral("mechanism")).toString());
	}
}

int UndoHistoryPanel::rowCount() const
{
	return m_list->count();
}

QString UndoHistoryPanel::summary() const
{
	return m_summary->text();
}

} // namespace lmms::gui
