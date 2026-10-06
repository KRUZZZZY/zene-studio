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

#include <cassert>

#include <QMutexLocker>

#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "BufferManager.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "Engine.h"
#include "MixHelpers.h"
#include "RackZoneFeed.h"

namespace lmms
{

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
	return m_effects ? m_effects->latencyFrames() : 0;
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
		// D6 (mixer concurrency audit): addPlayHandle()/removePlayHandle()
		// append to and erase from this QList under m_playHandleLock, and the
		// lock is taken here for the same range: without it a reallocation or
		// an erase under a live iteration is a use-after-free (the audited
		// trace is a live note-on from the MIDI thread, which reaches
		// AudioEngine::addPlayHandle -> AudioBusHandle::addPlayHandle while
		// this loop is running on a worker). The critical section is a
		// pointer-array scan plus the list's own append/erase.
		const QMutexLocker playHandleLockGuard(&m_playHandleLock);
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
	QMutexLocker lockGuard(&m_playHandleLock);
	m_playHandles.append(handle);
}


void AudioBusHandle::removePlayHandle(PlayHandle* handle)
{
	QMutexLocker lockGuard(&m_playHandleLock);
	PlayHandleList::Iterator it = std::find(m_playHandles.begin(), m_playHandles.end(), handle);
	if (it != m_playHandles.end())
	{
		m_playHandles.erase(it);
	}
}

} // namespace lmms
