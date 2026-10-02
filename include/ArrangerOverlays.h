/*
 * ArrangerOverlays.h - what the 0.5.0 arranger draws over the inherited song editor
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

#ifndef LMMS_GUI_ARRANGER_OVERLAYS_H
#define LMMS_GUI_ARRANGER_OVERLAYS_H

#include <QPointer>
#include <QWidget>

#include "lmms_export.h"

namespace lmms::gui
{

class SongEditor;
class TimeLineWidget;

/*! ED-048: the project's markers, drawn over the arranger's ruler - a flag and the name, in the
 *  marker's colour - from ProjectMarkers. A child of the ruler that follows its size and repaints
 *  with it; transparent to the mouse, so the ruler keeps every gesture it had. */
class LMMS_EXPORT MarkerRulerOverlay : public QWidget
{
	Q_OBJECT
public:
	explicit MarkerRulerOverlay(TimeLineWidget* ruler);

	//! The x of each marker drawn on the last paint (a test's view of it).
	QList<int> drawnMarkers() const { return m_drawn; }

protected:
	void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	QPointer<TimeLineWidget> m_ruler;
	QList<int> m_drawn;
};

//! Put the arranger's overlays on the song editor once (the shell calls it at start-up).
LMMS_EXPORT void installArrangerOverlays(SongEditor* editor);

} // namespace lmms::gui

#endif // LMMS_GUI_ARRANGER_OVERLAYS_H
