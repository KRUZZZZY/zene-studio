/*
 * SamplePlayHandle.cpp - implementation of class SamplePlayHandle
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

#include "SamplePlayHandle.h"

#include <algorithm>

#include "AudioEngine.h"
#include "AudioBusHandle.h"
#include "Engine.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "SampleTrack.h"

namespace lmms
{

SamplePlayHandle::SamplePlayHandle(Sample* sample, bool ownAudioBusHandle)
	: PlayHandle(Type::SamplePlayHandle)
	, m_sample(sample)
	, m_ownAudioBusHandle(ownAudioBusHandle)
{
	// The window this handle renders, taken once here. For the preview and
	// metronome paths (SamplePlayHandle.cpp:106-108) that is the sample's own
	// range, which is exactly what they rendered before Slice 0; the clip
	// constructors below replace it with the clip's authored window.
	m_window = { static_cast<f_cnt_t>(m_sample->startFrame()),
		static_cast<f_cnt_t>(m_sample->endFrame()) };
	m_state.setFrameIndex(m_sample->startFrame());
	if (ownAudioBusHandle)
	{
		setAudioBusHandle(new AudioBusHandle("SamplePlayHandle", false));
	}
}




SamplePlayHandle::SamplePlayHandle( const QString& sampleFile ) :
	SamplePlayHandle(new Sample(SampleBuffer::fromFile(sampleFile)), true)
{
}




SamplePlayHandle::SamplePlayHandle( SampleClip* clip ) :
	SamplePlayHandle(clip, clip->sampleWindow())
{
}




SamplePlayHandle::SamplePlayHandle( SampleClip* clip, const SampleWindow& window ) :
	SamplePlayHandle(&clip->sample(), false)
{
	m_track = clip->getTrack();
	setAudioBusHandle(((SampleTrack *)clip->getTrack())->audioBusHandle());

	// The clip's authored window is read, never written (Slice 0, I1). Sample's
	// frame fields already mirror it (SampleClip::setSampleWindow), so this only
	// has to start the render at the frame this pass begins on.
	m_window = window;
	m_state.setFrameIndex(static_cast<int>(m_window.sourceIn));

	// #597: the warp is snapshotted here for exactly the reason the window is -
	// a live handle renders the mapping it was created with, and nothing the
	// control thread does to the clip afterwards can move it. Both are value
	// copies of PODs, so this is audio-thread-safe (I8).
	m_warp = clip->warpMarkers();
	m_baseFramesPerTick = clip->clipFramesPerTick();
	m_naturalFramesPerTick = Engine::framesPerTick(m_sample->sampleRate());
	m_rendersLinearly = clip->rendersLinearly();
	if (!m_rendersLinearly)
	{
		// The handle's length is how long the window lasts ON THE TIMELINE under
		// the mapping, which is what a warp (or a tempo-leading clip) changes:
		// a 2x segment consumes its source frames in half the output frames.
		m_timelineFrames = static_cast<f_cnt_t>(clip->windowTicksFor(window)
			* Engine::framesPerTick(Engine::audioEngine()->outputSampleRate()));
	}
}




SamplePlayHandle::~SamplePlayHandle()
{
	if(m_ownAudioBusHandle)
	{
		delete audioBusHandle();
		delete m_sample;
	}
}




void SamplePlayHandle::play( std::span<SampleFrame> buffer )
{
	//play( 0, _try_parallelizing );
	if( framesDone() >= totalFrames() )
	{
		zeroSampleFrames(buffer.data(), buffer.size());
		return;
	}

	SampleFrame* workingBuffer = buffer.data();
	f_cnt_t frames = buffer.size();

	// apply offset for the first period
	if( framesDone() == 0 )
	{
		zeroSampleFrames(buffer.data(), offset());
		workingBuffer += offset();
		frames -= offset();
	}

	if( !( m_track && m_track->isMuted() )
				&& !(m_patternTrack && m_patternTrack->isMuted()))
	{
/*		StereoVolumeVector v =
			{ { m_volumeModel->value() / DefaultVolume,
				m_volumeModel->value() / DefaultVolume } };*/
		// SamplePlayHandle always plays the sample at its original pitch;
		// it is used only for previews, SampleTracks and the metronome.
		// #597: the fifth argument is the warp's rate for this period - the
		// local source-frames-per-output-frame, as a multiple of natural
		// playback. It is exactly 1.0 for every clip without markers and
		// without a declared source tempo, which is the value this call
		// already passed.
		if (!m_sample->play(workingBuffer, &m_state, frames, Sample::Loop::Off, warpRatio()))
		{
			zeroSampleFrames(workingBuffer, frames);
		}
	}

	m_frame += frames;
}




float SamplePlayHandle::warpRatio() const
{
	// No markers and no source tempo: the natural rate, and the pre-#597 value
	// of this argument, so the resampler ratio is bit for bit what it was.
	if (m_warp.empty() || m_naturalFramesPerTick <= 0.0f) { return 1.0f; }

	// The rate that governs from the source frame this period starts on. The
	// render has to pick one rate per period because `Sample::play` takes one
	// ratio per call; segments are half-open to the right, so a rate change
	// takes effect at the marker, and within a segment it is exact.
	const auto frame = std::clamp(static_cast<f_cnt_t>(std::max(0, m_state.frameIndex())),
		m_window.sourceIn, m_window.sourceOut);
	const auto rate = m_warp.framesPerTickAt(frame, m_baseFramesPerTick);
	return rate > 0.0f ? rate / m_naturalFramesPerTick : 1.0f;
}




bool SamplePlayHandle::isFinished() const
{
	return framesDone() >= totalFrames() && m_doneMayReturnTrue == true;
}




bool SamplePlayHandle::isFromTrack( const Track * _track ) const
{
	return m_track == _track || m_patternTrack == _track;
}




f_cnt_t SamplePlayHandle::totalFrames() const
{
	// The length comes from the window snapshotted at construction, not from the
	// sample's live frame fields: a later playback pass on another clip (or on
	// this one) cannot change how long this handle plays for.
	if (!m_rendersLinearly)
	{
		// A warped or tempo-leading clip: the window's own timeline span, which
		// the mapping is what decides (#597).
		return m_timelineFrames;
	}
	return m_window.length() *
			(static_cast<float>(Engine::audioEngine()->outputSampleRate()) / m_sample->sampleRate());
}


} // namespace lmms
