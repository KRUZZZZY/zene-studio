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

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>

#include <QCoreApplication>
#include <QPointer>
#include <QTimer>

#include "AudioEngine.h"
#include "Engine.h"
#include "MpscQueue.h"
#include "PatternTrack.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "SampleRecordAccumulator.h"
#include "Song.h"
#include "Timeline.h"


namespace lmms
{


//! What a handle hands back to the main thread: the accumulator (and its drain thread) and the
//! clip the take goes to. QPointer, so a clip deleted mid-take is simply not written to.
struct PendingRecordTake
{
	std::unique_ptr<SampleRecordAccumulator> accum;
	QPointer<SampleClip> clip;
	bool claimed = false;  //!< the clip claimed it: it was recording (see SampleRecordHandle::markClaimed)
};


namespace
{

//! Takes queued by destroyed handles (any thread, pushes) for the main thread (the one consumer).
//! 256 slots: the main-thread reaper runs every 10 ms, so this only fills if it stops running.
MpscQueue<PendingRecordTake*, 256> s_retiredTakes;
std::atomic<std::uint64_t> s_framesDropped{0};
std::atomic<std::uint64_t> s_framesPadded{0};
std::atomic<std::uint64_t> s_unpreparedTakes{0};
//! Takes that could not be queued (the queue was full): leaked rather than freed on the audio thread.
std::atomic<std::uint64_t> s_queueOverflows{0};

//! Clips armed for recording, main thread only. A QPointer per clip: a deleted clip drops out.
std::vector<QPointer<SampleClip>> s_armedClips;

//! Main thread only: start the reaper once. Without a running event loop (no QCoreApplication)
//! takes are pumped only by an explicit pumpTakes() call.
void ensureReaperTimer()
{
	static QTimer* timer = nullptr;
	if (timer != nullptr || qApp == nullptr) { return; }
	timer = new QTimer(qApp);
	timer->setInterval(10);
	QObject::connect(timer, &QTimer::timeout, qApp, [] { SampleRecordHandle::pumpTakes(); });
	timer->start();
}

} // namespace


SampleRecordHandle::SampleRecordHandle( SampleClip* clip ) :
	PlayHandle( Type::SamplePlayHandle ),
	m_take( new PendingRecordTake{ std::make_unique<SampleRecordAccumulator>(
		Engine::audioEngine() != nullptr ? Engine::audioEngine()->inputSampleRate() : 44100 ), clip } ),
	m_accum( m_take->accum.get() ),
	m_framesRecorded( 0 ),
	m_minLength( clip->length() ),
	m_track( clip->getTrack() ),
	m_patternTrack( nullptr ),
	m_clip( clip ),
	// R2.2: the round trip this take lags the playback by - dropped from its head, so the
	// take lines up with what was playing when it was performed. A cached atomic: this
	// constructor runs on the audio thread (SampleTrack::play).
	m_compensationFrames( Engine::audioEngine() != nullptr ? Engine::audioEngine()->recordingLatencyFrames() : 0 ),
	m_offsetApplied( false )
{
	// AudioEngine::addPlayHandle registers every handle with its bus, and this one had none:
	// the first record-armed clip that reached the engine dereferenced a null bus (BUGS_FOUND
	// 11.8 - unreachable until the window gate below stopped hiding it). It joins its track's
	// bus and uses no buffer, so the bus mixes nothing from it.
	setAudioBusHandle( static_cast<SampleTrack*>( clip->getTrack() )->audioBusHandle() );
	setUsesBuffer( false );
	ensureReaperTimer();
}




SampleRecordHandle::~SampleRecordHandle()
{
	// The engine destroys a handle on the audio thread when it is removed, so this must not
	// allocate, join the drain thread or touch the model. The accumulator is told to finish
	// (the drain thread assembles the take on its own) and the take is queued for the main
	// thread, which installs it in reapRetiredTakes().
	if (m_take == nullptr) { return; }
	m_accum->requestFinish();
	m_take->claimed = m_claimed;
	if (!s_retiredTakes.push(m_take)) { s_queueOverflows.fetch_add(1, std::memory_order_relaxed); }
	m_take = nullptr;
	m_accum = nullptr;
}




void SampleRecordHandle::reapRetiredTakes()
{
	PendingRecordTake* taken = nullptr;
	while (s_retiredTakes.pop(&taken))
	{
		std::unique_ptr<PendingRecordTake> take{ taken };
		// finish() joins the drain thread first, so the counts below are final.
		auto buffer = take->accum->finish();
		if (const auto dropped = take->accum->framesDropped(); dropped > 0)
		{
			s_framesDropped.fetch_add(dropped, std::memory_order_relaxed);
			s_framesPadded.fetch_add(take->accum->framesPadded(), std::memory_order_relaxed);
			qWarning("Clip recording lost %llu frames to a full capture ring; the take is padded with silence",
				static_cast<unsigned long long>(dropped));
		}
		// A take the clip never claimed was never recording: it only stops its drain thread.
		if (!take->claimed) { continue; }
		if (SampleClip* clip = take->clip.data())
		{
			// The transport still records: this pass ended at a loop wrap or a jump, so the take is a
			// lane take and the clip stays armed for the next pass.
			const Song* song = Engine::getSong();
			const bool continues = clip->isRecord() && song->isRecording() && song->isPlaying();
			clip->acceptRecordedTake(std::move(buffer), take->accum->framesDropped(), continues);
		}
	}
}




void SampleRecordHandle::pumpTakes()
{
	reapRetiredTakes();
	// Index loop: preparing a take may watch (append to) the list.
	for (std::size_t i = 0; i < s_armedClips.size(); )
	{
		SampleClip* clip = s_armedClips[i].data();
		if (clip == nullptr || !clip->isRecord())
		{
			s_armedClips.erase(s_armedClips.begin() + static_cast<std::ptrdiff_t>(i));
			continue;
		}
		clip->prepareRecordTake();
		++i;
	}
}




void SampleRecordHandle::watchArmedClip(SampleClip* clip)
{
	for (const auto& watched : s_armedClips)
	{
		if (watched.data() == clip) { return; }
	}
	s_armedClips.emplace_back(clip);
}




std::uint64_t SampleRecordHandle::framesPaddedTotal() noexcept
{
	return s_framesPadded.load(std::memory_order_relaxed);
}




std::uint64_t SampleRecordHandle::queueOverflows() noexcept
{
	return s_queueOverflows.load(std::memory_order_relaxed);
}




std::uint64_t SampleRecordHandle::framesDroppedTotal() noexcept
{
	return s_framesDropped.load(std::memory_order_relaxed);
}




std::uint64_t SampleRecordHandle::unpreparedTakes() noexcept
{
	return s_unpreparedTakes.load(std::memory_order_relaxed);
}




void SampleRecordHandle::countUnpreparedTake() noexcept
{
	s_unpreparedTakes.fetch_add(1, std::memory_order_relaxed);
}




namespace
{

//! What a take records outside an armed punch region: silence the length of the longest period,
//! made once at start-up, so the audio thread never allocates for it.
const std::array<SampleFrame, MAXIMUM_BUFFER_SIZE> s_punchSilence{};


} // namespace

void SampleRecordHandle::writePunched( const SampleFrame* period, f_cnt_t from, f_cnt_t to, const PunchFrames& punch )
{
	const auto silence = [this]( f_cnt_t count ) {
		for( ; count > 0; )
		{
			const f_cnt_t n = std::min( count, static_cast<f_cnt_t>( s_punchSilence.size() ) );
			writeBuffer( s_punchSilence.data(), n );
			count -= n;
		}
	};
	const f_cnt_t liveFrom = std::clamp( punch.begin, from, to );
	const f_cnt_t liveTo = std::clamp( punch.end, liveFrom, to );
	silence( liveFrom - from );
	if( liveTo > liveFrom ) { writeBuffer( period + liveFrom, liveTo - liveFrom ); }
	silence( to - liveTo );
}

void SampleRecordHandle::play( std::span<SampleFrame> /*buffer*/ )
{
	const SampleFrame* recbuf = Engine::audioEngine()->inputBuffer();
	f_cnt_t frames = Engine::audioEngine()->inputBufferFrames();
	// E3: the same frame-accurate punch gate the record routes get (AudioEngine::punchWindow),
	// in the period's own frame indices - taken before the head of the period is skipped.
	const PunchFrames punch = Engine::audioEngine()->punchWindow( frames );
	const f_cnt_t periodFrames = frames;
	// The clip started m_offset frames into this period (SampleTrack::play -> setOffset),
	// so the input before that point was performed before the take began: drop it with
	// the round trip, as SamplePlayHandle leaves the same frames silent on the way out.
	// It used to be recorded, and a take that started on a tick inside a period came out
	// late by the offset (BUGS_FOUND 11.17, RecordingLatencyTest).
	if( !m_offsetApplied )
	{
		m_compensationFrames += offset();
		m_offsetApplied = true;
	}
	// A period before the device's capture has delivered anything: the time passes and
	// there is no input, so the round trip's staging block this period stands for is one
	// the take never sees - count it as skipped. It used to be skipped again from the
	// first REAL input, and a take armed on the device's first period came out one period
	// early (-255 frames; BUGS_FOUND 11.29, RecordingLatencyTest under load).
	if( frames == 0 && !Engine::audioEngine()->inputStreamStarted() )
	{
		m_compensationFrames -= std::min( m_compensationFrames,
			static_cast<f_cnt_t>( Engine::audioEngine()->framesPerPeriod() ) );
	}
	const f_cnt_t skipped = std::min( frames, m_compensationFrames );
	m_compensationFrames -= skipped;
	recbuf += skipped;
	frames -= skipped;
	// Outside the punch window the take records silence, kept in place, so what is captured
	// inside the region stays aligned to the clip.
	writePunched( recbuf - skipped, skipped, periodFrames, punch );
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
