/*
 * SessionAudioCells.h - a launched AUDIO cell renders its clip (0.5.0 L02, WF-055 / WF-054)
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
 */

#ifndef LMMS_SESSION_AUDIO_CELLS_H
#define LMMS_SESSION_AUDIO_CELLS_H

#include <atomic>
#include <cstdint>

#include <QString>

#include "LmmsTypes.h"
#include "PlayHandle.h"
#include "lmms_export.h"

namespace lmms
{

class SampleBuffer;
class SampleTrack;
class Song;

/*! The playback path of AUDIO cells (docs/SESSION-AUDIO-RECORD.md section "Playback"). A MIDI cell renders a
 *  pattern through the pattern store; an audio cell had no path at all - the slot carried a file and a launch
 *  only silenced its column. Now a launched audio cell on a SAMPLE track's column plays its file through that
 *  track - its devices, fader, pan and mixer channel - from the clip's top at the launch, looping its length.
 *
 *  ONE PERSISTENT HANDLE PER SAMPLE TRACK that holds an audio cell, made on the model thread the first time
 *  a cell of its column is published and kept until the track goes (SampleTrack's destructor removes it, and
 *  the song-stop clear keeps it, as it keeps an input monitor). Each period it reads the scheduler's view of
 *  its column - what plays, from which launch - and copies the clip's frames with wrap-around: no allocation,
 *  no lock, no resampler on the audio thread. The clip was converted to the engine's rate when it was
 *  loaded, on the model thread. */
class LMMS_EXPORT SessionCellPlayHandle : public PlayHandle
{
public:
	explicit SessionCellPlayHandle(SampleTrack* track);

	void play(std::span<SampleFrame> buffer) override;
	bool isFinished() const override { return false; }
	bool isFromTrack(const Track* track) const override;

	//! Frames of a clip rendered since construction. Any thread; relaxed.
	std::uint64_t framesRendered() const noexcept { return m_framesRendered.load(std::memory_order_relaxed); }

private:
	SampleTrack* m_track;
	const SampleBuffer* m_clip = nullptr;  //!< the clip of the launch being played
	int m_scene = -1;
	tick_t m_launchTick = -1;
	std::size_t m_position = 0;
	std::atomic<std::uint64_t> m_framesRendered{0};
};

namespace sessionaudio
{

/*! Model thread: the clip of the audio cell whose file is @a source, loaded (and converted to the engine's
 *  rate) the first time, and a play handle for @a column's track when it is a sample track. nullptr when the
 *  file cannot be read or the column has no sample track - the cell then stays silent, as before. Never
 *  throws. The pointer stays valid until the project is closed twice over (retire()). */
LMMS_EXPORT const SampleBuffer* prepare(Song& song, int column, const QString& source) noexcept;

/*! Model thread, with no period rendering (Song::clearProject holds the model-change lock): the clips of
 *  the project being closed are retired, and the ones retired at the previous close are freed - by then the
 *  scheduler's reset has dropped every reference the audio thread had to them. */
LMMS_EXPORT void retire() noexcept;

//! Clips currently held (loaded for the open project). Model thread.
LMMS_EXPORT int heldClips() noexcept;

} // namespace sessionaudio

} // namespace lmms

#endif // LMMS_SESSION_AUDIO_CELLS_H
