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

#include <QDomElement>

#include <vector>

#include "EffectChain.h"
#include "Mixer.h"
#include "panning.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleRecordHandle.h"
#include "SampleTrackView.h"
#include "Song.h"
#include "volume.h"


namespace lmms
{


SampleTrack::SampleTrack(TrackContainer* tc) :
	Track(Track::Type::Sample, tc),
	m_volumeModel(DefaultVolume, MinVolume, MaxVolume, 0.1f, this, tr("Volume")),
	m_panningModel(DefaultPanning, PanningLeft, PanningRight, 0.1f, this, tr("Panning")),
	m_mixerChannelModel(0, 0, 0, this, tr("Mixer channel")),
	m_audioBusHandle(tr("Sample track"), true, &m_volumeModel, &m_panningModel, &m_mutedModel),
	m_isPlaying(false)
{
	setName(tr("Sample track"));
	m_panningModel.setCenterValue(DefaultPanning);
	m_mixerChannelModel.setRange(0, Engine::mixer()->numChannels()-1, 1);

	connect(&m_mixerChannelModel, SIGNAL(dataChanged()), this, SLOT(updateMixerChannel()));
}




SampleTrack::~SampleTrack()
{
	Engine::audioEngine()->removePlayHandlesOfTypes( this, PlayHandle::Type::SamplePlayHandle );
}




bool SampleTrack::play( const TimePos & _start, const f_cnt_t _frames,
					const f_cnt_t _offset, int _clip_num )
{
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
	};
	std::vector<ScheduledClip> clips;
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
		clips.push_back({sClip, sClip->sampleWindow()});
		if (trackContainer() == Engine::patternStore())
		{
			auto bufferFramesPerTick = Engine::framesPerTick(sClip->sample().sampleRate());
			f_cnt_t sampleStart = bufferFramesPerTick * _start;
			pattern_track = PatternTrack::findPatternTrack(_clip_num);
			// this pass starts inside the source; the clip's own window still
			// bounds it (was: sClip->setSampleStartFrame(), a model write)
			clips.back().window = { sampleStart, sClip->sampleWindow().sourceOut };
			sClip->setIsPlaying(true);
			setPlaying(true);
		}
	}
	else
	{
		bool nowPlaying = false;
		for( int i = 0; i < numOfClips(); ++i )
		{
			Clip * clip = getClip( i );
			auto sClip = dynamic_cast<SampleClip*>(clip);

			if( _start >= sClip->startPosition() && _start < sClip->endPosition() )
			{
				if( sClip->isPlaying() == false && _start >= (sClip->startPosition() + sClip->startTimeOffset()) )
				{
					// where this pass begins and ends inside the clip's window
					const auto windowStart = sClip->sourceFrameAt( _start );
					const auto windowEnd = sClip->sourceFrameAt( sClip->endPosition() );
					//we only play within the clip's window
					if( windowStart < windowEnd )
					{
						clips.push_back({sClip, { windowStart, windowEnd }});
						sClip->setIsPlaying( true );
						nowPlaying = true;
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

	for (const auto& scheduled : clips)
	{
		auto st = scheduled.clip;
		if( !st->isMuted() )
		{
			PlayHandle* handle;
			if( st->isRecord() )
			{
				if( !Engine::getSong()->isRecording() )
				{
					return played_a_note;
				}
				auto smpHandle = new SampleRecordHandle(st);
				handle = smpHandle;
			}
			else
			{
				auto smpHandle = new SamplePlayHandle(st, scheduled.window);
				smpHandle->setPatternTrack(pattern_track);
				handle = smpHandle;
			}
			handle->setOffset( _offset );
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
}




void SampleTrack::loadTrackSpecificSettings(const QDomElement & thisElem)
{
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
