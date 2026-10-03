/*
 * Modulation.cpp - a native chorus, phaser and tremolo
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
#include "Modulation.h"

#include <cmath>
#include <numbers>

#include "AudioEngine.h"
#include "Engine.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT modulation_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Modulation",
	QT_TRANSLATE_NOOP("PluginBrowser", "Chorus, phaser and tremolo"),
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

constexpr float MaxDelaySeconds = 0.06f;
constexpr float MaxRate = 192000.f;
constexpr float TwoPi = 2.f * std::numbers::pi_v<float>;

} // namespace

ModulationEffect::ModulationEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&modulation_plugin_descriptor, parent, key),
	m_controls(this)
{
	for (auto& line : m_delay) { line.assign(static_cast<std::size_t>(MaxDelaySeconds * MaxRate) + 4, 0.f); }
}

float ModulationEffect::chorus(int channel, float in, float lfo, float rate)
{
	std::vector<float>& line = m_delay[static_cast<std::size_t>(channel)];
	const float delay = (0.015f + 0.010f * m_controls.m_depthModel.value() * lfo) * rate;
	const float read = static_cast<float>(m_write) - std::max(1.f, delay);
	const float wrapped = read < 0.f ? read + static_cast<float>(line.size()) : read;
	const auto i0 = static_cast<std::size_t>(wrapped);
	const float frac = wrapped - static_cast<float>(i0);
	const float out = line[i0 % line.size()] * (1.f - frac) + line[(i0 + 1) % line.size()] * frac;
	line[m_write] = in + out * m_controls.m_feedbackModel.value();
	return 0.5f * (in + out);  // the voice beside the signal: a chorus at the device's full wet
}

float ModulationEffect::phaser(int channel, float in, float lfo, float rate)
{
	const float hz = 200.f + 4000.f * m_controls.m_depthModel.value() * (0.5f + 0.5f * lfo);
	const float t = std::tan(std::numbers::pi_v<float> * std::min(hz, rate * 0.45f) / rate);
	const float a = (t - 1.f) / (t + 1.f);
	auto& state = m_allpass[static_cast<std::size_t>(channel)];
	float x = in + m_feedback[static_cast<std::size_t>(channel)] * m_controls.m_feedbackModel.value();
	for (float& z : state)
	{
		const float y = a * x + z;
		z = x - a * y;
		x = y;
	}
	m_feedback[static_cast<std::size_t>(channel)] = x;
	return 0.5f * (in + x);  // the notches come from the sum with the signal
}

ProcessStatus ModulationEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	const float rate = Engine::audioEngine()->outputSampleRate();
	const auto mode = static_cast<ModulationControls::Mode>(m_controls.m_modeModel.value());
	const double step = m_controls.m_rateModel.value() / rate;
	const float spread = m_controls.m_spreadModel.value() / 360.f;
	const float depth = m_controls.m_depthModel.value();
	const float d = dryLevel();
	const float w = wetLevel();
	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		auto& frame = inOut.sampleFrameAt(f);
		for (int ch = 0; ch < 2; ++ch)
		{
			const float lfo = std::sin(TwoPi * static_cast<float>(m_phase + (ch == 1 ? spread : 0.f)));
			const float in = frame[ch];
			float wet = in;
			if (mode == ModulationControls::Mode::Chorus) { wet = chorus(ch, in, lfo, rate); }
			else if (mode == ModulationControls::Mode::Phaser) { wet = phaser(ch, in, lfo, rate); }
			else { wet = in * (1.f - depth * (0.5f + 0.5f * lfo)); }
			frame[ch] = in * d + wet * w;
		}
		m_write = (m_write + 1) % m_delay[0].size();
		m_phase += step;
		if (m_phase >= 1.0) { m_phase -= 1.0; }
	}
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new ModulationEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
