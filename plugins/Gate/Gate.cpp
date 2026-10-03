/*
 * Gate.cpp - a native gate, expander and ducker
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
#include "Gate.h"

#include <algorithm>
#include <cmath>

#include "AudioBuffer.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT gate_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Gate",
	QT_TRANSLATE_NOOP("PluginBrowser", "A gate, expander and ducker, keyed by the channel or a sidechain"),
	"Zene Studio contributors",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("zene-plugin-logo"),
	nullptr,
	nullptr,
};

}

namespace
{

//! The one-pole coefficient that settles in @a ms at @a rate.
float settle(float ms, float rate) { return std::exp(-1.f / std::max(1.f, ms * 0.001f * rate)); }

//! The key's peak at frame @a f: the sidechain when one is routed here, else the signal itself.
float keyPeak(const float* key0, const float* key1, const SampleFrame& frame, f_cnt_t f)
{
	if (key0 != nullptr) { return std::max(std::abs(key0[f]), std::abs(key1[f])); }
	return std::max(std::abs(frame[0]), std::abs(frame[1]));
}

} // namespace

GateEffect::GateEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&gate_plugin_descriptor, parent, key),
	m_controls(this)
{
}

float GateEffect::targetGain(int mode, float levelDb, float thresholdDb, float rangeDb, float ratio)
{
	switch (static_cast<GateControls::Mode>(mode))
	{
	case GateControls::Mode::Expander:
		return levelDb >= thresholdDb ? 1.f : dbfsToAmp(std::max(rangeDb, (levelDb - thresholdDb) * (ratio - 1.f)));
	case GateControls::Mode::Duck:
		return levelDb > thresholdDb ? dbfsToAmp(rangeDb) : 1.f;
	case GateControls::Mode::Gate:
	default:
		return levelDb >= thresholdDb ? 1.f : dbfsToAmp(rangeDb);
	}
}

float GateEffect::held(float target, int mode, f_cnt_t hold)
{
	// Hold: a gate that has just been open stays open for the hold time before it closes.
	if (target >= 1.f) { m_holdLeft = hold; return target; }
	if (m_holdLeft > 0 && mode != static_cast<int>(GateControls::Mode::Duck)) { --m_holdLeft; return 1.f; }
	return target;
}

ProcessStatus GateEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	const float rate = Engine::audioEngine()->outputSampleRate();
	const int mode = m_controls.m_modeModel.value();
	const float threshold = m_controls.m_thresholdModel.value();
	const float range = m_controls.m_rangeModel.value();
	const float ratio = m_controls.m_ratioModel.value();
	const float attack = settle(m_controls.m_attackModel.value(), rate);
	const float release = settle(m_controls.m_releaseModel.value(), rate);
	const auto hold = static_cast<f_cnt_t>(m_controls.m_holdModel.value() * 0.001f * rate);
	const float d = dryLevel();
	const float w = wetLevel();
	const AudioBuffer* key = sidechainBuffer();
	const float* key0 = key != nullptr ? key->buffer(0).data() : nullptr;
	const float* key1 = key != nullptr ? key->buffer(1).data() : nullptr;
	float deepest = 1.f;
	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		auto& frame = inOut.sampleFrameAt(f);
		const float peak = keyPeak(key0, key1, frame, f);
		// The envelope rises at once and falls at the release time: a peak follower.
		m_envelope = peak > m_envelope ? peak : peak + release * (m_envelope - peak);
		const float target = held(targetGain(mode, ampToDbfs(std::max(m_envelope, 1e-6f)), threshold, range, ratio), mode, hold);
		const float coeff = target > m_gain ? attack : release;
		m_gain = target + coeff * (m_gain - target);
		deepest = std::min(deepest, m_gain);
		frame = frame * d + frame * (m_gain * w);
	}
	m_reductionDb = deepest < 1.f ? -ampToDbfs(std::max(deepest, 1e-6f)) : 0.f;
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new GateEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
