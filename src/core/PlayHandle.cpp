/*
 * PlayHandle.cpp - base class PlayHandle - core of rendering engine
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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
 
#include "PlayHandle.h"
#include "AudioEngine.h"
#include "BufferManager.h"
#include "RealtimeScope.h"
#include "Engine.h"
#include "SampleFrame.h"

#include <QThread>


namespace lmms
{

PlayHandle::PlayHandle(const Type type, f_cnt_t offset, bool poolBuffer) :
		m_type(type),
		m_offset(offset),
		m_affinity(QThread::currentThread()),
		m_playHandleBuffer((poolBuffer && RealtimeScope::isActive()) ? BufferManager::acquire() : BufferManager::leaseBlocking()),
		m_bufferReleased(true),
		m_usesBuffer(true),
		m_audioBusHandle(nullptr)
{
}


PlayHandle::~PlayHandle()
{
}


void PlayHandle::doProcessing()
{
	if( m_usesBuffer )
	{
		if (!m_playHandleBuffer) { return; } // exhausted pool: nothing to render into this period
		m_bufferReleased = false;
		const auto frames = m_playHandleBuffer.frames();
		zeroSampleFrames(frames.data(), frames.size());
		play(frames);
	}
	else
	{
		play(std::span<SampleFrame>{});
	}
}


void PlayHandle::releaseBuffer()
{
	m_bufferReleased = true;
}

std::span<SampleFrame> PlayHandle::buffer()
{
	return (m_bufferReleased || !m_playHandleBuffer) ? std::span<SampleFrame>{} : m_playHandleBuffer.frames();
};

} // namespace lmms
