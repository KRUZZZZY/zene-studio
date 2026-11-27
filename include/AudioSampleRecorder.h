/*
 * AudioSampleRecorder.h - device-class that implements recording
 *                         audio-buffers into RAM
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_AUDIO_SAMPLE_RECORDER_H
#define LMMS_AUDIO_SAMPLE_RECORDER_H

#include <QList>
#include <QPair>
#include <cstddef>
#include <memory>

#include "AudioDevice.h"

namespace lmms
{

class SampleBuffer;


class AudioSampleRecorder : public AudioDevice
{
public:
	AudioSampleRecorder( const ch_cnt_t _channels, bool & _success_ful, AudioEngine* audioEngine );
	~AudioSampleRecorder() override;

	f_cnt_t framesRecorded() const;
	/*! The recorded take as one buffer, or nullptr when its frame total cannot be
	 *  represented by the destination (nothing is allocated in that case). */
	std::shared_ptr<const SampleBuffer> createSampleBuffer();

	/*! Whether the @a n frame counts at @a counts lay out end to end inside a destination of
	 *  @a capacity frames, and their sum when they do. The sum is checked, never wrapped: a
	 *  total the destination cannot hold is refused instead of overflowing the offset the copy
	 *  indexes with (cluster new-p10-505). On refusal @a total is left untouched, so a caller
	 *  cannot size or index a buffer with a partial sum. Static and allocation-free - the
	 *  boundaries (two INT_MAX blocks, a count at SIZE_MAX) are reachable by a unit test without
	 *  allocating the recording. */
	static bool totalFramesWithin( const f_cnt_t* counts, std::size_t n,
					std::size_t capacity, std::size_t& total ) noexcept;

private:
	void writeBuffer(const SampleFrame* _ab, const f_cnt_t _frames);

	using BufferList = QList<QPair<SampleFrame*, f_cnt_t>>;
	BufferList m_buffers;

} ;

} // namespace lmms

#endif // LMMS_AUDIO_SAMPLE_RECORDER_H
