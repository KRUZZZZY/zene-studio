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

	if (m_bufferUsage)
	{
		// handle volume and panning
		// has both vol and pan models
		if (m_volumeModel && m_panningModel)
		{
			ValueBuffer* volBuf = m_volumeModel->valueBuffer();
			ValueBuffer* panBuf = m_panningModel->valueBuffer();

			// both vol and pan have s.ex.data:
			if (volBuf && panBuf)
			{
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float v = volBuf->values()[f] * 0.01f;
					float p = panBuf->values()[f] * 0.01f;
					m_buffer[f][0] *= (p <= 0 ? 1.0f : 1.0f - p) * v;
					m_buffer[f][1] *= (p >= 0 ? 1.0f : 1.0f + p) * v;
				}
			}

			// only vol has s.ex.data:
			else if (volBuf)
			{
				float p = m_panningModel->value() * 0.01f;
				float l = (p <= 0 ? 1.0f : 1.0f - p);
				float r = (p >= 0 ? 1.0f : 1.0f + p);
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float v = volBuf->values()[f] * 0.01f;
					m_buffer[f][0] *= v * l;
					m_buffer[f][1] *= v * r;
				}
			}

			// only pan has s.ex.data:
			else if (panBuf)
			{
				float v = m_volumeModel->value() * 0.01f;
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float p = panBuf->values()[f] * 0.01f;
					m_buffer[f][0] *= (p <= 0 ? 1.0f : 1.0f - p) * v;
					m_buffer[f][1] *= (p >= 0 ? 1.0f : 1.0f + p) * v;
				}
			}

			// neither has s.ex.data:
			else
			{
				float p = m_panningModel->value() * 0.01f;
				float v = m_volumeModel->value() * 0.01f;
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					m_buffer[f][0] *= (p <= 0 ? 1.0f : 1.0f - p) * v;
					m_buffer[f][1] *= (p >= 0 ? 1.0f : 1.0f + p) * v;
				}
			}
		}

		// has vol model only
		else if (m_volumeModel)
		{
			ValueBuffer* volBuf = m_volumeModel->valueBuffer();

			if (volBuf)
			{
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					float v = volBuf->values()[f] * 0.01f;
					m_buffer[f][0] *= v;
					m_buffer[f][1] *= v;
				}
			}
			else
			{
				float v = m_volumeModel->value() * 0.01f;
				for (f_cnt_t f = 0; f < fpp; ++f)
				{
					m_buffer[f][0] *= v;
					m_buffer[f][1] *= v;
				}
			}
		}

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
