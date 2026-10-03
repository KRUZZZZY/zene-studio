/*
 * MixerPostInserts.cpp - a mixer channel's post-fader inserts (0.5.0 MX-082)
 *
 * Copyright (c) 2026 Zene Studio contributors
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
#include "MixerPostInserts.h"

#include <algorithm>

#include <QDomElement>

#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"

namespace lmms
{

namespace
{

const QString ElementName = QStringLiteral("postinserts");

//! Whether a receiver of @a sender through @a route (nullptr: the master output) gets the baked signal.
bool bakedFor(const MixerChannel& sender, const MixerRoute* route)
{
	return sender.m_postInserts.baked() && (route == nullptr || !route->feedback());
}

} // namespace

PostFaderInserts::PostFaderInserts() :
	m_preFader(Engine::audioEngine()->framesPerPeriod())
{
}

float PostFaderInserts::process(MixerChannel& channel, float volume, f_cnt_t frames)
{
	m_baked = false;
	if (m_chain.effects().empty() || m_preFader.size() < frames) { return volume; }
	std::copy_n(channel.m_buffer, frames, m_preFader.begin());
	const float* const post0 = channel.m_postFaderBuffer.buffer(0).data();
	const float* const post1 = channel.m_postFaderBuffer.buffer(1).data();
	for (f_cnt_t f = 0; f < frames; ++f) { channel.m_buffer[f] = SampleFrame(post0[f], post1[f]); }
	// Claim audio (1 = quiet): a device after the fader must not sleep on a stale flag.
	channel.m_bus.quietChannels().reset(0);
	channel.m_bus.quietChannels().reset(1);
	m_chain.processAudioBuffer(channel.m_bus);
	m_baked = true;
	return 1.f;
}

void PostFaderInserts::save(QDomDocument& doc, QDomElement& channelElement)
{
	if (m_chain.effects().empty()) { return; }
	QDomElement element = doc.createElement(ElementName);
	channelElement.appendChild(element);
	m_chain.saveState(doc, element);
}

void PostFaderInserts::load(const QDomElement& channelElement)
{
	const QDomElement element = channelElement.firstChildElement(ElementName);
	if (element.isNull())
	{
		clear();
		return;
	}
	m_chain.restoreState(element.firstChildElement(m_chain.nodeName()));
}

void PostFaderInserts::clear()
{
	if (!m_chain.effects().empty()) { m_chain.clear(); }
}

namespace postinserts
{

float faderValue(const MixerChannel& sender, const MixerRoute* route)
{
	return bakedFor(sender, route) ? 1.f : sender.m_volumeModel.value();
}

ValueBuffer* faderBuffer(MixerChannel& sender, const MixerRoute* route)
{
	return bakedFor(sender, route) ? nullptr : sender.m_volumeModel.valueBuffer();
}

const SampleFrame* sendSource(const MixerChannel& sender, const MixerRoute& route)
{
	// A muted sender is not processed this period (its flag is last period's): its silent buffer, never a stale copy.
	const bool keptPreFader = sender.m_postInserts.baked() && route.preFader() && !route.feedback()
		&& !sender.m_muted.load(std::memory_order_relaxed);
	return keptPreFader ? sender.m_postInserts.preFader() : sender.m_buffer;
}

} // namespace postinserts

} // namespace lmms
