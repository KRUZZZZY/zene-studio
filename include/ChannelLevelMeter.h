/*
 * ChannelLevelMeter.h - the level of the mixer channel a Device panel's devices play into
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

#ifndef LMMS_GUI_CHANNEL_LEVEL_METER_H
#define LMMS_GUI_CHANNEL_LEVEL_METER_H

#include <utility>

#include <QString>
#include <QTimer>
#include <QWidget>

#include "lmms_export.h"

namespace lmms::gui
{

/*! 0.5.0 L10 (MX-101): the level of the channel whose devices the Device panel shows - two bars,
 *  -60..0 dBFS, from the channel's own RMS meter (the value mixer.strip_get reports as rms_left /
 *  rms_right), read without taking it from the mixer's own meters. It finds the channel again by
 *  its ch-<n> id on every refresh (20 a second while shown), so a deleted channel reads as silence. */
class LMMS_EXPORT ChannelLevelMeter : public QWidget
{
	Q_OBJECT
public:
	ChannelLevelMeter(const QString& channelId, QWidget* parent = nullptr);

	const QString& channelId() const { return m_channel; }
	//! The levels drawn now, in dBFS (left, right); -inf for silence.
	std::pair<float, float> levels() const { return {m_left, m_right}; }
	//! Read the channel's meter now.
	void refresh();

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	QString m_channel;
	float m_left;
	float m_right;
	QTimer m_timer;
};

} // namespace lmms::gui

#endif // LMMS_GUI_CHANNEL_LEVEL_METER_H
