/*
 * SampleClipView.h
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_GUI_SAMPLE_CLIP_VIEW_H
#define LMMS_GUI_SAMPLE_CLIP_VIEW_H

#include <vector>
#include "ClipView.h"

#include "SampleClipOverlays.h"
#include "SampleClipWarpEdit.h"
#include "SampleThumbnail.h"

namespace lmms
{

class SampleClip;

namespace gui
{


class SampleClipView : public ClipView
{
	Q_OBJECT

public:
	SampleClipView( SampleClip * _clip, TrackView * _tv );
	~SampleClipView() override = default;

	// Q050-063: warp marker authoring over the waveform. The markers are always drawn; while warp editing is on
	// a double-click adds one (warp.add), a double-click on one removes it (warp.remove) and a drag moves it
	// (warp.move on release), snapped to the grid or a transient, with the waveform previewed as it goes.
	bool warpEditing() const { return m_warpEditing; }
	void setWarpEditing(bool editing);
	WarpSnap warpSnap() const { return m_warpSnap; }
	void setWarpSnap(WarpSnap snap) { m_warpSnap = snap; }
	//! True between the press on a marker and its release.
	bool warpDragging() const { return m_warpDrag >= 0; }
	//! The geometry the overlays are drawn and hit in: ticks to pixels from pixel @a xOrigin.
	ClipOverlayFrame warpOverlayFrame(int xOrigin) { return overlayFrame(xOrigin); }
	//! The markers as drawn: the clip's own map, with the marker being dragged at its proposed position.
	std::vector<WarpGuide> shownWarpGuides() const;
	//! The waveform stretches as drawn (empty for an unwarped or reversed clip, which draw as ever), from pixel @a xOrigin.
	std::vector<WarpWaveformSegment> shownWarpSegments(int xOrigin = 0);

public slots:
	void updateSample();
	void reverseSample();
	void setAutomationGhost();
#ifdef LMMS_HAVE_STEM_SPLIT
	void splitToStems();
#endif

	/*! The transient ticks the overlay draws, relative to the clip's own start.
	 *
	 *  The frames are analysed once per audio and per analysed range
	 *  (m_transientFrames) and placed on the timeline again whenever the clip's
	 *  timing changes. Public so the overlay's cache invalidation can be
	 *  regression-tested (a reversal moves the analysed range). */
	const std::vector<int>& transientTicks();



protected:
	void constructContextMenu(QMenu* cm) override;
	void mousePressEvent( QMouseEvent * _me ) override;
	void mouseReleaseEvent( QMouseEvent * _me ) override;
	void mouseMoveEvent( QMouseEvent * _me ) override;
	void dragEnterEvent( QDragEnterEvent * _dee ) override;
	void dropEvent( QDropEvent * _de ) override;
	void mouseDoubleClickEvent( QMouseEvent * ) override;
	void paintEvent( QPaintEvent * ) override;


private:
	SampleClip * m_clip;
	SampleThumbnail m_sampleThumbnail;
	QPixmap m_paintPixmap;
	long m_paintPixmapXPosition;
	// Q050-063: the warp marker authoring state.
	bool m_warpEditing = false;
	WarpSnap m_warpSnap = WarpSnap::Grid;
	int m_warpDrag = -1;           //!< the index of the marker being dragged, or -1
	f_cnt_t m_warpDragFrame = 0;   //!< the source frame it pins (identity for warp.move)
	tick_t m_warpDragStart = 0;    //!< its offset when the drag began
	tick_t m_warpDragOffset = 0;   //!< the offset the drag proposes
	int warpShift() const;         //!< the clip's startTimeOffset in ticks: the warp origin from the clip's left edge
	WarpMarkers shownWarpMap() const;
	bool startWarpDrag(QMouseEvent* me);
	void dragWarpMarker(QMouseEvent* me);
	void finishWarpDrag();
	void addWarpMarkerAt(int x);
	void removeWarpMarkerAt(const QPoint& pos);
	void addWarpEditMenu(QMenu* cm);
	bool paintWarpedWaveform(QPainter& p, const QRect& viewPortRect, int spacing, int xOrigin);
	// 0.5.0 L05 (ED-173): the fade handle being dragged and the length it shows.
	FadeHandle m_fadeDrag = FadeHandle::None;
	int m_fadeDragTicks = 0;
	/*! Q050-125: what a press on the waveform took, and what the drag in progress is editing. The
	 *  gesture is previewed in `m_dragEdits` (what the paint shows) and sent as ONE registered
	 *  command on release: `clip.set_gain`, or `clip.gain_envelope_set` for a point. */
	enum class GainDrag { None, Handle, Point };
	GainDrag m_gainDrag = GainDrag::None;
	int m_gainDragPoint = -1;
	float m_gainDragStartDb = 0.f;
	//! The press's own position: the handle's drag level is measured from its y, and a pointer that
	//! never leaves it has not moved the edit (a hover move at the same pixel is not a drag).
	QPoint m_gainDragPress{};
	//! True once the pointer has actually MOVED during the drag: a press/release without a move must
	//! write nothing, or its no-op checkpoint would shadow the edit before it.
	bool m_gainDragMoved = false;
	ClipEdits m_dragEdits;
	float pixelsPerTickNow();
	ClipOverlayFrame overlayFrame(int xOrigin);
	//! The clip's edits as drawn: a fade or gain drag in progress shown before it is sent.
	ClipEdits shownEdits() const;
	//! The fade handle at @a pos, or None - always None on a locked clip.
	FadeHandle fadeHandleUnder(const QPoint& pos);
	bool startFadeDrag(QMouseEvent* me);
	//! Q050-125: a press on the clip-gain handle or on an envelope point starts that drag.
	bool startGainDrag(QMouseEvent* me);
	//! Q050-125: what the gain/handle drag in progress would write, and the command that writes it.
	void finishGainDrag();
	//! Q050-125: the waveform's gain multiplier at @a t (0..1 across the drawn audio), for the amplitude.
	float gainAtNormalised(float t);
	// ED-150: the transients drawn when arrangement.modes show_transients is on, analysed once per audio
	// and per analysed range (m_transientFrames) and placed as ticks again when the clip's timing changes.
	std::vector<f_cnt_t> m_transientFrames;
	QString m_transientAudioKey;
	std::vector<int> m_transientTicks;
	QString m_transientKey;
} ;


} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_SAMPLE_CLIP_VIEW_H
