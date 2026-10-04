/*
 * ArrangerEditPreview.h - what a clip's drag will do, drawn before the drop
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

#ifndef LMMS_GUI_ARRANGER_EDIT_PREVIEW_H
#define LMMS_GUI_ARRANGER_EDIT_PREVIEW_H

#include <QPointer>
#include <QRect>
#include <QVector>
#include <QWidget>

#include "ArrangementEdits.h"
#include "lmms_export.h"

namespace lmms::gui
{

class ClipView;
class SongEditor;

/*! 0.5.0 theme 2 (owner decision 2026-10-03: a dragged clip trims what it lands on): while a clip is
 *  dragged in the arranger, this overlay shows what the drop will do before the button is released -
 *  a GHOST outline where the clip started, every part of another clip the overlap mode will TRIM away
 *  (or crossfade over), a locked clip under it in the refusal colour (the drop will be refused), and a
 *  SNAP LINE labelled with what the clip snapped to (grid, clip edge or marker). It paints over the
 *  arranger's viewport and never takes a mouse event; ClipView tells it about the drag
 *  (ClipView::mouseMoveEvent / mouseReleaseEvent). Interface only. */
class LMMS_EXPORT ArrangerEditPreview : public QWidget
{
	Q_OBJECT
public:
	explicit ArrangerEditPreview(SongEditor* editor);

	//! @a view's clip is being dragged: it started at @a from, and its last snap is @a snap.
	static void showDrag(ClipView* view, tick_t from, const arrangement::SnapHit& snap);
	//! The drag of @a view ended (dropped or abandoned): the preview goes.
	static void endDrag(ClipView* view);

	//! A test's view of the last paint: the ghost, each previewed effect, the snap line's x (-1 for none)
	//! and whether the drop would be refused.
	QRect ghostRect() const { return m_ghost; }
	QVector<QRect> effectRects() const { return m_effects; }
	int snapLineX() const { return m_snapX; }
	bool refused() const { return m_refused; }

protected:
	void paintEvent(QPaintEvent* event) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	//! The overlay's x of @a tick, measured from the dragged clip view's left edge.
	int xOf(tick_t tick) const;
	void layout();
	void paintEffects(QPainter& p) const;
	void paintSnap(QPainter& p) const;

	QPointer<SongEditor> m_editor;
	QPointer<ClipView> m_view;
	tick_t m_from = 0;
	arrangement::SnapHit m_snap;
	QRect m_row;
	QRect m_ghost;
	QVector<QRect> m_effects;
	QVector<bool> m_lockedEffects;
	int m_snapX = -1;
	bool m_refused = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_ARRANGER_EDIT_PREVIEW_H
