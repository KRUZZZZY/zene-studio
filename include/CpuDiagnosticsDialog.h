/*
 * CpuDiagnosticsDialog.h - each instrument's synthesis load and the engine's late periods, in a window
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

#ifndef LMMS_GUI_CPU_DIAGNOSTICS_DIALOG_H
#define LMMS_GUI_CPU_DIAGNOSTICS_DIALOG_H

#include <QDialog>
#include <QJsonObject>

#include "lmms_export.h"

class QLabel;
class QTableWidget;
class QTimer;

namespace lmms::gui
{

/*! Q050-074.1: the CPU readout's diagnostics - every instrument track's synthesis share of the period and
 *  the engine's late periods at their song positions; Q050-074.2: the audio device's negotiated timing
 *  and its own xruns beside them ('unavailable' where the backend reports nothing). Everything it shows is mixer.cpu_load's answer,
 *  asked through ControlRegistry, so the window and an agent read the same state. Twice a second while
 *  shown; read-only, nothing to undo or save. */
class LMMS_EXPORT CpuDiagnosticsDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CpuDiagnosticsDialog(QWidget* parent = nullptr);

	//! Asks mixer.cpu_load again and shows its answer.
	void refresh();

	QLabel* summary() const { return m_summary; }
	QTableWidget* tracks() const { return m_tracks; }
	QTableWidget* latePeriods() const { return m_late; }
	//! Q050-074.2: the audio device's readings - one row per reading, its key in column 0's Qt::UserRole.
	QTableWidget* audioDevice() const { return m_device; }

	//! The one window for @a parent's top-level window, built on first use, shown and refreshed.
	static CpuDiagnosticsDialog* open(QWidget* parent);

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	void showAudioDevice(const QJsonObject& device, double latePeriods);

	QLabel* m_summary;
	QTableWidget* m_tracks;
	QTableWidget* m_late;
	QTableWidget* m_device;
	QTimer* m_timer;
};

//! Q050-074.1: the "CPU diagnostics..." right-click action on the header's CPU readout @a readout.
LMMS_EXPORT void addCpuDiagnosticsAction(QWidget* readout);

} // namespace lmms::gui

#endif // LMMS_GUI_CPU_DIAGNOSTICS_DIALOG_H
