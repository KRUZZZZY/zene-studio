/*
 * TakeWaveform.h - one take's audio waveform, cached for the lane rows and the comp strip
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

#ifndef LMMS_GUI_TAKE_WAVEFORM_H
#define LMMS_GUI_TAKE_WAVEFORM_H

#include <memory>

#include <QColor>
#include <QHash>
#include <QMetaObject>
#include <QRect>

#include "SampleThumbnail.h"
#include "lmms_export.h"

class QPainter;

namespace lmms
{
class SampleBuffer;
class SampleClip;
}

namespace lmms::gui
{

/*! 0.5.0 L03 (Q050-123): the waveform of one take, drawn into the take clip's own rectangle.
 *
 *  Both places a take is shown - a lane row in the arrangement and a lane in the comp strip - use
 *  this one painter, so the two cannot disagree about what a take looks like. The x positions
 *  come from the caller's timeline mapping (the clip's rectangle), which is what keeps the
 *  waveform aligned to the ruler when the arranger scrolls or zooms: this class never invents a
 *  time mapping of its own.
 *
 *  Thumbnails are generated once per buffer and kept here; a clip's audio changing (a new take,
 *  an undo) replaces the entry because its SampleBuffer is a different buffer. GUI thread only:
 *  no allocation, locking or painting happens on the audio thread. */
class LMMS_EXPORT TakeWaveform
{
public:
	~TakeWaveform();

	//! Paint @a clip's waveform inside @a clipRect, clipped to @a viewport. Nothing is drawn for
	//! a clip without loaded audio. Call on the GUI thread.
	void paint(QPainter& painter, const SampleClip& clip, const QRect& clipRect, const QRect& viewport,
		const QColor& color);

private:
	struct Entry
	{
		std::shared_ptr<const SampleBuffer> buffer;
		SampleThumbnail thumbnail;
	};
	QHash<const SampleClip*, Entry> m_cache;
	//! A cached thumbnail owns its sample buffer; remove it when its clip is deleted, and disconnect
	//! these callbacks if the widget that owns this painter is deleted first.
	QHash<const SampleClip*, QMetaObject::Connection> m_clipDestroyedConnections;
};

} // namespace lmms::gui

#endif // LMMS_GUI_TAKE_WAVEFORM_H
