/*
 * InputLevelMeter.h - 0.5.0 L08 (MX-134): the engine's audio input, metered live
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

#ifndef LMMS_GUI_INPUT_LEVEL_METER_H
#define LMMS_GUI_INPUT_LEVEL_METER_H

#include <array>

#include <QElapsedTimer>
#include <QWidget>

#include "lmms_export.h"

namespace lmms::gui
{

/*! MX-134: two bars, the left and right of the engine's stereo input bus, -60..0 dB, polled from
 *  inputmeter::live() (the peaks since its last read - meter.input's take is left alone) every 40 ms. The bars
 *  move by the mixer's ballistics (MixerViewOptions.h, MX-128): each falls by the chosen fall-off when the
 *  input is lower, and a line holds the peak for the chosen time. A reading at full scale latches a clip
 *  light, cleared by a click. Its accessible description says the levels in words. GUI thread only. */
class LMMS_EXPORT InputLevelMeter : public QWidget
{
	Q_OBJECT
public:
	explicit InputLevelMeter(QWidget* parent);

	//! One reading, linear: each bar takes it when it is higher, else falls by the fall-off.
	void tick(float left, float right);

	float level(int side) const { return m_level.at(side); }  //!< 0 left, 1 right; linear
	float held(int side) const { return m_held.at(side); }    //!< the hold line, linear
	bool clipped() const { return m_clipped; }

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;

private:
	void poll();
	void describe();

	std::array<float, 2> m_level{};
	std::array<float, 2> m_held{};
	std::array<QElapsedTimer, 2> m_heldSince;
	bool m_clipped = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_INPUT_LEVEL_METER_H
