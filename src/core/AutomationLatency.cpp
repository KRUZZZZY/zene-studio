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
#include <memory>
#include <thread>

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

//! A mixed-target clip is rejected while compensation is enabled because it has no valid shared read time.
constexpr int IncompatibleTargetLatencies = -1;

int sharedTargetLatency(const AutomationClip::objectVector& targets, bool on)
{
	if (!on || targets.empty()) { return 0; }
	int frames = IncompatibleTargetLatencies;
	for (const auto& target : targets)
	{
		if (target == nullptr) { continue; }
		const int targetFrames = AutomationLatency::upstreamFrames(target.data());
		if (frames >= 0 && targetFrames != frames) { return IncompatibleTargetLatencies; }
		frames = targetFrames;
	}
	return frames < 0 ? 0 : frames;
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

AutomationLatency::ReadSnapshot::ReadSnapshot(const AutomationLatency* owner, std::uint32_t state) :
	m_owner(owner), m_state(state)
{
}

AutomationLatency::ReadSnapshot::ReadSnapshot(ReadSnapshot&& other) noexcept :
	m_owner(other.m_owner), m_state(other.m_state)
{
	other.m_owner = nullptr;
}

AutomationLatency::ReadSnapshot& AutomationLatency::ReadSnapshot::operator=(ReadSnapshot&& other) noexcept
{
	if (this == &other) { return *this; }
	if (m_owner != nullptr) { m_owner->releaseSnapshot(m_state); }
	m_owner = other.m_owner;
	m_state = other.m_state;
	other.m_owner = nullptr;
	return *this;
}

AutomationLatency::ReadSnapshot::~ReadSnapshot()
{
	if (m_owner != nullptr) { m_owner->releaseSnapshot(m_state); }
}

bool AutomationLatency::ReadSnapshot::enabled() const
{
	return (m_state & EnabledBit) != 0;
}

int AutomationLatency::ReadSnapshot::latencyFrames(const AutomationClip* clip) const
{
	// An unpinned fallback (its acquisition raced the final validation) reads no bank at all.
	return (clip == nullptr || m_owner == nullptr) ? 0 : clip->latencyFramesForSnapshot(m_state & SlotBit);
}

bool AutomationLatency::enabled() const
{
	return (m_state.load(std::memory_order_acquire) & EnabledBit) != 0;
}

AutomationLatency::ReadSnapshot AutomationLatency::readSnapshot() const
{
	// Strictly bounded and non-waiting (audio thread): register on the published bank and validate that
	// the publication did not change while registering. A publication that raced the registration is
	// retried at most SnapshotAttempts times; after that the snapshot pins NO bank and reads this block
	// uncompensated - coherently for every clip - so a refresh can never make the audio thread wait or
	// spin unboundedly. The writer side (refreshClips) is where waiting for or skipping a busy bank
	// lives, on the control thread.
	for (int attempt = 0; attempt < SnapshotAttempts; ++attempt)
	{
		const std::uint32_t state = m_state.load(std::memory_order_seq_cst);
		const std::uint32_t slot = state & SlotBit;
		m_readers[slot].fetch_add(1, std::memory_order_seq_cst);
		if (m_state.load(std::memory_order_seq_cst) == state) { return ReadSnapshot(this, state); }
		m_readers[slot].fetch_sub(1, std::memory_order_seq_cst);
	}
	return ReadSnapshot(nullptr, 0);
}

void AutomationLatency::releaseSnapshot(std::uint32_t state) const
{
	m_readers[state & SlotBit].fetch_sub(1, std::memory_order_seq_cst);
}

void AutomationLatency::setEnabledState(bool on)
{
	const std::uint32_t state = m_state.load(std::memory_order_seq_cst);
	const std::uint32_t generation = (state + GenerationStep) & ~(SlotBit | EnabledBit);
	m_state.store(generation | (state & SlotBit) | (on ? EnabledBit : 0u), std::memory_order_seq_cst);
}

void AutomationLatency::setEnabled(bool on)
{
	// Readers capture the switch and bank together; enabling publishes only after refresh has filled its bank.
	if (!on) { setEnabledState(false); }
	refreshAs(on);
	if (on) { setEnabledState(true); }
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

void AutomationLatency::refresh() { refreshAs(enabled(), false); }

void AutomationLatency::refreshAfterProjectLoad()
{
	if (Engine::mixer() == nullptr || Engine::patternStore() == nullptr) { return; }
	refreshClips(enabled(), true);
}

void AutomationLatency::refreshAs(bool on, bool waitForFreeBank)
{
	Song* song = Engine::getSong();
	// Mid-load the track list is still being built (a load processes events): the load's own walk catches up.
	if (song == nullptr || song->isLoadingProject() || Engine::mixer() == nullptr || Engine::patternStore() == nullptr) { return; }
	refreshClips(on, waitForFreeBank);
}

bool AutomationLatency::refreshClips(bool on, bool waitForFreeBank)
{
	std::uint32_t state = m_state.load(std::memory_order_seq_cst);
	std::uint32_t slot = (state & SlotBit) ^ 1u;
	while (m_readers[slot].load(std::memory_order_seq_cst) != 0)
	{
		if (!waitForFreeBank) { return false; }
		std::this_thread::yield();
		state = m_state.load(std::memory_order_seq_cst);
		slot = (state & SlotBit) ^ 1u;
	}
	forEachAutomationClip([on, slot](AutomationClip* clip) {
		const auto targets = clip->objectsSnapshot();
		clip->setLatencyFramesForSnapshot(slot, sharedTargetLatency(targets, on));
	});
	state = m_state.load(std::memory_order_seq_cst);
	const std::uint32_t generation = (state + GenerationStep) & ~(SlotBit | EnabledBit);
	m_state.store(generation | (state & EnabledBit) | slot, std::memory_order_seq_cst);
	return true;
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
	const auto snapshot = instance().readSnapshot();
	forEachAutomationClip([&clips, largestFrames, &snapshot](AutomationClip* clip) {
		const int frames = snapshot.latencyFrames(clip);
		if (frames <= 0) { return; }
		++clips;
		*largestFrames = std::max(*largestFrames, frames);
	});
	return clips;
}

TimePos AutomationLatency::readTime(const Clip* clip, TimePos time) const
{
	const auto snapshot = readSnapshot();
	return readTime(clip, time, snapshot);
}

TimePos AutomationLatency::readTime(const Clip* clip, TimePos time, const ReadSnapshot& snapshot) const
{
	if (!snapshot.enabled()) { return time; }
	const auto* automation = dynamic_cast<const AutomationClip*>(clip);
	const int frames = snapshot.latencyFrames(automation);
	return frames > 0 ? TimePos(time.getTicks() - ticksFor(frames, Engine::framesPerTick())) : time;
}

void AutomationLatency::shiftBlock(const AutomationClip* clip, double framesPerTick, TimePos* start, int* frameOffsetInTick) const
{
	const auto snapshot = readSnapshot();
	shiftBlock(clip, framesPerTick, start, frameOffsetInTick, snapshot);
}

void AutomationLatency::shiftBlock(const AutomationClip* clip, double framesPerTick, TimePos* start,
	int* frameOffsetInTick, const ReadSnapshot& snapshot) const
{
	if (!snapshot.enabled() || clip == nullptr) { return; }
	const int frames = snapshot.latencyFrames(clip);
	if (frames <= 0) { return; }
	if (!shiftFrames(frames, framesPerTick, start, frameOffsetInTick)) { *start = TimePos(-1); }
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
	setEnabledState(element.attribute(QStringLiteral("enabled"), QStringLiteral("0")).toInt() != 0);
	poll(enabled());
	if (enabled()) { refreshOnceLoaded(); }
}

void AutomationLatency::refreshOnceLoaded()
{
	Song* song = Engine::getSong();
	if (song == nullptr || m_poll == nullptr) { return; }
	// One shot: by Song::projectLoaded the clips have resolved their parameters and the chains are in, so an
	// offline render that starts right after the load already reads compensated (the channels' PDC alignment
	// is published by the first audio period, and the poll takes it up).
	auto connection = std::make_shared<QMetaObject::Connection>();
	*connection = QObject::connect(song, &Song::projectLoaded, m_poll, [this, connection] {
		QObject::disconnect(*connection);
		refreshClips(enabled(), true);
	});
}

void AutomationLatency::clear()
{
	// The project's clips are going away: no walk here, a new clip starts at 0.
	setEnabledState(false);
	poll(false);
}

} // namespace lmms
