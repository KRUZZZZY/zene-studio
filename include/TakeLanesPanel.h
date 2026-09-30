/*
 * TakeLanesPanel.h - M3 item 6: take lanes with audition and consolidate
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

#ifndef LMMS_GUI_TAKE_LANES_PANEL_H
#define LMMS_GUI_TAKE_LANES_PANEL_H

#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>

#include "lmms_export.h"

class QComboBox;
class QLabel;
class QSpinBox;
class QToolButton;
class QVBoxLayout;

namespace lmms::gui
{

/*! One track's take lanes (relief plan M3 item 6, the R3.1/R3.2 surface): its lanes with the
 *  takes on each, the composite, and the three comping gestures - audition a lane whole, paint
 *  a lane over a tick range (comp.select), and consolidate the comped region into one clip
 *  (clip.consolidate). REGISTRY-FIRST, like the clip-launch grid: every button runs a comp.* or
 *  clip.* command and every repaint is drawn from comp.get_state, so the panel shows what an
 *  agent reading the socket sees. */
class LMMS_EXPORT TakeLanesPanel : public QWidget
{
	Q_OBJECT
public:
	explicit TakeLanesPanel(const QString& trackId, QWidget* parent = nullptr);

	//! Re-reads comp.get_state and redraws; the timer calls it. Public for tests.
	void refresh();

	//! The controls, for tests and the agent-surface sweep.
	QToolButton* auditionButton(int lane) const;
	QToolButton* addLaneButton() const { return m_addLane; }
	QToolButton* compButton() const { return m_comp; }
	QToolButton* consolidateButton() const { return m_consolidate; }
	QSpinBox* beginBox() const { return m_begin; }
	QSpinBox* endBox() const { return m_end; }
	QComboBox* laneBox() const { return m_lane; }
	QString compositeText() const;
	QString statusText() const;

private:
	QToolButton* makeButton(const QString& command, const QString& text);
	void run(const QString& command, const QJsonObject& args);
	void audition(int lane);
	void showLanes(const QJsonArray& lanes);
	void showComposite(const QJsonObject& composite, bool hasLanes);

	QString m_trackId;
	QVBoxLayout* m_lanes = nullptr;
	QLabel* m_composite = nullptr;
	QLabel* m_status = nullptr;
	QSpinBox* m_begin = nullptr;
	QSpinBox* m_end = nullptr;
	QComboBox* m_lane = nullptr;
	QToolButton* m_addLane = nullptr;
	QToolButton* m_comp = nullptr;
	QToolButton* m_consolidate = nullptr;
	std::vector<QToolButton*> m_auditions;
	std::vector<int> m_laneIndices;
	int m_auditioning = -1;
	int m_spanBegin = 0;
	int m_spanEnd = 0;
	QTimer m_timer;
};

} // namespace lmms::gui

#endif // LMMS_GUI_TAKE_LANES_PANEL_H
