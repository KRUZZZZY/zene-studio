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
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "Engine.h"
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
	m_table->setHorizontalHeaderLabels({tr("Bar"), tr("Tick"), tr("Tempo (BPM)"), tr("Time signature")});
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->setAccessibleName(tr("Tempo map events"));

	auto* add = commandButton(tr("Add at playhead"), "transport.tempo_map_add", this);
	auto* remove = commandButton(tr("Remove"), "transport.tempo_map_remove", this);
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
	buttons->addWidget(remove);
	buttons->addStretch();
	buttons->addWidget(importMidi);
	buttons->addWidget(exportMidi);
	buttons->addWidget(clear);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_active);
	layout->addWidget(m_summary);
	layout->addWidget(m_table, 1);
	layout->addLayout(buttons);
	resize(480, 360);
	refresh();
}

void TempoMapPanel::refresh()
{
	const QJsonObject map = run("transport.tempo_map_get").result;
	{
		const QSignalBlocker block(m_active);
		m_active->setChecked(map.value(QStringLiteral("active")).toBool());
	}
	m_summary->setText(tr("%1 of %2 events; project tempo %3 BPM")
		.arg(map.value(QStringLiteral("event_count")).toInt())
		.arg(map.value(QStringLiteral("max_events")).toInt())
		.arg(map.value(QStringLiteral("global_tempo")).toInt()));
	const QJsonArray events = map.value(QStringLiteral("events")).toArray();
	const int ticksPerBar = Engine::getSong()->ticksPerBar();
	m_table->setRowCount(static_cast<int>(events.size()));
	for (int row = 0; row < events.size(); ++row)
	{
		const QJsonObject event = events.at(row).toObject();
		const int tick = event.value(QStringLiteral("tick")).toInt();
		const int numerator = event.value(QStringLiteral("numerator")).toInt();
		const QStringList cells{QString::number(tick / std::max(1, ticksPerBar) + 1), QString::number(tick),
			cell(event.value(QStringLiteral("bpm")).toInt()),
			numerator > 0 ? QStringLiteral("%1/%2").arg(numerator).arg(event.value(QStringLiteral("denominator")).toInt())
				: QStringLiteral("-")};
		for (int column = 0; column < cells.size(); ++column)
		{
			m_table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
		}
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

bool TempoMapPanel::removeRow(int row)
{
	if (row < 0 || row >= m_table->rowCount()) { return false; }
	const ControlResult removed = run("transport.tempo_map_remove",
		{{QStringLiteral("tick"), m_table->item(row, 1)->text().toInt()}});
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
