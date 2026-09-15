
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
#include "AudioStretcher.h"
#include "ClipEdits.h"
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
	/*! Row 30: render a rate change through the pitch-preserving stretcher
	 *  (`WarpStretchMode::PreservePitch`) instead of the resampler. Snapshotted
	 *  at construction like the window and the warp - a live handle renders the
	 *  mode it was created with. False for every clip that did not ask, which is
	 *  every clip in every project written before this feature. */
	bool m_preservePitch = false;
	/*! The stretcher, prepared only for a clip that asked for it (a no-op for
	 *  everyone else: `isPrepared()` false means `process()` returns 0 without
	 *  touching the source). Fixed size, allocated with the handle - nothing on
	 *  the render path allocates (I8). */
	AudioStretcher m_stretcher;
	/*! The clip's fades and its gain, snapshotted at construction exactly as the
	 *  window and the warp are (invariant I1): a live handle renders the envelope
	 *  it was created with, and nothing the control thread does to the clip
	 *  afterwards can move it. A POD value copy - no allocation, no lock (I8). */
	ClipEdits m_edits;
	//! The clip's whole rendered span in output frames, and where this pass
	//! starts inside it. The envelope is measured against these, not against
	//! `m_frame`, because a pass that begins in the middle of a clip has to
	//! continue the ramp it interrupted rather than restart it.
	f_cnt_t m_clipFrames = 0;
	f_cnt_t m_envelopeStart = 0;
	//! The two fade lengths in output frames, derived once from their tick
	//! lengths so the audio path never divides by a clip length.
	f_cnt_t m_fadeInFrames = 0;
	f_cnt_t m_fadeOutFrames = 0;
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

	/*! Renders \a frames output frames of a clip whose rate change is
	 *  pitch-preserving (row 30), reading the clip's window through
	 *  `m_stretcher` instead of `Sample::play`'s resampler.
	 *
	 *  The source frames per output frame are
	 *  `(1 / warpRatio()) / (sampleRateRatio * freqRatio)` — the reciprocal of
	 *  the converter ratio the resample path would hand `AudioResampler`, so
	 *  both modes put the same source frames on the same timeline and differ
	 *  only in what they do to the waveform's period. The handle's playback
	 *  state is advanced to the stretcher's own analysis cursor, which is what
	 *  the next period's rate is derived from. */
	void renderPreservingPitch(SampleFrame* dst, f_cnt_t frames);

	/*! Multiplies the clip's fade-and-gain envelope into the frames this period
	 *  just rendered. Never called for a neutral clip, so the default path pays
	 *  nothing at all for this feature - not a branch, not a multiply. */
	void applyClipEdits(SampleFrame* buffer, f_cnt_t frames) const;
	//! Derives m_clipFrames / m_envelopeStart / the two fade lengths from the
	//! clip and the window this handle renders (construction time only).
	void snapshotClipEdits(const SampleClip* clip, const SampleWindow& window);
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_PLAY_HANDLE_H
