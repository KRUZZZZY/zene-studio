/*
 * DynamicEq.cpp - a native four-band dynamic EQ that can work on the mid or the side alone
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

#include "DynamicEq.h"

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

Plugin::Descriptor PLUGIN_EXPORT dynamiceq_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Dynamic EQ",
	QT_TRANSLATE_NOOP("PluginBrowser", "Four EQ bands that move with the level, on both sides, the mid or the side"),
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

constexpr int kUpdateFrames = 32;
constexpr float kDynamicSpanDb = 12.f;
constexpr std::array<float, DynamicEqControls::Bands> kFrequencies{100.f, 500.f, 2500.f, 8000.f};

BiquadShape shapeOf(int band)
{
	return band == 0 ? BiquadShape::LowShelf : band == DynamicEqControls::Bands - 1 ? BiquadShape::HighShelf : BiquadShape::Peak;
}

} // namespace

DynamicEqControls::DynamicEqControls(DynamicEqEffect* effect) :
	ParameterControls(effect, QStringLiteral("DynamicEqControls")),
	m_channelsModel(this, tr("Channels")),
	m_outputModel(0.f, -24.f, 24.f, 0.1f, this, tr("Output"))
{
	m_channelsModel.addItem(tr("Both sides"));
	m_channelsModel.addItem(tr("Mid only"));
	m_channelsModel.addItem(tr("Side only"));
	addParameter(&m_channelsModel, QStringLiteral("channels"));
	for (int b = 0; b < Bands; ++b)
	{
		const auto k = static_cast<std::size_t>(b);
		const QString n = QString::number(b + 1);
		m_frequency[k] = std::make_unique<FloatModel>(kFrequencies[k], 20.f, 20000.f, 1.f, this, tr("Frequency %1").arg(n));
		m_gain[k] = std::make_unique<FloatModel>(0.f, -24.f, 24.f, 0.1f, this, tr("Gain %1").arg(n));
		m_q[k] = std::make_unique<FloatModel>(0.7f, 0.1f, 10.f, 0.01f, this, tr("Q %1").arg(n));
		m_threshold[k] = std::make_unique<FloatModel>(-30.f, -60.f, 0.f, 0.1f, this, tr("Threshold %1").arg(n));
		m_dynamic[k] = std::make_unique<FloatModel>(0.f, -24.f, 24.f, 0.1f, this, tr("Dynamic %1").arg(n));
	}
	// Type by type, so the dialog's four columns are the four bands.
	for (const auto& [models, key] : {std::pair{&m_frequency, "freq"}, std::pair{&m_gain, "gain"}, std::pair{&m_q, "q"},
			std::pair{&m_threshold, "threshold"}, std::pair{&m_dynamic, "dynamic"}})
	{
		for (int b = 0; b < Bands; ++b) { addParameter((*models)[static_cast<std::size_t>(b)].get(), QStringLiteral("%1%2").arg(QLatin1String(key)).arg(b + 1)); }
	}
	addParameter(&m_outputModel, QStringLiteral("output"));
}

DynamicEqEffect::DynamicEqEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&dynamiceq_plugin_descriptor, parent, key),
	m_controls(this)
{
}

float DynamicEqEffect::dynamicGainDb(float gainDb, float dynamicDb, float levelDb, float thresholdDb)
{
	return gainDb + dynamicDb * std::clamp((levelDb - thresholdDb) / kDynamicSpanDb, 0.f, 1.f);
}

void DynamicEqEffect::update(int b, float rate)
{
	const auto k = static_cast<std::size_t>(b);
	Band& band = m_bands[k];
	const float hz = m_controls.m_frequency[k]->value();
	const float q = m_controls.m_q[k]->value();
	const float level = ampToDbfs(std::max(band.envelope, 1e-6f));
	const float db = dynamicGainDb(m_controls.m_gain[k]->value(), m_controls.m_dynamic[k]->value(), level, m_controls.m_threshold[k]->value());
	if (std::abs(db - band.appliedDb) < 0.05f && hz == band.hz && q == band.q) { return; }
	band.filter.set(designBiquad(shapeOf(b), hz, q, rate, db));
	if (hz != band.hz || q != band.q) { band.detector.set(designBiquad(BiquadShape::BandPass, hz, q, rate)); }
	band.appliedDb = db;
	band.hz = hz;
	band.q = q;
}

SampleFrame DynamicEqEffect::equalise(const SampleFrame& in, Channels channels, float attack, float release)
{
	// Mid / side: the bands run on one of the two, and the pair is turned back into left / right.
	const float mid = 0.5f * (in[0] + in[1]);
	const float side = 0.5f * (in[0] - in[1]);
	float a = channels == Channels::Stereo ? in[0] : channels == Channels::Mid ? mid : side;
	float b = channels == Channels::Stereo ? in[1] : 0.f;
	for (Band& band : m_bands)
	{
		const float level = std::abs(band.detector.run(channels == Channels::Stereo ? 0.5f * (a + b) : a, 0));
		band.envelope = level + (level > band.envelope ? attack : release) * (band.envelope - level);
		a = band.filter.run(a, 0);
		if (channels == Channels::Stereo) { b = band.filter.run(b, 1); }
	}
	if (channels == Channels::Mid) { return SampleFrame(a + side, a - side); }
	if (channels == Channels::Side) { return SampleFrame(mid + a, mid - a); }
	return SampleFrame(a, b);
}

ProcessStatus DynamicEqEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	const float rate = Engine::audioEngine()->outputSampleRate();
	const auto channels = static_cast<Channels>(m_controls.m_channelsModel.value());
	const float attack = settleCoefficient(5.f, rate);
	const float release = settleCoefficient(80.f, rate);
	const float output = dbfsToAmp(m_controls.m_outputModel.value());
	const float d = dryLevel();
	const float w = wetLevel();
	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		if (--m_countdown <= 0)
		{
			for (int b = 0; b < DynamicEqControls::Bands; ++b) { update(b, rate); }
			m_countdown = kUpdateFrames;
		}
		auto& frame = inOut.sampleFrameAt(f);
		frame = frame * d + equalise(frame, channels, attack, release) * (output * w);
	}
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new DynamicEqEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
