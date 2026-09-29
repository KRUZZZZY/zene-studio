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
#include "TakeLane.h"
#include "Track.h"

#include <algorithm>
#include <cmath>

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

	/*! Row 30: whether this clip renders its rate change pitch-preservingly.
	 *
	 *  Snapshotted here with the window and the warp, and only when the clip
	 *  HAS a rate change to preserve: a clip that renders linearly has no
	 *  warp speed to apply, so the historical resampler path stays exactly the
	 *  path it was even if the mode was somehow set. The stretcher is prepared
	 *  once here (a table fill, no allocation) and its analysis cursor starts
	 *  at the head of the window, in window-relative frames. */
	const WarpStretchMode stretchMode = clip->warpStretchMode();
	m_preservePitch = (stretchMode == WarpStretchMode::PreservePitch
		|| stretchMode == WarpStretchMode::RubberBand) && !m_rendersLinearly;

	/*! Owner decision 12: a RubberBand clip claims one of its pool's voices -
	 *  a lock-free compare-and-swap, the voice already built and reset off this
	 *  thread. The pitch scale corrects the source rate to the output rate (the
	 *  stretcher keeps a period in SAMPLES, so without it a 44.1 kHz source on a
	 *  48 kHz engine would come out sharp). No voice - a build without the
	 *  library, or both voices still in use or dirty - is the WSOLA render,
	 *  which is the documented fallback and the pool counts it. */
	if (m_preservePitch && stretchMode == WarpStretchMode::RubberBand && clip->rubberBandPool() != nullptr)
	{
		m_rubberBand = clip->rubberBandPool()->claim();
		if (m_rubberBand != nullptr)
		{
			m_rubberBand->begin(static_cast<double>(m_sample->sampleRate())
				/ static_cast<double>(Engine::audioEngine()->outputSampleRate()));
		}
	}
	if (m_preservePitch && m_rubberBand == nullptr)
	{
		m_stretcher.prepare();
		m_stretcher.seek(0.0);
	}

	// The clip's fades and its gain (the fade/crossfade/clip-gain wave), taken
	// here for exactly the reason the window and the warp are: a live handle
	// renders the envelope it was created with. A neutral clip leaves m_edits
	// neutral, and play() then skips the envelope entirely.
	snapshotClipEdits(clip, window);
	snapshotComp(clip);
}


/*! \brief Author-side to render-side conversion for the envelope.
 *
 *  The fade lengths the model stores are TICKS; the envelope this handle applies
 *  is indexed by OUTPUT FRAMES. The conversion is one multiply by
 *  `Engine::framesPerTick(outputRate)` and it happens once, here, rather than
 *  once per frame on the audio thread. That is also what makes the tick a real
 *  unit rather than a proportion: a fade-out of N ticks is N ticks of the
 *  timeline however long the clip is, which is what the design promises ("a
 *  fade-out of N ticks produces a monotonic envelope over exactly N ticks").
 *
 *  The span the ramps are measured against is therefore the CLIP's own timeline
 *  length in output frames - not the handle's `totalFrames()`, which is bounded
 *  by the SOURCE and can be shorter than the clip when a clip is longer than the
 *  audio it references. `m_envelopeStart` is the same span measured from the
 *  clip's start to this pass's start, so a pass that begins mid-clip continues
 *  the ramp it interrupted instead of restarting it.
 *
 *  The last step clamps the pair so the two ramps can never overlap inside one
 *  clip. `clip.set_fade` already refuses that pair through the socket, so this
 *  only covers a project file that was hand-edited; the fade-in wins the meeting
 *  point and the fade-out is shortened to it, which keeps the envelope monotonic.
 *  (A pair that overlaps BY DESIGN - a crossfade across two clips - never reaches
 *  this clamp: each clip's ramp is bounded by ITS OWN length.)
 */
void SamplePlayHandle::snapshotClipEdits(const SampleClip* clip, const SampleWindow& window)
{
	m_edits = clip->clipEdits();
	// The comp gate (R3.1) measures against the same span, so the span is taken
	// for a comped clip too; a clip with neither stays on the early return.
	const Track* owner = clip->getTrack();
	const bool comped = owner != nullptr && !owner->takeLanes().segments().empty();
	if (m_edits.isNeutral() && !comped) { return; }

	const auto outputRate = Engine::audioEngine()->outputSampleRate();
	const double outputFramesPerTick = Engine::framesPerTick(outputRate);
	const int clipTicks = clip->length().getTicks();
	if (clipTicks <= 0 || outputFramesPerTick <= 0.0) { return; }

	const auto framesFor = [outputFramesPerTick](int ticks) {
		if (ticks <= 0) { return f_cnt_t(0); }
		return static_cast<f_cnt_t>(std::llround(static_cast<double>(ticks) * outputFramesPerTick));
	};
	m_clipFrames = framesFor(clipTicks);
	m_fadeInFrames = framesFor(m_edits.fadeInTicks);
	m_fadeOutFrames = framesFor(m_edits.fadeOutTicks);

	// Where this pass starts inside the clip's span. For a linear clip the source
	// frame offset scales by the output/source rate, which is exactly
	// framesPerTick(output)/framesPerTick(source); a warped or tempo-leading clip
	// has to ask the mapping, because its source frames are not evenly spaced in
	// timeline ticks.
	const auto clipWindow = clip->sampleWindow();
	if (m_rendersLinearly)
	{
		const double ratio = static_cast<double>(outputRate)
			/ static_cast<double>(m_sample->sampleRate());
		const auto offsetFrames = window.sourceIn >= clipWindow.sourceIn
			? window.sourceIn - clipWindow.sourceIn : 0;
		m_envelopeStart = static_cast<f_cnt_t>(
			std::llround(static_cast<double>(offsetFrames) * ratio));
	}
	else
	{
		const SampleWindow head{ clipWindow.sourceIn,
			window.sourceIn > clipWindow.sourceIn ? window.sourceIn : clipWindow.sourceIn };
		m_envelopeStart = framesFor(clip->windowTicksFor(head));
	}

	if (m_fadeInFrames > m_clipFrames) { m_fadeInFrames = m_clipFrames; }
	if (m_fadeInFrames + m_fadeOutFrames > m_clipFrames)
	{
		m_fadeOutFrames = m_clipFrames - m_fadeInFrames;
	}
}


/*! \brief The envelope, applied to the frames this period rendered.
 *
 *  `m_envelopeStart + m_frame` is the position of this period's first frame
 *  inside the clip's rendered span; the period's frames are contiguous after it.
 *  The multiply is skipped wholesale for the neutral case by play()'s own guard,
 *  so this function is never entered for a clip nobody has edited.
 */
void SamplePlayHandle::snapshotComp(const SampleClip* clip)
{
	const Track* owner = clip->getTrack();
	if (owner == nullptr) { return; }
	const TakeLaneModel& comp = owner->takeLanes();
	if (comp.segments().empty() && comp.auditionLane() < 0) { return; }
	const double fpt = Engine::framesPerTick(Engine::audioEngine()->outputSampleRate());
	const int start = clip->startPosition().getTicks();
	const int end = clip->endPosition().getTicks();
	const int lane = clip->laneIndex();
	m_comped = true;
	m_compSpanCount = 0;
	// Auditioning a lane (comp.audition) replaces the composite: that lane's
	// takes sound whole and every other lane is silent.
	if (comp.auditionLane() >= 0)
	{
		if (lane == comp.auditionLane())
		{
			m_compSpans[0] = {0, static_cast<f_cnt_t>(std::llround((end - start) * fpt))};
			m_compSpanCount = 1;
		}
		return;
	}
	for (const TakeLaneSegment& segment : comp.segments())
	{
		if (segment.laneIndex != lane || m_compSpanCount >= MaxCompSpans) { continue; }
		const int from = std::max(segment.beginTick, start);
		const int to = std::min(segment.endTick, end);
		if (to <= from) { continue; }
		m_compSpans[static_cast<std::size_t>(m_compSpanCount++)] = {
			static_cast<f_cnt_t>(std::llround((from - start) * fpt)),
			static_cast<f_cnt_t>(std::llround((to - start) * fpt))};
	}
}

void SamplePlayHandle::applyComp(SampleFrame* buffer, f_cnt_t frames) const
{
	const f_cnt_t base = m_envelopeStart + m_frame;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const f_cnt_t at = base + f;
		float gain = 0.0f;
		for (int i = 0; i < m_compSpanCount; ++i)
		{
			const auto [from, to] = m_compSpans[static_cast<std::size_t>(i)];
			if (at < from || at >= to) { continue; }
			// A linear ramp over CompRampFrames at each edge: the lane switch is a
			// crossfade of two clips, each fading at its own span's edge.
			const f_cnt_t edge = std::min(at - from, to - 1 - at);
			gain = std::max(gain, std::min(1.0f, static_cast<float>(edge + 1) / CompRampFrames));
		}
		buffer[f][0] *= gain;
		buffer[f][1] *= gain;
	}
}

void SamplePlayHandle::applyClipEdits(SampleFrame* buffer, f_cnt_t frames) const
{
	const f_cnt_t base = m_envelopeStart + m_frame;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const float envelope = clipFadeGainAt(base + f, m_clipFrames,
			m_fadeInFrames, m_fadeOutFrames, m_edits.fadeInShape, m_edits.fadeOutShape);
		const float gain = m_edits.gain * envelope;
		buffer[f][0] *= gain;
		buffer[f][1] *= gain;
	}
}




SamplePlayHandle::~SamplePlayHandle()
{
	// The voice goes back dirty; the pool's recycler resets it off this thread.
	if (m_rubberBand != nullptr) { m_rubberBand->release(); }
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

	if( !( m_track && m_track->isMuted() && !m_ignoresTrackMute )
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
		//
		// Row 30: a clip whose stretch mode is PreservePitch does NOT come
		// through here - its rate change is rendered by the stretcher, which
		// is the only difference between the two modes.
		if (m_preservePitch)
		{
			renderPreservingPitch(workingBuffer, frames);
		}
		else if (!m_sample->play(workingBuffer, &m_state, frames, Sample::Loop::Off, warpRatio()))
		{
			zeroSampleFrames(workingBuffer, frames);
		}

		/*! The clip's fade-and-gain envelope (the fade/crossfade/clip-gain wave).
		 *
		 *  Applied HERE, on the frames this handle just rendered, and deliberately
		 *  NOT in `Sample::render`: that function is shared with the browser
		 *  preview and the metronome (the `Sample*` constructors above), and a fade
		 *  there would fade the metronome click (docs/CLIP-CAPTURE-DESIGN.md §3
		 *  row 3, OQ-2). A clip nobody has edited is neutral, so this costs the
		 *  default render nothing at all and leaves its output bit for bit what it
		 *  was before the feature existed.
		 */
		if (!m_edits.isNeutral())
		{
			applyClipEdits(workingBuffer, frames);
		}
		// R3.1: a comped track plays only its composite - this clip sounds over
		// the spans the composite selects its lane for.
		if (m_comped)
		{
			applyComp(workingBuffer, frames);
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
	if (rate <= 0.0f) { return 1.0f; }

	/*! The converter ratio, in the convention the engine and libsamplerate
	 *  BOTH use: output frames per input frame.
	 *
	 *  MEASURED, not assumed - libsamplerate's `SRC_DATA::src_ratio` is
	 *  output/input: `src_ratio = 2.0` consumes 4096 input frames and generates
	 *  8192 output frames (tests/src/core/AudioResamplerRatioTest.cpp asserts
	 *  exactly that, on the library actually linked). `Sample::play`'s `ratio`
	 *  is the same quantity, documented as "output sample rate divided by input
	 *  sample rate" (include/AudioResampler.h).
	 *
	 *  A warp at speed `rate / natural` consumes `rate / natural` source frames
	 *  per output frame, so the ratio it needs is the RECIPROCAL of that speed:
	 *  `natural / rate` output frames per input frame. That is what this function
	 *  returns, and it is arithmetic, not a workaround.
	 *
	 *  Three prose sites (this comment, tests/src/tracks/SampleClipWarpTest.cpp
	 *  and docs/WARP.md §3.1) used to claim libsamplerate treats the ratio as
	 *  input/output - "the converter's long-standing inversion" - and §3.1
	 *  concluded from that a live defect on the pre-existing mismatch-rate path.
	 *  There is no inversion and there is no defect: a 48 kHz source in a 44.1 kHz
	 *  project passes 44100/48000 = 0.919 output frames per input frame, i.e. it
	 *  consumes 1.088 source frames per output frame, which is exactly the pitch-
	 *  preserving rate. Inverting AudioResampler::process() to "fix" the phantom
	 *  inversion is what would break it. The convention is pinned by the test
	 *  named above so that cannot happen silently.
	 */
	return m_naturalFramesPerTick / rate;
}




/*! Row 30 of the 0.3.0 list: the pitch-preserving render.
 *
 *  Same timeline, same source frames consumed, different waveform. The rate
 *  the mapping asks for is carried by the STRETCHER's analysis hop instead of
 *  by the resampler's ratio, and the stretcher's alignment search is what
 *  keeps the waveform's own period (the pitch) where it was: the source is
 *  consumed `speed` frames per output frame, but the grains that carry it are
 *  re-aligned to each other rather than shortened.
 *
 *  The source is the clip's own WINDOW, not the whole buffer, so a trimmed
 *  clip cannot bleed the audio on either side of its trim into the render
 *  (reads outside the window are silence, exactly as `Sample::render` stops at
 *  the window's end). The cursor is therefore window-relative and the state's
 *  frame index is the window-relative cursor added back to `sourceIn`.
 */
void SamplePlayHandle::renderPreservingPitch(SampleFrame* dst, f_cnt_t frames)
{
	const SampleFrame* source = m_sample->data();
	const f_cnt_t windowFrames = m_window.length();
	if (source == nullptr || windowFrames == 0)
	{
		zeroSampleFrames(dst, frames);
		return;
	}
	const SampleFrame* window = source + static_cast<std::ptrdiff_t>(m_window.sourceIn);

	/*! source frames consumed per output frame.
	 *
	 *  `warpRatio()` is `natural / rate` (the converter ratio, output frames
	 *  per input frame), so its reciprocal is the speed the mapping asks for
	 *  in source frames per output frame. The sample's own rate ratio and its
	 *  tuning divide into it exactly as they multiply into the resampler's
	 *  ratio - the two modes therefore consume the source identically and the
	 *  difference between them is only the waveform. */
	const double sampleRateRatio = static_cast<double>(Engine::audioEngine()->outputSampleRate())
		/ static_cast<double>(m_sample->sampleRate());
	const double freqRatio = static_cast<double>(m_sample->frequency()) / DefaultBaseFreq;
	const double converterRatio = static_cast<double>(warpRatio());
	const double speed = converterRatio > 0.0
		? 1.0 / (converterRatio * sampleRateRatio * freqRatio)
		: 1.0;

	// Owner decision 12: the same speed, the same window, a different stretcher.
	const f_cnt_t written = m_rubberBand != nullptr
		? m_rubberBand->render(window, windowFrames, dst, frames, speed)
		: m_stretcher.process(window, windowFrames, dst, frames, speed);
	if (written < frames) { zeroSampleFrames(dst + written, frames - written); }

	// The analysis cursor IS the source position this period reached; the next
	// period's rate is taken from the frame it starts on.
	const double sourcePosition = m_rubberBand != nullptr
		? m_rubberBand->sourcePosition() : m_stretcher.sourcePosition();
	m_state.setFrameIndex(static_cast<int>(m_window.sourceIn
		+ static_cast<f_cnt_t>(std::llround(sourcePosition))));
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
