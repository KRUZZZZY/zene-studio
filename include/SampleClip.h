/*
 * SampleClip.h
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

#ifndef LMMS_SAMPLE_CLIP_H
#define LMMS_SAMPLE_CLIP_H

#include <array>
#include <atomic>
#include <memory>
#include <span>
#include <vector>

#include "Clip.h"
#include "RubberBandStretch.h"
#include "Sample.h"
#include "SampleWindow.h"
#include "WarpMarkers.h"

namespace lmms
{

class SampleBuffer;
class SampleRecordHandle;
class SampleTrack;

namespace gui
{

class SampleClipView;

} // namespace gui


// The warp vocabulary (WarpTempoMode / WarpStretchMode) moved to
// WarpMarkers.h with the warp state itself (SPEC-ARCH-4 census row 5: warp is
// native to the base clip); both names still reach this header through
// Clip.h/WarpMarkers.h, so no caller of theirs changed.


class SampleClip : public Clip
{
	Q_OBJECT
	mapPropertyFromModel(bool,isRecord,setRecord,m_recordModel);
public:
	SampleClip(Track* track, Sample sample, bool isPlaying);
	SampleClip(Track* track);
	~SampleClip() override;

	SampleClip& operator=( const SampleClip& that ) = delete;

	void changeLength( const TimePos & _length ) override;
	void updateLength() override;

	const QString& sampleFile() const;
	bool hasSampleFileLoaded(const QString & filename) const;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;
	static const QString classNodeName() { return "sampleclip"; }
	inline QString nodeName() const override
	{
		return classNodeName();
	}

	Sample& sample()
	{
		return m_sample;
	}

	//! The take's audio, read-only: the waveform painters (TakeWaveform) need its peaks.
	const Sample& sample() const
	{
		return m_sample;
	}

	TimePos sampleLength() const;

	/*! The clip's authored window into its source audio (design §2.2).
	 *
	 *  This is the state a trim, a slip or a split authors. The playback path
	 *  reads it and never writes it: a playback pass derives the window it renders
	 *  and hands it to the play handle as a snapshot (I1).
	 *
	 *  The window is a PLAY range: the frames the clip plays, in play order. */
	SampleWindow sampleWindow() const { return m_window; }

	/*! The source frames the clip actually sounds.
	 *
	 *  For a forward source that is the window itself. For a reversed source it
	 *  is the window's MIRROR: Sample::render reverses the WHOLE buffer, so the
	 *  frame played at play frame `p` is the buffer frame `size - 1 - p` and a
	 *  reversed clip sounds the mirror of the window it was given. Everything
	 *  that looks at the clip's AUDIO - the transient detector, the slice and
	 *  the quantise commands - has to look at this range, not at the window. */
	SampleWindow audibleWindow() const;

	/*! The play frame of a source frame: the position in the window traversal.
	 *
	 *  This is the coordinate the warp map is written in (the render path asks
	 *  the map about play frames) and the coordinate a reversed clip's
	 *  transients have to be ordered in: for a reversed source the play frame
	 *  of the buffer frame `f` is `size - 1 - f`. A source frame outside the
	 *  window is clamped to the window's nearest edge, exactly as the timeline
	 *  mapping clamps it. */
	f_cnt_t playFrameFor(f_cnt_t sourceFrame) const;

	/*! Authors the window; the single place the well-formedness rule (I4) lives.
	 *
	 *  An edit that would leave the window empty is rejected outright. A window
	 *  that is not the whole buffer is a manual edit, so it also clears
	 *  auto-resize (design §2.6 / OQ-4), which is what keeps it stable across a
	 *  tempo change and a reload. */
	void setSampleWindow(const SampleWindow& window);

	//! The legacy absolute setters: the window's start frame, and its END frame
	//! (`length` is a source frame index, as it always was - not a frame count).
	void setSampleStartFrame( f_cnt_t startFrame );
	void setSamplePlayLength( f_cnt_t length );

	/*! The play frame a timeline position plays: the position in the window
	 *  traversal, which is the coordinate the warp map is written in and the
	 *  one the play handles unfold. For a forward source it is the source
	 *  frame; for a reversed source it is the position whose source frame is
	 *  the buffer's mirror. */
	f_cnt_t sourceFrameAt(TimePos timelinePos) const override;

	/*! The timeline position a SOURCE frame is reached at: where the buffer
	 *  frame `sourceFrame` sounds, in the clip's play direction. A frame
	 *  outside the window is clamped to the window's nearest edge. */
	TimePos timelinePosAt(f_cnt_t sourceFrame) const override;

	/*! The clip's own rate: source frames per project tick (#597).
	 *
	 *  For a `FollowProject` clip this is exactly `Engine::framesPerTick()`, so
	 *  the mapping is the pre-warp one bit for bit. For a `SourceTempo` leader
	 *  it is scaled by `projectTempo / sourceTempo`, which is what makes one bar
	 *  of the clip's own music occupy one bar of the project.
	 */
	float clipFramesPerTick() const;

	//! The clip's warp map. Empty means "no warp": the linear map is the answer.
	const WarpMarkers& warpMarkers() const { return m_warp; }

	/*! Authors the marker set (control thread). Returns false — leaving the
	 *  previous set intact — for a set that is not strictly increasing in both
	 *  coordinates, or one with more than `WarpMarkers::MaxMarkers` entries. */
	bool setWarpMarkers(std::span<const WarpMarker> markers);
	void clearWarpMarkers();

	WarpTempoMode warpTempoMode() const { return m_tempoMode; }
	void setWarpTempoMode(WarpTempoMode mode);

	/*! How this clip renders a rate change: varispeed resampling (the default
	 *  and the spec's Re-Pitch — the pitch moves with the rate) or one of the
	 *  pitch-keeping stretches (WSOLA at its default parameters, the
	 *  WSOLA-parameterised Tones / Texture, or the Rubber Band-backed Beats /
	 *  Complex, Q050-036). Only read for a clip that
	 *  does not `rendersLinearly()`: with no rate change every mode is the
	 *  same render, and the playback path takes the historical one. */
	WarpStretchMode warpStretchMode() const { return m_stretchMode.load(std::memory_order_acquire); }
	void setWarpStretchMode(WarpStretchMode mode);

	/*! The Rubber Band voices this clip's play handles claim (owner decision
	 *  12), or nullptr when the current mode renders no pool. Built on the
	 *  control thread the first time the clip takes one of the two
	 *  Rubber Band-backed modes (Beats, Complex) - by the setter, a restore or
	 *  a copy - and kept for the clip's lifetime, one pool per mode (Q050-036),
	 *  because a voice's options are fixed at construction and a handle may
	 *  still hold a voice when the mode changes again. The unused mode's pool
	 *  is simply never claimed. */
	RubberBandPool* rubberBandPool() const;
	//! The pool of @a mode (nullptr for a mode without one, or one not built): a
	//! reader that took the mode with warpStretchMode() asks for THAT mode's pool,
	//! so a switch between the two reads cannot hand it the other mode's voices.
	RubberBandPool* rubberBandPool(WarpStretchMode mode) const;

	//! Clip::restoreState plus the Rubber Band pool a restored mode may need.
	void restoreState(const QDomElement& element) override;

	//! The tempo the clip was recorded at, in BPM; only read in `SourceTempo`.
	float sourceTempo() const { return m_sourceTempo; }
	void setSourceTempo(float bpm);

	/*! The ticks the window `window` spans on the timeline under this clip's
	 *  mapping. Equals the pre-warp `window.length() / framesPerTick()` for a
	 *  clip with no markers and no source tempo, which is why `sampleLength()`
	 *  is unchanged for every existing project. */
	int windowTicksFor(const SampleWindow& window) const;

	/*! True while this clip's playback is the pre-warp linear one: no markers
	 *  and the default (project-following) tempo. SamplePlayHandle uses it to
	 *  keep the historical length arithmetic bit for bit. */
	bool rendersLinearly() const
	{
		return m_warp.empty() && m_tempoMode == WarpTempoMode::FollowProject;
	}

	void setStartTimeOffset(const TimePos& startTimeOffset) override;

	/*! 0.5.0 ED-021: the sub-tick part of a frame-exact edit. The clip's audio starts this many SOURCE
	 *  frames after its tick origin (start + offset), less than one tick's worth: a split or a trim at a
	 *  frame between two ticks puts the remainder here, so the cut frame sounds exactly where it did.
	 *  0 for every clip a frame-exact edit never touched, which keeps the mapping below (and every saved
	 *  project) exactly as it was. Saved as `lead`, read back reset-on-absence. Linear clips only: a
	 *  warped clip's frame edits are refused (SampleFrameEdits.h). */
	f_cnt_t leadFrames() const { return m_leadFrames.load(std::memory_order_relaxed); }
	void setLeadFrames(f_cnt_t frames);
	/*! The OUTPUT frames of silence still owed before the audio starts when a pass begins at @a at - what
	 *  SampleTrack adds to the play handle's offset. 0 without a lead, or once the lead has passed. */
	f_cnt_t leadOutputFramesAt(TimePos at) const;

	gui::ClipView * createView( gui::TrackView * _tv ) override;


	bool isPlaying() const;
	void setIsPlaying(bool isPlaying);
	void setSampleBuffer(std::shared_ptr<const SampleBuffer> sb);
	//! Audio thread: take the take prepared for this clip (see prepareRecordTake), or nullptr
	//! when none is ready. Allocates nothing; the caller owns the handle.
	SampleRecordHandle* claimRecordHandle() noexcept;
	//! Main thread: a recording pass ended with \a take (may be null) and \a droppedFrames lost
	//! on overflow. While the transport still records (\a continues, a loop wrap or a jump) the
	//! take becomes a new take clip on a take lane and the clip stays armed; otherwise a
	//! first-pass take is the clip's own content and the clip is disarmed.
	void acceptRecordedTake(std::shared_ptr<const SampleBuffer> take, std::uint64_t droppedFrames, bool continues,
		std::uint64_t generation);
	//! The arming this clip is in: it changes on every arm, so a take prepared under an earlier
	//! arming can tell it no longer belongs to the current one.
	std::uint64_t armGeneration() const noexcept { return m_armGeneration; }
	//! Takes this clip holds ready for its next passes (0 to 2). Zero once disarmed.
	int recordTakesReady() const noexcept { return (m_preparedRecord != nullptr) + (m_spareRecord != nullptr); }
	//! Frames the last take of this clip lost to capture overflow (0 before any take).
	std::uint64_t lastTakeDroppedFrames() const noexcept { return m_lastTakeDroppedFrames.load(std::memory_order_relaxed); }
	//! 0.5.0: a new source and the window to play of it, in one owner call - what a destructive edit (crop, a range
	//! edit, a printed chain) does; the window is written here, by the clip, as the one-writer rule requires.
	void replaceSample(std::shared_ptr<const SampleBuffer> sb, const SampleWindow& window);

	SampleClip* clone() override
	{
		return new SampleClip(*this);
	}

public slots:
	void setSampleFile(const QString& sf);
	void toggleRecord();
	void playbackPositionChanged();
	void updateTrackClips();
	void tempoChanged();
	//! Main thread: build the clip's next record take (its accumulator and drain thread) while
	//! the clip is armed, so SampleTrack::play never allocates one. Idempotent.
	void prepareRecordTake();
	//! Main thread: the record model changed. Arming resets this clip's playing latch, so a
	//! clip armed inside its span starts with the next pass; then prepares its takes.
	void armRecordTake();

protected:
	SampleClip( const SampleClip& orig );

private:
	Sample m_sample;
	//! The authored window (design §2.2, Slice 0). Written only by this class -
	//! never by the playback path - and mirrored into Sample's render-time frame
	//! fields so drawing and Sample::render see it.
	SampleWindow m_window;
	// The warp state (m_warp, m_tempoMode, m_sourceTempo, m_stretchMode) moved
	// to Clip with its `<warp>` serialisation (SPEC-ARCH-4 census row 5); the
	// accessors and the mapping above read the inherited members unchanged.
	BoolModel m_recordModel;
	//! The take prepared for the next recording pass (main thread builds it, the audio thread
	//! claims it through claimRecordHandle()). Written under the engine's change lock.
	SampleRecordHandle* m_preparedRecord = nullptr;
	//! The take behind m_preparedRecord: claimed, it becomes the ready one, so a loop wrap
	//! never finds the clip without a take.
	SampleRecordHandle* m_spareRecord = nullptr;
	//! Recording passes of the current arming that became takes on the lanes.
	int m_recordPasses = 0;
	std::uint64_t m_armGeneration = 0;
	//! Written on the main thread by the reaper; read by record.get_state, whichever thread asks.
	std::atomic<std::uint64_t> m_lastTakeDroppedFrames{0};
	//! Main thread: build the missing takes into m_preparedRecord / m_spareRecord.
	void releaseRecordTakes();
	//! Main thread, at the arm: one journal step, a checkpoint of the track as it was before the arm.
	void journalArmStep();
	//! Main thread: an armed clip's take on a new take lane; \a newestIsActive makes it the active
	//! take even over a take the user chose (Keep History).
	void addRecordedLane(std::shared_ptr<const SampleBuffer> take, bool newestIsActive = false);
	//! Main thread (Q050-094): this clip's own audio, as a copy, on a new take lane of its group.
	void addHistoryLane();
	//! Main thread: a clip for take lane \a lane of this clip's group, a copy of \a source or (no source)
	//! holding \a take. Called with the engine's change lock held.
	SampleClip* newLaneClip(SampleTrack* track, SampleClip* source, std::shared_ptr<const SampleBuffer> take, int lane);
	//! Main thread (Q050-094): the first pass of an arming lands according to the record mode.
	void installFirstPass(std::shared_ptr<const SampleBuffer> take);
	//! Main thread: the Overdub landing - mixed into this clip's audio, or a clip of its own over it.
	void overdub(std::shared_ptr<const SampleBuffer> take);
	//! See rubberBandPool(): one slot per Rubber Band-backed mode, each built
	//! at most once (the mode's own voices; never replaced once built).
	std::array<std::unique_ptr<RubberBandPool>, WarpRubberBandModeSlots> m_rubberBands;
	//! The same pools as raw pointers, published with release AFTER construction;
	//! the render thread reads these, never the unique_ptrs.
	std::array<std::atomic<RubberBandPool*>, WarpRubberBandModeSlots> m_rubberBandPublished{};
	//! Builds the current mode's pool if the mode asks for one and it is absent.
	void ensureRubberBandPool();
	//! Builds @a mode's pool if it asks for one and it is absent, then publishes it.
	void ensureRubberBandPool(WarpStretchMode mode);
	void prepareWarpStretchMode(WarpStretchMode mode) override { ensureRubberBandPool(mode); }
	bool m_isPlaying;
	int m_startFrameOffset;
	//! ED-021: see leadFrames(). Written on the UI thread, read by the playback path: a relaxed atomic, the
	//! pattern Sample's own frame fields use.
	std::atomic<f_cnt_t> m_leadFrames = 0;

	//! Points the window (and Sample's frame fields) at the whole buffer; called
	//! wherever the source buffer is replaced.
	void resetWindowToFullBuffer();
	//! Sets m_window and points Sample's frame fields at it; the window is already well formed.
	void installWindow(const SampleWindow& window);

	friend class gui::SampleClipView;


signals:
	void sampleChanged();
	void wasReversed();
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_CLIP_H
