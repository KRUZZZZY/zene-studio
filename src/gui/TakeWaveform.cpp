/*
 * TakeWaveform.cpp - one take's audio waveform, cached for the lane rows and the comp strip
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

#include "TakeWaveform.h"

#include <QPainter>

#include "SampleBuffer.h"
#include "SampleClip.h"

namespace lmms::gui
{

void TakeWaveform::paint(QPainter& painter, const SampleClip& clip, const QRect& clipRect,
	const QRect& viewport, const QColor& color)
{
	if (clipRect.isEmpty() || viewport.isEmpty()) { return; }
	if (clip.sample().sampleSize() <= 0) { return; }

	Entry& entry = m_cache[&clip];
	const auto buffer = clip.sample().buffer();
	if (entry.buffer != buffer)
	{
		entry.buffer = buffer;
		entry.thumbnail = SampleThumbnail{clip.sample()};
	}

	const QRect renderRect = clipRect.intersected(viewport);
	if (renderRect.isEmpty()) { return; }

	painter.save();
	painter.setClipRect(viewport);
	painter.setPen(color);
	const SampleThumbnail::VisualizeParameters parameters{
		.sampleRect = clipRect,
		.viewportRect = viewport,
	};
	entry.thumbnail.visualize(parameters, painter);
	painter.restore();
}

} // namespace lmms::gui
