/*
 * LayerRackPanel.cpp - 0.5.0 F2 interface: an instrument track's layer rack (MX-112)
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

#include "LayerRackPanel.h"

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "CommandListDialog.h"
#include "CommandLists.h"
#include "ControlInstrumentCommands.h"
#include "DevicePicker.h"
#include "Instrument.h"
#include "InstrumentLayers.h"
#include "InstrumentTrack.h"
#include "PluginView.h"

namespace lmms::gui
{

namespace
{

constexpr int RefreshMs = 500;
constexpr double PercentPerUnit = 100.0;

QString layerName(int layer)
{
	return QStringLiteral("layer-%1").arg(layer);
}

//! The row's caption: the layer, then its instrument ("the track's own" for layer-0).
QString caption(const QJsonObject& layer)
{
	const QString instrument = layer.value(QStringLiteral("display_name")).toString();
	return layer.value(QStringLiteral("base")).toBool()
		? LayerRackPanel::tr("%1 (the track's own): %2").arg(layer.value(QStringLiteral("id")).toString(), instrument)
		: QStringLiteral("%1: %2").arg(layer.value(QStringLiteral("id")).toString(), instrument);
}

} // namespace


LayerRackPanel::LayerRackPanel(const QString& trackId, QWidget* parent) :
	QWidget(parent),
	m_trackId(trackId)
{
	ControlResult unknown;
	const InstrumentTrack* track = control::resolveInstrumentTarget(trackId, &unknown);
	setWindowTitle(tr("Layer rack - %1").arg(track != nullptr ? track->name() : trackId));
	setObjectName(QStringLiteral("LayerRackPanel"));
	auto* outer = new QVBoxLayout(this);
	m_grid = new QGridLayout;
	outer->addLayout(m_grid);
	auto* actions = new QHBoxLayout;
	m_add = makeButton(QStringLiteral("rack.layer_add"), tr("Add Layer..."));
	connect(m_add, &QToolButton::clicked, this, [this] {
		const QString chosen = DevicePicker::pick(this, DevicePicker::Kind::Instrument, tr("Layer an instrument"));
		if (!chosen.isEmpty()) { addLayer(chosen); }
	});
	m_zones = makeButton(QStringLiteral("rack.layers"), tr("Zones..."));
	m_zones->setToolTip(tr("The key and velocity zones that pick which layers a note plays on"));
	connect(m_zones, &QToolButton::clicked, this, [this] { showCommandList(this, CommandLists::layerZones(m_trackId)); });
	actions->addWidget(m_add);
	actions->addWidget(m_zones);
	actions->addStretch(1);
	outer->addLayout(actions);
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Layer rack status"));
	m_status->setWordWrap(true);
	outer->addWidget(m_status);
	outer->addStretch(1);
	connect(&m_timer, &QTimer::timeout, this, &LayerRackPanel::refresh);
	m_timer.start(RefreshMs);
	refresh();
}


QToolButton* LayerRackPanel::makeButton(const QString& command, const QString& text)
{
	auto* button = new QToolButton(this);
	button->setText(text);
	button->setAccessibleName(text);
	button->setProperty("controlCommand", command);
	button->setFocusPolicy(Qt::StrongFocus);
	return button;
}


ControlResult LayerRackPanel::run(const QString& command, const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	m_status->setText(result.ok ? tr("%1: done").arg(command) : result.errorMessage);
	refresh();
	return result;
}


void LayerRackPanel::setMix(int layer, const char* key, const QJsonValue& value)
{
	run(QStringLiteral("rack.layer_set"), {{QStringLiteral("target"), m_trackId}, {QStringLiteral("layer"), layerName(layer)},
		{QLatin1String(key), value}});
}


ControlResult LayerRackPanel::addLayer(const QString& device)
{
	return run(QStringLiteral("rack.layer_add"), {{QStringLiteral("target"), m_trackId}, {QStringLiteral("device"), device}});
}


QWidget* LayerRackPanel::openLayer(int layer)
{
	ControlResult unknown;
	InstrumentTrack* track = control::resolveInstrumentTarget(m_trackId, &unknown);
	InstrumentTrack* hosted = track != nullptr ? track->layers().layer(layer) : nullptr;
	Instrument* instrument = hosted != nullptr ? hosted->instrument() : nullptr;
	if (instrument == nullptr)
	{
		m_status->setText(tr("%1 has no instrument to open").arg(layerName(layer)));
		return nullptr;
	}
	auto* window = new QWidget(nullptr, Qt::Window);
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->setWindowTitle(tr("%1 - %2").arg(instrument->displayName(), layerName(layer)));
	auto* column = new QVBoxLayout(window);
	column->setContentsMargins(0, 0, 0, 0);
	column->addWidget(instrument->createView(window));
	// The window goes with the layer: removing it (here or from the socket) deletes the instrument.
	const QPointer<QWidget> guarded = window;
	connect(instrument, &QObject::destroyed, window, [guarded] { delete guarded.data(); });
	window->show();
	return window;
}


void LayerRackPanel::refresh()
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("rack.layers"), {{QStringLiteral("target"), m_trackId}});
	if (!state.ok)
	{
		m_status->setText(state.errorMessage);
		return;
	}
	const QJsonArray layers = state.result.value(QStringLiteral("layers")).toArray();
	QStringList signature;
	for (const QJsonValue& value : layers) { signature << caption(value.toObject()); }
	if (signature != m_signature) { rebuild(layers, signature); }
	showMix(layers);
	m_add->setEnabled(state.result.value(QStringLiteral("layer_count")).toInt() < state.result.value(QStringLiteral("max_layers")).toInt());
}


void LayerRackPanel::rebuild(const QJsonArray& layers, const QStringList& signature)
{
	for (const Row& row : m_rows)
	{
		for (QWidget* widget : std::initializer_list<QWidget*>{row.name, row.mute, row.solo, row.level, row.open, row.remove})
		{
			if (widget != nullptr) { widget->hide(); widget->deleteLater(); }
		}
	}
	m_rows.clear();
	m_signature = signature;
	for (int layer = 0; layer < layers.size(); ++layer) { addRow(layer); }
}


void LayerRackPanel::addRow(int layer)
{
	Row row;
	row.name = new QLabel(m_signature.value(layer), this);
	row.mute = makeButton(QStringLiteral("rack.layer_set"), tr("Mute"));
	row.solo = makeButton(QStringLiteral("rack.layer_set"), tr("Solo"));
	row.mute->setAccessibleName(tr("Mute %1").arg(layerName(layer)));
	row.solo->setAccessibleName(tr("Solo %1").arg(layerName(layer)));
	for (QToolButton* toggle : {row.mute, row.solo}) { toggle->setCheckable(true); }
	connect(row.mute, &QToolButton::clicked, this, [this, layer](bool on) { setMix(layer, "muted", on); });
	connect(row.solo, &QToolButton::clicked, this, [this, layer](bool on) { setMix(layer, "solo", on); });
	m_grid->addWidget(row.name, layer, 0);
	m_grid->addWidget(row.mute, layer, 1);
	m_grid->addWidget(row.solo, layer, 2);
	if (layer > 0)  // layer-0 is the track's own instrument: its level is the track's volume, it stays, it has its window
	{
		row.level = new QDoubleSpinBox(this);
		row.level->setRange(0.0, InstrumentLayers::MaxLevel * PercentPerUnit);
		row.level->setSuffix(QStringLiteral(" %"));
		row.level->setAccessibleName(tr("Level of %1").arg(layerName(layer)));
		row.level->setProperty("controlCommand", QStringLiteral("rack.layer_set"));
		connect(row.level, &QDoubleSpinBox::editingFinished, this, [this, layer, box = row.level] {
			setMix(layer, "level", box->value() / PercentPerUnit);
		});
		row.open = makeButton(QStringLiteral("rack.layers"), tr("Open"));
		row.open->setAccessibleName(tr("Open %1's instrument").arg(layerName(layer)));
		connect(row.open, &QToolButton::clicked, this, [this, layer] { openLayer(layer); });
		row.remove = makeButton(QStringLiteral("rack.layer_remove"), tr("Remove"));
		row.remove->setAccessibleName(tr("Remove %1").arg(layerName(layer)));
		connect(row.remove, &QToolButton::clicked, this, [this, layer] {
			run(QStringLiteral("rack.layer_remove"), {{QStringLiteral("target"), m_trackId}, {QStringLiteral("layer"), layerName(layer)}});
		});
		m_grid->addWidget(row.level, layer, 3);
		m_grid->addWidget(row.open, layer, 4);
		m_grid->addWidget(row.remove, layer, 5);
	}
	m_rows.push_back(row);
}


void LayerRackPanel::showMix(const QJsonArray& layers)
{
	for (int layer = 0; layer < layers.size() && layer < rowCount(); ++layer)
	{
		const QJsonObject mix = layers.at(layer).toObject();
		const Row& row = m_rows[static_cast<std::size_t>(layer)];
		row.mute->setChecked(mix.value(QStringLiteral("muted")).toBool());
		row.solo->setChecked(mix.value(QStringLiteral("solo")).toBool());
		if (row.level != nullptr && !row.level->hasFocus())  // never under the user's typing
		{
			const QSignalBlocker blocker(row.level);
			row.level->setValue(mix.value(QStringLiteral("level")).toDouble(1.0) * PercentPerUnit);
		}
	}
}


const LayerRackPanel::Row* LayerRackPanel::rowAt(int layer) const
{
	return layer >= 0 && layer < rowCount() ? &m_rows[static_cast<std::size_t>(layer)] : nullptr;
}

QToolButton* LayerRackPanel::muteButton(int layer) const { return rowAt(layer) != nullptr ? rowAt(layer)->mute : nullptr; }
QToolButton* LayerRackPanel::soloButton(int layer) const { return rowAt(layer) != nullptr ? rowAt(layer)->solo : nullptr; }
QToolButton* LayerRackPanel::openButton(int layer) const { return rowAt(layer) != nullptr ? rowAt(layer)->open : nullptr; }
QToolButton* LayerRackPanel::removeButton(int layer) const { return rowAt(layer) != nullptr ? rowAt(layer)->remove : nullptr; }
QDoubleSpinBox* LayerRackPanel::levelBox(int layer) const { return rowAt(layer) != nullptr ? rowAt(layer)->level : nullptr; }

QString LayerRackPanel::statusText() const
{
	return m_status->text();
}


LayerRackPanel* showLayerRack(QWidget* parent, const QString& trackId)
{
	auto* panel = new LayerRackPanel(trackId, parent);
	panel->setWindowFlag(Qt::Window);
	panel->setAttribute(Qt::WA_DeleteOnClose);
	panel->show();
	return panel;
}

} // namespace lmms::gui
