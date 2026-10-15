/*
 * TempoMapPanel.cpp - Edit > Tempo Map: the tempo and time-signature map, from the interface
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

#include "TempoMapPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

#include "CommandForm.h"
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "Engine.h"
#include "MusicalTime.h"
#include "Song.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

ControlResult run(const char* command, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), args);
}

QPushButton* commandButton(const QString& label, const char* command, QWidget* parent)
{
	auto* button = new QPushButton(label, parent);
	button->setProperty("controlCommand", QString::fromLatin1(command));
	return button;
}

QString cell(int value) { return value > 0 ? QString::number(value) : QStringLiteral("-"); }

//! The row of the last event that carries a tempo, or -1: a ramp on it has no next tempo to reach and is
//! ignored (TempoMap.h, TempoMapEvent::ramp).
int lastTempoRow(const QJsonArray& events)
{
	for (auto row = static_cast<int>(events.size()) - 1; row >= 0; --row)
	{
		if (events.at(row).toObject().value(QStringLiteral("has_tempo")).toBool()) { return row; }
	}
	return -1;
}

//! The bar @a row's event falls in, integrating the map's OWN time-signature events: an event's meter
//! runs from its own tick to the next, so a 3/4 event shortens every bar after it. Dividing every tick by
//! one global ticksPerBar ignored the map and showed the wrong bar once the metre changed (cluster 1768).
//! @a events must be in ascending tick order, as transport.tempo_map_get returns them.
int barAt(const QJsonArray& events, int row, int projectTicksPerBar)
{
	int perBar = std::max(1, projectTicksPerBar);
	int barStart = 0;
	int bar = 1;
	for (int i = 0; i <= row; ++i)
	{
		const QJsonObject event = events.at(i).toObject();
		const int tick = event.value(QStringLiteral("tick")).toInt();
		while (tick - barStart >= perBar)
		{
			barStart += perBar;
			++bar;
		}
		// The meter from THIS tick on; the bar it sits in was measured with the previous one.
		const int numerator = event.value(QStringLiteral("numerator")).toInt();
		const int denominator = event.value(QStringLiteral("denominator")).toInt();
		if (numerator > 0 && denominator > 0)
		{
			perBar = std::max(1, DefaultTicksPerBar * numerator / denominator);
		}
	}
	return bar;
}

//! One event's cells: bar, position (in the time format - bars|beats unless ticks or time is chosen, theme 7),
//! tempo (with its ramp, when it has a later tempo to ramp to) and time signature.
QStringList eventCells(const QJsonObject& event, int bar, bool ramps)
{
	const int tick = event.value(QStringLiteral("tick")).toInt();
	const int numerator = event.value(QStringLiteral("numerator")).toInt();
	return {QString::number(bar), musicaltime::positionText(tick),
		cell(event.value(QStringLiteral("bpm")).toInt()) + (ramps ? QStringLiteral("  \u2197 ramp") : QString()),
		numerator > 0 ? QStringLiteral("%1/%2").arg(numerator).arg(event.value(QStringLiteral("denominator")).toInt())
			: QStringLiteral("-")};
}

} // namespace

TempoMapPanel::TempoMapPanel(QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Tempo Map"));
	setAccessibleName(tr("Tempo map"));
	m_active = new QCheckBox(tr("Follow the tempo map (off: the project's single tempo)"), this);
	m_active->setProperty("controlCommand", QStringLiteral("transport.tempo_map_set_active"));
	m_summary = new QLabel(this);
	m_table = new QTableWidget(0, 4, this);
	m_table->setHorizontalHeaderLabels({tr("Bar"), tr("Position"), tr("Tempo (BPM)"), tr("Time signature")});
	// Theme 7: positions read in bars and beats unless ticks or time is chosen - here, or anywhere a position shows.
	m_format = new QComboBox(this);
	m_format->setAccessibleName(tr("Show positions as"));
	m_format->setProperty("controlCommand", QStringLiteral("transport.time_format"));
	for (int i = 0; i < musicaltime::formatNames().size(); ++i)
	{
		m_format->addItem(musicaltime::formatLabel(static_cast<musicaltime::Format>(i)), musicaltime::formatNames().at(i));
	}
	connect(m_format, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
		run("transport.time_format", {{QStringLiteral("format"), m_format->itemData(index).toString()}});
	});
	// Queued: the change is announced from inside transport.time_format, and refresh() runs commands of its own.
	connect(musicaltime::formatSignal(), &musicaltime::FormatSignal::changed, this, &TempoMapPanel::refresh, Qt::QueuedConnection);
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setAccessibleName(tr("Tempo map events"));

	auto* add = commandButton(tr("Add at playhead"), "transport.tempo_map_add", this);
	add->setToolTip(tr("Add an event at the play position with the project's tempo and time signature"));
	auto* remove = commandButton(tr("Remove"), "transport.tempo_map_remove", this);
	// ED-050: any event, typed (tick, tempo, metre, ramp), and a ramp to the next event toggled on a row.
	auto* addForm = commandButton(tr("Add..."), "transport.tempo_map_add", this);
	addForm->setToolTip(tr("Type an event's tick, tempo, time signature and ramp; an event already at that tick is changed"));
	auto* ramp = commandButton(tr("Ramp to Next"), "transport.tempo_map_add", this);
	ramp->setToolTip(tr("Switch the selected tempo event between holding and moving gradually to the next tempo event"));
	// Remove and Ramp act on the selected row, so they are off while there is none (review ux05 #99).
	const auto followRow = [this, remove, ramp] {
		remove->setEnabled(m_table->currentRow() >= 0);
		ramp->setEnabled(m_table->currentRow() >= 0);
	};
	connect(m_table, &QTableWidget::currentCellChanged, this, followRow);
	connect(addForm, &QPushButton::clicked, this, [this] {
		Song* song = Engine::getSong();
		CommandForm* form = nullptr;
		runCommand(this, CommandCall{QStringLiteral("transport.tempo_map_add"), {},
			{{QStringLiteral("tick"), static_cast<int>(song->getPlayPos().getTicks())}, {QStringLiteral("bpm"), song->getTempo()}},
			tr("Add Tempo Event"), true}, &form);
		if (form != nullptr) { connect(form, &CommandForm::ran, this, [this] { refresh(); }); }
	});
	connect(ramp, &QPushButton::clicked, this, [this] { toggleRamp(m_table->currentRow()); });
	auto* clear = commandButton(tr("Clear"), "transport.tempo_map_clear", this);
	auto* exportMidi = commandButton(tr("Export MIDI..."), "interchange.smf_export", this);
	auto* importMidi = commandButton(tr("Import MIDI..."), "interchange.smf_import", this);
	exportMidi->setToolTip(tr("Write the tempo map as a Standard MIDI File conductor track another DAW reads"));
	importMidi->setToolTip(tr("Replace the tempo map with a Standard MIDI File's tempo and time-signature events"));
	connect(m_active, &QCheckBox::toggled, this, [this](bool on) {
		run("transport.tempo_map_set_active", {{QStringLiteral("active"), on}});
		refresh();
	});
	connect(add, &QPushButton::clicked, this, [this] {
		addEvent(static_cast<int>(Engine::getSong()->getPlayPos().getTicks()), 0);
	});
	connect(remove, &QPushButton::clicked, this, [this] { removeRow(m_table->currentRow()); });
	connect(clear, &QPushButton::clicked, this, [this] {
		run("transport.tempo_map_clear");
		refresh();
	});
	const auto report = [this](const QString& refusal) {
		if (refusal.isEmpty()) { return; }
		if (isUnattendedRun()) { qWarning("tempo map interchange refused: %s", qPrintable(refusal)); }
		else { QMessageBox::warning(this, windowTitle(), refusal); }
	};
	connect(exportMidi, &QPushButton::clicked, this, [this, report] {
		const QString path = QFileDialog::getSaveFileName(this, tr("Export Tempo Map"), QString(),
			tr("MIDI file (*.mid *.midi)"));
		if (!path.isEmpty()) { report(exportTo(path)); }
	});
	connect(importMidi, &QPushButton::clicked, this, [this, report] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Import Tempo Map"), QString(),
			tr("MIDI file (*.mid *.midi)"));
		if (!path.isEmpty()) { report(importFrom(path)); }
	});

	auto* buttons = new QHBoxLayout;
	buttons->addWidget(add);
	buttons->addWidget(addForm);
	buttons->addWidget(ramp);
	buttons->addWidget(remove);
	buttons->addStretch();
	buttons->addWidget(importMidi);
	buttons->addWidget(exportMidi);
	buttons->addWidget(clear);
	auto* format = new QHBoxLayout;
	auto* formatLabel = new QLabel(tr("Show positions as"), this);
	formatLabel->setBuddy(m_format);
	format->addWidget(formatLabel);
	format->addWidget(m_format);
	format->addStretch();
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_active);
	layout->addLayout(format);
	layout->addWidget(m_summary);
	layout->addWidget(m_table, 1);
	layout->addLayout(buttons);
	resize(480, 360);
	refresh();
	followRow();
}

void TempoMapPanel::refresh()
{
	const QJsonObject map = run("transport.tempo_map_get").result;
	{
		const QSignalBlocker block(m_active);
		m_active->setChecked(map.value(QStringLiteral("active")).toBool());
	}
	{
		const QSignalBlocker block(m_format);
		m_format->setCurrentIndex(static_cast<int>(musicaltime::currentFormat()));
	}
	m_summary->setText(tr("%1 events (the map holds at most %2); project tempo %3 BPM")
		.arg(map.value(QStringLiteral("event_count")).toInt())
		.arg(map.value(QStringLiteral("max_events")).toInt())
		.arg(map.value(QStringLiteral("global_tempo")).toInt()));
	const QJsonArray events = map.value(QStringLiteral("events")).toArray();
	const int ticksPerBar = Engine::getSong()->ticksPerBar();
	const int lastTempo = lastTempoRow(events);
	m_table->setRowCount(static_cast<int>(events.size()));
	for (int row = 0; row < events.size(); ++row)
	{
		const QJsonObject event = events.at(row).toObject();
		const bool ramps = event.value(QStringLiteral("ramp")).toBool() && row < lastTempo;
		const QStringList cells = eventCells(event, barAt(events, row, ticksPerBar), ramps);
		for (int column = 0; column < cells.size(); ++column)
		{
			m_table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
		}
		m_table->item(row, 0)->setData(Qt::UserRole, event.value(QStringLiteral("tick")).toInt());  // the event's own tick
	}
}

int TempoMapPanel::rowCount() const
{
	return m_table->rowCount();
}

bool TempoMapPanel::addEvent(int tick, int bpm)
{
	Song* song = Engine::getSong();
	const ControlResult added = run("transport.tempo_map_add", {{QStringLiteral("tick"), tick},
		{QStringLiteral("bpm"), bpm > 0 ? bpm : song->getTempo()},
		{QStringLiteral("numerator"), song->getTimeSigModel().getNumerator()},
		{QStringLiteral("denominator"), song->getTimeSigModel().getDenominator()}});
	if (!added.ok) { m_summary->setText(added.errorMessage); return false; }
	refresh();
	return true;
}

bool TempoMapPanel::toggleRamp(int row)
{
	const QJsonArray events = run("transport.tempo_map_get").result.value(QStringLiteral("events")).toArray();
	if (row < 0 || row >= events.size()) { return false; }
	const QJsonObject event = events.at(row).toObject();
	if (!event.value(QStringLiteral("has_tempo")).toBool()) { return false; }
	const bool ramps = !event.value(QStringLiteral("ramp")).toBool();
	// The last tempo event has no next tempo to ramp to: the map ignores its ramp (review ux05 #97).
	if (ramps && row >= lastTempoRow(events))
	{
		m_summary->setText(tr("Ramp to Next needs a later tempo event to ramp to."));
		return false;
	}
	const ControlResult set = run("transport.tempo_map_add", {{QStringLiteral("tick"), event.value(QStringLiteral("tick"))},
		{QStringLiteral("bpm"), event.value(QStringLiteral("bpm"))}, {QStringLiteral("ramp"), ramps}});
	if (!set.ok) { m_summary->setText(set.errorMessage); return false; }
	refresh();
	return true;
}

bool TempoMapPanel::removeRow(int row)
{
	if (row < 0 || row >= m_table->rowCount()) { return false; }
	const ControlResult removed = run("transport.tempo_map_remove",
		{{QStringLiteral("tick"), m_table->item(row, 0)->data(Qt::UserRole).toInt()}});
	if (!removed.ok) { m_summary->setText(removed.errorMessage); return false; }
	refresh();
	return true;
}

QString TempoMapPanel::exportTo(const QString& path)
{
	const ControlResult result = run("interchange.smf_export",
		{{QStringLiteral("path"), path}, {QStringLiteral("overwrite"), true}});
	return result.ok ? QString() : result.errorMessage;
}

QString TempoMapPanel::importFrom(const QString& path)
{
	const ControlResult result = run("interchange.smf_import", {{QStringLiteral("path"), path}});
	refresh();
	return result.ok ? QString() : result.errorMessage;
}

} // namespace lmms::gui
