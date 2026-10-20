/*
 * TakeLaneRows.h - a sample track's take lanes as rows in the arrangement
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

#ifndef LMMS_GUI_TAKE_LANE_ROWS_H
#define LMMS_GUI_TAKE_LANE_ROWS_H

#include <vector>

#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "LmmsTypes.h"
#include "TakeWaveform.h"
#include "lmms_export.h"

class QPainter;

namespace lmms
{
class Clip;
class Track;
}

namespace lmms::gui
{

class TrackView;

/*! 0.5.0 L03 (Q050-123): a sample track with take lanes shows one row per lane IN the arrangement,
 *  each take clip at its own start and length with its waveform drawn on the arranger's time
 *  mapping. The rows lie over the track's content widget and take no mouse input: the clip views
 *  underneath keep their gestures. The model stays the source - the rows are derived from the
 *  track's lanes and its clips' lane tags every time the timer sees a change, so a comp, an undo
 *  or a project load redraws without adding saved display state.
 *
 *  A track without lanes has no rows (the widget hides), which is what leaves every ordinary
 *  track's arrangement exactly as it was. */
class LMMS_EXPORT TakeLaneRows : public QWidget
{
	Q_OBJECT
public:
	struct Band
	{
		int lane = 0;
		tick_t begin = 0;
		tick_t end = 0;
		QRect rect;          //!< where the take is drawn, in this widget's pixels
		//! Guarded: an undo or a project load replaces the track's clips; a paint between that
		//! and the next watch must not follow a deleted clip.
		QPointer<Clip> clip;

		bool operator==(const Band&) const = default;
	};

	TakeLaneRows(Track* track, TrackView* view);

	//! The take clips drawn on the last refresh, with the rectangle each occupies.
	const std::vector<Band>& bands() const { return m_bands; }
	//! The lanes shown, in the model's order.
	const std::vector<int>& lanes() const { return m_lanes; }
	//! Re-read the model now (the timer calls it; a test can too).
	void refreshNow();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	void watch();
	int xOfTick(tick_t tick) const;
	int rowOfLane(int lane) const;

	Track* m_track;
	TrackView* m_view;
	QTimer m_timer;
	TakeWaveform m_waves;
	std::vector<Band> m_bands;
	std::vector<int> m_lanes;
};

} // namespace lmms::gui

#endif // LMMS_GUI_TAKE_LANE_ROWS_H
