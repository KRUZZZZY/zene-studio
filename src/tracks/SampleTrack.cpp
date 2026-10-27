/*
 * SampleTrack.cpp - implementation of class SampleTrack, a track which
 *                   provides arrangement of samples
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

#include "SampleTrack.h"

#include <array>
#include <atomic>

#include <QDomElement>

#include <vector>

#include "ClipAudioLoop.h"
#include "ClipPreFades.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "panning.h"
#include "InputMonitor.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleRecordHandle.h"
#include "SampleTrackView.h"
#include "TakeLane.h"
#include "Song.h"
#include "volume.h"


namespace lmms
{


SampleTrack::SampleTrack(TrackContainer* tc) :
	Track(Track::Type::Sample, tc),
	m_volumeModel(DefaultVolume, MinVolume, MaxVolume, 0.1f, this, tr("Volume")),
	m_panningModel(DefaultPanning, PanningLeft, PanningRight, 0.1f, this, tr("Panning")),
	m_mixerChannelModel(0, 0, 0, this, tr("Mixer channel")),
	m_audioBusHandle(tr("Sample track"), true, &m_volumeModel, &m_panningModel, &m_mutedModel)
{
	setName(tr("Sample track"));
	m_panningModel.setCenterValue(DefaultPanning);
	m_mixerChannelModel.setRange(0, Engine::mixer()->numChannels()-1, 1);

	connect(&m_mixerChannelModel, SIGNAL(dataChanged()), this, SLOT(updateMixerChannel()));
}




SampleTrack::~SampleTrack()
{
	Engine::audioEngine()->removePlayHandlesOfTypes( this, PlayHandle::Type::SamplePlayHandle
		| PlayHandle::Type::InputMonitorHandle | PlayHandle::Type::SessionCellHandle );
}




//! R2.1: the monitor handle lives exactly while the mode is not Off, so a track that never
//! monitors costs the render nothing.
void SampleTrack::monitorModeChanged()
{
	if( monitorMode() == MonitorMode::Off )
	{
		if( m_monitorHandle != nullptr )
		{
			Engine::audioEngine()->removePlayHandlesOfTypes( this, PlayHandle::Type::InputMonitorHandle );
			m_monitorHandle = nullptr;
		}
		return;
	}
	if( m_monitorHandle == nullptr )
	{
		m_monitorHandle = new InputMonitorHandle( this );
		if (!Engine::audioEngine()->addPlayHandle(m_monitorHandle))
		{
			delete m_monitorHandle;
			m_monitorHandle = nullptr;
		}
	}
}




namespace
{
//! Clip starts deferred by the per-pass cap in SampleTrack::play, process-wide.
std::atomic<std::uint64_t> s_deferredClipStarts{0};

//! True when the track's composite names this clip's lane somewhere: that take plays its comped
//! spans whatever its mute flag says.
bool namedByComposite( const Track& track, const SampleClip& clip )
{
	for( const TakeLaneSegment& segment : track.takeLanes().segments() )
	{
		if( segment.laneIndex == clip.laneIndex() ) { return true; }
	}
	return false;
}

//! The active-pass predicate used by the audio-thread clip scheduler.
bool activeTakeForPlayback(const SampleClip& clip, bool comped, bool playAllTakes)
{
	return !clip.hasTakeGroup() || comped || playAllTakes
		|| clip.activeTakeLane() == clip.laneIndex();
}
} // namespace

std::uint64_t SampleTrack::deferredClipStarts() noexcept
{
	return s_deferredClipStarts.load(std::memory_order_relaxed);
}


void SampleTrack::notifyPlayingChanged()
{
	const bool playing = isPlaying();
	if (m_lastNotifiedPlaying == playing) { return; }
	m_lastNotifiedPlaying = playing;
	emit playingChanged();
}

bool SampleTrack::play( const TimePos & _start, const f_cnt_t _frames,
					const f_cnt_t _offset, int _clip_num )
{
	// The frozen take (freeze / bounce-in-place), the same substitution
	// InstrumentTrack::play makes: inside the take's window the track plays the
	// render and schedules none of its own clips. A single-clip playback
	// (_clip_num >= 0) is not the arrangement and keeps playing the source.
	if (isFrozen() && _clip_num < 0)
	{
		// A whole-track take owns every pass; a region take only its window, and a take whose audio is
		// missing none (frozenTakeSilencesSource): outside that the source plays on.
		const bool played = playFrozenTake(_start, _frames, _offset);
		if (frozenTakeSilencesSource(_start.getTicks())) { return played; }
	}

	bool played_a_note = false; // will be return variable

	// Slice 0 of the clip-and-capture wave (task #611, docs/CLIP-CAPTURE-DESIGN.md
	// §§2.1, 2.5, I1): the playback path READS the clip's authored window and never
	// writes it. What a pass renders is derived here from the transport position and
	// handed to the play handle as a snapshot, so a trim survives the pass that
	// used to overwrite it.
	struct ScheduledClip
	{
		SampleClip* clip;
		SampleWindow window;
		f_cnt_t lead = 0;  //!< 0.5.0 ED-021: output frames before a frame-exact clip's audio starts
	};
	// Fixed storage: the audio thread never grows it. The cap is on the clips that START in one
	// pass, not on overlaps: overlapping clips are allowed (a script can stack many on one track).
	// A clip past the cap is deferred: its isPlaying flag stays clear, so it starts on a later pass,
	// and each deferral is counted (deferredClipStarts).
	constexpr std::size_t MaxScheduledClipsPerPass = 64;
	std::array<ScheduledClip, MaxScheduledClipsPerPass> clips{};
	std::size_t clipCount = 0;
	class PatternTrack * pattern_track = nullptr;
	if( _clip_num >= 0 )
	{
		auto sClip = dynamic_cast<SampleClip*>(getClip(_clip_num));

		if (_start > sClip->length())
		{
			setPlaying(false);
		}
		if (sClip->isPlaying())
		{
			return false;
		}
		clips[clipCount++] = {sClip, sClip->sampleWindow()};
		if (trackContainer() == Engine::patternStore())
		{
			auto bufferFramesPerTick = Engine::framesPerTick(sClip->sample().sampleRate());
			f_cnt_t sampleStart = bufferFramesPerTick * _start;
			pattern_track = PatternTrack::findPatternTrack(_clip_num);
			// this pass starts inside the source; the clip's own window still
			// bounds it (was: sClip->setSampleStartFrame(), a model write)
			clips[clipCount - 1].window = { sampleStart, sClip->sampleWindow().sourceOut };
			sClip->setIsPlaying(true);
			setPlaying(true);
		}
	}
	else
	{
		bool nowPlaying = false;
		const TakeLaneModel& takeModel = takeLanes();
		const bool comped = !takeModel.segments().empty() || takeModel.auditionLane() >= 0;
		const bool playAllTakes = takeModel.playAllTakes();
		for( int i = 0; i < numOfClips(); ++i )
		{
			Clip * clip = getClip( i );
			auto sClip = dynamic_cast<SampleClip*>(clip);

			// 0.5.0 ED-179: a clip with pre-fades sounds from before its start to after its end; every
			// other clip's span, and the window below, are exactly its own (ClipPreFades.h).
			const TimePos spanStart = prefades::spanStart( *sClip );
			if( _start >= spanStart && _start < prefades::spanEnd( *sClip ) )
			{
				if( sClip->isPlaying() == false && _start >= (spanStart + sClip->startTimeOffset()) )
				{
					// where this pass begins and ends inside the clip's window
					// 0.5.0 ED-029: a looped clip's window is unfolded (ClipAudioLoop.h); the handle folds its start.
					const SampleWindow window = audioloop::engaged( *sClip ) ? audioloop::window( *sClip, _start )
						: prefades::window( *sClip, _start );
					const auto windowStart = window.sourceIn;
					const auto windowEnd = window.sourceOut;
					//we only play within the clip's window - except a clip armed to RECORD, which
					//holds no sample yet (its window is empty) and is exactly the clip that must
					//start: #611 Slice 0 added the window test and silently stopped clip
					//recording (BUGS_FOUND 11.8, RecordingLatencyTest)
					// A record-armed clip starts only while the song records: otherwise it is not
					// scheduled and its playing latch stays clear, so it starts on the pass that
					// records (BUGS P3: a latch set here blocked the take until the span was re-entered).
					const bool startable = !sClip->isRecord() || Engine::getSong()->isRecording();
					const bool activeTake = activeTakeForPlayback(*sClip, comped, playAllTakes);
					if( startable && activeTake && ( windowStart < windowEnd || sClip->isRecord() ) )
					{
						if( clipCount < clips.size() )
						{
							clips[clipCount++] = {sClip, { windowStart, windowEnd }, sClip->leadOutputFramesAt( _start )};
							sClip->setIsPlaying( true );
							nowPlaying = true;
						}
						else
						{
							s_deferredClipStarts.fetch_add( 1, std::memory_order_relaxed );
						}
					}
				}
			}
			else
			{
				sClip->setIsPlaying( false );
			}
			nowPlaying = nowPlaying || sClip->isPlaying();
		}
		setPlaying(nowPlaying);
	}

	for (std::size_t i = 0; i < clipCount; ++i)
	{
		const auto& scheduled = clips[i];
		auto st = scheduled.clip;
		// A muted take still plays where the composite names its lane: the comp, not the mute,
		// decides what a comped take contributes (BUGS P6, round 4).
		if( !st->isMuted() || namedByComposite( *this, *st ) )
		{
			PlayHandle* handle;
			if( st->isRecord() )
			{
				// Not scheduled while the song is not recording (see above); kept as a guard.
				if( !Engine::getSong()->isRecording() )
				{
					continue;
				}
				// The take was built on the main thread when the clip was armed or the transport
				// started (SampleClip::prepareRecordTake): the audio thread only claims it.
				handle = st->claimRecordHandle();
				if( handle == nullptr )
				{
					st->setIsPlaying( false );
					SampleRecordHandle::countUnpreparedTake();
					continue;
				}
			}
			else
			{
				auto smpHandle = new SamplePlayHandle(st, scheduled.window);
				smpHandle->setPatternTrack(pattern_track);
				handle = smpHandle;
			}
			handle->setOffset( _offset + scheduled.lead );
			// send it to the audio engine
			Engine::audioEngine()->addPlayHandle( handle );
			played_a_note = true;
		}
	}

	return played_a_note;
}




gui::TrackView * SampleTrack::createView( gui::TrackContainerView* tcv )
{
	return new gui::SampleTrackView( this, tcv );
}




Clip * SampleTrack::createClip(const TimePos & pos)
{
	auto sClip = new SampleClip(this);
	sClip->movePosition(pos);
	return sClip;
}




void SampleTrack::saveTrackSpecificSettings(QDomDocument& doc, QDomElement& thisElem, bool /*presetMode*/)
{
	m_audioBusHandle.effects()->saveState(doc, thisElem);
	m_volumeModel.saveSettings(doc, thisElem, "vol");
	m_panningModel.saveSettings(doc, thisElem, "pan");
	m_mixerChannelModel.saveSettings(doc, thisElem, "mixch");
	if (m_audioBusHandle.offsetFrames() != 0) { thisElem.setAttribute("offsetframes", m_audioBusHandle.offsetFrames()); }  // Q050-339
}




void SampleTrack::loadTrackSpecificSettings(const QDomElement & thisElem)
{
	m_audioBusHandle.setOffsetFrames(thisElem.attribute("offsetframes", "0").toInt());  // Q050-339 (absent: none)
	QDomNode node = thisElem.firstChild();
	m_audioBusHandle.effects()->clear();
	while(!node.isNull())
	{
		if (node.isElement())
		{
			if (m_audioBusHandle.effects()->nodeName() == node.nodeName())
			{
				m_audioBusHandle.effects()->restoreState(node.toElement());
			}
		}
		node = node.nextSibling();
	}
	m_volumeModel.loadSettings(thisElem, "vol");
	m_panningModel.loadSettings(thisElem, "pan");
	m_mixerChannelModel.setRange(0, Engine::mixer()->numChannels() - 1);
	m_mixerChannelModel.loadSettings(thisElem, "mixch");
}




void SampleTrack::updateClips()
{
	if( clipUpdatesSuspended() ) { return; }
	Engine::audioEngine()->removePlayHandlesOfTypes( this, PlayHandle::Type::SamplePlayHandle );
	setPlayingClips( false );
}




void SampleTrack::setPlayingClips( bool isPlaying )
{
	for( int i = 0; i < numOfClips(); ++i )
	{
		Clip * clip = getClip( i );
		auto sClip = dynamic_cast<SampleClip*>(clip);
		sClip->setIsPlaying( isPlaying );
	}
}




void SampleTrack::updateMixerChannel()
{
	m_audioBusHandle.setNextMixerChannel(m_mixerChannelModel.value());
}


} // namespace lmms
