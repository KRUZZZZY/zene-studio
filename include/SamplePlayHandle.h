
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
#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <utility>

#include "AudioStretcher.h"
#include "RubberBandStretch.h"
#include "ClipEdits.h"
#include "SampleWindow.h"
#include "WarpMarkers.h"
#include "PlayHandle.h"
#include "TakeLane.h"

namespace lmms
{


class CompSlipReaders;
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
		// Pool slots are constructed on the engine/control thread and reused by
		// whichever callback or worker renders the next period.
		return !isPooledVoice();
	}


	void play( std::span<SampleFrame> buffer ) override;
	bool isFinished() const override;

	bool isFromTrack( const Track * _track ) const override;

	f_cnt_t totalFrames() const;

	/*! An offline render of the clip's own audio (clip.consolidate) sounds whatever the
	 *  track's mute says: consolidating a muted track must not bake silence. Set on a
	 *  handle that never reaches the engine; the live path never sets it. */
	void setIgnoresTrackMute(bool ignores) { m_ignoresTrackMute = ignores; }

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
	//! These methods are only for AudioEngine's fixed sample-voice pool.
	static constexpr std::uint16_t NotPooled = std::numeric_limits<std::uint16_t>::max();
	void setPoolSlot(std::uint16_t slot, bool frozenTake) noexcept
	{
		m_poolSlot = slot;
		m_frozenTakePoolVoice = frozenTake;
	}
	bool isPooledVoice() const noexcept { return m_poolSlot != NotPooled; }
	std::uint16_t poolSlot() const noexcept { return m_poolSlot; }
	bool isFrozenTakePoolVoice() const noexcept { return isPooledVoice() && m_frozenTakePoolVoice; }
	Track* pooledTrack() const noexcept { return isFrozenTakePoolVoice() ? m_track : nullptr; }
	void armPooledVoice(std::shared_ptr<const SampleBuffer> buffer, int sourceIn, int sourceOut,
		float amplification, f_cnt_t offset, Track* track) noexcept;
	void resetPooledVoice() noexcept;
	//! Prepare/adopt/discard the handle state and its comp-slip readers together.
	auto prepareConverterMode(AudioResampler::Mode mode) noexcept -> bool;
	void adoptPreparedConverterMode() noexcept;
	void discardPreparedConverterMode() noexcept;

private:
	SamplePlayHandle(Sample* sample, bool ownAudioBusHandle, AudioResampler::RealtimeVoiceTag realtimeVoice);
	void initializeSampleState();

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
	/*! Owner decision 12: the Rubber Band voice this handle claimed from its
	 *  clip's pool, or nullptr (WSOLA renders). Borrowed, never owned: released
	 *  dirty in the destructor, reset by the pool's recycler off this thread. */
	RubberBandVoice* m_rubberBand = nullptr;
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
	double m_outputFramesPerTick = 1.0;  //!< ED-042: frames per tick, for the gain envelope
	f_cnt_t m_envelopeStart = 0;
	//! The two fade lengths in output frames, derived once from their tick
	//! lengths so the audio path never divides by a clip length.
	f_cnt_t m_fadeInFrames = 0;
	f_cnt_t m_fadeOutFrames = 0;
	//! Owner decision 2026-10-08: how far inside the clip the fade-in starts and the fade-out ends, in output
	//! frames (0: at the clip's edges), and - only when either is set - the short automatic edge fade, applied
	//! apart from the ramps so it stays on the clip's real edges.
	f_cnt_t m_fadeInOffsetFrames = 0;
	f_cnt_t m_fadeOutOffsetFrames = 0;
	f_cnt_t m_edgeFadeFrames = 0;
	//! ED-152 / ED-179: the speed fades in output frames, and whether the clip changes speed at all.
	f_cnt_t m_speedInFrames = 0;
	f_cnt_t m_speedOutFrames = 0;
	bool m_speeds = false;
	f_cnt_t m_frame = 0;
	Sample* m_sample = nullptr;
	Track* m_track = nullptr;
	PatternTrack* m_patternTrack = nullptr;
	bool m_doneMayReturnTrue = true;
	bool m_ownAudioBusHandle = false;
	bool m_ignoresTrackMute = false;
	std::uint16_t m_poolSlot = NotPooled;
	bool m_frozenTakePoolVoice = false;

	//! The resampler ratio for the period about to be rendered: the local
	//! warp rate at the source frame this period starts on, over the natural
	//! rate. Exactly 1.0 for every clip with no markers and no source tempo.
	float warpRatio() const;
	//! ED-152 / ED-179: the clip's speed this period (clipSpeedAt); exactly 1 for a clip that keeps its speed.
	float speedHere() const;

	/*! Renders \a frames output frames of a clip whose rate change is
	 *  pitch-preserving (row 30), reading the clip's window through
	 *  `m_stretcher` instead of `Sample::play`'s resampler.
	 *
	 *  The source frames per output frame are
	 *  `(speedHere() / warpRatio()) / (sampleRateRatio * freqRatio)` — the
	 *  reciprocal of the converter ratio the resample path would hand
	 *  `AudioResampler` (`warpRatio() / speedHere()` times the sample's own
	 *  ratios), so both modes put the same source frames on the same timeline
	 *  and differ only in what they do to the waveform's period. The handle's
	 *  playback state is advanced to the stretcher's own analysis cursor, which
	 *  is what the next period's rate is derived from. */
	void renderPreservingPitch(SampleFrame* dst, f_cnt_t frames);

	/*! Multiplies the clip's fade-and-gain envelope into the frames this period
	 *  just rendered. Never called for a neutral clip, so the default path pays
	 *  nothing at all for this feature - not a branch, not a multiply. */
	void applyClipEdits(SampleFrame* buffer, f_cnt_t frames) const;
	bool editsOrAutoFade(int autoFadeMs) const;
	bool needsClipEdits() const;
	//! Derives m_clipFrames / m_envelopeStart / the two fade lengths from the
	//! clip and the window this handle renders (construction time only).
	void snapshotClipEdits(const SampleClip* clip, const SampleWindow& window);
	void fitFades();  //!< clamp the ramps and their offsets to the clip (owner decision 2026-10-08)

	/*! R3.1, comp PLAYBACK: when the clip's track has a composite (take lanes
	 *  comped into one performance, include/TakeLane.h), the spans of this clip's
	 *  own lane the composite selects - in output frames from the clip's start,
	 *  snapshotted here like the fades. A clip whose track has no composite has
	 *  m_comped false and pays nothing (bit-identical render). */
	void snapshotComp(const SampleClip* clip);
	//! Silences the frames outside the selected spans, with a short ramp at
	//! every edge so a lane switch does not click. Only run when m_comped.
	void applyComp(SampleFrame* buffer, f_cnt_t frames) const;
	//! Lock-free active-take gate; comp playback bypasses this selection entirely.
	void applyActiveTakeGate(SampleFrame* buffer, f_cnt_t frames) const;
	//! How many selected spans of this clip's own lane one handle can hold. This is the
	//! composite's per-lane budget (TakeLaneModel::MaxCompositeSpansPerLane), which the
	//! comp's authoring paths enforce, so a rendered comp is never silently cut. A fixed
	//! array keeps applyComp() allocation-free and bounded on the render path.
	static constexpr int MaxCompSpans = TakeLaneModel::MaxCompositeSpansPerLane;
	static constexpr f_cnt_t CompRampFrames = 64;
	bool m_comped = false;
	int m_takeGroupId = -1;
	int m_takeLaneIndex = 0;
	const std::atomic<int>* m_activeTakeLane = nullptr;
	const std::atomic<bool>* m_playAllTakes = nullptr;
	//! 0.5.0 L05 (ED-177): an automatic fade applies to this neutral clip.
	bool m_autoFaded = false;
	int m_compSpanCount = 0;
	std::array<std::pair<f_cnt_t, f_cnt_t>, MaxCompSpans> m_compSpans{};
	//! 0.5.0 E2: the reads of this take's SLIPPED spans (include/CompSlip.h); null - and nothing extra runs - when
	//! the clip's lane has no slipped span, or the clip does not render linearly at its own pitch.
	std::unique_ptr<CompSlipReaders> m_compSlip;

	/*! 0.5.0 W3 (ED-179): a clip with pre-fades (include/ClipPreFades.h) plays a copy of its Sample whose window
	 *  reaches past the clip's own, and its envelope is measured from the clip's first frame with the frames before
	 *  it negative. Snapshotted at construction; a clip without pre-fades leaves m_preFaded false and takes the old
	 *  arithmetic. Defined in src/core/ClipPreFades.cpp. */
	void snapshotPreFades(const SampleClip* clip, const SampleWindow& window);
	void applyPreFaded(SampleFrame* buffer, f_cnt_t frames) const;
	std::optional<Sample> m_preFadeSample;
	PreFadeSpan m_preFadeSpan;
	std::int64_t m_preRollFrames = 0;  //!< the frames this pass plays before the clip's first frame
	bool m_preFaded = false;

	/*! 0.5.0 ED-029 (audio): a looped clip (include/ClipAudioLoop.h) plays a copy of its Sample whose loop points
	 *  are the clip's loop, in Sample::Loop::On, from the folded frame this pass starts on. Snapshotted at
	 *  construction; a clip without a loop leaves m_loopMode Off - the render it always had. Defined in
	 *  src/core/ClipAudioLoop.cpp. */
	void snapshotAudioLoop(const SampleClip* clip, const SampleWindow& window);
	std::optional<Sample> m_loopSample;
	Sample::Loop m_loopMode = Sample::Loop::Off;
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_PLAY_HANDLE_H
