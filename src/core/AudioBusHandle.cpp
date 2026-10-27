/*
 * AudioBusHandle.cpp - ThreadableJob between PlayHandle and MixerChannel
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2025 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#include "AudioBusHandle.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <thread>
#include <QThread>

#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "BufferManager.h"
#include "Engine.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "MixHelpers.h"
#include "RackZoneFeed.h"
#include "RealtimeScope.h"

namespace lmms
{

namespace
{

constexpr qsizetype PlayHandleListLowWater = static_cast<qsizetype>(PlayHandle::MaxNumber / 4);

//! A producer that finds the edit queue full waits this long for the consumer to make progress before it
//! concludes there is none. A render drains the queue every period (a few ms), so a live one is never judged dead.
constexpr std::chrono::milliseconds PlayHandleConsumerProgressTimeout{50};

} // namespace

thread_local bool AudioBusHandle::s_renderStageOwnsList = false;

AudioBusHandle::RenderStageScope::RenderStageScope() noexcept
	: m_previous(s_renderStageOwnsList)
{
	s_renderStageOwnsList = true;
}

AudioBusHandle::RenderStageScope::~RenderStageScope() noexcept
{
	s_renderStageOwnsList = m_previous;
}

AudioBusHandle::AudioBusHandle(const QString& name, bool hasEffectChain,
	FloatModel* volumeModel, FloatModel* panningModel,
	BoolModel* mutedModel) :
	m_bufferUsage(false),
	m_buffer(BufferManager::acquire()),
	m_trackChannels(m_buffer.data()),
	m_bus(&m_trackChannels, 1, m_buffer.size()),
	m_extOutputEnabled(false),
	m_nextMixerChannel(0),
	m_name(name),
	m_effects(hasEffectChain ? new EffectChain(nullptr) : nullptr),
	m_volumeModel(volumeModel),
	m_panningModel(panningModel),
	m_mutedModel(mutedModel)
{
	// Mark all track channels as quiet
	m_bus.quietChannels().set();

	// PDC (#605): preallocate the compensation history on the control thread.
	m_compensation.init(m_buffer.size());
	// Track and instrument buses reserve one full bounded polyphony batch on the
	// control thread. Standalone sample buses created on the audio path use the
	// inline handle slot below until the main thread tops up their list.
	if (!RealtimeScope::isActive() && QThread::currentThread() == Engine::audioEngine()->thread())
	{
		m_playHandles.reserve(static_cast<qsizetype>(PlayHandle::MaxNumber));
	}

	Engine::audioEngine()->addAudioBusHandle(this);
	setExtOutputEnabled(true);
}


int AudioBusHandle::latencyFrames() const
{
	// MX-157: a source that reports no latency adds 0, so such a track is aligned exactly as before.
	return sourceLatencyFrames() + (m_effects ? m_effects->latencyFrames() : 0);
}


void AudioBusHandle::setSourceLatencyFrames(int frames)
{
	m_sourceLatencyFrames.store(std::max(0, frames), std::memory_order_relaxed);
}


void AudioBusHandle::setOffsetFrames(int frames)
{
	const int bound = LatencyCompensation::MaxManualOffsetFrames;
	m_offsetFrames.store(std::clamp(frames, -bound, bound), std::memory_order_relaxed);
}




AudioBusHandle::~AudioBusHandle()
{
	setExtOutputEnabled(false);
	Engine::audioEngine()->removeAudioBusHandle(this);
}




void AudioBusHandle::setExtOutputEnabled(bool enabled)
{
	if (enabled != m_extOutputEnabled)
	{
		m_extOutputEnabled = enabled;
		if (m_extOutputEnabled)
		{
			Engine::audioEngine()->audioDev()->registerPort(this);
		}
		else
		{
			Engine::audioEngine()->audioDev()->unregisterPort(this);
		}
	}
}




void AudioBusHandle::setName(const QString& newName)
{
	m_name = newName;
	Engine::audioEngine()->audioDev()->renamePort(this);
}




bool AudioBusHandle::processEffects()
{
	if (m_effects)
	{
		bool more = m_effects->processAudioBuffer(m_bus);
		return more;
	}
	return false;
}


//! The track's volume and panning, applied to m_buffer in place: sample-exact where a model has a value
//! buffer this period, its value otherwise. One loop for the four cases the code used to spell out (and for
//! a volume model without a panning model, where p = 0 makes both factors 1): the products are the same
//! two multiplies in the same order, so the output is bit-identical to the four-branch version.
void AudioBusHandle::applyVolumeAndPanning(const f_cnt_t fpp)
{
	if (m_volumeModel == nullptr) { return; }  // a panning model alone was never applied
	const ValueBuffer* volBuf = m_volumeModel->valueBuffer();
	const ValueBuffer* panBuf = m_panningModel != nullptr ? m_panningModel->valueBuffer() : nullptr;
	// 0.5.0 F2: an instrument layer's own gain (setGain); x * 1.0f is exactly x, so a unity gain changes no bit.
	const float gain = m_gain.load(std::memory_order_relaxed);
	const float volume = m_volumeModel->value() * 0.01f * gain;
	const float panning = m_panningModel != nullptr ? m_panningModel->value() * 0.01f : 0.0f;
	for (f_cnt_t f = 0; f < fpp; ++f)
	{
		const float v = volBuf != nullptr ? volBuf->values()[f] * 0.01f * gain : volume;
		const float p = panBuf != nullptr ? panBuf->values()[f] * 0.01f : panning;
		m_buffer[f][0] *= (p <= 0 ? 1.0f : 1.0f - p) * v;
		m_buffer[f][1] *= (p >= 0 ? 1.0f : 1.0f + p) * v;
	}
}


void AudioBusHandle::doProcessing()
{
	// Cluster 5467: non-worker off-stage edits land here before this bus reads the list. Worker-stage
	// edits are drained by the render owner after the worker queue joins.
	applyPlayHandleEdits();
	if (m_mutedModel && m_mutedModel->value())
	{
		// Keep the compensation timeline aligned while muted (#605).
		m_compensation.advanceSilence(m_buffer.size());
		return;
	}

	const f_cnt_t fpp = m_buffer.size();

	m_bus.silenceAllChannels();

	//qDebug( "Playhandles: %d", m_playHandles.size() );
	{
		// D6 (mixer concurrency audit): the list was appended to and erased from
		// by other threads under a lock held here. Cluster 5467 removes that lock:
		// non-worker edits are applied above, owner-stage edits run before this bus
		// worker stage, and worker-stage edits are drained after their jobs join.
		// This loop therefore reads a stable list with no concurrent writer.
		// 0.5.0 E20/E38: the notes a key/velocity zone of the channel's rack claims play into that zone's
		// chain, with this track's volume and pan; their buffers are released, so the loop below skips them.
		const auto gain = [this, fpp] { applyVolumeAndPanning(fpp); };
		RackZoneFeed zoneFeed(m_nextMixerChannel, m_effects.get());
		if (zoneFeed.run(m_playHandles, m_buffer, gain)) { m_bus.silenceAllChannels(); }
		const auto mixPlayHandle = [this, fpp](PlayHandle* ph) {
			if (auto phBuffer = ph->buffer(); phBuffer.data() != nullptr)
			{
				assert(phBuffer.size() == fpp);
				if (ph->usesBuffer()
					&& (ph->type() == PlayHandle::Type::NotePlayHandle
						|| !MixHelpers::isSilent(phBuffer.data(), phBuffer.size())))
				{
					m_bufferUsage = true;
					MixHelpers::add(m_buffer.data(), phBuffer.data(), fpp);
				}
				ph->releaseBuffer(); 	// gets rid of playhandle's buffer and sets
										// pointer to null, so if it doesn't get re-acquired we know to skip it next time
			}
		};
		for (PlayHandle* ph : m_playHandles) { mixPlayHandle(ph); }
		if (m_inlinePlayHandle != nullptr) { mixPlayHandle(m_inlinePlayHandle); }
	}

	if (m_bufferUsage)
	{
		applyVolumeAndPanning(fpp);

		const auto sanitized = Engine::audioEngine()->sanitizationEnabled() ? m_bus.sanitizeAll() : false;
		m_corrupted.store(sanitized, std::memory_order_relaxed);

		// Update silence status of all channels for instrument output
		m_bus.updateAll();
	}
	// as of now there's no situation where we only have panning model but no volume model
	// if we have neither, we don't have to do anything here - just pass the audio as is

	// handle effects
	const bool anyOutputAfterEffects = processEffects();

	// PDC (#605): delay this track's output to the alignment point of the
	// mixer channel it feeds. A zero delay is an exact no-op, so a track whose
	// chain reports no latency is bit-identical to the uncompensated path.
	// Q050-339: a manual offset is part of the path's latency as the graph sees it (alignedLatencyFrames), so a
	// positive one delays this track by that much more and a negative one raises the channel's alignment point.
	// Q050-174: a latent instrument monitored in low-latency mode skips its alignment delay (0 is the no-op).
	m_compensation.setDelayFrames(monitorUnaligned()
		? 0 : Engine::mixer()->channelInputLatency(m_nextMixerChannel) - alignedLatencyFrames());
	m_compensation.processInPlace(m_buffer.data(), fpp);

	if (anyOutputAfterEffects || m_bufferUsage || m_compensation.delayFrames() > 0)
	{
		// TODO: improve the flow here - convert to pull model
		Engine::mixer()->mixToChannel(m_bus, m_nextMixerChannel); // send output to mixer
		m_bufferUsage = false;
	}
}


void AudioBusHandle::addPlayHandle(PlayHandle* handle)
{
	topUpPlayHandleCapacityOnControlThread();
	postPlayHandleEdit(PlayHandleEdit{handle, true});
}

void AudioBusHandle::removePlayHandle(PlayHandle* handle)
{
	postPlayHandleEdit(PlayHandleEdit{handle, false});
}

void AudioBusHandle::removePlayHandleUnderEngineChangeLock(PlayHandle* handle)
{
	// Engine removal already holds its change lock, so drain prior edits and apply this one in order.
	applyPlayHandleEdits();
	applyPlayHandleEdit(PlayHandleEdit{handle, false});
}

void AudioBusHandle::postPlayHandleEdit(PlayHandleEdit edit)
{
	if (s_renderStageOwnsList)
	{
		// Note setup and post-worker cleanup run between bus reads. Editing here avoids
		// waiting for a consumer stage that cannot start until this stage returns.
		applyPlayHandleEdit(edit);
		return;
	}
	if (RealtimeScope::isActive() && AudioEngineWorkerThread::deferPlayHandleEdit(this, edit.handle, edit.add))
	{
		return;
	}
	if (m_playHandleEdits.push(edit)) { return; }
	if (!RealtimeScope::isActive() && pushPlayHandleEditWhileConsumerDrains(edit)) { return; }
	if (RealtimeScope::isActive())
	{
		// An unexpected realtime producer has no safe list-ownership handoff here.
		// Count the refusal and return; worker-stage adds normally use fixed buffers,
		// while owner-stage additions defer at capacity for the next period.
		m_droppedPlayHandleEdits.fetch_add(1, std::memory_order_relaxed);
		Engine::audioEngine()->recordDroppedPlayHandleEdit();
		return;
	}

	// Nothing has drained the queue for a while (no render is running, or it is blocked behind a change lock
	// this thread holds). A control producer can wait for the existing engine change lock and apply the edit
	// with rendering stopped: the lock makes it the queue's only consumer.
	AudioEngine* engine = Engine::audioEngine();
	engine->requestChangeInModel();
	m_producerDrainedPlayHandleEdits.fetch_add(1, std::memory_order_relaxed);
	applyPlayHandleEdits();
	applyPlayHandleEdit(edit);
	engine->doneChangeInModel();
}

void AudioBusHandle::applyPlayHandleEdits()
{
	std::size_t remaining = 0;
	for (std::size_t i = 0; i < m_capacityDeferredPlayHandleEditCount; ++i)
	{
		const PlayHandleEdit edit = m_capacityDeferredPlayHandleEdits[i];
		if (!appendPlayHandleWithoutGrowth(edit.handle))
		{
			m_capacityDeferredPlayHandleEdits[remaining++] = edit;
		}
	}
	m_capacityDeferredPlayHandleEditCount = remaining;

	PlayHandleEdit edit{};
	while (m_playHandleEdits.pop(&edit))
	{
		applyPlayHandleEdit(edit);
		m_playHandleEditsConsumed.fetch_add(1, std::memory_order_release);
	}
}

bool AudioBusHandle::pushPlayHandleEditWhileConsumerDrains(const PlayHandleEdit& edit)
{
	using Clock = std::chrono::steady_clock;
	auto seen = m_playHandleEditsConsumed.load(std::memory_order_acquire);
	auto deadline = Clock::now() + PlayHandleConsumerProgressTimeout;
	while (Clock::now() < deadline)
	{
		if (m_playHandleEdits.push(edit)) { return true; }
		std::this_thread::yield();
		const auto now = m_playHandleEditsConsumed.load(std::memory_order_acquire);
		if (now != seen)
		{
			seen = now;
			deadline = Clock::now() + PlayHandleConsumerProgressTimeout;
		}
	}
	return false;
}

void AudioBusHandle::applyPlayHandleEdit(const PlayHandleEdit& edit)
{
	if (edit.add)
	{
		if (appendPlayHandleWithoutGrowth(edit.handle)) { return; }
		m_capacityDeferredPlayHandleEditCountTotal.fetch_add(1, std::memory_order_relaxed);
		if (m_capacityDeferredPlayHandleEditCount < m_capacityDeferredPlayHandleEdits.size())
		{
			m_capacityDeferredPlayHandleEdits[m_capacityDeferredPlayHandleEditCount++] = edit;
		}
		else
		{
			// The static deferral bound exceeded the realistic polyphony range.
			// Keep the refusal visible in the engine's control.ping audio status.
			m_droppedPlayHandleEdits.fetch_add(1, std::memory_order_relaxed);
			Engine::audioEngine()->recordDroppedPlayHandleEdit();
		}
		return;
	}
	PlayHandleList::Iterator it = std::find(m_playHandles.begin(), m_playHandles.end(), edit.handle);
	if (it != m_playHandles.end())
	{
		m_playHandles.erase(it);
		return;
	}
	if (m_inlinePlayHandle == edit.handle)
	{
		m_inlinePlayHandle = nullptr;
		return;
	}
	for (std::size_t i = 0; i < m_capacityDeferredPlayHandleEditCount; ++i)
	{
		if (m_capacityDeferredPlayHandleEdits[i].handle != edit.handle) { continue; }
		for (std::size_t move = i + 1; move < m_capacityDeferredPlayHandleEditCount; ++move)
		{
			m_capacityDeferredPlayHandleEdits[move - 1] = m_capacityDeferredPlayHandleEdits[move];
		}
		--m_capacityDeferredPlayHandleEditCount;
		return;
	}
}

bool AudioBusHandle::appendPlayHandleWithoutGrowth(PlayHandle* handle) noexcept
{
	if (m_playHandles.size() >= m_playHandles.capacity())
	{
		if (m_playHandles.isEmpty() && m_inlinePlayHandle == nullptr)
		{
			m_inlinePlayHandle = handle;
			m_playHandleCapacityLow.store(true, std::memory_order_relaxed);
			Engine::audioEngine()->requestPlayHandleCapacityRefresh();
			return true;
		}
		m_playHandleCapacityLow.store(true, std::memory_order_relaxed);
		Engine::audioEngine()->requestPlayHandleCapacityRefresh();
		return false;
	}
	m_playHandles.append(handle);
	if (m_playHandles.capacity() - m_playHandles.size() <= PlayHandleListLowWater)
	{
		m_playHandleCapacityLow.store(true, std::memory_order_relaxed);
		Engine::audioEngine()->requestPlayHandleCapacityRefresh();
	}
	return true;
}

void AudioBusHandle::topUpPlayHandleCapacityOnControlThread()
{
	if (RealtimeScope::isActive() || QThread::currentThread() != Engine::audioEngine()->thread())
	{
		return;
	}
	Engine::audioEngine()->topUpPlayHandleCapacityOnControlThread();
}


void AudioBusHandle::topUpPlayHandleCapacityUnderEngineChangeLock()
{
	if (!m_playHandleCapacityLow.exchange(false, std::memory_order_relaxed)) { return; }
	const qsizetype capacity = m_playHandles.capacity();
	if (capacity - m_playHandles.size() <= PlayHandleListLowWater)
	{
		m_playHandles.reserve(capacity + static_cast<qsizetype>(PlayHandle::MaxNumber));
	}
}

} // namespace lmms
