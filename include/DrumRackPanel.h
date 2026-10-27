/*
 * DrumRackPanel.h - a drum rack's sample pads: name, trigger key and sample (Q050-066)
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

#ifndef LMMS_GUI_DRUM_RACK_PANEL_H
#define LMMS_GUI_DRUM_RACK_PANEL_H

#include <functional>
#include <vector>

#include <QJsonArray>
#include <QTimer>
#include <QWidget>

#include "ControlRegistry.h"
#include "lmms_export.h"

class QGridLayout;
class QLabel;
class QLineEdit;
class QSpinBox;
class QToolButton;

namespace lmms::gui
{

/*! A drum rack's pads (Q050-066 stage 1), opened from the rack folder's Folder menu: a row per pad with its
 *  name, its trigger key and its sample (Sample... replaces it), and under the rows Add Pad... (a sample file,
 *  then a pad for it). REGISTRY-FIRST, like the layer rack: every control runs a track.drum_rack_* command
 *  and every redraw is read from track.drum_rack_get_state, so the panel shows what an agent reads over the
 *  socket, and each edit is the command's one undo step. */
class LMMS_EXPORT DrumRackPanel : public QWidget
{
	Q_OBJECT
public:
	explicit DrumRackPanel(const QString& folderId, QWidget* parent = nullptr);

	//! Re-reads track.drum_rack_get_state and redraws; the timer calls it. Public for tests.
	void refresh();

	int rowCount() const { return static_cast<int>(m_rows.size()); }
	//! Row @a pad's controls (nullptr past the last row).
	QLineEdit* nameEdit(int pad) const;
	QSpinBox* keyBox(int pad) const;
	QToolButton* sampleButton(int pad) const;
	QToolButton* addButton() const { return m_add; }
	QString statusText() const;

	//! For the tests: answer the sample-file question with @a chooser instead of a file dialog; an empty one
	//! restores the dialog.
	static void setSampleChooser(std::function<QString(QWidget*)> chooser);

private:
	struct Row
	{
		QString track;
		QLineEdit* name = nullptr;
		QSpinBox* key = nullptr;
		QLabel* sample = nullptr;
		QToolButton* replace = nullptr;
	};

	QString chooseSample();
	ControlResult run(const QString& command, const QJsonObject& args);
	void setPad(int pad, const QString& field, const QJsonValue& value);
	void rebuild(const QJsonArray& pads, const QStringList& signature);
	void addRow(int pad, const QString& track);
	void showPads(const QJsonArray& pads);
	const Row* rowAt(int pad) const;

	QString m_folderId;
	QGridLayout* m_grid = nullptr;
	QToolButton* m_add = nullptr;
	QLabel* m_status = nullptr;
	std::vector<Row> m_rows;
	QStringList m_signature;
	QTimer m_timer;
};

//! Show the pads of the drum rack @a folderId in a window of its own (deleted on close) and return it.
LMMS_EXPORT DrumRackPanel* showDrumRackPanel(QWidget* parent, const QString& folderId);

} // namespace lmms::gui

#endif // LMMS_GUI_DRUM_RACK_PANEL_H
