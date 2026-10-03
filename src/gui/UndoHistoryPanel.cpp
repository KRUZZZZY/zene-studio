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

#include <functional>

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ProjectJournal.h"

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

//! A spin box that runs @a command with { @a argument: value * @a scale } when edited.
QSpinBox* limitBox(int min, int max, const QString& suffix, const char* command, const char* argument,
	qint64 scale, const QString& name, QWidget* parent, std::function<void()> after)
{
	auto* box = new QSpinBox(parent);
	box->setRange(min, max);
	box->setSuffix(suffix);
	box->setAccessibleName(name);
	box->setProperty("controlCommand", QString::fromLatin1(command));
	box->setKeyboardTracking(false);  // apply on Enter / focus-out, not per keystroke
	QObject::connect(box, QOverload<int>::of(&QSpinBox::valueChanged), box, [=](int value) {
		ControlRegistry::instance()->invoke(QString::fromLatin1(command),
			{{QString::fromLatin1(argument), static_cast<double>(value * scale)}});
		after();
	});
	return box;
}

//! Why a step the panel ran did not go all the way: the refusal, or where a history jump stopped; empty when it did.
QString problemOf(const ControlResult& result)
{
	if (!result.ok) { return result.errorMessage; }
	return result.result.value(QStringLiteral("stopped")).toString();
}

//! What the journal holds, in brief: it changes whenever any client - an agent, a script, an editor - adds or takes a step.
QString journalMark()
{
	const ProjectJournal* journal = Engine::projectJournal();
	if (journal == nullptr) { return QString(); }
	return QStringLiteral("%1/%2/%3").arg(journal->undoDepth()).arg(journal->redoDepth()).arg(journal->topStepSerial());
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
		"undone by Undo too, but it is not listed here. Double-click a command, or select it and press "
		"Undo to Here, to undo it and every step after it (control.history_jump); Redo brings them back."), this);
	note->setWordWrap(true);

	m_undo = commandButton(tr("Undo"), QStringLiteral("control.undo"), this);
	m_redo = commandButton(tr("Redo"), QStringLiteral("control.redo"), this);
	auto* back = commandButton(tr("Undo to Here"), QStringLiteral("control.history_jump"), this);
	back->setToolTip(tr("Undo the selected command and every step after it"));
	back->setEnabled(false);
	auto* again = new QPushButton(tr("Refresh"), this);
	// A refusal, or a jump an irreversible step stopped short, is shown under the summary - not dropped.
	const auto report = [this](const ControlResult& result) {
		refresh();
		const QString why = problemOf(result);
		if (!why.isEmpty()) { m_summary->setText(m_summary->text() + QLatin1Char('\n') + tr("Not done: %1").arg(why)); }
	};
	for (auto [button, command] : {std::pair{m_undo, "control.undo"}, std::pair{m_redo, "control.redo"}})
	{
		connect(button, &QPushButton::clicked, this, [report, command = command] {
			report(ControlRegistry::instance()->invoke(QString::fromLatin1(command), QJsonObject{}));
		});
	}
	connect(again, &QPushButton::clicked, this, [this] { refresh(); });
	// 0.5.0 W3 (WF-102): back to a point in the history in one go - the row and every newer one.
	m_list->setProperty("controlCommand", QStringLiteral("control.history_jump"));
	const auto jump = [this, report] {
		const int row = m_list->currentRow();
		if (row >= 0) { report(ControlRegistry::instance()->invoke(QStringLiteral("control.history_jump"), {{QStringLiteral("steps"), -(row + 1)}})); }
	};
	connect(m_list, &QListWidget::itemDoubleClicked, this, jump);
	connect(back, &QPushButton::clicked, this, jump);
	connect(m_list, &QListWidget::currentRowChanged, back, [back](int row) { back->setEnabled(row >= 0); });
	// The record changes under other clients too (agents, scripts, editors): re-read when the journal moved.
	auto* watch = new QTimer(this);
	connect(watch, &QTimer::timeout, this, [this] {
		if (isVisible() && journalMark() != m_list->property("journalMark").toString()) { refresh(); }
	});
	watch->start(500);

	const auto reread = [this] { refresh(); };
	m_steps = limitBox(1, ProjectJournal::MaxUndoStateLimit, tr(" steps"), "control.set_undo_depth", "steps", 1,
		tr("Undo steps kept"), this, reread);
	m_megabytes = limitBox(1, static_cast<int>(ProjectJournal::MaxUndoByteLimit / (1024 * 1024)), tr(" MB"),
		"control.set_undo_depth", "bytes", 1024 * 1024, tr("Undo memory kept"), this, reread);
	m_coalesce = limitBox(0, control::MaxUndoCoalesceWindowMs, tr(" ms"), "control.set_undo_coalescing", "window_ms", 1,
		tr("Merge repeated edits within"), this, reread);
	m_coalesce->setToolTip(tr("A run of the same edit on the same thing inside this window is one undo step "
		"(a drag). 0 keeps every step."));
	auto* limits = new QHBoxLayout;
	limits->addWidget(new QLabel(tr("Keep:"), this));
	limits->addWidget(m_steps);
	limits->addWidget(m_megabytes);
	limits->addWidget(new QLabel(tr("Merge within:"), this));
	limits->addWidget(m_coalesce);
	limits->addStretch();

	auto* buttons = new QHBoxLayout;
	buttons->addWidget(m_undo);
	buttons->addWidget(m_redo);
	buttons->addWidget(back);
	buttons->addStretch();
	buttons->addWidget(again);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_summary);
	layout->addLayout(limits);
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
	{
		const QSignalBlocker a(m_steps), b(m_megabytes), c(m_coalesce);
		m_steps->setValue(depth.value(QStringLiteral("cap_steps")).toInt());
		m_megabytes->setValue(static_cast<int>(depth.value(QStringLiteral("cap_bytes")).toDouble() / (1024.0 * 1024.0)));
		m_coalesce->setValue(depth.value(QStringLiteral("coalescing")).toObject().value(QStringLiteral("window_ms")).toInt());
	}
	m_list->clear();
	m_list->setProperty("journalMark", journalMark());  // what the list was read at (the panel's watch compares it)
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
