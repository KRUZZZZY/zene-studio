/*
 * ModulationLayerPublisher.cpp - the audio thread's view of the modulation layer
 *
 * The publisher's out-of-line half: building the plain-value ModulationAudioView
 * the audio thread copies, and the liveness watches that stand in for the
 * QPointer the view must not carry (include/ModulationLayer.h, "Realtime
 * contract"). The authored layer and its control-side runtime stay in
 * ModulationLayer.cpp.
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
 */

#include "ModulationLayer.h"

#include <QObject>
#include <thread>

#include "AutomatableModel.h"
#include "Instrument.h"

namespace lmms
{

namespace
{

void removeUnfollowedVoices(ModulationVoices& voices,
	const std::array<std::atomic<int>, ModulationLayer::MaxModulators>& triggerTrack,
	const std::array<std::atomic<bool>, ModulationLayer::MaxModulators>& perVoice)
{
	for (int slot = 0; slot < ModulationVoices::VoiceCount; ++slot)
	{
		ModulationVoice voice;
		if (!voices.voice(slot, &voice)) { continue; }
		bool followed = false;
		for (std::size_t m = 0; m < triggerTrack.size() && !followed; ++m)
		{
			followed = perVoice[m].load(std::memory_order_relaxed)
				&& triggerTrack[m].load(std::memory_order_relaxed) == voice.track;
		}
		if (!followed) { voices.noteOff(voice.track, voice.channel, voice.key); }
	}
}

} // namespace

ModulationLayerPublisher::ModulationLayerPublisher()
{
	// -1 for both, never the zero a bare atomic starts at: 0 is a time, and can be a track's id.
	for (auto& track : m_triggerTrack) { track.store(-1, std::memory_order_relaxed); }
	for (auto& at : m_triggeredAt) { at.store(-1.0, std::memory_order_relaxed); }
}

ModulationLayerPublisher::~ModulationLayerPublisher()
{
	for (QMetaObject::Connection& watch : m_watch) { QObject::disconnect(watch); }
	for (QMetaObject::Connection& watch : m_instrumentWatch) { QObject::disconnect(watch); }
}

void ModulationLayerPublisher::publishTriggers()
{
	bool any = false;
	bool anyPerVoice = false;
	for (int m = 0; m < ModulationLayer::MaxModulators; ++m)
	{
		const auto slot = static_cast<std::size_t>(m);
		const int track = m < m_runtime.sourceCount ? m_runtime.sources[slot].triggerTrack : -1;
		// MX-120: a per-voice modulator counts the notes of its trigger track.
		const bool perVoice = track >= 0 && m_runtime.sources[slot].perVoice;
		m_perVoice[slot].store(perVoice, std::memory_order_relaxed);
		anyPerVoice = anyPerVoice || perVoice;
		// A modulator that starts following a track (or another one) waits for its first note.
		if (m_triggerTrack[slot].load(std::memory_order_relaxed) != track) { m_triggeredAt[slot].store(-1.0, std::memory_order_relaxed); }
		m_triggerTrack[slot].store(track, std::memory_order_relaxed);
		any = any || track >= 0;
	}
	m_anyTrigger.store(any, std::memory_order_release);
	m_anyPerVoice.store(anyPerVoice, std::memory_order_release);
	if (!anyPerVoice) { m_voices.clear(); }
	else { removeUnfollowedVoices(m_voices, m_triggerTrack, m_perVoice); }
}

void ModulationLayerPublisher::voiceChanged(int trackId, bool on, int channel, int key, f_cnt_t offset) noexcept
{
	if (!m_anyPerVoice.load(std::memory_order_acquire)) { return; }
	bool followed = false;
	for (std::size_t m = 0; m < m_perVoice.size() && !followed; ++m)
	{
		followed = m_perVoice[m].load(std::memory_order_relaxed)
			&& m_triggerTrack[m].load(std::memory_order_relaxed) == trackId;
	}
	if (!followed) { return; }
	if (!on)
	{
		m_voices.noteOff(trackId, channel, key);
		return;
	}
	const sample_rate_t rate = m_blockRate.load(std::memory_order_relaxed);
	const double late = rate > 0 ? static_cast<double>(offset) / static_cast<double>(rate) : 0.0;
	m_voices.noteOn(trackId, channel, key, m_blockSeconds.load(std::memory_order_relaxed) + late);
}

void ModulationLayerPublisher::noteStarted(int trackId) noexcept
{
	if (!m_anyTrigger.load(std::memory_order_acquire)) { return; }
	const double now = m_blockSeconds.load(std::memory_order_relaxed);
	for (std::size_t m = 0; m < m_triggerTrack.size(); ++m)
	{
		if (m_triggerTrack[m].load(std::memory_order_relaxed) == trackId) { m_triggeredAt[m].store(now, std::memory_order_relaxed); }
	}
}

void ModulationLayerPublisher::retireTarget(std::atomic<std::uint32_t>& alive,
	std::atomic<std::uint64_t>& writers, std::uint32_t token)
{
	std::uint64_t state = writers.load(std::memory_order_acquire);
	while (targetGeneration(state) == token)
	{
		if ((targetWriterState(state) & targetClosingBit) == 0)
		{
			const std::uint64_t closed = state | targetClosingBit;
			if (!writers.compare_exchange_weak(state, closed, std::memory_order_acq_rel,
				std::memory_order_acquire))
			{
				continue;
			}
			state = closed;
		}

		while (targetGeneration(state) == token
			&& (targetWriterState(state) & targetWriterCountMask) != 0)
		{
			std::this_thread::yield();
			state = writers.load(std::memory_order_acquire);
		}
		if (targetGeneration(state) != token) { return; }

		std::uint32_t expected = token;
		alive.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
		return;
	}
}

double ModulationLayerPublisher::sinceTrigger(int modulator, int triggerTrack, double seconds) const noexcept
{
	if (triggerTrack < 0 || modulator < 0 || modulator >= ModulationLayer::MaxModulators) { return seconds; }
	const double at = m_triggeredAt[static_cast<std::size_t>(modulator)].load(std::memory_order_relaxed);
	return at < 0.0 || seconds < at ? 0.0 : seconds - at;
}

void ModulationLayerPublisher::publishView(ModulationAudioView& staged)
{
	m_hasLayer.store(m_layer.shouldPersist(), std::memory_order_release);
	publishTriggers();  // MX-121

	ModulationAudioView view;
	view.sources = m_runtime.sources;
	view.sourceCount = m_runtime.sourceCount;
	view.entryCount = m_runtime.entryCount;
	for (std::size_t slot = 0; slot < m_watch.size(); ++slot)
	{
		// Every slot is re-issued: a view published before this edit carries
		// the old tokens, so it skips the slot rather than writing a target
		// that may since have been re-bound.
		auto& writers = m_targetWriters[slot];
		std::uint64_t writerState = writers.load(std::memory_order_acquire);
		while ((targetWriterState(writerState) & targetClosingBit) == 0)
		{
			const std::uint64_t closed = writerState | targetClosingBit;
			if (writers.compare_exchange_weak(writerState, closed, std::memory_order_acq_rel,
				std::memory_order_acquire))
			{
				writerState = closed;
				break;
			}
		}
		while ((targetWriterState(writerState) & targetWriterCountMask) != 0)
		{
			std::this_thread::yield();
			writerState = writers.load(std::memory_order_acquire);
		}
		QObject::disconnect(m_watch[slot]);
		m_watch[slot] = QMetaObject::Connection{};
		QObject::disconnect(m_instrumentWatch[slot]);
		m_instrumentWatch[slot] = QMetaObject::Connection{};
		m_alive[slot].store(0, std::memory_order_release);
		if (slot >= static_cast<std::size_t>(m_runtime.entryCount)) { continue; }

		const ModulationRuntime::Entry& from = m_runtime.entries[slot];
		ModulationAudioView::Entry& to = view.entries[slot];
		to.modulator = from.modulator;
		to.depth = from.depth;
		to.perSample = from.perSample;
		to.base = from.base;
		to.minimum = from.minimum;
		to.maximum = from.maximum;
		to.instrument = from.instrument;  // MX-120
		to.noteTarget = from.noteTarget;

		AutomatableModel* model = from.model.data();
		if (model == nullptr) { continue; }
		if (++m_nextToken == 0) { m_nextToken = 1; }
		const std::uint32_t token = m_nextToken;
		to.model = model;
		to.token = token;
		m_alive[slot].store(token, std::memory_order_release);

		// Close the slot before AutomatableModel tears down its value state, and
		// wait off the audio thread for any write that already pinned this token.
		writers.store(targetGateState(token, targetClosingBit), std::memory_order_release);
		std::atomic<std::uint32_t>* alive = &m_alive[slot];
		std::atomic<std::uint64_t>* writersPtr = &writers;
		m_watch[slot] = QObject::connect(model, &AutomatableModel::aboutToBeDestroyed,
			[alive, writersPtr, token] {
				ModulationLayerPublisher::retireTarget(*alive, *writersPtr, token);
			});
		if (from.instrument != nullptr)
		{
			m_instrumentWatch[slot] = QObject::connect(from.instrument, &Instrument::aboutToBeDestroyed,
				[alive, writersPtr, token] {
					ModulationLayerPublisher::retireTarget(*alive, *writersPtr, token);
				});
		}
		writers.store(targetGateState(token, 0), std::memory_order_release);
	}
	// The staged view is handed to the caller, which swaps it into m_view
	// inside the reader-visible write window. Nothing here is reader-visible
	// yet, so the audio thread keeps reading the last published view while
	// this waits for the writers of the targets it rebinds.
	staged = view;
}

namespace
{

/*! MX-120: one per-voice entry - each sounding note of the modulator's trigger track gets its own cycle
 *  (from the note's start) and the value goes to the instrument for that note only. Allocation-free: a
 *  bounded walk of the fixed voice pool. */
void applyVoiceEntry(const ModulationLayerPublisher& publisher, const ModulationAudioView::Entry& entry,
	const ModulatorSource& source, double seconds)
{
	const float range = entry.maximum - entry.minimum;
	ModulationVoice voice;
	for (int slot = 0; slot < ModulationVoices::VoiceCount; ++slot)
	{
		if (!publisher.voices().voice(slot, &voice) || voice.track != source.triggerTrack) { continue; }
		const double since = seconds > voice.startedAt ? seconds - voice.startedAt : 0.0;
		const float value = modulatedValue(source, entry.base, entry.depth, entry.minimum, entry.maximum, since);
		const float normalized = range > 0.0f ? (value - entry.minimum) / range : 0.0f;
		entry.instrument->modulateNote(entry.noteTarget, voice.channel, voice.key, value - entry.base, normalized);
	}
}

/*! The source entry @a i is driven by this block, or nullptr: a destroyed (or re-bound) target is skipped,
 *  never dereferenced; a zero depth is bookkeeping, not a modulation (see the runtime form); an inactive
 *  source drives nothing. */
const ModulatorSource* drivingSource(const ModulationLayerPublisher& publisher, const ModulationAudioView& view, int i)
{
	const ModulationAudioView::Entry& entry = view.entries[static_cast<std::size_t>(i)];
	if (entry.model == nullptr || !publisher.targetAlive(i, entry.token)) { return nullptr; }
	if (entry.depth == 0.0f) { return nullptr; }
	if (entry.modulator < 0 || entry.modulator >= view.sourceCount) { return nullptr; }
	const ModulatorSource& source = view.sources[static_cast<std::size_t>(entry.modulator)];
	return source.active ? &source : nullptr;
}

//! MX-120: a per-voice modulator's entry whose instrument takes per-note values for its parameter.
bool perNoteEntry(const ModulatorSource& source, const ModulationAudioView::Entry& entry)
{
	return source.perVoice && source.triggerTrack >= 0 && entry.instrument != nullptr && entry.noteTarget >= 0;
}

} // namespace

void applyModulationBlock(const ModulationLayerPublisher& publisher,
	const ModulationAudioView& view, double seconds, ModulationBlock block)
{
	publisher.setBlockSeconds(seconds);  // MX-121: what a note in this block is stamped with
	publisher.setBlockRate(block.sampleRate);  // MX-120: and its frame offset
	if (!view.active()) { return; }
	for (int i = 0; i < view.entryCount; ++i)
	{
		const ModulationAudioView::Entry& entry = view.entries[static_cast<std::size_t>(i)];
		const ModulatorSource* source = drivingSource(publisher, view, i);
		if (source == nullptr) { continue; }
		if (!publisher.beginTargetWrite(i, entry.token)) { continue; }
		struct TargetWritePin
		{
			const ModulationLayerPublisher& publisher;
			int slot;
			~TargetWritePin() { publisher.endTargetWrite(slot); }
		} targetPin{publisher, i};
		// MX-120: per note where the instrument takes it; otherwise the newest note drives the parameter.
		if (perNoteEntry(*source, entry))
		{
			applyVoiceEntry(publisher, entry, *source, seconds);
			continue;
		}
		// MX-121: a modulator that follows a track runs from that track's last note.
		writeModulatedEntry(entry.model, *source, entry.base, entry.depth, entry.minimum, entry.maximum,
			publisher.sinceTrigger(entry.modulator, source->triggerTrack, seconds), entry.perSample, block);
	}
}

} // namespace lmms
