/*
 * ModulatorPanel.h - Edit > Modulators: the modulation layer's LFOs and the parameters they drive
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

#ifndef LMMS_GUI_MODULATOR_PANEL_H
#define LMMS_GUI_MODULATOR_PANEL_H

#include <QDialog>

#include "lmms_export.h"

class QComboBox;
class QDoubleSpinBox;
class QJsonObject;
class QListWidget;
class QTableWidget;

namespace lmms::gui
{

/*! Edit > Modulators (KNOWN-LIMITATIONS "The modulation layer is in the engine and on the socket, and
 *  there is no interface for it"): every modulator modulator.get_state reports, Add LFO and Remove
 *  (modulator.create / modulator.remove), the selected one's shape and rate (modulator.rate_set),
 *  the parameters it drives with their depth (modulator.target_remove to unbind), and a Bind row that
 *  picks a mixer channel's effect and parameter from dsp.get_state and a depth (modulator.target_set).
 *  Every change is its command's one undo step. */
class LMMS_EXPORT ModulatorPanel : public QDialog
{
public:
	explicit ModulatorPanel(QWidget* parent = nullptr);

	void refresh();
	int modulatorCount() const;
	int targetCount() const;
	//! For the buttons and the tests. Each returns the refusal's message, or an empty string.
	QString addModulator(const QString& shape, double rateHz);
	QString removeSelected();
	QString setShapeAndRate(const QString& shape, double rateHz);
	QString bind(const QString& channel, int effect, const QString& parameter, double depth);
	QString unbindRow(int row);
	void selectModulator(int row);

private:
	QString selectedId() const;
	void showSelected();
	void fillChannels();
	void fillEffects();
	void fillParameters();

	QListWidget* m_modulators = nullptr;
	QComboBox* m_shape = nullptr;
	QDoubleSpinBox* m_rate = nullptr;
	QTableWidget* m_targets = nullptr;
	QComboBox* m_channel = nullptr;
	QComboBox* m_effect = nullptr;
	QComboBox* m_parameter = nullptr;
	QDoubleSpinBox* m_depth = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_MODULATOR_PANEL_H
