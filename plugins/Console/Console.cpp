/*
 * Console.cpp - a native console channel: soft saturation and a console's tone, for every channel of a mix
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

#include "Console.h"

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

Plugin::Descriptor PLUGIN_EXPORT console_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Console",
	QT_TRANSLATE_NOOP("PluginBrowser", "A console channel's saturation and tone, for every channel of a mix"),
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

//! The bias that makes the curve slightly asymmetric (even-order colour).
constexpr float kBias = 0.05f;
//! Q050-088: the glue stage's fixed ballistics, a bus compressor's.
constexpr float kGlueAttackMs = 10.f;
constexpr float kGlueReleaseMs = 100.f;

} // namespace

ConsoleControls::ConsoleControls(ConsoleEffect* effect) :
	ParameterControls(effect, QStringLiteral("ConsoleControls")),
	m_characterModel(this, tr("Character")),
	m_driveModel(30.f, 0.f, 100.f, 0.1f, this, tr("Drive")),
	m_outputModel(0.f, -12.f, 12.f, 0.1f, this, tr("Output")),
	m_oversamplingModel(this, tr("Oversampling")),
	m_glueModel(false, this, tr("Glue")),
	m_glueThresholdModel(-10.f, -40.f, 0.f, 0.1f, this, tr("Glue threshold")),
	m_glueRatioModel(4.f, 1.f, 20.f, 0.1f, this, tr("Glue ratio")),
	m_glueMakeupModel(0.f, 0.f, 24.f, 0.1f, this, tr("Glue makeup"))
{
	m_oversamplingModel.addItem(tr("No oversampling"));
	m_oversamplingModel.addItem(tr("2x oversampling"));
	m_oversamplingModel.addItem(tr("4x oversampling"));
	m_characterModel.addItem(tr("Clean"));
	m_characterModel.addItem(tr("Warm"));
	m_characterModel.addItem(tr("Bright"));
	addParameter(&m_characterModel, QStringLiteral("character"));
	addParameter(&m_driveModel, QStringLiteral("drive"));
	addParameter(&m_outputModel, QStringLiteral("output"));
	addParameter(&m_oversamplingModel, QStringLiteral("oversampling"));
	// Q050-088: an absent key loads its default (AutomatableModel::loadSettings resets it), so the stage is off.
	addParameter(&m_glueModel, QStringLiteral("glue"));
	addParameter(&m_glueThresholdModel, QStringLiteral("gluethreshold"));
	addParameter(&m_glueRatioModel, QStringLiteral("glueratio"));
	addParameter(&m_glueMakeupModel, QStringLiteral("gluemakeup"));
}

ConsoleEffect::ConsoleEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&console_plugin_descriptor, parent, key),
	m_controls(this)
{
}

float ConsoleEffect::saturate(float x, float drive)
{
	// tanh around a small bias, its offset taken out and its slope at 0 normalised to 1.
	const float offset = std::tanh(drive * kBias);
	const float slope = drive * (1.f - offset * offset);
	return (std::tanh(drive * (x + kBias)) - offset) / slope;
}

float ConsoleEffect::glueGainDb(float levelDb, float thresholdDb, float ratio)
{
	if (levelDb <= thresholdDb || ratio <= 1.f) { return 0.f; }
	return (levelDb - thresholdDb) * (1.f / ratio - 1.f);
}

void ConsoleEffect::glue(SampleFrame& frame, float attack, float release)
{
	// Stereo-linked: the louder side sets the gain both take, so the image does not move.
	const float level = std::max(std::abs(frame[0]), std::abs(frame[1]));
	const float levelDb = level > 1e-6f ? ampToDbfs(level) : -120.f;
	const float target = glueGainDb(levelDb, m_controls.m_glueThresholdModel.value(), m_controls.m_glueRatioModel.value());
	// Falling gain (more reduction) moves at the attack, rising gain at the release - one pole each.
	const float coefficient = target < m_glueEnvelopeDb ? attack : release;
	m_glueEnvelopeDb = target + coefficient * (m_glueEnvelopeDb - target);
	const float gain = dbfsToAmp(m_glueEnvelopeDb + m_controls.m_glueMakeupModel.value());
	frame[0] *= gain;
	frame[1] *= gain;
}

void ConsoleEffect::design(Character character, float rate)
{
	const int stages = std::clamp(m_controls.m_oversamplingModel.value(), 0, MaxStages);
	if (stages != m_stages || rate != m_rate)
	{
		// Plain arithmetic on fixed members (the half-band coefficients), as SlewDistortion does.
		for (int c = 0; c < 2; ++c)
		{
			m_upsampler[static_cast<std::size_t>(c)].setup(stages, rate);
			m_downsampler[static_cast<std::size_t>(c)].setup(stages, rate);
		}
		m_stages = stages;
	}
	if (static_cast<int>(character) == m_character && rate == m_rate) { return; }
	m_dcBlock.set(designBiquad(BiquadShape::HighPass, 10.0, 0.7071, rate));
	const double lowDb = character == Character::Warm ? 1.0 : 0.0;
	const double highDb = character == Character::Warm ? -1.5 : character == Character::Bright ? 1.5 : 0.0;
	m_low.set(designBiquad(BiquadShape::LowShelf, 120.0, 0.7071, rate, lowDb));
	m_high.set(designBiquad(BiquadShape::HighShelf, character == Character::Warm ? 9000.0 : 10000.0, 0.7071, rate, highDb));
	m_character = static_cast<int>(character);
	m_rate = rate;
}

float ConsoleEffect::saturated(int c, float x, float drive)
{
	if (m_stages == 0) { return saturate(x, drive); }
	auto& over = m_over[static_cast<std::size_t>(c)];
	m_upsampler[static_cast<std::size_t>(c)].processSample(over.data(), x);
	for (int i = 0; i < (1 << m_stages); ++i) { over[static_cast<std::size_t>(i)] = saturate(over[static_cast<std::size_t>(i)], drive); }
	return m_downsampler[static_cast<std::size_t>(c)].processSample(over.data());
}

ProcessStatus ConsoleEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	const float rate = Engine::audioEngine()->outputSampleRate();
	design(static_cast<Character>(m_controls.m_characterModel.value()), rate);
	const float drive = dbfsToAmp(-12.f + 20.f * m_controls.m_driveModel.value() / 100.f);
	const float output = dbfsToAmp(m_controls.m_outputModel.value());
	const float d = dryLevel();
	const float w = wetLevel();
	const bool glued = m_controls.m_glueModel.value();
	const float attack = std::exp(-1000.f / (kGlueAttackMs * rate));
	const float release = std::exp(-1000.f / (kGlueReleaseMs * rate));
	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		auto& frame = inOut.sampleFrameAt(f);
		SampleFrame shaped;
		for (int c = 0; c < 2; ++c)
		{
			const float driven = m_dcBlock.run(saturated(c, frame[c], drive), c);
			shaped[c] = m_high.run(m_low.run(driven, c), c);
		}
		if (glued) { glue(shaped, attack, release); }  // Q050-088: skipped while off - the pre-glue arithmetic, bit for bit
		frame = frame * d + shaped * output * w;
	}
	if (!glued) { m_glueEnvelopeDb = 0.f; }  // switched back on, it starts from no reduction
	m_reductionDb.store(-m_glueEnvelopeDb, std::memory_order_relaxed);
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new ConsoleEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
