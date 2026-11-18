/*
 * DeEsser.cpp - a native de-esser: the high band turned down while it is loud
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

#include "DeEsser.h"

#include <algorithm>
#include <cmath>

#include "AudioEngine.h"
#include "Engine.h"
#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT deesser_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"De-esser",
	QT_TRANSLATE_NOOP("PluginBrowser", "Turns down sibilance: the high band, while it is loud"),
	"Zene Studio contributors",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("zene-plugin-logo"),
	nullptr,
	nullptr,
};

}

DeEsserControls::DeEsserControls(DeEsserEffect* effect) :
	ParameterControls(effect, QStringLiteral("DeEsserControls")),
	m_modeModel(this, tr("Mode")),
	m_frequencyModel(6000.f, 2000.f, 16000.f, 1.f, this, tr("Frequency")),
	m_thresholdModel(-30.f, -60.f, 0.f, 0.1f, this, tr("Threshold")),
	m_rangeModel(12.f, 0.f, 24.f, 0.1f, this, tr("Range")),
	m_listenModel(false, this, tr("Listen"))
{
	m_modeModel.addItem(tr("Split (the high band only)"));
	m_modeModel.addItem(tr("Wide (the whole signal)"));
	addParameter(&m_modeModel, QStringLiteral("mode"));
	addParameter(&m_frequencyModel, QStringLiteral("frequency"));
	addParameter(&m_thresholdModel, QStringLiteral("threshold"));
	addParameter(&m_rangeModel, QStringLiteral("range"));
	addParameter(&m_listenModel, QStringLiteral("listen"));
}

DeEsserEffect::DeEsserEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&deesser_plugin_descriptor, parent, key),
	m_controls(this)
{
}

float DeEsserEffect::reductionDb(float levelDb, float thresholdDb, float rangeDb)
{
	const float over = levelDb - thresholdDb;
	return over <= 0.f ? 0.f : std::min(rangeDb, over * 0.75f);
}

void DeEsserEffect::design(float frequency, float rate)
{
	if (frequency == m_frequency && rate == m_rate) { return; }
	m_detector.set(designBiquad(BiquadShape::HighPass, frequency, 1.0 / std::sqrt(2.0), rate));
	m_low.set(designBiquad(BiquadShape::LowPass, frequency, 1.0 / std::sqrt(2.0), rate));
	m_frequency = frequency;
	m_rate = rate;
}

SampleFrame DeEsserEffect::shaped(const SampleFrame& in, Mode mode, bool listen)
{
	// The high band is what the low-pass leaves: low + high is the input exactly, in phase, so
	// turning only the high part down cancels it cleanly and leaves the lows as they were.
	const SampleFrame high(in[0] - m_low.run(in[0], 0), in[1] - m_low.run(in[1], 1));
	if (listen) { return high; }
	if (mode == Mode::Split) { return in + high * (m_gain - 1.f); }
	return in * m_gain;
}

ProcessStatus DeEsserEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	const float rate = Engine::audioEngine()->outputSampleRate();
	design(m_controls.m_frequencyModel.value(), rate);
	const auto mode = static_cast<Mode>(m_controls.m_modeModel.value());
	const bool listen = m_controls.m_listenModel.value();
	const float threshold = m_controls.m_thresholdModel.value();
	const float range = m_controls.m_rangeModel.value();
	const float attack = settleCoefficient(1.f, rate);
	const float release = settleCoefficient(60.f, rate);
	const float d = dryLevel();
	const float w = wetLevel();
	float deepest = 1.f;
	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		auto& frame = inOut.sampleFrameAt(f);
		// Linked detection: the louder of the two high bands, not their sum. The sum of sibilance
		// that is opposite in phase across the channels is near silence, so a mono-sum detector
		// hears nothing in exactly the material a de-esser exists for.
		const float level = std::max(std::abs(m_detector.run(frame[0], 0)), std::abs(m_detector.run(frame[1], 1)));
		m_envelope = level + (level > m_envelope ? attack : release) * (m_envelope - level);
		const float target = dbfsToAmp(-reductionDb(ampToDbfs(std::max(m_envelope, 1e-6f)), threshold, range));
		m_gain = target + (target < m_gain ? attack : release) * (m_gain - target);
		deepest = std::min(deepest, m_gain);
		// Listen is a monitor: the band alone, whatever the device's dry/wet mix says.
		frame = listen ? shaped(frame, mode, true) : frame * d + shaped(frame, mode, false) * w;
	}
	m_reductionDb.store(deepest < 1.f ? -ampToDbfs(std::max(deepest, 1e-6f)) : 0.f, std::memory_order_relaxed);
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new DeEsserEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
