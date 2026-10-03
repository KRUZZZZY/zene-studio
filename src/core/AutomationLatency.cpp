/*
 * AutomationLatency.cpp - automation read where the latent audio is (0.5.0 MX-160)
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
#include "AutomationLatency.h"

#include <algorithm>
#include <cmath>

#include <QDomElement>
#include <QTimer>

#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerPostInserts.h"
#include "PatternStore.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

namespace
{

//! How often the control thread re-reads the latencies while compensation is on.
constexpr int PollMs = 250;

//! The device @a model belongs to (its controls are its QObject children), or nullptr.
const Effect* owningEffect(const AutomatableModel* model)
{
	for (const QObject* object = model; object != nullptr; object = object->parent())
	{
		if (const auto* effect = dynamic_cast<const Effect*>(object)) { return effect; }
	}
	return nullptr;
}

//! The latency of the running devices of @a chain before @a stop.
int framesBefore(const EffectChain& chain, const Effect* stop)
{
	if (chain.latencyFrames() <= 0) { return 0; }  // a disabled chain, or one with no latent device
	int frames = 0;
	for (const Effect* effect : chain.effects())
	{
		if (effect == stop) { break; }
		if (effect->isEnabled() && effect->isOkay() && !effect->dontRun()) { frames += std::max(0, effect->latencyFrames()); }
	}
	return frames;
}

//! Where @a chain starts on channel @a channel after PDC, or -1 when it is none of the channel's chains.
int channelChainStart(const MixerChannel& channel, const EffectChain* chain)
{
	const int input = channel.inputLatencyFrames();
	if (chain == &channel.m_fxChain) { return input; }
	for (int i = 1; i < channel.m_rack.chainCount(); ++i)
	{
		if (chain == channel.m_rack.chain(i)) { return input; }  // a rack's parallel chains start at the input too
	}
	if (chain == &channel.m_postInserts.chain()) { return input + std::max(0, channel.m_fxChain.latencyFrames()); }
	return -1;
}

//! Where @a chain starts after PDC: a mixer channel's alignment point, or 0 for a track's chain.
int chainStart(const EffectChain* chain)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < mixer->numChannels(); ++i)
	{
		const int start = channelChainStart(*mixer->mixerChannel(i), chain);
		if (start >= 0) { return start; }
	}
	return 0;
}

//! A mixer channel's fader or pan sits after its inserts: their latency; -1 for any other model.
int faderFrames(const AutomatableModel* model)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < mixer->numChannels(); ++i)
	{
		const MixerChannel* channel = mixer->mixerChannel(i);
		if (model == &channel->m_volumeModel || model == &channel->m_panModel)
		{
			return channel->inputLatencyFrames() + std::max(0, channel->m_fxChain.latencyFrames());
		}
	}
	return -1;
}

//! Every automation clip of the song, the pattern store and the global automation track.
template<typename Visit>
void forEachAutomationClip(Visit visit)
{
	std::vector<Track*> tracks = Engine::getSong()->tracks();
	tracks.insert(tracks.end(), Engine::patternStore()->tracks().begin(), Engine::patternStore()->tracks().end());
	tracks.push_back(Engine::getSong()->globalAutomationTrack());
	for (Track* track : tracks)
	{
		for (Clip* clip : track->getClips())
		{
			if (auto* automation = dynamic_cast<AutomationClip*>(clip)) { visit(automation); }
		}
	}
}

} // namespace

AutomationLatency& AutomationLatency::instance()
{
	static AutomationLatency* latency = [] {
		auto* made = new AutomationLatency();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *latency;
}

void AutomationLatency::setEnabled(bool on)
{
	m_enabled.store(on, std::memory_order_relaxed);
	refresh();
	poll(on);
}

void AutomationLatency::poll(bool on)
{
	if (on && m_poll == nullptr)
	{
		m_poll = new QTimer();
		m_poll->setInterval(PollMs);
		QObject::connect(m_poll, &QTimer::timeout, [this] { refresh(); });
	}
	if (m_poll == nullptr) { return; }
	if (on) { m_poll->start(); }
	else { m_poll->stop(); }
}

void AutomationLatency::refresh()
{
	Song* song = Engine::getSong();
	// Mid-load the track list is still being built (a load processes events): the next poll catches up.
	if (song == nullptr || song->isLoadingProject() || Engine::mixer() == nullptr || Engine::patternStore() == nullptr) { return; }
	const bool on = enabled();
	forEachAutomationClip([on](AutomationClip* clip) {
		clip->setLatencyFrames(on && !clip->objects().empty() ? upstreamFrames(clip->firstObject()) : 0);
	});
}

int AutomationLatency::upstreamFrames(const AutomatableModel* model)
{
	if (model == nullptr) { return 0; }
	if (const Effect* effect = owningEffect(model); effect != nullptr && effect->effectChain() != nullptr)
	{
		return chainStart(effect->effectChain()) + framesBefore(*effect->effectChain(), effect);
	}
	return std::max(0, faderFrames(model));
}

int AutomationLatency::compensatedClips(int* largestFrames)
{
	int clips = 0;
	*largestFrames = 0;
	if (Engine::getSong() == nullptr || Engine::patternStore() == nullptr) { return 0; }
	forEachAutomationClip([&clips, largestFrames](AutomationClip* clip) {
		if (clip->latencyFrames() <= 0) { return; }
		++clips;
		*largestFrames = std::max(*largestFrames, clip->latencyFrames());
	});
	return clips;
}

TimePos AutomationLatency::readTime(const Clip* clip, TimePos time) const
{
	if (!enabled()) { return time; }
	const auto* automation = dynamic_cast<const AutomationClip*>(clip);
	const int frames = automation != nullptr ? automation->latencyFrames() : 0;
	return frames > 0 ? TimePos(time.getTicks() - ticksFor(frames, Engine::framesPerTick())) : time;
}

void AutomationLatency::shiftBlock(const AutomationClip* clip, double framesPerTick, TimePos* start, int* frameOffsetInTick) const
{
	if (!enabled() || clip == nullptr || clip->latencyFrames() <= 0) { return; }
	if (!shiftFrames(clip->latencyFrames(), framesPerTick, start, frameOffsetInTick)) { *start = TimePos(-1); }
}

int AutomationLatency::ticksFor(int latencyFrames, double framesPerTick)
{
	if (latencyFrames <= 0 || framesPerTick <= 0.0) { return 0; }
	return static_cast<int>(std::lround(static_cast<double>(latencyFrames) / framesPerTick));
}

bool AutomationLatency::shiftFrames(int latencyFrames, double framesPerTick, TimePos* start, int* frameOffsetInTick)
{
	if (latencyFrames <= 0 || framesPerTick <= 0.0) { return true; }
	const double at = static_cast<double>(start->getTicks()) * framesPerTick + *frameOffsetInTick - latencyFrames;
	if (at < 0.0) { return false; }
	const auto tick = static_cast<tick_t>(std::floor(at / framesPerTick));
	const int lastFrame = std::max(0, static_cast<int>(std::ceil(framesPerTick)) - 1);
	*start = TimePos(tick);
	*frameOffsetInTick = std::clamp(static_cast<int>(std::floor(at - static_cast<double>(tick) * framesPerTick)), 0, lastFrame);
	return true;
}

void AutomationLatency::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(elementName());
	element.setAttribute(QStringLiteral("enabled"), 1);
	parent.appendChild(element);
}

void AutomationLatency::load(const QDomElement& element)
{
	// The tracks may not be loaded yet: the poll computes the clips' latencies once the project is in.
	m_enabled.store(element.attribute(QStringLiteral("enabled"), QStringLiteral("0")).toInt() != 0, std::memory_order_relaxed);
	poll(enabled());
}

void AutomationLatency::clear()
{
	// The project's clips are going away: no walk here, a new clip starts at 0.
	m_enabled.store(false, std::memory_order_relaxed);
	poll(false);
}

} // namespace lmms
