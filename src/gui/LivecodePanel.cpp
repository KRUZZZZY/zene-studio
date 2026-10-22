/*
 * LivecodePanel.cpp - Edit > Live Code: schedule Lua scripts from the interface
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

#include "LivecodePanel.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "ScriptClock.h"
#include "ScriptEngine.h"

namespace lmms::gui
{

namespace
{

ControlResult run(const char* command, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), args);
}

QString number(qint64 value) { return QString::number(value); }

} // namespace

LivecodePanel::LivecodePanel(QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Live Code"));
	setAccessibleName(tr("Live code"));
	resize(760, 620);

	m_source = new QPlainTextEdit(this);
	m_source->setObjectName(QStringLiteral("livecode_source"));
	m_source->setAccessibleName(tr("Lua source to schedule"));
	m_source->setPlaceholderText(tr("Lua evaluated on the chosen hook - for example\n"
		"local channel = zene.mixer():channel(0)\n"
		"if channel:gain() > 0.9 then channel:setGain(0.5) else channel:setGain(1.0) end"));
	m_source->setTabChangesFocus(false);
	m_source->setLineWrapMode(QPlainTextEdit::NoWrap);

	m_id = new QLineEdit(this);
	m_id->setObjectName(QStringLiteral("livecode_id"));
	m_id->setAccessibleName(tr("Schedule id"));
	m_id->setPlaceholderText(tr("lc-1 (empty: the clock picks the next id)"));
	m_id->setToolTip(tr("Scheduling an id that already exists replaces that schedule's script; "
		"one Edit > Undo puts the previous script back"));

	m_hook = new QComboBox(this);
	m_hook->setObjectName(QStringLiteral("livecode_hook"));
	m_hook->setAccessibleName(tr("Boundary the script runs on"));
	m_hook->addItem(tr("Every bar"), QStringLiteral("bar"));
	m_hook->addItem(tr("Every beat"), QStringLiteral("beat"));
	m_hook->addItem(tr("Transport play/stop"), QStringLiteral("transport"));

	m_budget = new QSpinBox(this);
	m_budget->setObjectName(QStringLiteral("livecode_budget"));
	m_budget->setAccessibleName(tr("Instructions per fire"));
	m_budget->setRange(0, 0x7fffffff);
	m_budget->setSpecialValueText(tr("inherit the engine's"));
	m_budget->setToolTip(tr("Per-fire instruction budget; 0 inherits the engine's "
		"(5,000,000 by default). A runaway fire is stopped and counted under 'Budget exceeded'."));

	auto* scheduleButton = new QPushButton(tr("Schedule"), this);
	scheduleButton->setObjectName(QStringLiteral("livecode_schedule"));
	scheduleButton->setProperty("controlCommand", QStringLiteral("livecode.schedule"));
	scheduleButton->setToolTip(tr("Compile-check and schedule the editor's source on the chosen boundary"));

	auto* cancelButton = new QPushButton(tr("Cancel Schedule"), this);
	cancelButton->setObjectName(QStringLiteral("livecode_cancel"));
	cancelButton->setProperty("controlCommand", QStringLiteral("livecode.unschedule"));
	cancelButton->setToolTip(tr("Take the selected schedule off the clock"));
	m_cancelButton = cancelButton;

	auto* loadButton = new QPushButton(tr("Load Source"), this);
	loadButton->setObjectName(QStringLiteral("livecode_load"));
	loadButton->setProperty("controlCommand", QStringLiteral("livecode.get_source"));
	loadButton->setToolTip(tr("Put the selected schedule's captured script into the editor"));
	m_loadButton = loadButton;

	auto* undoButton = new QPushButton(tr("Undo"), this);
	undoButton->setObjectName(QStringLiteral("livecode_undo"));
	undoButton->setProperty("controlCommand", QStringLiteral("control.undo"));
	undoButton->setToolTip(tr("One control.undo: restores the script a schedule replaced, or "
		"rearms a schedule that was cancelled"));
	m_undoButton = undoButton;

	auto* refreshButton = new QPushButton(tr("Refresh"), this);
	refreshButton->setObjectName(QStringLiteral("livecode_refresh"));
	refreshButton->setToolTip(tr("Re-read the clocks and counters now"));
	auto* openButton = new QPushButton(tr("Open File..."), this);
	openButton->setObjectName(QStringLiteral("livecode_open"));
	openButton->setToolTip(tr("Read a .lua file into the editor; scheduling is a separate step"));

	m_summary = new QLabel(this);
	m_summary->setObjectName(QStringLiteral("livecode_summary"));
	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("livecode_status"));
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_help = new QLabel(this);
	m_help->setObjectName(QStringLiteral("livecode_help"));
	m_help->setWordWrap(true);
	m_help->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_help->setText(helpText());

	m_table = new QTableWidget(0, 9, this);
	m_table->setObjectName(QStringLiteral("livecode_schedules"));
	m_table->setAccessibleName(tr("Scheduled scripts"));
	m_table->setHorizontalHeaderLabels({tr("Id"), tr("Hook"), tr("Budget"), tr("Bytes"),
		tr("Fires"), tr("Errors"), tr("Over budget"), tr("Last fire (ticks)"), tr("Last failure")});
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->horizontalHeader()->setStretchLastSection(true);

	connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
		if (row < 0 || !m_id->text().trimmed().isEmpty()) { return; }
		const QTableWidgetItem* item = m_table->item(row, 0);
		if (item != nullptr) { m_id->setText(item->text()); }
	});
	connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { loadSource(targetId()); });
	connect(scheduleButton, &QPushButton::clicked, this, [this] { schedule(); });
	connect(cancelButton, &QPushButton::clicked, this, [this] { unschedule(); });
	connect(loadButton, &QPushButton::clicked, this, [this] { loadSource(targetId()); });
	connect(undoButton, &QPushButton::clicked, this, [this] { undo(); });
	connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
	connect(openButton, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Open Lua Script"), QString(),
			tr("Lua script (*.lua);;All files (*)"));
		if (!path.isEmpty()) { openFile(path); }
	});
	// The transport the counters are measured against keeps moving: a light poll keeps the
	// fire count current without a command per repaint (the registry handler runs on this thread).
	auto* poll = new QTimer(this);
	poll->setInterval(500);
	connect(poll, &QTimer::timeout, this, [this] { refresh(); });
	poll->start();

	auto* idRow = new QHBoxLayout;
	auto* idLabel = new QLabel(tr("Id"), this);
	idLabel->setBuddy(m_id);
	idRow->addWidget(idLabel);
	idRow->addWidget(m_id, 1);
	auto* hookLabel = new QLabel(tr("Hook"), this);
	hookLabel->setBuddy(m_hook);
	idRow->addWidget(hookLabel);
	idRow->addWidget(m_hook);
	auto* budgetLabel = new QLabel(tr("Budget"), this);
	budgetLabel->setBuddy(m_budget);
	idRow->addWidget(budgetLabel);
	idRow->addWidget(m_budget);
	idRow->addWidget(scheduleButton);

	auto* buttons = new QHBoxLayout;
	buttons->addWidget(loadButton);
	buttons->addWidget(cancelButton);
	buttons->addStretch();
	buttons->addWidget(openButton);
	buttons->addWidget(undoButton);
	buttons->addWidget(refreshButton);

	auto* layout = new QVBoxLayout(this);
	layout->addLayout(idRow);
	layout->addWidget(m_source, 2);
	layout->addLayout(buttons);
	layout->addWidget(m_summary);
	layout->addWidget(m_table, 2);
	layout->addWidget(m_status);
	layout->addWidget(m_help);

	refresh();
}

QString LivecodePanel::source() const { return m_source->toPlainText(); }

void LivecodePanel::setSource(const QString& source) { m_source->setPlainText(source); }

QString LivecodePanel::scheduleId() const { return m_id->text().trimmed(); }

void LivecodePanel::setScheduleId(const QString& id) { m_id->setText(id); }

void LivecodePanel::setHook(const QString& hook)
{
	const int index = m_hook->findData(hook);
	if (index >= 0) { m_hook->setCurrentIndex(index); }
}

void LivecodePanel::setBudget(int budget) { m_budget->setValue(budget); }

QString LivecodePanel::runAndReport(const char* command, const QJsonObject& args, bool* ok)
{
	const ControlResult result = run(command, args);
	if (ok != nullptr) { *ok = result.ok; }
	if (result.ok)
	{
		m_status->setText(tr("%1: done").arg(QString::fromLatin1(command)));
		return QString();
	}
	// A refusal is shown IN the panel, not in a modal box: an agent-driven instance must not be
	// parked on a dialog, and the person needs the text next to the buffer they are editing.
	m_status->setText(tr("%1 refused: %2").arg(QString::fromLatin1(command), result.errorMessage));
	return result.errorMessage;
}

QString LivecodePanel::schedule()
{
	QJsonObject args{{QStringLiteral("source"), source()},
		{QStringLiteral("hook"), m_hook->currentData().toString()},
		{QStringLiteral("budget"), m_budget->value()}};
	const QString id = scheduleId();
	if (!id.isEmpty()) { args.insert(QStringLiteral("id"), id); }
	const ControlResult result = run("livecode.schedule", args);
	if (!result.ok)
	{
		// The command refused before the clock moved: whatever was scheduled under this id is
		// STILL the schedule that fires. Keep its row and the editor buffer on screen.
		m_status->setText(tr("livecode.schedule refused: %1").arg(result.errorMessage));
		m_source->setFocus();
		return result.errorMessage;
	}
	// The command answers the id it armed, including a generated one: name it in the box so
	// Cancel / Load / Undo address the schedule that was just made, not an empty field.
	const QString armed = result.result.value(QStringLiteral("id")).toString();
	m_id->setText(armed);
	refresh();
	m_status->setText(tr("Scheduled '%1' on the %2 hook - it runs on its own from now on")
		.arg(armed, m_hook->currentText()));
	return QString();
}

QString LivecodePanel::unschedule()
{
	const QString id = targetId();
	if (id.isEmpty())
	{
		m_status->setText(tr("Select a schedule to cancel."));
		return m_status->text();
	}
	bool ok = false;
	const QString refusal = runAndReport("livecode.unschedule", {{QStringLiteral("id"), id}}, &ok);
	if (!ok) { return refusal; }
	refresh();
	m_status->setText(tr("Cancelled '%1' - Undo rearms it with the script it held").arg(id));
	return QString();
}

QString LivecodePanel::loadSource(const QString& id)
{
	if (id.isEmpty())
	{
		m_status->setText(tr("Select a schedule to load."));
		return m_status->text();
	}
	const ControlResult read = run("livecode.get_source", {{QStringLiteral("id"), id}});
	if (!read.ok) { m_status->setText(read.errorMessage); return read.errorMessage; }
	m_source->setPlainText(read.result.value(QStringLiteral("source")).toString());
	m_id->setText(id);
	setHook(read.result.value(QStringLiteral("hook")).toString());
	m_budget->setValue(read.result.value(QStringLiteral("budget")).toInt());
	m_status->setText(tr("Loaded '%1' (%2 bytes) into the editor; Schedule replaces it in place")
		.arg(id).arg(read.result.value(QStringLiteral("source_bytes")).toInt()));
	return QString();
}

QString LivecodePanel::undo()
{
	const QString loaded = targetId();
	bool ok = false;
	const QString refusal = runAndReport("control.undo", {}, &ok);
	if (!ok) { return refusal; }
	refresh();
	// Re-read what the id now holds, so the editor never shows a definition the clock does not
	// have: an undo of a replacement puts the previous script back under the same id.
	if (!loaded.isEmpty())
	{
		const ControlResult read = run("livecode.get_source", {{QStringLiteral("id"), loaded}});
		if (read.ok)
		{
			const QString text = read.result.value(QStringLiteral("source")).toString();
			if (text != source())
			{
				m_source->setPlainText(text);
				setHook(read.result.value(QStringLiteral("hook")).toString());
				m_budget->setValue(read.result.value(QStringLiteral("budget")).toInt());
			}
		}
	}
	m_status->setText(tr("Undid the last edit - a schedule's previous script is back in place "
		"(its fire counters are runtime history, not part of the undo)"));
	return QString();
}

QString LivecodePanel::openFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		m_status->setText(tr("Cannot read '%1': %2").arg(path, file.errorString()));
		return m_status->text();
	}
	// The scheduler caps a source in BYTES; refuse an over-cap file before it is all in memory,
	// the way livecode.schedule's own path reader does.
	QByteArray bytes = file.read(3 * static_cast<qint64>(ScriptClock::SourceByteCap) + 4);
	if (bytes.size() > ScriptClock::SourceByteCap)
	{
		m_status->setText(tr("'%1' is over the %2-byte source cap a scheduled script has; it was "
			"not loaded").arg(QFileInfo(path).fileName()).arg(ScriptClock::SourceByteCap));
		return m_status->text();
	}
	m_source->setPlainText(QString::fromUtf8(bytes));
	m_status->setText(tr("Read '%1' into the editor; Schedule compiles and arms it")
		.arg(QFileInfo(path).fileName()));
	return QString();
}

QString LivecodePanel::targetId() const
{
	const QString typed = scheduleId();
	if (!typed.isEmpty()) { return typed; }
	const int row = m_table->currentRow();
	const QTableWidgetItem* item = row >= 0 ? m_table->item(row, 0) : nullptr;
	return item != nullptr ? item->text() : QString();
}

void LivecodePanel::refresh()
{
	const QJsonObject state = run("livecode.get_state").result;
	const QJsonArray schedules = state.value(QStringLiteral("schedules")).toArray();
	const int activeRow = m_table->currentRow();
	m_table->setRowCount(static_cast<int>(schedules.size()));
	for (int row = 0; row < schedules.size(); ++row)
	{
		const QJsonObject schedule = schedules.at(row).toObject();
		const QStringList cells{
			schedule.value(QStringLiteral("id")).toString(),
			schedule.value(QStringLiteral("hook")).toString(),
			number(schedule.value(QStringLiteral("budget")).toVariant().toLongLong()),
			number(schedule.value(QStringLiteral("source_bytes")).toVariant().toLongLong()),
			number(schedule.value(QStringLiteral("fires")).toVariant().toLongLong()),
			number(schedule.value(QStringLiteral("errors")).toVariant().toLongLong()),
			number(schedule.value(QStringLiteral("budget_exceeded")).toVariant().toLongLong()),
			number(schedule.value(QStringLiteral("last_fire_ticks")).toVariant().toLongLong()),
			schedule.value(QStringLiteral("last_error")).toString(),
		};
		for (int column = 0; column < cells.size(); ++column)
		{
			m_table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
		}
		if (cells.at(8).isEmpty()) { m_table->item(row, 8)->setText(tr("-")); }
	}
	const QJsonObject counters = state.value(QStringLiteral("counters")).toObject();
	m_summary->setText(tr("%1 of %2 schedules; %3 fires, %4 errors, %5 over budget, %6 busy skips, "
		"%7 coalesced crossings; transport %8 at tick %9")
		.arg(schedules.size()).arg(ScriptClock::MaxScheduleCount)
		.arg(counters.value(QStringLiteral("fires")).toVariant().toLongLong())
		.arg(counters.value(QStringLiteral("errors")).toVariant().toLongLong())
		.arg(counters.value(QStringLiteral("budget_exceeded")).toVariant().toLongLong())
		.arg(counters.value(QStringLiteral("busy_skips")).toVariant().toLongLong())
		.arg(counters.value(QStringLiteral("coalesced")).toVariant().toLongLong())
		.arg(state.value(QStringLiteral("playing")).toBool() ? tr("playing") : tr("stopped"))
		.arg(state.value(QStringLiteral("position_ticks")).toVariant().toLongLong()));
	if (activeRow >= 0 && activeRow < m_table->rowCount()) { m_table->setCurrentCell(activeRow, 0); }
	const bool hasSchedules = !schedules.isEmpty();
	m_cancelButton->setEnabled(hasSchedules);
	m_loadButton->setEnabled(hasSchedules);
}

int LivecodePanel::scheduleCount() const
{
	return m_table->rowCount();
}

int LivecodePanel::rowCount() const
{
	return m_table->rowCount();
}

QString LivecodePanel::statusText() const { return m_status->text(); }

QString LivecodePanel::helpText() const
{
	return tr("Each fire gets a fresh Lua state, so Lua globals and closures do not persist from "
		"one bar, beat or play/stop edge to the next - only the project state a script changes "
		"does. The source is captured and compile-checked when you schedule it; editing a file "
		"behind it or this buffer changes nothing until Schedule runs again, and scheduling an "
		"existing id replaces that slot in place. Bounds: a source is capped at %1 bytes (%2 KiB), "
		"the clock holds at most %3 schedules, and the transport is observed every %4 ms, so a "
		"fire lands up to one poll after its boundary - it is not sample-accurate. Bar and beat "
		"hooks fire only while the transport plays; a seek or meter change re-anchors the grid, "
		"and more than %5 crossings in one observation are coalesced rather than fired late. Every "
		"fire is bounded by its instruction budget (0 inherits the engine's 5,000,000), and Lua "
		"memory per run is capped by File > Script Memory Budget (default %6 MiB, accepted range "
		"%7 KiB to %8 MiB). Schedules live in this process only: they are not saved in the "
		"project, so they do not come back after a restart, and they never run on the audio "
		"thread.")
		.arg(ScriptClock::SourceByteCap)
		.arg(ScriptClock::SourceByteCap / 1024)
		.arg(ScriptClock::MaxScheduleCount)
		.arg(ScriptClock::PollMs)
		.arg(ScriptClockGrid::CrossCap)
		.arg(ScriptEngine::DefaultMemoryBudgetBytes / (1024 * 1024))
		.arg(ScriptEngine::MinMemoryBudgetBytes / 1024)
		.arg(ScriptEngine::MaxMemoryBudgetBytes / (1024 * 1024));
}

} // namespace lmms::gui
