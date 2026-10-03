/*
 * LevelBar.h - a thin level meter that reads its level from a function
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
#ifndef LMMS_GUI_LEVEL_BAR_H
#define LMMS_GUI_LEVEL_BAR_H

#include <functional>

#include <QWidget>

#include "lmms_export.h"

class QTimer;

namespace lmms
{
class Track;
}

namespace lmms::gui
{

/*! 0.5.0 L08/L09: a thin vertical meter, -60..0 dB, in the meter colours. Twenty times a second while it is
 *  shown it asks @a level for a linear level (the interface thread; the source reads atomics the mixer worker
 *  writes) and falls back gently from its last peak; hidden, it polls nothing. Its tooltip reads the level in dB.
 *  Used by the Inspector's send rows (MX-053) and the track headers (MX-011). */
class LMMS_EXPORT LevelBar : public QWidget
{
	Q_OBJECT
public:
	LevelBar(std::function<float()> level, const QString& name, QWidget* parent);

protected:
	bool event(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	std::function<float()> m_source;
	QString m_name;
	QTimer* m_timer;
	float m_level = 0.f;
};

//! MX-011: a LevelBar on @a track's mixer channel, after its fader (the channel's RMS); silent for a track with none.
LMMS_EXPORT LevelBar* trackLevelBar(Track* track, QWidget* parent);

} // namespace lmms::gui

#endif // LMMS_GUI_LEVEL_BAR_H
