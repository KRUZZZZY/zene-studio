/*
 * InputChannelMeters.h - a meter for every capture channel and for every record-armed track's input
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

#ifndef LMMS_GUI_INPUT_CHANNEL_METERS_H
#define LMMS_GUI_INPUT_CHANNEL_METERS_H

#include <vector>

#include <QDialog>
#include <QString>

#include "lmms_export.h"

namespace lmms::gui
{

/*! Q050-340: the Input Meters window (the mixer view's menu, or window.input_meters) - a labelled bar per
 *  capture channel the input carries ("In 1", "In 2", ...) and, under them, one per record-armed track, named
 *  by the track and the input it records ("Vocal - In 3"). Polled every 40 ms from the interface's own maxima
 *  (inputreadings::take(Interface): an agent's meter.inputs never empties them), -60..0 dB, with the mixer's
 *  fall-off (MixerViewOptions.h); a channel that delivers nothing falls to silence. GUI thread only. */
class LMMS_EXPORT InputChannelMeters : public QDialog
{
	Q_OBJECT
public:
	struct Meter
	{
		QString label;
		int inputChannel = 0;
		int route = -1;  //!< the record route of an armed track's meter; -1 for a capture channel's
		float level = 0.f;  //!< linear, after the fall-off
	};

	explicit InputChannelMeters(QWidget* parent);

	//! One reading now (the timer's work): the channels' and the armed tracks' bars take it or fall.
	void poll();
	const std::vector<Meter>& channels() const { return m_channels; }
	const std::vector<Meter>& armed() const { return m_armed; }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	void describe();

	std::vector<Meter> m_channels;
	std::vector<Meter> m_armed;
};

//! Open the window (or raise the open one); it is deleted when closed.
LMMS_EXPORT InputChannelMeters* showInputMeters();
//! The open window, or nullptr.
LMMS_EXPORT InputChannelMeters* openInputMeters();

} // namespace lmms::gui

#endif // LMMS_GUI_INPUT_CHANNEL_METERS_H
