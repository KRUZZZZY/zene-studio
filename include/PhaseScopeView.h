/*
 * PhaseScopeView.h - 0.5.0 L08 (MX-137): the stereo phase view - a goniometer beside the correlation
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

#ifndef LMMS_GUI_PHASE_SCOPE_VIEW_H
#define LMMS_GUI_PHASE_SCOPE_VIEW_H

#include <cstdint>
#include <vector>

#include <QDialog>
#include <QElapsedTimer>
#include <QPointF>
#include <QWidget>

#include "lmms_export.h"

class QComboBox;
class QLabel;
class QProgressBar;

namespace lmms::gui
{

/*! MX-137: a goniometer (vectorscope). Each frame is a point turned 45 degrees: up is the mid (L+R)/sqrt2,
 *  across the side (R-L)/sqrt2 - so a mono signal stands up the middle, an out-of-phase one lies flat, the
 *  left channel alone leans to the left diagonal and the right alone to the right. Drawn scaled to the
 *  window's loudest frame, so a quiet signal still shows its shape. GUI thread only. */
class LMMS_EXPORT PhaseScopeView : public QWidget
{
	Q_OBJECT
public:
	explicit PhaseScopeView(QWidget* parent);

	//! The frames to draw (@a count left and right samples); none draws "no signal".
	void setFrames(const float* left, const float* right, std::size_t count);
	std::size_t pointCount() const { return m_points.size(); }
	//! Point @a i, unscaled: x the side, y the mid.
	QPointF point(std::size_t i) const { return m_points.at(i); }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	std::vector<QPointF> m_points;
	double m_peak = 0.0;
};

/*! MX-137: the Phase Scope window - one mixer channel (the master unless chosen) as a goniometer, with its
 *  phase correlation in figures and as a bar under it (MixerChannel::m_correlation, the reading the strips'
 *  tooltip and Large Meters give). It points PhaseScopeTap at its channel - followed by the channel's stable
 *  id, so a move does not change what it shows - polls it every 40 ms, and lets go of it when closed. One at
 *  a time: window.phase_scope and the Mixer View menu raise the open one. */
class LMMS_EXPORT PhaseScopeWindow : public QDialog
{
	Q_OBJECT
public:
	explicit PhaseScopeWindow(QWidget* parent);
	~PhaseScopeWindow() override;

	//! Look at mixer channel @a index (the master when it is not a channel); the channel it shows already
	//! keeps its picture and the tap's count.
	void setChannel(int index);
	int channel() const;  //!< its index now
	//! Read the tap and the correlation now; answers the correlation line.
	QString refresh();

private:
	void fillChannels();

	QComboBox* m_channels;
	PhaseScopeView* m_view;
	QLabel* m_correlation;
	QProgressBar* m_correlationBar;
	int m_channelId = -1;  //!< MixerChannel::id(); none until the first setChannel()
	std::uint64_t m_lastWritten = 0;
	QElapsedTimer m_sinceNew;  //!< since the tap last had new frames
	std::vector<float> m_left;
	std::vector<float> m_right;
};

//! Open the Phase Scope on channel @a index, or raise the open one and move it there.
LMMS_EXPORT PhaseScopeWindow* showPhaseScope(int index);
//! The open Phase Scope, or none.
LMMS_EXPORT PhaseScopeWindow* openPhaseScope();

} // namespace lmms::gui

#endif // LMMS_GUI_PHASE_SCOPE_VIEW_H
