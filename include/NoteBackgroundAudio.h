/*
 * NoteBackgroundAudio.h - an audio clip's waveform drawn behind the notes
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
#ifndef LMMS_GUI_NOTE_BACKGROUND_AUDIO_H
#define LMMS_GUI_NOTE_BACKGROUND_AUDIO_H

#include <QPointer>
#include <QString>

#include "SampleThumbnail.h"
#include "lmms_export.h"

class QPainter;
class QRect;

namespace lmms
{
class MidiClip;
class SampleBuffer;
class SampleClip;
}

namespace lmms::gui
{

class NoteGrid;

/*! 0.5.0 L07 (ED-081): the waveform of an audio clip drawn faintly behind the notes, at its place in
 *  the song, so a part can be written against a vocal or a loop. The clip is chosen by id, or "auto":
 *  the first audio clip of any track that sounds while the MIDI clip does. */
class LMMS_EXPORT NoteBackgroundAudio
{
public:
	//! The audio clip @a choice names ("" none, "auto", or a clip id) for @a midiClip, or nullptr.
	static SampleClip* resolve(const QString& choice, const MidiClip& midiClip);
	//! The first audio clip of any track that overlaps @a midiClip in time, or nullptr.
	static SampleClip* overlapping(const MidiClip& midiClip);

	//! Draw @a clip's waveform over @a area, placed by @a grid relative to @a midiClip.
	void paint(QPainter& p, const NoteGrid& grid, const MidiClip& midiClip, SampleClip* clip, const QRect& area) const;

private:
	// Rebuilt when the clip or its sample buffer changes (a new file, or the same path reloaded) -
	// building a thumbnail reads the whole sample. The thumbnail holds the buffer it was built
	// from, so m_buffer cannot dangle into a later buffer at the same address.
	mutable QPointer<SampleClip> m_clip;
	mutable const SampleBuffer* m_buffer = nullptr;
	mutable SampleThumbnail m_thumbnail;
};

} // namespace lmms::gui

#endif // LMMS_GUI_NOTE_BACKGROUND_AUDIO_H
