/*
 * SampleClip.cpp
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
 
#include "SampleClip.h"

#include <algorithm>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <utility>

#include <QDomElement>
#include <QFileInfo>

#include "AudioEngine.h"
#include "ControlEdit.h"
#include "Engine.h"
#include "PatternStore.h"
#include "PathUtil.h"
#include "RecordOptions.h"
#include "SampleClipView.h"
#include "SampleRecordHandle.h"
#include "SampleTrack.h"
#include "Song.h"

namespace lmms
{

SampleClip::SampleClip(Track* _track, Sample sample, bool isPlaying):
	Clip(_track),
	m_sample(std::move(sample)),
	m_window(SampleWindow::full(m_sample.sampleSize())),
	m_isPlaying(false),
	m_startFrameOffset(0)
{
	saveJournallingState( false );
	setSampleFile( "" );
	restoreJournallingState();

	// we need to receive bpm-change-events, because then we have to
	// change length of this Clip
	connect(Engine::getSong(), &Song::tempoChanged, this, &SampleClip::tempoChanged, Qt::DirectConnection);
	connect(Engine::getSong(), &Song::timeSignatureChanged, this, &SampleClip::updateLength);

	//playbutton clicked or space key / on Export Song set isPlaying to false
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//a record take is built here, on the main thread, before the transport can claim it
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()), this, SLOT(prepareRecordTake()) );
	connect( &m_recordModel, SIGNAL(dataChanged()), this, SLOT(armRecordTake()) );
	//care about loops and jumps
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &SampleClip::playbackPositionChanged, Qt::DirectConnection);
	//care about mute Clips
	connect( this, SIGNAL(dataChanged()), this, SLOT(playbackPositionChanged()));
	//care about mute track
	connect( getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about Clip position
	connect( this, SIGNAL(positionChanged()), this, SLOT(updateTrackClips()));

	// The record model does not journal its own changes: the arm's step (journalArmStep) does.
	// Without this, the model's entry sits in front of the arm's step and a second undo does nothing.
	m_recordModel.setJournalling( false );
	updateTrackClips();
}

SampleClip::SampleClip(Track* track)
	: SampleClip(track, Sample(), false)
{
}

SampleClip::SampleClip(const SampleClip& orig) :
	Clip(orig),
	m_sample(std::move(orig.m_sample)),
	m_window(orig.m_window),
	// The warp members moved to Clip (SPEC-ARCH-4 census row 5);
	// Clip(const Clip&) copies them for every clip type now.
	m_isPlaying(orig.m_isPlaying),
	m_startFrameOffset(orig.m_startFrameOffset),
	m_leadFrames(orig.leadFrames())
{
	saveJournallingState( false );
	setSampleFile( "" );
	restoreJournallingState();

	// we need to receive bpm-change-events, because then we have to
	// change length of this Clip
	connect(Engine::getSong(), &Song::tempoChanged, this, &SampleClip::tempoChanged, Qt::DirectConnection);
	connect( Engine::getSong(), SIGNAL(timeSignatureChanged(int,int)),
					this, SLOT(updateLength()));

	//playbutton clicked or space key / on Export Song set isPlaying to false
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//a record take is built here, on the main thread, before the transport can claim it
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()), this, SLOT(prepareRecordTake()) );
	connect( &m_recordModel, SIGNAL(dataChanged()), this, SLOT(armRecordTake()) );
	//care about loops and jumps
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &SampleClip::playbackPositionChanged, Qt::DirectConnection);
	//care about mute Clips
	connect( this, SIGNAL(dataChanged()), this, SLOT(playbackPositionChanged()));
	//care about mute track
	connect( getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about Clip position
	connect( this, SIGNAL(positionChanged()), this, SLOT(updateTrackClips()));

	// The record model does not journal its own changes: the arm's step (journalArmStep) does.
	// Without this, the model's entry sits in front of the arm's step and a second undo does nothing.
	m_recordModel.setJournalling( false );
	updateTrackClips();
	// A copy carries the mode (Clip's copy constructor) but never the source's
	// voices: each clip claims from its own pool.
	ensureRubberBandPool();
}




SampleClip::~SampleClip()
{
	// main thread: takes nobody claimed (their handles queue the drain threads)
	delete m_preparedRecord;
	delete m_spareRecord;
	auto sampletrack = dynamic_cast<SampleTrack*>(getTrack());
	if ( sampletrack )
	{
		sampletrack->updateClips();
	}
}




void SampleClip::changeLength( const TimePos & _length )
{
	Clip::changeLength(std::max(static_cast<int>(_length), 1));
}



const QString& SampleClip::sampleFile() const
{
	return m_sample.sampleFile();
}

bool SampleClip::hasSampleFileLoaded(const QString & filename) const
{
	return m_sample.sampleFile() == filename;
}

SampleRecordHandle* SampleClip::claimRecordHandle() noexcept
{
	auto* handle = m_preparedRecord;
	m_preparedRecord = m_spareRecord;
	m_spareRecord = nullptr;
	if (handle != nullptr) { handle->markClaimed(); }
	return handle;
}

void SampleClip::prepareRecordTake()
{
	// The connections are automatic: a signal emitted on the audio thread arrives here queued,
	// so this always runs on the main thread, where a handle may allocate and start its thread.
	if (!isRecord())
	{
		releaseRecordTakes();
		return;
	}
	SampleRecordHandle::watchArmedClip(this);
	for (SampleRecordHandle** slot : { &m_preparedRecord, &m_spareRecord })
	{
		bool empty = false;
		{
			// the slot is read under the change lock the audio thread's claim also takes
			const auto guard = Engine::audioEngine()->requestChangesGuard();
			empty = *slot == nullptr && isRecord();
		}
		if (!empty) { continue; }
		auto* handle = new SampleRecordHandle(this);  // allocates: built outside the lock
		handle->setArmGeneration(m_armGeneration);
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		if (*slot == nullptr && isRecord()) { *slot = handle; }
		else { delete handle; }  // lost a race with a release or another prepare
	}
}

void SampleClip::armRecordTake()
{
	// Journalling: the arm is one undo step (the track's checkpoint below). The clip's own changes
	// while it records - the disarm that ends the recording - are not journalled, so one undo takes
	// the whole recording back. Journalling resumes when the clip disarms.
	if (!isRecord()) { setJournalling(true); }
	// The arm transition (the model signals changes only): a new arming, and a clip armed inside
	// its span must not stay latched as playing from the pass before (BUGS P3).
	if (isRecord())
	{
		++m_armGeneration;
		// The pass count belongs to the arming it counted: a clip armed again after a loop pass
		// must record its first take as the clip's own content, not as one more take on a lane
		// (BUG 11, new-p12-206).
		m_recordPasses = 0;
		{
			// The render thread reads the playing latch; write it under the lock it renders under.
			const auto guard = Engine::audioEngine()->requestChangesGuard();
			setIsPlaying(false);
		}
		// The track is checkpointed once here, so one undo step takes back the
		// recording's take lanes and active-take model state.
		journalArmStep();
		setJournalling(false);
	}
	prepareRecordTake();
}

void SampleClip::journalArmStep()
{
	// The live-take guarantee on undo: an undo rebuilds the track's clips (Track::loadTrack deletes them
	// and recreates them from the snapshot), so the armed clip is destroyed. Its destructor removes its
	// live play handle, and the reaper finds no clip to install the live take into, so nothing is
	// installed when the transport stops. That guarantee rests on the rebuild destroying the armed
	// clip: if an undo ever kept this clip object, its live take would be installed on stop.
	if (Track* track = getTrack()) { track->addJournalCheckPoint(); }
}

void SampleClip::releaseRecordTakes()
{
	SampleRecordHandle* released[2] = { nullptr, nullptr };
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		released[0] = std::exchange(m_preparedRecord, nullptr);
		released[1] = std::exchange(m_spareRecord, nullptr);
	}
	// Deleting an unclaimed handle queues its take; the reaper stops its drain thread.
	delete released[0];
	delete released[1];
}

namespace
{

//! Holds a track's clip updates suspended for its lifetime (see SampleTrack::setClipUpdatesSuspended).
class ClipUpdatesSuspension
{
public:
	explicit ClipUpdatesSuspension(SampleTrack* track) : m_track(track)
	{
		if (m_track != nullptr) { m_track->setClipUpdatesSuspended(true); }
	}
	~ClipUpdatesSuspension()
	{
		if (m_track != nullptr) { m_track->setClipUpdatesSuspended(false); }
	}
	ClipUpdatesSuspension(const ClipUpdatesSuspension&) = delete;
	ClipUpdatesSuspension& operator=(const ClipUpdatesSuspension&) = delete;

private:
	SampleTrack* m_track;
};

} // namespace

void SampleClip::acceptRecordedTake(std::shared_ptr<const SampleBuffer> take, std::uint64_t droppedFrames, bool continues,
	std::uint64_t generation)
{
	m_lastTakeDroppedFrames.store(droppedFrames, std::memory_order_relaxed);
	if (droppedFrames > 0) { Engine::getSong()->reportRecordTakeDropped(droppedFrames); }
	// A take of an earlier arming (the clip was re-armed before its take was installed) keeps
	// its audio as a lane take, and leaves the current arming alone: it must not disarm the clip.
	if (generation != m_armGeneration)
	{
		if (take) { addRecordedLane(std::move(take)); }
		return;
	}
	// A take becomes a lane take when the transport goes on recording after it (a loop pass), or
	// when an earlier pass of this arming already did; the first take of a plain recording stays
	// the clip's own content, as it always did.
	if (continues || m_recordPasses > 0)
	{
		if (take) { addRecordedLane(std::move(take)); }
		++m_recordPasses;
		if (!continues) { m_recordPasses = 0; }
	}
	else if (take)
	{
		installFirstPass(std::move(take));
	}
	if (!continues)
	{
		setRecord(false);  // not journalled: the arm's step is the recording's undo step
	}
}

void SampleClip::installFirstPass(std::shared_ptr<const SampleBuffer> take)
{
	// Q050-094: the first (or only) pass of an arming lands according to the record mode, on this
	// main-thread path. A clip with no audio takes the pass as its own content in every mode
	// (Overdub and Replace), exactly as before the modes applied to audio.
	const bool hasAudio = m_sample.sampleSize() > 0;
	switch (recordoptions::options().mode)
	{
	case recordoptions::Mode::KeepHistory:
		// Every pass is a take lane: the clip's earlier audio becomes one first, then the pass.
		if (hasAudio) { addHistoryLane(); }
		addRecordedLane(std::move(take), true);
		return;
	case recordoptions::Mode::Overdub:
		if (hasAudio)
		{
			overdub(std::move(take));
			return;
		}
		break;
	case recordoptions::Mode::Replace:
		break;
	}
	setSampleBuffer(std::move(take));
}

void SampleClip::overdub(std::shared_ptr<const SampleBuffer> take)
{
	// The pass can be mixed into the clip's buffer only where the buffer plays as recorded: from its
	// first frame, forwards, at its own rate, with default pitch and gain, through the whole buffer.
	// A trimmed, reversed, warped, pitch-shifted, gain-adjusted, frame-led or offset clip is left
	// unchanged; the pass goes in a clip of its own over it, so neither sound is lost.
	const bool plain = rendersLinearly() && !m_sample.reversed() && m_sample.amplification() == 1.0f
		&& m_sample.frequency() == DefaultBaseFreq && leadFrames() == 0 && startTimeOffset() == 0
		&& m_window == SampleWindow::full(static_cast<f_cnt_t>(m_sample.sampleSize()));
	if (plain && m_sample.buffer() != nullptr)
	{
		replaceSample(recordoptions::overdubMix(*m_sample.buffer(), *take), SampleWindow());
		return;
	}
	auto* track = dynamic_cast<SampleTrack*>(getTrack());
	if (track == nullptr) { return; }
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	const ClipUpdatesSuspension suspended{track};
	auto* layer = new SampleClip(track, Sample(std::move(take)), false);
	layer->setJournalling(false);  // its placement is not an undo step of its own: the arm's step covers it
	layer->movePosition(startPosition());
	layer->updateLength();
	layer->setJournalling(true);  // ... but the clip is saved with the project and in later checkpoints
	Engine::getSong()->setModified();
}

SampleClip* SampleClip::newLaneClip(SampleTrack* track, SampleClip* source, std::shared_ptr<const SampleBuffer> take, int lane)
{
	// The clip is created and added to the track under the change lock the audio thread's
	// processing takes, as every other clip edit is (the caller holds it).
	// The lane clip's constructor and its position change reach updateClips(), which would remove
	// the track's live play handles - the take of the pass that has just begun. Suspended for the
	// creation, even if it throws.
	const ClipUpdatesSuspension suspended{track};
	auto* take_clip = source != nullptr ? new SampleClip(*source) : new SampleClip(track, Sample(std::move(take)), false);
	// Placing it is not an undo step of its own: the arm's step covers the lane (one undo step). A clip
	// whose journalling stays off is not written by saveState(), so it would be lost from the saved
	// project and from every later track checkpoint (a take choice's undo) - journalling comes back on.
	take_clip->setJournalling(false);
	control::assignTakeLane(take_clip, lane);
	take_clip->setTakeGroupId(id());
	take_clip->movePosition(startPosition());
	take_clip->updateLength();
	take_clip->setJournalling(true);
	return take_clip;
}

void SampleClip::addRecordedLane(std::shared_ptr<const SampleBuffer> take, bool newestIsActive)
{
	auto* track = dynamic_cast<SampleTrack*>(getTrack());
	if (track == nullptr) { return; }
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		const int lane = track->takeLanes().addLane();
		newLaneClip(track, nullptr, std::move(take), lane);
		// The model makes the newest pass active unless the user explicitly chose a take from this armed
		// clip's group (Keep History: the new pass is active whatever was chosen). No clip mute flag is
		// changed here.
		track->takeLanes().recordTake(id(), lane, track->getClips(), newestIsActive);
	}
	Engine::getSong()->setModified();
}

void SampleClip::addHistoryLane()
{
	auto* track = dynamic_cast<SampleTrack*>(getTrack());
	if (track == nullptr) { return; }
	const TimePos keptLength = length();
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		const int lane = track->takeLanes().addLane();
		newLaneClip(track, this, nullptr, lane);  // a copy: the audio, its window and how it plays
		track->takeLanes().recordTake(id(), lane, track->getClips(), true);
	}
	// This clip is now the (empty) shell the passes' lanes hang from, as a loop recording leaves it.
	// It keeps its length; the lane clips carry the audio.
	setAutoResize(false);
	setSampleBuffer(SampleBuffer::emptyBuffer());
	changeLength(keptLength);
}


void SampleClip::setSampleBuffer(std::shared_ptr<const SampleBuffer> sb)
{
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		m_sample = Sample(std::move(sb));
		// a new source means a new window: the whole buffer (Slice 0)
		resetWindowToFullBuffer();
	}
	updateLength();

	emit sampleChanged();

	Engine::getSong()->setModified();
}

void SampleClip::replaceSample(std::shared_ptr<const SampleBuffer> sb, const SampleWindow& window)
{
	{
		// The buffer and the window played of it change under ONE guard: no audio period sees the new
		// buffer through a full-buffer window in between (review L17 #10).
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		// A new buffer constructor starts at the defaults (Sample::Sample(shared_ptr)), which
		// silently un-reversed and un-pitched and un-gained a clip the caller edited: this call
		// replaces the clip's SOURCE, it does not restate how the clip plays it, so the three
		// playback settings are carried over. Read under the same guard as the write.
		const float amplification = m_sample.amplification();
		const float frequency = m_sample.frequency();
		const bool reversed = m_sample.reversed();
		m_sample = Sample(std::move(sb));
		m_sample.setAmplification(amplification);
		m_sample.setFrequency(frequency);
		m_sample.setReversed(reversed);
		const auto frames = static_cast<f_cnt_t>(m_sample.sampleSize());
		const auto clamped = SampleWindow::clamped(window.sourceIn, window.sourceOut, frames);
		installWindow(clamped.empty() ? SampleWindow::full(frames) : clamped);
	}
	updateLength();
	// A window that is not the whole buffer is a manual edit, as setSampleWindow() has it.
	if (m_window != SampleWindow::full(static_cast<f_cnt_t>(m_sample.sampleSize()))) { setAutoResize(false); }
	emit sampleChanged();
	Engine::getSong()->setModified();
}

void SampleClip::setSampleFile(const QString& sf)
{
	// Remove any prior offset in the clip
	setStartTimeOffset(0);
	if (!sf.isEmpty())
	{
		// A different source: the markers were anchored to frames of the old
		// one, so they go with it. `setSampleFile("")` (the copy constructor)
		// deliberately keeps them - the source has not changed.
		m_warp.clear();
		setLeadFrames(0);  // ED-021: the lead was a frame of the old source too
		m_sample = Sample(SampleBuffer::fromFile(sf));
		resetWindowToFullBuffer();
		updateLength();
	}
	else
	{
		// If there is no sample, make the clip a bar long
		float nom = Engine::getSong()->getTimeSigModel().getNumerator();
		float den = Engine::getSong()->getTimeSigModel().getDenominator();
		changeLength(DefaultTicksPerBar * (nom / den));
	}

	emit sampleChanged();
	emit playbackPositionChanged();
}




void SampleClip::toggleRecord()
{
	m_recordModel.setValue( !m_recordModel.value() );
	emit dataChanged();
}




void SampleClip::playbackPositionChanged()
{
	auto st = dynamic_cast<SampleTrack*>(getTrack());
	// A lane take being built (addRecordedLane) changes its own clip's signals, which reach here:
	// they must not remove the live take of the pass that has just begun (BUGS P6, round 4).
	if( st != nullptr && st->clipUpdatesSuspended() ) { return; }
	Engine::audioEngine()->removePlayHandlesOfTypes( getTrack(), PlayHandle::Type::SamplePlayHandle );
	st->setPlayingClips( false );
}




void SampleClip::updateTrackClips()
{
	auto sampletrack = dynamic_cast<SampleTrack*>(getTrack());
	if( sampletrack)
	{
		sampletrack->updateClips();
	}
}




bool SampleClip::isPlaying() const
{
	return m_isPlaying;
}




void SampleClip::setIsPlaying(bool isPlaying)
{
	m_isPlaying = isPlaying;
}




void SampleClip::updateLength()
{
	// If the clip has already been manually resized, don't automatically resize it.
	if (getAutoResize())
	{
		if (getTrack()->trackContainer() == Engine::patternStore())
		{
			changeLength(TimePos::ticksPerBar() * Engine::patternStore()->lengthOfPattern(getTrack()->getClipNum(this)));
			return;
		}
		changeLength(sampleLength());
		setStartTimeOffset(0);
	}

	emit sampleChanged();
}


void SampleClip::tempoChanged()
{
	Clip::setStartTimeOffset(std::round(1.0f * m_startFrameOffset / Engine::framesPerTick()));
	updateLength();
	emit sampleChanged();
}

void SampleClip::setStartTimeOffset(const TimePos& startTimeOffset)
{
	m_startFrameOffset = startTimeOffset * Engine::framesPerTick();
	Clip::setStartTimeOffset(startTimeOffset);
}


void SampleClip::setLeadFrames(f_cnt_t frames)
{
	m_leadFrames.store(frames, std::memory_order_relaxed);
}


f_cnt_t SampleClip::leadOutputFramesAt(TimePos at) const
{
	const f_cnt_t lead = leadFrames();
	if (lead == 0) { return 0; }
	const double framesPerTick = clipFramesPerTick();
	if (framesPerTick <= 0.0) { return 0; }
	const auto relativeTicks = at.getTicks() - startPosition().getTicks() - startTimeOffset().getTicks();
	const auto elapsed = relativeTicks > 0 ? TimePos(relativeTicks).frames(framesPerTick) : f_cnt_t{0};
	if (elapsed >= lead) { return 0; }
	const double outputPerSource = Engine::framesPerTick(Engine::audioEngine()->outputSampleRate()) / framesPerTick;
	return static_cast<f_cnt_t>(std::llround(static_cast<double>(lead - elapsed) * outputPerSource));
}


TimePos SampleClip::sampleLength() const
{
	// The clip's audio length is the length of its window, not of the whole file:
	// for a clip without a trim and without a warp the two are the same, which is
	// what keeps this behaviour-preserving (Slice 0). `windowTicksFor()` is the
	// pre-warp `length() / framesPerTick()` expression in exactly that case.
	return TimePos(windowTicksFor(m_window));
}


int SampleClip::windowTicksFor(const SampleWindow& window) const
{
	const auto framesPerTick = clipFramesPerTick();
	// BUG 6 (cluster new-p12-204): a nonpositive divisor - a source that still
	// carries a zero rate from somewhere other than a project load - maps to no
	// length instead of an infinity narrowed to int, which is undefined
	// behaviour. The same rule leadOutputFramesAt() and restoreState() already
	// apply to this value.
	if (framesPerTick <= 0.0f) { return 0; }
	if (m_warp.empty())
	{
		return static_cast<int>(window.length() / framesPerTick);
	}
	return m_warp.timelineOffsetAt(window.sourceOut, window.sourceIn, framesPerTick)
		- m_warp.timelineOffsetAt(window.sourceIn, window.sourceIn, framesPerTick);
}


float SampleClip::clipFramesPerTick() const
{
	// The one place the tempo enters the clip's mapping (design §2.4).
	if (m_tempoMode == WarpTempoMode::SourceTempo && m_sourceTempo > 0.0f)
	{
		const auto projectTempo = static_cast<float>(Engine::getSong()->getTempo());
		if (projectTempo > 0.0f)
		{
			// A leader: resample the clip's own music onto the project grid.
			return Engine::framesPerTick(m_sample.sampleRate()) * projectTempo / m_sourceTempo;
		}
	}
	return Engine::framesPerTick(m_sample.sampleRate());
}


bool SampleClip::setWarpMarkers(std::span<const WarpMarker> markers)
{
	if (!m_warp.set(markers))
	{
		Engine::getSong()->collectError(tr("Warp markers rejected: the set is not "
			"strictly increasing in source frame and timeline position."));
		return false;
	}
	Engine::getSong()->setModified();
	emit sampleChanged();
	return true;
}


void SampleClip::clearWarpMarkers()
{
	if (m_warp.empty()) { return; }
	m_warp.clear();
	Engine::getSong()->setModified();
	emit sampleChanged();
}


void SampleClip::setWarpTempoMode(WarpTempoMode mode)
{
	if (m_tempoMode == mode) { return; }
	m_tempoMode = mode;
	Engine::getSong()->setModified();
	emit sampleChanged();
}


void SampleClip::setSourceTempo(float bpm)
{
	// Not a tempo at all: +Inf and NaN are refused with the negatives, because
	// clipFramesPerTick() divides by this value - an infinite tempo scales the
	// clip's rate to exactly zero and windowTicksFor() then converts an
	// infinity to int, which is undefined behaviour.
	if (!std::isfinite(bpm) || bpm < 0.0f || m_sourceTempo == bpm) { return; }
	m_sourceTempo = bpm;
	Engine::getSong()->setModified();
	emit sampleChanged();
}


void SampleClip::setWarpStretchMode(WarpStretchMode mode)
{
	// The same shape as the two setters above: a change of the clip's own
	// serialized state, marked modified and announced. Rendering reads it at
	// handle construction, so a change takes effect on the next playback pass.
	if (m_stretchMode == mode) { return; }
	// Voices first, mode second (both on this control thread): the render thread
	// reads the mode with an acquire load, so a mode it sees has its pool published.
	ensureRubberBandPool(mode);
	m_stretchMode.store(mode, std::memory_order_release);
	Engine::getSong()->setModified();
	emit sampleChanged();
}


void SampleClip::restoreState(const QDomElement& element)
{
	Clip::restoreState(element);
	const double framesPerTick = clipFramesPerTick();
	if (framesPerTick <= 0.0 || static_cast<double>(leadFrames()) >= framesPerTick)
	{
		setLeadFrames(0);
	}
	// A load, a paste or an undo can bring the RubberBand mode back; the voices
	// have to exist before the next play handle looks for them, and building
	// them is the one step the audio thread must never take.
	ensureRubberBandPool();
}


RubberBandPool* SampleClip::rubberBandPool() const
{
	return rubberBandPool(warpStretchMode());
}


RubberBandPool* SampleClip::rubberBandPool(WarpStretchMode mode) const
{
	const int slot = warpModeRubberBandSlot(mode);
	return slot < 0 ? nullptr : m_rubberBandPublished[static_cast<std::size_t>(slot)].load(std::memory_order_acquire);
}


void SampleClip::ensureRubberBandPool()
{
	ensureRubberBandPool(warpStretchMode());
}


void SampleClip::ensureRubberBandPool(WarpStretchMode mode)
{
	if (!RubberBandPool::available()) { return; }
	const int slot = warpModeRubberBandSlot(mode);
	if (slot < 0 || m_rubberBands[static_cast<std::size_t>(slot)])
	{
		return;
	}
	m_rubberBands[static_cast<std::size_t>(slot)] =
		std::make_unique<RubberBandPool>(m_sample.sampleRate(), mode);
	m_rubberBandPublished[static_cast<std::size_t>(slot)].store(
		m_rubberBands[static_cast<std::size_t>(slot)].get(), std::memory_order_release);
}




void SampleClip::setSampleStartFrame(f_cnt_t startFrame)
{
	// The legacy absolute setter for the window's start. Nothing on the playback
	// path calls it any more: the window is authored state (Slice 0).
	setSampleWindow({ startFrame, m_window.sourceOut });
}




void SampleClip::setSamplePlayLength(f_cnt_t length)
{
	// The legacy absolute setter for the window's END frame, as it always was.
	setSampleWindow({ m_window.sourceIn, length });
}




void SampleClip::setSampleWindow(const SampleWindow& window)
{
	const auto bufferFrames = static_cast<f_cnt_t>(m_sample.sampleSize());
	const auto clamped = SampleWindow::clamped(window.sourceIn, window.sourceOut, bufferFrames);
	if (clamped.empty() || clamped == m_window)
	{
		// I4: a window that is not well formed is a rejected edit, not a clamped
		// one - and an edit that changes nothing is not an edit.
		return;
	}

	installWindow(clamped);

	// The clip plays exactly its window, so an edited window is a different amount
	// of audio and the clip's timeline length has to follow it (BUG new-p16-205).
	// The length is moved here, while auto-resize may still be on, because the
	// moment it goes off below nothing would move the length any more. A clip in
	// the pattern store is the one exception: there the pattern owns the length,
	// exactly as updateLength() has it.
	if (getTrack()->trackContainer() != Engine::patternStore()) { changeLength(sampleLength()); }

	if (m_window != SampleWindow::full(bufferFrames))
	{
		// A window that is not the whole buffer is a manual edit, so it must
		// survive a tempo change and a reload (design §2.6, OQ-4).
		setAutoResize(false);
	}

	Engine::getSong()->setModified();
	emit sampleChanged();
}




void SampleClip::resetWindowToFullBuffer()
{
	installWindow(SampleWindow::full(static_cast<f_cnt_t>(m_sample.sampleSize())));
}




void SampleClip::installWindow(const SampleWindow& window)
{
	m_window = window;

	// Sample's frame fields are the render-time scratch they already were (OQ-1):
	// pointing them at the window keeps Sample::render, sampleDuration() and the
	// waveform drawing reading the window, while the authored state stays here.
	m_sample.setStartFrame(static_cast<int>(m_window.sourceIn));
	m_sample.setEndFrame(static_cast<int>(m_window.sourceOut));
}




f_cnt_t SampleClip::sourceFrameAt(TimePos timelinePos) const
{
	// The one mapping of the window onto the timeline (design §2.4). With no
	// markers it is the linear one exactly as the clip-and-capture wave froze
	// it; with markers it is the warp map. No allocation, no lock: this runs on
	// the audio thread (I8).
	const auto framesPerTick = clipFramesPerTick();
	const auto originTicks = startPosition().getTicks() + startTimeOffset().getTicks();
	const auto relativeTicks = timelinePos.getTicks() - originTicks;

	if (m_warp.empty())
	{
		const auto elapsed = relativeTicks > 0
			? TimePos(relativeTicks).frames(framesPerTick)
			: f_cnt_t{ 0 };
		// ED-021: a frame-exact edit's sub-tick lead holds the audio back (0 for every other clip).
		const f_cnt_t lead = leadFrames();
		const auto relative = elapsed > lead ? elapsed - lead : f_cnt_t{ 0 };
		return std::clamp(m_window.sourceIn + relative, m_window.sourceIn, m_window.sourceOut);
	}

	return m_warp.sourceFrameAt(relativeTicks, m_window.sourceIn, m_window.sourceOut, framesPerTick);
}




SampleWindow SampleClip::audibleWindow() const
{
	// The window is a PLAY range and a reversed source reads the buffer's
	// mirror (Sample::render reverses the whole buffer), so the frames a
	// reversed clip sounds are the window's mirror. A forward clip sounds the
	// window itself.
	if (!m_sample.reversed()) { return m_window; }
	const auto size = static_cast<f_cnt_t>(m_sample.sampleSize());
	return { size - m_window.sourceOut, size - m_window.sourceIn };
}




f_cnt_t SampleClip::playFrameFor(f_cnt_t sourceFrame) const
{
	// Mirrored over the buffer, in signed arithmetic (an unsigned subtraction
	// wraps for a frame past the buffer end), then clamped to the window's
	// edge - the same rule the timeline mapping uses.
	std::int64_t played = static_cast<std::int64_t>(sourceFrame);
	if (m_sample.reversed())
	{
		played = static_cast<std::int64_t>(m_sample.sampleSize()) - 1 - played;
	}
	return static_cast<f_cnt_t>(std::clamp(played,
		static_cast<std::int64_t>(m_window.sourceIn), static_cast<std::int64_t>(m_window.sourceOut)));
}




TimePos SampleClip::timelinePosAt(f_cnt_t sourceFrame) const
{
	const auto framesPerTick = clipFramesPerTick();
	const auto originTicks = startPosition().getTicks() + startTimeOffset().getTicks();
	// BUG new-p17-158: a source whose rate is zero has no frames per tick, so
	// there is nothing to divide by (windowTicksFor() and leadOutputFramesAt()
	// refuse the same rate). The mapping answers with the clip's own origin -
	// the defined fallback the linear branch already gets from
	// TimePos::fromFrames() - instead of a position computed from a zero rate,
	// which put the frame hundreds of ticks outside the clip that owns it.
	if (!(framesPerTick > 0.0f)) { return TimePos(originTicks); }
	// Luna cluster 2484: this mapping never consulted the play DIRECTION, so a reversed clip placed every
	// source frame where the forward clip placed it. Sample::render reverses the WHOLE buffer: buffer frame
	// f is read at play frame (size - 1 - f), and the window is played in play frames. So a reversed frame
	// sounds where play frame size - 1 - f sounds, not where in + out - 1 - f would (those differ whenever
	// the window does not end at the buffer's end). playFrameFor() is that mirror, clamped to the
	// window's edge - the same rule the rest of the clip model reads the direction by.
	const auto frame = playFrameFor(sourceFrame);

	if (m_warp.empty())
	{
		return TimePos(originTicks
			+ static_cast<int>(TimePos::fromFrames(frame - m_window.sourceIn + leadFrames(), framesPerTick)));
	}

	return TimePos(originTicks + m_warp.timelineOffsetAt(frame, m_window.sourceIn, framesPerTick));
}




void SampleClip::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	if( _this.parentNode().nodeName() == "clipboard" )
	{
		_this.setAttribute( "pos", -1 );
	}
	else
	{
		_this.setAttribute( "pos", startPosition() );
	}
	_this.setAttribute( "len", length() );
	_this.setAttribute( "muted", isMuted() );
	_this.setAttribute( "src", sampleFile() );
	_this.setAttribute( "off", startTimeOffset() );
	_this.setAttribute("autoresize", QString::number(getAutoResize()));
	if( sampleFile() == "" )
	{
		QString s;
		_this.setAttribute("data", m_sample.toBase64());
	}

	_this.setAttribute( "sample_rate", m_sample.sampleRate());
	// The source layout (Q050-106): a mono downmix writes `channels="1"` beside the data it
	// already writes. Additive like the window and the lead - a stereo clip writes nothing, so
	// every existing project serialises byte for byte as it did.
	if (m_sample.channels() != DEFAULT_CHANNELS)
	{
		_this.setAttribute("channels", m_sample.channels());
	}
	// The authored window, in source frames (Slice 1 of task #611). Additive: a
	// clip that plays its whole source writes neither attribute, so a project
	// without a trim serialises exactly as it did before this slice (I9).
	if (m_window != SampleWindow::full(static_cast<f_cnt_t>(m_sample.sampleSize())))
	{
		_this.setAttribute( "srcin", QString::number(m_window.sourceIn) );
		_this.setAttribute( "srcout", QString::number(m_window.sourceOut) );
	}
	// ED-021: the sub-tick lead of a frame-exact edit; additive like the window (a clip without one writes nothing).
	if (leadFrames() > 0) { _this.setAttribute("lead", QString::number(leadFrames())); }
	// The warp map (#597): the `<warp>` child moved to the base clip
	// (Clip::saveWarp — SPEC-ARCH-4 census row 5). Same element, same
	// mode/tempo/stretch/marker vocabulary, same additive rule (I9), now
	// written by every clip type instead of only this one.
	// The clip's fades and its clip gain (fade/crossfade/clip-gain wave), written
	// by the base class so every clip type that serialises shares one rule.
	// Additive like the two blocks above: a clip with no fade and unity gain
	// writes nothing, so a project without them serialises as it always did.
	saveClipEdits(_this);
	if (const auto& c = color())
	{
		_this.setAttribute("color", c->name());
	}
	if (m_sample.reversed())
	{
		_this.setAttribute("reversed", "true");
	}
}




void SampleClip::loadSettings( const QDomElement & _this )
{
	if( _this.attribute( "pos" ).toInt() >= 0 )
	{
		movePosition( _this.attribute( "pos" ).toInt() );
	}

	if (const auto srcFile = _this.attribute("src"); !srcFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(srcFile)).exists())
		{
			setSampleFile(srcFile);
		}
		else { Engine::getSong()->collectError(QString("%1: %2").arg(tr("Sample not found"), srcFile)); }
	}

	if( sampleFile().isEmpty() && _this.hasAttribute( "data" ) )
	{
		// BUG 6 (cluster new-p12-204): a stated rate that is not positive - "0",
		// a negative one, or any text QString::toInt() reads back as 0 - would
		// build a zero-rate buffer, whose clipFramesPerTick() is exactly the
		// zero windowTicksFor() divides by. An invalid rate loads the way an
		// absent one already does: the engine's own output rate.
		const auto statedRate = _this.hasAttribute("sample_rate")
			? _this.attribute("sample_rate").toInt() : 0;
		const auto sampleRate = statedRate > 0
			? statedRate : Engine::audioEngine()->outputSampleRate();
		// Q050-106: `channels` is written ONLY for a mono downmix; its absence is the
		// pre-existing stereo layout every older project has. Any other value is read as
		// stereo, the only other layout a SampleFrame carries.
		const int channels = _this.attribute("channels", QString::number(DEFAULT_CHANNELS)).toInt() == 1
			? 1 : DEFAULT_CHANNELS;

		auto buffer = SampleBuffer::fromBase64(_this.attribute("data"), sampleRate, channels);
		m_sample = Sample(std::move(buffer));
		resetWindowToFullBuffer();
	}
	changeLength( _this.attribute( "len" ).toInt() );
	setMuted( _this.attribute( "muted" ).toInt() );
	setStartTimeOffset( _this.attribute( "off" ).toInt() );

	// The authored window (Slice 1 of task #611). Applied after `len` and `off` -
	// which stay authoritative for a file that carries a window, exactly as the
	// design asks (§2.6) - and before `autoresize`, so the file's own value decides
	// that flag rather than the "this was a manual edit" rule in setSampleWindow().
	if (_this.hasAttribute("srcin") || _this.hasAttribute("srcout"))
	{
		const auto srcOut = _this.attribute("srcout");
		// A window edit moves the clip's length (BUG new-p16-205), and here the
		// file's own `len` is the one that counts: the length the window moved is
		// put back, so a file that carries a window loads with the length it was
		// saved with.
		const auto savedLength = length();
		setSampleWindow({ _this.attribute("srcin", "0").toULongLong(),
			srcOut.isEmpty() ? static_cast<qulonglong>(m_sample.sampleSize())
				: srcOut.toULongLong() });
		changeLength(savedLength);
	}

	setAutoResize(_this.attribute("autoresize", "1").toInt());
	// ED-021: reset on absence, so a checkpoint taken before a clip's first frame-exact edit takes it back.
	bool leadOk = false;
	const auto lead = _this.attribute("lead", "0").toULongLong(&leadOk);
	setLeadFrames(leadOk ? static_cast<f_cnt_t>(lead) : f_cnt_t{0});

	// The `<warp>` child moved to the base clip (Clip::loadWarp — SPEC-ARCH-4
	// census row 5). It reads right AFTER this function returns, which keeps
	// the order the old inline code had: after `len`/`off`/`srcin`/`srcout`/
	// `autoresize` and after `setSampleFile()` (the call that replaces the
	// source the markers are anchored to), and it still resets the warp state
	// when the element is absent - the checkpoint-inverse rule lives at the
	// new site.

	if (_this.hasAttribute("color"))
	{
		setColor(QColor{_this.attribute("color")});
	}

	// The clip's fades and its clip gain (fade/crossfade/clip-gain wave). Read
	// last because nothing else in this function depends on them and they depend
	// on nothing here: a file without the attributes keeps the neutral defaults.
	loadClipEdits(_this);

	// `reversed` is written ONLY for a reversed source (saveSettings above), so on
	// restore its absence is an explicit "plays forwards", not "no change". Reading
	// it as "no change" left the live flag alone, so a restore over a clip the GUI
	// had since reversed left the clip reversed (BUG-CLIPREV). A value that is
	// present but says "not true" ("false"/"0") is read the same way. The window
	// attributes above stay additive on purpose: THEIR absent case is the declared
	// limit in docs/KNOWN-LIMITATIONS.md ("no reset-on-absence for it").
	const auto reversedText = _this.attribute("reversed");
	const bool reversed = _this.hasAttribute("reversed")
		&& reversedText != QStringLiteral("false") && reversedText != QStringLiteral("0");
	if(m_sample.reversed() != reversed)
	{
		m_sample.setReversed(reversed);
		emit wasReversed(); // tell SampleClipView to update the view
	}
}




gui::ClipView * SampleClip::createView( gui::TrackView * _tv )
{
	return new gui::SampleClipView( this, _tv );
}


} // namespace lmms
