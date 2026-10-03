/*
 * TransientShaper.cpp - a native transient shaper: the attacks and the tails turned up or down
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

#include "TransientShaper.h"

#include <algorithm>
#include <cmath>

#include "AudioEngine.h"
#include "BiquadDesign.h"
#include "Engine.h"
#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT transientshaper_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Transient Shaper",
	QT_TRANSLATE_NOOP("PluginBrowser", "Turns a sound's attacks and tails up or down"),
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

constexpr float kMaxDifferenceDb = 24.f;

//! One envelope follower step: up at @a up, down at @a down (one-pole coefficients).
float follow(float envelope, float level, float up, float down)
{
	return level + (level > envelope ? up : down) * (envelope - level);
}

} // namespace

TransientShaperControls::TransientShaperControls(TransientShaperEffect* effect) :
	ParameterControls(effect, QStringLiteral("TransientShaperControls")),
	m_attackModel(0.f, -100.f, 100.f, 0.1f, this, tr("Attack")),
	m_sustainModel(0.f, -100.f, 100.f, 0.1f, this, tr("Sustain")),
	m_outputModel(0.f, -24.f, 24.f, 0.1f, this, tr("Output"))
{
	addParameter(&m_attackModel, QStringLiteral("attack"));
	addParameter(&m_sustainModel, QStringLiteral("sustain"));
	addParameter(&m_outputModel, QStringLiteral("output"));
}

TransientShaperEffect::TransientShaperEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&transientshaper_plugin_descriptor, parent, key),
	m_controls(this)
{
}

float TransientShaperEffect::gainDb(float differenceDb, float attackPercent, float sustainPercent)
{
	const float difference = std::clamp(differenceDb, -kMaxDifferenceDb, kMaxDifferenceDb);
	// Half the envelopes' difference at 100 %: at most +-12 dB on an attack or a tail.
	return 0.5f * (difference > 0.f ? difference * attackPercent / 100.f : -difference * sustainPercent / 100.f);
}

ProcessStatus TransientShaperEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	const float rate = Engine::audioEngine()->outputSampleRate();
	const float fastUp = settleCoefficient(0.5f, rate);
	const float fastDown = settleCoefficient(20.f, rate);
	const float slowUp = settleCoefficient(20.f, rate);
	const float slowDown = settleCoefficient(250.f, rate);
	const float attack = m_controls.m_attackModel.value();
	const float sustain = m_controls.m_sustainModel.value();
	const float output = m_controls.m_outputModel.value();
	const float d = dryLevel();
	const float w = wetLevel();
	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		auto& frame = inOut.sampleFrameAt(f);
		const float level = std::max(std::abs(frame[0]), std::abs(frame[1]));
		m_fast = follow(m_fast, level, fastUp, fastDown);
		m_slow = follow(m_slow, level, slowUp, slowDown);
		const float difference = ampToDbfs(std::max(m_fast, 1e-6f)) - ampToDbfs(std::max(m_slow, 1e-6f));
		frame = frame * d + frame * (dbfsToAmp(gainDb(difference, attack, sustain) + output) * w);
	}
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new TransientShaperEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
