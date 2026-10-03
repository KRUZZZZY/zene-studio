/*
 * NoteBackgroundAudio.cpp - an audio clip's waveform drawn behind the notes
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
#include "NoteBackgroundAudio.h"

#include <algorithm>

#include <QPainter>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "MidiClip.h"
#include "NoteGrid.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms::gui
{

SampleClip* NoteBackgroundAudio::resolve(const QString& choice, const MidiClip& midiClip)
{
	if (choice.isEmpty()) { return nullptr; }
	if (choice == QLatin1String("auto")) { return overlapping(midiClip); }
	control::ClipRef ref;
	ControlResult ignored;
	return control::resolveClip(choice, &ref, &ignored) ? dynamic_cast<SampleClip*>(ref.clip) : nullptr;
}

SampleClip* NoteBackgroundAudio::overlapping(const MidiClip& midiClip)
{
	const tick_t start = midiClip.startPosition().getTicks();
	const tick_t end = start + midiClip.length().getTicks();
	for (const Track* track : Engine::getSong()->tracks())
	{
		for (Clip* clip : track->getClips())
		{
			auto* audio = dynamic_cast<SampleClip*>(clip);
			const tick_t from = clip->startPosition().getTicks();
			if (audio != nullptr && from < end && from + clip->length().getTicks() > start) { return audio; }
		}
	}
	return nullptr;
}

void NoteBackgroundAudio::paint(QPainter& p, const NoteGrid& grid, const MidiClip& midiClip, SampleClip* clip, const QRect& area) const
{
	if (clip == nullptr || clip->sample().sampleSize() == 0) { return; }
	if (m_clip != clip || m_buffer != clip->sample().buffer().get())
	{
		m_clip = clip;
		m_buffer = clip->sample().buffer().get();
		m_thumbnail = SampleThumbnail(clip->sample());
	}
	// Ticks relative to the MIDI clip: where the sample begins (a clip trimmed from the left starts
	// inside it) and the span the audio clip actually sounds.
	const double shift = clip->startPosition().getTicks() - midiClip.startPosition().getTicks();
	const double sampleStart = shift + clip->startTimeOffset().getTicks();
	const int sampleLeft = static_cast<int>(grid.xOfTick(sampleStart));
	const int sampleRight = static_cast<int>(grid.xOfTick(sampleStart + clip->sampleLength().getTicks()));
	const int clipLeft = static_cast<int>(grid.xOfTick(shift));
	const int clipRight = static_cast<int>(grid.xOfTick(shift + clip->length().getTicks()));
	const QRect sampleRect(sampleLeft, area.top(), std::max(1, sampleRight - sampleLeft), area.height());
	const QRect viewport = QRect(QPoint(clipLeft, area.top()), QPoint(clipRight, area.bottom())).intersected(area);
	if (viewport.isEmpty()) { return; }
	p.save();
	QColor wave = tokens::palette().textMuted;
	wave.setAlpha(70);
	p.setPen(wave);
	m_thumbnail.visualize({.sampleRect = sampleRect, .viewportRect = viewport, .amplification = clip->sample().amplification(),
		.reversed = clip->sample().reversed()}, p);
	p.restore();
}

} // namespace lmms::gui
