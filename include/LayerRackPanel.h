/*
 * LayerRackPanel.h - 0.5.0 F2 interface: an instrument track's layer rack (MX-112)
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

#ifndef LMMS_GUI_LAYER_RACK_PANEL_H
#define LMMS_GUI_LAYER_RACK_PANEL_H

#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "ControlRegistry.h"
#include "lmms_export.h"

class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QToolButton;

namespace lmms::gui
{

/*! One instrument track's layer rack (MX-112): a row per layer - layer-0 is the track's own instrument -
 *  with its instrument, Mute, Solo, its Level, Open (the layer instrument's own view) and Remove; under the
 *  rows Add Layer... (the instrument picker, then rack.layer_add) and Zones... (the rack's key/velocity
 *  zones, CommandLists::layerZones). REGISTRY-FIRST, like the take lanes panel: every control runs a rack.*
 *  command and every redraw is read from rack.layers, so the panel shows what an agent reading the socket
 *  sees, and each edit is one journalled undo step. */
class LMMS_EXPORT LayerRackPanel : public QWidget
{
	Q_OBJECT
public:
	explicit LayerRackPanel(const QString& trackId, QWidget* parent = nullptr);

	//! Re-reads rack.layers and redraws; the timer calls it. Public for tests.
	void refresh();

	//! Layers the panel shows, layer-0 included.
	int rowCount() const { return static_cast<int>(m_rows.size()); }
	//! Row @a layer's controls (nullptr past the last row; layer-0 has no Open, no Remove and no Level).
	QToolButton* muteButton(int layer) const;
	QToolButton* soloButton(int layer) const;
	QToolButton* openButton(int layer) const;
	QToolButton* removeButton(int layer) const;
	QDoubleSpinBox* levelBox(int layer) const;
	QToolButton* addButton() const { return m_add; }
	QToolButton* zonesButton() const { return m_zones; }
	QString statusText() const;

	//! What Add Layer... runs once its picker has chosen the instrument @a device (dev-<n>).
	ControlResult addLayer(const QString& device);
	//! What Open runs: the layer instrument's own view in a window of its own (closed with the layer); nullptr
	//! when the layer has no instrument.
	QWidget* openLayer(int layer);

private:
	struct Row
	{
		QLabel* name = nullptr;
		QToolButton* mute = nullptr;
		QToolButton* solo = nullptr;
		QDoubleSpinBox* level = nullptr;
		QToolButton* open = nullptr;
		QToolButton* remove = nullptr;
	};

	QToolButton* makeButton(const QString& command, const QString& text);
	ControlResult run(const QString& command, const QJsonObject& args);
	void setMix(int layer, const char* key, const QJsonValue& value);
	//! The rows again when the layers changed (their ids and instruments): @a signature is that list.
	void rebuild(const QJsonArray& layers, const QStringList& signature);
	void addRow(int layer);
	void showMix(const QJsonArray& layers);
	const Row* rowAt(int layer) const;

	QString m_trackId;
	QGridLayout* m_grid = nullptr;
	QToolButton* m_add = nullptr;
	QToolButton* m_zones = nullptr;
	QLabel* m_status = nullptr;
	std::vector<Row> m_rows;
	QStringList m_signature;
	QTimer m_timer;
};

//! Show the layer rack of @a trackId in a window of its own (deleted on close) and return it.
LMMS_EXPORT LayerRackPanel* showLayerRack(QWidget* parent, const QString& trackId);

} // namespace lmms::gui

#endif // LMMS_GUI_LAYER_RACK_PANEL_H
