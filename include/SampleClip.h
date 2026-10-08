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

#include <atomic>
#include <memory>
#include <span>
#include "Clip.h"
#include "RubberBandStretch.h"
#include "Sample.h"
#include "SampleWindow.h"
#include "WarpMarkers.h"

namespace lmms
{

class SampleBuffer;
class SampleRecordHandle;

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

	TimePos sampleLength() const;

	/*! The clip's authored window into its source audio (design §2.2).
	 *
	 *  This is the state a trim, a slip or a split authors. The playback path
	 *  reads it and never writes it: a playback pass derives the window it renders
	 *  and hands it to the play handle as a snapshot (I1). */
	SampleWindow sampleWindow() const { return m_window; }

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

	f_cnt_t sourceFrameAt(TimePos timelinePos) const override;
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

	/*! How this clip renders a rate change: plain resampling (the default,
	 *  and the pitch moves with the rate) or the pitch-preserving stretch
	 *  (WSOLA, feature-list row 30). Only read for a clip that does not
	 *  `rendersLinearly()`: with no rate change both modes are the same
	 *  render, and the playback path takes the historical one. */
	WarpStretchMode warpStretchMode() const { return m_stretchMode; }
	void setWarpStretchMode(WarpStretchMode mode);

	/*! The Rubber Band voices this clip's play handles claim (owner decision
	 *  12), or nullptr. Built on the control thread the first time the clip is
	 *  in the RubberBand mode - by the setter, a restore or a copy - and kept
	 *  for the clip's lifetime, because a handle may still hold a voice when
	 *  the mode changes back. */
	RubberBandPool* rubberBandPool() const { return m_rubberBand.get(); }

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
	void acceptRecordedTake(std::shared_ptr<const SampleBuffer> take, std::uint64_t droppedFrames, bool continues);
	//! Takes this clip holds ready for its next passes (0 to 2). Zero once disarmed.
	int recordTakesReady() const noexcept { return (m_preparedRecord != nullptr) + (m_spareRecord != nullptr); }
	//! Frames the last take of this clip lost to capture overflow (0 before any take).
	std::uint64_t lastTakeDroppedFrames() const noexcept { return m_lastTakeDroppedFrames; }
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
	std::uint64_t m_lastTakeDroppedFrames = 0;
	//! Main thread: build the missing takes into m_preparedRecord / m_spareRecord.
	void releaseRecordTakes();
	//! Main thread: an armed clip's take on a new take lane.
	void addRecordedLane(std::shared_ptr<const SampleBuffer> take);
	//! See rubberBandPool(); never replaced once built.
	std::unique_ptr<RubberBandPool> m_rubberBand;
	//! Builds m_rubberBand if the mode asks for it and it does not exist yet.
	void ensureRubberBandPool();
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
	//! A recording pass lost \a frames to capture overflow; the GUI may show it. Main thread.
	void recordTakeDropped(qulonglong frames);
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_CLIP_H
