/*
 * SampleClipEffects.cpp - an audio clip's own realtime effects (Q050-111)
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

/*! The clip half of Q050-111. Each SampleClip owns one EffectChain (SampleClip::clipEffects()); this file holds
 *  its part of the clip's state - the copy a duplicated or split clip carries, the `<fxchain>` child a project
 *  saves - and the per-period processing the clip's play handle calls. It lives apart from SampleClip.cpp, an
 *  upstream file, so the inherited file only gains the calls into it.
 *
 *  Where the processing sits is the whole point of the feature: SamplePlayHandle::play renders ONE clip's audio
 *  into the handle's own buffer, and AudioBusHandle sums the track's handles into the track buffer only after
 *  that (src/core/AudioBusHandle.cpp, mixPlayHandle). A clip's chain run here therefore sees that clip alone -
 *  a sibling clip on the same track, overlapping or not, never passes through it - while the track's own chain
 *  still runs on the sum, after it. */

#include "SampleClip.h"

#include <QDomDocument>
#include <QDomElement>

#include "AudioBus.h"
#include "EffectChain.h"

namespace lmms
{

void SampleClip::copyClipEffects(const SampleClip& orig)
{
	// The copy gets devices of its own, built from the source's chain document - the source's instances
	// stay where they are. A source with no device copies nothing (and costs nothing).
	if (orig.m_clipEffects->effects().empty()) { return; }
	QDomDocument doc;
	QDomElement holder = doc.createElement(QStringLiteral("clip"));
	doc.appendChild(holder);
	orig.saveClipEffects(doc, holder);
	loadClipEffects(holder);
}


void SampleClip::saveClipEffects(QDomDocument& doc, QDomElement& element) const
{
	// Additive: a clip without a device writes nothing, so every project that never used the feature serialises
	// byte for byte as it did. The chain's own saveState writes the devices' plug-in keys, settings, order and
	// each one's on/off (bypass) state - the same document a track's chain writes.
	if (m_clipEffects->effects().empty()) { return; }
	m_clipEffects->saveState(doc, element);
}


void SampleClip::loadClipEffects(const QDomElement& element)
{
	const QDomElement chain = element.firstChildElement(m_clipEffects->nodeName());
	if (!chain.isNull())
	{
		m_clipEffects->restoreState(chain);
		return;
	}
	// Reset on absence: an older project, or a checkpoint taken before the clip's first device, has no chain -
	// so a restore over a clip that has since gained devices takes them off. A clip that has none already is
	// left alone, so loading a project that never used the feature takes no model-change lock per clip.
	if (!m_clipEffects->effects().empty()) { m_clipEffects->clear(); }
}


void SampleClip::processClipEffects(std::span<SampleFrame> buffer) noexcept
{
	EffectChain* chain = m_clipEffects.get();
	// Every clip without a device returns here: one load of the chain's switch and one of its list size, both
	// stable for the render period (edits to either take the engine's model-change guard, which excludes it).
	if (!chain->isEnabled() || chain->effects().empty()) { return; }
	// One runner per period. The live schedule holds one handle per clip (SampleTrack::play's playing latch),
	// so this never fails there; if two handles of one clip ever rendered at once on two workers, the second
	// plays dry rather than run the chain's state concurrently. A flag, not a lock: nothing ever waits on it.
	if (m_clipEffectsBusy.exchange(true, std::memory_order_acquire)) { return; }
	SampleFrame* channels[] = {buffer.data()};
	AudioBus bus{channels, 1, static_cast<f_cnt_t>(buffer.size())};
	chain->processAudioBuffer(bus);
	m_clipEffectsBusy.store(false, std::memory_order_release);
}

} // namespace lmms
