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

#include "AutomatableModel.h"

namespace lmms
{

ModulationLayerPublisher::ModulationLayerPublisher()
{
	// -1 for both, never the zero a bare atomic starts at: 0 is a time, and can be a track's id.
	for (auto& track : m_triggerTrack) { track.store(-1, std::memory_order_relaxed); }
	for (auto& at : m_triggeredAt) { at.store(-1.0, std::memory_order_relaxed); }
}

ModulationLayerPublisher::~ModulationLayerPublisher()
{
	for (QMetaObject::Connection& watch : m_watch) { QObject::disconnect(watch); }
}

void ModulationLayerPublisher::publishTriggers()
{
	bool any = false;
	for (int m = 0; m < ModulationLayer::MaxModulators; ++m)
	{
		const auto slot = static_cast<std::size_t>(m);
		const int track = m < m_runtime.sourceCount ? m_runtime.sources[slot].triggerTrack : -1;
		// A modulator that starts following a track (or another one) waits for its first note.
		if (m_triggerTrack[slot].load(std::memory_order_relaxed) != track) { m_triggeredAt[slot].store(-1.0, std::memory_order_relaxed); }
		m_triggerTrack[slot].store(track, std::memory_order_relaxed);
		any = any || track >= 0;
	}
	m_anyTrigger.store(any, std::memory_order_release);
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

double ModulationLayerPublisher::sinceTrigger(int modulator, int triggerTrack, double seconds) const noexcept
{
	if (triggerTrack < 0 || modulator < 0 || modulator >= ModulationLayer::MaxModulators) { return seconds; }
	const double at = m_triggeredAt[static_cast<std::size_t>(modulator)].load(std::memory_order_relaxed);
	return at < 0.0 || seconds < at ? 0.0 : seconds - at;
}

void ModulationLayerPublisher::publishView()
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
		QObject::disconnect(m_watch[slot]);
		m_watch[slot] = QMetaObject::Connection{};
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

		AutomatableModel* model = from.model.data();
		if (model == nullptr) { continue; }
		if (++m_nextToken == 0) { m_nextToken = 1; }
		const std::uint32_t token = m_nextToken;
		to.model = model;
		to.token = token;
		m_alive[slot].store(token, std::memory_order_release);

		// destroyed() is emitted from ~QObject, the same moment a QPointer
		// nulls itself, and on whichever thread destroys the model - hence an
		// atomic, compared against the token so a stale watch cannot clear a
		// slot that was re-issued in the meantime.
		std::atomic<std::uint32_t>* alive = &m_alive[slot];
		m_watch[slot] = QObject::connect(model, &QObject::destroyed, [alive, token]() {
			std::uint32_t expected = token;
			alive->compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
		});
	}
	m_view = view;
}

void applyModulationBlock(const ModulationLayerPublisher& publisher,
	const ModulationAudioView& view, double seconds, ModulationBlock block)
{
	publisher.setBlockSeconds(seconds);  // MX-121: what a note in this block is stamped with
	if (!view.active()) { return; }
	for (int i = 0; i < view.entryCount; ++i)
	{
		const ModulationAudioView::Entry& entry = view.entries[static_cast<std::size_t>(i)];
		// A destroyed (or re-bound) target is skipped, never dereferenced.
		if (entry.model == nullptr || !publisher.targetAlive(i, entry.token)) { continue; }
		// A zero depth is bookkeeping, not a modulation (see the runtime form).
		if (entry.depth == 0.0f) { continue; }
		if (entry.modulator < 0 || entry.modulator >= view.sourceCount) { continue; }
		const ModulatorSource& source = view.sources[static_cast<std::size_t>(entry.modulator)];
		if (!source.active) { continue; }
		// MX-121: a modulator that follows a track runs from that track's last note.
		writeModulatedEntry(entry.model, source, entry.base, entry.depth, entry.minimum, entry.maximum,
			publisher.sinceTrigger(entry.modulator, source.triggerTrack, seconds), entry.perSample, block);
	}
}

} // namespace lmms
