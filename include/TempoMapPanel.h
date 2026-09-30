/*
 * TempoMapPanel.h - Edit > Tempo Map: the tempo and time-signature map, from the interface
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

#ifndef LMMS_GUI_TEMPO_MAP_PANEL_H
#define LMMS_GUI_TEMPO_MAP_PANEL_H

#include <QDialog>

#include "lmms_export.h"

class QCheckBox;
class QLabel;
class QTableWidget;

namespace lmms::gui
{

/*! Edit > Tempo Map (KNOWN-LIMITATIONS "nothing in src/gui/ draws, edits or reads a tempo map"):
 *  the map's events as transport.tempo_map_get reports them - bar, tick, tempo, time signature -
 *  with its Active switch (transport.tempo_map_set_active), "Add at playhead" (tempo_map_add, at
 *  the song's position, the song's tempo and signature, which the row can then be edited from),
 *  Remove (tempo_map_remove) and Clear (tempo_map_clear). An inactive or empty map leaves the
 *  project on its single tempo, as it always was. */
class LMMS_EXPORT TempoMapPanel : public QDialog
{
public:
	explicit TempoMapPanel(QWidget* parent = nullptr);

	void refresh();
	int rowCount() const;
	//! Adds an event at @a tick with @a bpm (0: the song's tempo); for the button and the tests.
	bool addEvent(int tick, int bpm);
	bool removeRow(int row);
	//! Writes the map as a Standard MIDI File conductor track (interchange.smf_export, replacing
	//! @a path) and replaces it with a file's tempo and meter events (interchange.smf_import); for
	//! the Export MIDI... / Import MIDI... buttons and the tests. The message is a refusal's, or empty.
	QString exportTo(const QString& path);
	QString importFrom(const QString& path);

private:
	QCheckBox* m_active = nullptr;
	QLabel* m_summary = nullptr;
	QTableWidget* m_table = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_TEMPO_MAP_PANEL_H
