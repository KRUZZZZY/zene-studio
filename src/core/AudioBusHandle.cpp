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

#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BufferManager.h"
#include "Engine.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "MixHelpers.h"
#include "RackZoneFeed.h"
#include "RealtimeScope.h"

namespace lmms
{

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
	// Cluster 5467: the play-handle edits other threads posted since the last period land here, before
	// anything reads the list (this is the bus's only consumer, and it runs once per period).
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
		// edits are queued (postPlayHandleEdit) and applied only by
		// applyPlayHandleEdits() above, on this thread, so the list has one writer
		// and one reader and nothing can reallocate or erase under this loop.
		// 0.5.0 E20/E38: the notes a key/velocity zone of the channel's rack claims play into that zone's
		// chain, with this track's volume and pan; their buffers are released, so the loop below skips them.
		const auto gain = [this, fpp] { applyVolumeAndPanning(fpp); };
		RackZoneFeed zoneFeed(m_nextMixerChannel, m_effects.get());
		if (zoneFeed.run(m_playHandles, m_buffer, gain)) { m_bus.silenceAllChannels(); }
		for (PlayHandle* ph : m_playHandles) // now we mix all playhandle buffers into our internal buffer
		{
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
		}
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
	m_compensation.setDelayFrames(
		Engine::mixer()->channelInputLatency(m_nextMixerChannel) - latencyFrames());
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
	if (m_playHandleEdits.push(edit)) { return; }
	if (RealtimeScope::isActive())
	{
		// An unexpected realtime producer has no safe list-ownership handoff here.
		// Count the refusal and return; note setup uses RenderStageScope and never drops.
		m_droppedRealtimePlayHandleEdits.fetch_add(1, std::memory_order_relaxed);
		return;
	}

	// A control producer can wait for the existing engine change lock and apply the
	// edit with rendering stopped. It never spins waiting for the audio consumer.
	AudioEngine* engine = Engine::audioEngine();
	engine->requestChangeInModel();
	applyPlayHandleEdits();
	applyPlayHandleEdit(edit);
	engine->doneChangeInModel();
}

void AudioBusHandle::applyPlayHandleEdits()
{
	PlayHandleEdit edit{};
	while (m_playHandleEdits.pop(&edit)) { applyPlayHandleEdit(edit); }
}

void AudioBusHandle::applyPlayHandleEdit(const PlayHandleEdit& edit)
{
	if (edit.add)
	{
		m_playHandles.append(edit.handle);
		return;
	}
	PlayHandleList::Iterator it = std::find(m_playHandles.begin(), m_playHandles.end(), edit.handle);
	if (it != m_playHandles.end())
	{
		m_playHandles.erase(it);
	}
}

} // namespace lmms
