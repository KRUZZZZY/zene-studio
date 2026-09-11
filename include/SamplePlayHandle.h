
/*
 * SamplePlayHandle.h - play-handle for playing a sample
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

#ifndef LMMS_SAMPLE_PLAY_HANDLE_H
#define LMMS_SAMPLE_PLAY_HANDLE_H

#include "Sample.h"
#include "SampleWindow.h"
#include "WarpMarkers.h"
#include "PlayHandle.h"

namespace lmms
{


class PatternTrack;
class SampleClip;
class Track;


class LMMS_EXPORT SamplePlayHandle : public PlayHandle
{
public:
	SamplePlayHandle(Sample* sample, bool ownAudioBusHandle = true);
	SamplePlayHandle( const QString& sampleFile );
	//! Renders the clip's whole authored window from its start.
	SamplePlayHandle( SampleClip* clip );
	/*! Renders one snapshot of a clip's window, starting at `window.sourceIn`.
	 *
	 *  SampleTrack::play derives this from the transport position for each pass
	 *  and never writes the clip, so a trim survives the pass (Slice 0 of task
	 *  #611: docs/CLIP-CAPTURE-DESIGN.md §2.5, invariant I1). */
	SamplePlayHandle( SampleClip* clip, const SampleWindow& window );
	~SamplePlayHandle() override;

	inline bool affinityMatters() const override
	{
		return true;
	}


	void play( std::span<SampleFrame> buffer ) override;
	bool isFinished() const override;

	bool isFromTrack( const Track * _track ) const override;

	f_cnt_t totalFrames() const;
	inline f_cnt_t framesDone() const
	{
		return( m_frame );
	}
	void setDoneMayReturnTrue( bool _enable )
	{
		m_doneMayReturnTrue = _enable;
	}

	void setPatternTrack(PatternTrack* pt)
	{
		m_patternTrack = pt;
	}

private:
	Sample::PlaybackState m_state;
	//! The source window this handle renders, snapshotted at construction: nothing
	//! that happens to the clip or the Sample afterwards changes this handle's
	//! length (Slice 0, invariant I1).
	SampleWindow m_window;
	/*! The clip's warp map (#597), snapshotted for the same reason the window is:
	 *  a live handle renders the warp it was created with. A fixed-capacity value
	 *  copy - 1.5 KB of memcpy, no allocation, no lock (I8). */
	WarpMarkers m_warp;
	//! The clip's own rate at construction (leader/follower), and the rate that
	//! means "play at the source's natural speed". Their ratio is the resampler
	//! ratio this handle renders at when no marker governs the position.
	float m_baseFramesPerTick = 0.0f;
	float m_naturalFramesPerTick = 0.0f;
	/*! The handle's length in output frames when the clip is warped or leads:
	 *  the ticks the window spans under the mapping, at the output rate.
	 *  `m_rendersLinearly` keeps the historical expression for every clip that
	 *  is neither, so an unwarped project's arithmetic is unchanged. */
	f_cnt_t m_timelineFrames = 0;
	bool m_rendersLinearly = true;
	f_cnt_t m_frame = 0;
	Sample* m_sample = nullptr;
	Track* m_track = nullptr;
	PatternTrack* m_patternTrack = nullptr;
	bool m_doneMayReturnTrue = true;
	bool m_ownAudioBusHandle = false;

	//! The resampler ratio for the period about to be rendered: the local
	//! warp rate at the source frame this period starts on, over the natural
	//! rate. Exactly 1.0 for every clip with no markers and no source tempo.
	float warpRatio() const;
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_PLAY_HANDLE_H
