/*
 * CpuDiagnosticsDialog.cpp - each instrument's synthesis load and the engine's late periods, in a window
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

#include "CpuDiagnosticsDialog.h"

#include <QAction>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPointer>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

constexpr int RefreshMilliseconds = 500;

QTableWidget* makeTable(const QStringList& headers, const QString& name, QWidget* parent)
{
	auto* table = new QTableWidget(0, static_cast<int>(headers.size()), parent);
	table->setObjectName(name);
	table->setHorizontalHeaderLabels(headers);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionMode(QAbstractItemView::NoSelection);
	table->verticalHeader()->setVisible(false);
	table->horizontalHeader()->setStretchLastSection(true);
	return table;
}

void setRow(QTableWidget* table, int row, const QStringList& cells)
{
	for (int column = 0; column < cells.size(); ++column)
	{
		table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
	}
}

QString percentText(double percent)
{
	return QStringLiteral("%1%").arg(percent, 0, 'f', 1);
}

QString millisecondsText(double micros)
{
	return QObject::tr("%1 ms").arg(micros / 1000.0, 0, 'f', 2);
}

//! Q050-074.2: one device reading as text - its value and unit, or "unavailable".
QString readingText(const QJsonObject& reading)
{
	if (!reading.value(QStringLiteral("available")).toBool()) { return QObject::tr("unavailable"); }
	const QString unit = reading.value(QStringLiteral("unit")).toString();
	const double value = reading.value(QStringLiteral("value")).toDouble();
	if (unit == QStringLiteral("count")) { return QString::number(value, 'f', 0); }
	QString text = QStringLiteral("%1 %2").arg(QString::number(value, 'f', 0), unit);
	if (reading.contains(QStringLiteral("ms")))
	{
		text += QStringLiteral(" (%1, %2)").arg(millisecondsText(1000.0 * reading.value(QStringLiteral("ms")).toDouble()),
			reading.value(QStringLiteral("basis")).toString());
	}
	return text;
}

QString framesText(double frames)
{
	return QObject::tr("%1 frames").arg(QString::number(frames, 'f', 0));
}

} // namespace

CpuDiagnosticsDialog::CpuDiagnosticsDialog(QWidget* parent) :
	QDialog(parent),
	m_summary(new QLabel(this)),
	m_tracks(makeTable({tr("Track"), tr("Instrument"), tr("Synthesis")}, QStringLiteral("cpuSynthesisTable"), this)),
	m_late(makeTable({tr("#"), tr("Position"), tr("Took"), tr("Period"), tr("Load"), tr("Transport")},
		QStringLiteral("cpuLatePeriodTable"), this)),
	m_device(makeTable({tr("Reading"), tr("Value")}, QStringLiteral("cpuAudioDeviceTable"), this)),
	m_timer(new QTimer(this))
{
	setObjectName(QStringLiteral("cpuDiagnosticsDialog"));
	setWindowTitle(tr("CPU diagnostics"));
	m_summary->setObjectName(QStringLiteral("cpuDiagnosticsSummary"));
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_summary);
	auto* synthesis = new QLabel(tr("Instrument synthesis - each instrument track's share of the audio period, "
		"apart from its mixer channel and effects"), this);
	synthesis->setWordWrap(true);
	layout->addWidget(synthesis);
	layout->addWidget(m_tracks);
	auto* late = new QLabel(tr("Late periods - renders that took longer than the period lasts, at the song position "
		"they started (the engine's deadline misses, not the audio device's xruns)"), this);
	late->setWordWrap(true);
	layout->addWidget(late);
	layout->addWidget(m_late);
	auto* device = new QLabel(tr("Audio device - what the backend negotiated and the device's own xruns (it ran dry), "
		"apart from the engine's late periods; 'unavailable' where the backend reports nothing"), this);
	device->setWordWrap(true);
	layout->addWidget(device);
	layout->addWidget(m_device);
	m_timer->setInterval(RefreshMilliseconds);
	connect(m_timer, &QTimer::timeout, this, &CpuDiagnosticsDialog::refresh);
	resize(560, 680);
}

void CpuDiagnosticsDialog::refresh()
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("mixer.cpu_load"));
	if (!state.ok)
	{
		m_summary->setText(tr("CPU load unavailable: %1").arg(state.errorMessage));
		return;
	}
	const QJsonObject late = state.result.value(QStringLiteral("late_periods")).toObject();
	m_summary->setText(tr("Engine %1 - instruments %2 - mixer %3 - late periods: %4")
		.arg(percentText(state.result.value(QStringLiteral("engine_percent")).toDouble()))
		.arg(percentText(state.result.value(QStringLiteral("synthesis_total_percent")).toDouble()))
		.arg(percentText(state.result.value(QStringLiteral("total_percent")).toDouble()))
		.arg(late.value(QStringLiteral("count")).toVariant().toULongLong()));

	const QJsonArray tracks = state.result.value(QStringLiteral("tracks")).toArray();
	m_tracks->setRowCount(static_cast<int>(tracks.size()));
	for (int row = 0; row < tracks.size(); ++row)
	{
		const QJsonObject track = tracks.at(row).toObject();
		setRow(m_tracks, row, {track.value(QStringLiteral("name")).toString(), track.value(QStringLiteral("instrument")).toString(),
			percentText(track.value(QStringLiteral("synthesis_percent")).toDouble())});
		m_tracks->item(row, 0)->setData(Qt::UserRole, track.value(QStringLiteral("track")).toString());
	}

	// Newest first: the mark just made is the one a musician looks for.
	const QJsonArray marks = late.value(QStringLiteral("marks")).toArray();
	m_late->setRowCount(static_cast<int>(marks.size()));
	for (int i = 0; i < marks.size(); ++i)
	{
		const QJsonObject mark = marks.at(static_cast<int>(marks.size()) - 1 - i).toObject();
		setRow(m_late, i, {QString::number(mark.value(QStringLiteral("serial")).toDouble(), 'f', 0),
			mark.value(QStringLiteral("position")).toString(), millisecondsText(mark.value(QStringLiteral("took_us")).toDouble()),
			millisecondsText(mark.value(QStringLiteral("period_us")).toDouble()),
			percentText(mark.value(QStringLiteral("percent")).toDouble()),
			mark.value(QStringLiteral("playing")).toBool() ? tr("playing") : tr("stopped")});
		m_late->item(i, 1)->setData(Qt::UserRole, mark.value(QStringLiteral("tick")).toInt());
	}
	showAudioDevice(state.result.value(QStringLiteral("audio_device")).toObject(),
		late.value(QStringLiteral("count")).toDouble());
}

void CpuDiagnosticsDialog::showAudioDevice(const QJsonObject& device, double latePeriods)
{
	const QJsonObject engine = device.value(QStringLiteral("engine")).toObject();
	const QList<std::pair<QString, QStringList>> rows = {
		{QStringLiteral("backend"), {tr("Backend"), device.value(QStringLiteral("backend")).toString()}},
		{QStringLiteral("engine_sample_rate"), {tr("Engine sample rate"),
			QStringLiteral("%1 Hz").arg(engine.value(QStringLiteral("sample_rate_hz")).toDouble(), 0, 'f', 0)}},
		{QStringLiteral("sample_rate"), {tr("Device sample rate"), readingText(device.value(QStringLiteral("sample_rate")).toObject())}},
		{QStringLiteral("input_sample_rate"), {tr("Input sample rate"),
			readingText(device.value(QStringLiteral("input_sample_rate")).toObject())}},
		{QStringLiteral("engine_period"), {tr("Engine period"), framesText(engine.value(QStringLiteral("period_frames")).toDouble())}},
		{QStringLiteral("period_frames"), {tr("Device period"), readingText(device.value(QStringLiteral("period_frames")).toObject())}},
		{QStringLiteral("buffer_frames"), {tr("Device buffer"), readingText(device.value(QStringLiteral("buffer_frames")).toObject())}},
		{QStringLiteral("input_latency"), {tr("Input latency"), readingText(device.value(QStringLiteral("input_latency")).toObject())}},
		{QStringLiteral("output_latency"), {tr("Output latency"), readingText(device.value(QStringLiteral("output_latency")).toObject())}},
		{QStringLiteral("xruns"), {tr("Device xruns"), readingText(device.value(QStringLiteral("xruns")).toObject())}},
		{QStringLiteral("late_periods"), {tr("Engine late periods"), QString::number(latePeriods, 'f', 0)}}};
	m_device->setRowCount(static_cast<int>(rows.size()));
	for (int row = 0; row < rows.size(); ++row)
	{
		setRow(m_device, row, rows.at(row).second);
		m_device->item(row, 0)->setData(Qt::UserRole, rows.at(row).first);
	}
}

CpuDiagnosticsDialog* CpuDiagnosticsDialog::open(QWidget* parent)
{
	static QPointer<CpuDiagnosticsDialog> s_dialog;
	QWidget* window = parent != nullptr ? parent->window() : nullptr;
	if (s_dialog == nullptr || s_dialog->parentWidget() != window) { s_dialog = new CpuDiagnosticsDialog(window); }
	s_dialog->refresh();
	s_dialog->show();
	s_dialog->raise();
	return s_dialog;
}

void CpuDiagnosticsDialog::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	m_timer->start();
}

void CpuDiagnosticsDialog::hideEvent(QHideEvent* event)
{
	m_timer->stop();
	QDialog::hideEvent(event);
}

void addCpuDiagnosticsAction(QWidget* readout)
{
	readout->setContextMenuPolicy(Qt::ActionsContextMenu);
	auto* action = new QAction(QObject::tr("CPU diagnostics..."), readout);
	action->setObjectName(QStringLiteral("cpuDiagnosticsAction"));
	action->setProperty("controlCommand", QStringLiteral("mixer.cpu_load"));
	QObject::connect(action, &QAction::triggered, readout, [readout] { CpuDiagnosticsDialog::open(readout); });
	readout->addAction(action);
	readout->setToolTip(QObject::tr("CPU: the engine's load in percent of the audio period. Right-click for CPU "
		"diagnostics - each instrument's synthesis load, the late periods and the audio device's timing and xruns "
		"(mixer.cpu_load)"));
}

} // namespace lmms::gui
