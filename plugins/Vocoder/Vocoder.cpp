/*
 * Vocoder.cpp - a native channel vocoder: a voice's spectrum imposed on the channel's sound
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

#include "Vocoder.h"

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

Plugin::Descriptor PLUGIN_EXPORT vocoder_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Vocoder",
	QT_TRANSLATE_NOOP("PluginBrowser", "Imposes a voice (the sidechain) on the channel's sound, band by band"),
	"Zene Studio contributors",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("zene-plugin-logo"),
	nullptr,
	nullptr,
};

}

VocoderControls::VocoderControls(VocoderEffect* effect) :
	ParameterControls(effect, QStringLiteral("VocoderControls")),
	m_sourceModel(this, tr("Voice")),
	m_bandsModel(16.f, 4.f, static_cast<float>(VocoderEffect::MaxBands), 1.f, this, tr("Bands")),
	m_lowModel(120.f, 40.f, 1000.f, 1.f, this, tr("Low")),
	m_highModel(8000.f, 1000.f, 16000.f, 1.f, this, tr("High")),
	m_widthModel(1.f, 0.25f, 4.f, 0.01f, this, tr("Band width")),
	m_releaseModel(40.f, 5.f, 500.f, 1.f, this, tr("Release")),
	m_outputModel(0.f, -24.f, 24.f, 0.1f, this, tr("Output"))
{
	m_sourceModel.addItem(tr("Sidechain input"));
	m_sourceModel.addItem(tr("Left side (carrier on the right)"));
	addParameter(&m_sourceModel, QStringLiteral("source"));
	addParameter(&m_bandsModel, QStringLiteral("bands"));
	addParameter(&m_lowModel, QStringLiteral("low"));
	addParameter(&m_highModel, QStringLiteral("high"));
	addParameter(&m_widthModel, QStringLiteral("width"));
	addParameter(&m_releaseModel, QStringLiteral("release"));
	addParameter(&m_outputModel, QStringLiteral("output"));
}

VocoderEffect::VocoderEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&vocoder_plugin_descriptor, parent, key),
	m_controls(this)
{
}

void VocoderEffect::configure(int bands, float low, float high, float width, float rate)
{
	if (bands == m_bands && low == m_low && high == m_high && width == m_width && rate == m_rate) { return; }
	const double top = std::max(static_cast<double>(high), static_cast<double>(low) * 2.0);
	const double ratio = std::pow(top / static_cast<double>(low), 1.0 / (bands - 1));
	// The quality that makes neighbouring bands meet near -3 dB, narrowed or widened by 'width'.
	const double q = std::sqrt(ratio) / (ratio - 1.0) / static_cast<double>(width);
	for (int b = 0; b < bands; ++b)
	{
		const BiquadCoefficients band = designBiquad(BiquadShape::BandPass, static_cast<double>(low) * std::pow(ratio, b), q, rate);
		m_carrier[static_cast<std::size_t>(b)].set(band);
		m_voice[static_cast<std::size_t>(b)].set(band);
	}
	m_bands = bands;
	m_low = low;
	m_high = high;
	m_width = width;
	m_rate = rate;
	m_makeup = 4.f * std::sqrt(static_cast<float>(bands));
}

SampleFrame VocoderEffect::vocode(float voice, float left, float right, float release)
{
	float outLeft = 0.f;
	float outRight = 0.f;
	for (int b = 0; b < m_bands; ++b)
	{
		const auto k = static_cast<std::size_t>(b);
		const float level = std::abs(m_voice[k].run(voice, 0));
		// Up at once, down at the release time: the band's share of the voice.
		m_envelope[k] = level > m_envelope[k] ? level : level + release * (m_envelope[k] - level);
		outLeft += m_carrier[k].run(left, 0) * m_envelope[k];
		outRight += m_carrier[k].run(right, 1) * m_envelope[k];
	}
	return SampleFrame(outLeft, outRight) * m_makeup;
}

ProcessStatus VocoderEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	const float rate = Engine::audioEngine()->outputSampleRate();
	const int bands = std::clamp(static_cast<int>(std::lround(m_controls.m_bandsModel.value())), 4, MaxBands);
	configure(bands, m_controls.m_lowModel.value(), m_controls.m_highModel.value(), m_controls.m_widthModel.value(), rate);
	const bool fromLeft = static_cast<Source>(m_controls.m_sourceModel.value()) == Source::LeftChannel;
	const AudioBuffer* key = fromLeft ? nullptr : sidechainBuffer();
	if (!fromLeft && key == nullptr) { return ProcessStatus::ContinueIfNotQuiet; }  // no voice: the input as it is
	const float* key0 = key != nullptr ? key->buffer(0).data() : nullptr;
	const float* key1 = key != nullptr ? key->buffer(1).data() : nullptr;
	const float release = settleCoefficient(m_controls.m_releaseModel.value(), rate);
	const float gain = dbfsToAmp(m_controls.m_outputModel.value());
	const float d = dryLevel();
	const float w = wetLevel();
	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		auto& frame = inOut.sampleFrameAt(f);
		const float voice = fromLeft ? frame[0] : 0.5f * (key0[f] + key1[f]);
		const SampleFrame carrier = fromLeft ? SampleFrame(frame[1], frame[1]) : frame;
		frame = carrier * d + vocode(voice, carrier[0], carrier[1], release) * (gain * w);
	}
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new VocoderEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
