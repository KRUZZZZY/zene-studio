/*
 * SampleRecordHandle.cpp - implementation of class SampleRecordHandle
 *
 * Copyright (c) 2008 Csaba Hruska <csaba.hruska/at/gmail.com>
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


#include "SampleRecordHandle.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "PatternTrack.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleRecordAccumulator.h"


namespace lmms
{


SampleRecordHandle::SampleRecordHandle( SampleClip* clip ) :
	PlayHandle( Type::SamplePlayHandle ),
	m_accum( std::make_unique<SampleRecordAccumulator>(
		Engine::audioEngine() != nullptr ? Engine::audioEngine()->inputSampleRate() : 44100 ) ),
	m_framesRecorded( 0 ),
	m_minLength( clip->length() ),
	m_track( clip->getTrack() ),
	m_patternTrack( nullptr ),
	m_clip( clip )
{
}




SampleRecordHandle::~SampleRecordHandle()
{
	// D9c: the drain thread inside the accumulator has already assembled the
	// take and its SampleBuffer, so the audio thread only installs the finished
	// buffer here instead of building it. See SampleRecordAccumulator.h.
	if (m_accum != nullptr)
	{
		if (auto buffer = m_accum->finish()) { m_clip->setSampleBuffer(std::move(buffer)); }
	}
	m_clip->setRecord( false );
}




void SampleRecordHandle::play( std::span<SampleFrame> /*buffer*/ )
{
	const SampleFrame* recbuf = Engine::audioEngine()->inputBuffer();
	const f_cnt_t frames = Engine::audioEngine()->inputBufferFrames();
	writeBuffer( recbuf, frames );
	m_framesRecorded += frames;

	TimePos len = (tick_t)( m_framesRecorded / Engine::framesPerTick() );
	if( len > m_minLength )
	{
//		m_clip->changeLength( len );
		m_minLength = len;
	}
}




bool SampleRecordHandle::isFinished() const
{
	return false;
}




bool SampleRecordHandle::isFromTrack( const Track * _track ) const
{
	return (m_track == _track || m_patternTrack == _track);
}




f_cnt_t SampleRecordHandle::framesRecorded() const
{
	return( m_framesRecorded );
}




void SampleRecordHandle::writeBuffer( const SampleFrame* _ab, const f_cnt_t _frames )
{
	// D9c (audit grade-B-recording.md): this used to be a per-period
	// `new SampleFrame[_frames]` appended to a QList that grew for the whole
	// take. The accumulator stages the frames into one pre-allocated ring
	// instead, so the audio thread performs no allocation here.
	if (m_accum != nullptr) { m_accum->append( _ab, _frames ); }
}


} // namespace lmms
