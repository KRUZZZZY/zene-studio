/*
 * ConvolutionReverb.cpp - a native convolution reverb: a room's impulse response played through
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

#include "ConvolutionReverb.h"

#include <algorithm>
#include <cmath>
#include <new>

#include <QDomElement>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleBufferResample.h"
#include "embed.h"
#include "lmms_math.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT convolutionreverb_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Convolution Reverb",
	QT_TRANSLATE_NOOP("PluginBrowser", "A room, hall or plate from its impulse response"),
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

constexpr float kMaxRate = 192000.f;
constexpr float kHallSeconds = 2.f;
//! Source frames past the kept length that are resampled too, so the last kept frames see the filter's whole reach.
constexpr std::size_t kFilterReach = 4096;

//! A generated hall: decaying, slightly darkening, uncorrelated noise per side (-60 dB at its end).
std::vector<float> generatedHall(float rate, std::uint32_t seed)
{
	const auto frames = static_cast<std::size_t>(kHallSeconds * rate);
	std::vector<float> impulse(frames);
	std::uint32_t state = seed;
	float smooth = 0.f;
	for (std::size_t i = 0; i < frames; ++i)
	{
		state = state * 1664525u + 1013904223u;
		const float noise = static_cast<float>(state >> 8) / static_cast<float>(1u << 24) * 2.f - 1.f;
		const float t = static_cast<float>(i) / static_cast<float>(frames);
		smooth += (0.3f + 0.6f * (1.f - t)) * (noise - smooth);
		impulse[i] = smooth * std::exp(-6.9f * t);
	}
	return impulse;
}

//! @a impulse scaled to unit energy, so a long and a short response sound about as loud.
void normalise(std::vector<float>* left, std::vector<float>* right)
{
	double energy = 0.0;
	for (const float x : *left) { energy += static_cast<double>(x) * x; }
	for (const float x : *right) { energy += static_cast<double>(x) * x; }
	const auto gain = static_cast<float>(energy > 0.0 ? 1.0 / std::sqrt(energy / 2.0) : 1.0);
	for (float& x : *left) { x *= gain; }
	for (float& x : *right) { x *= gain; }
}

//! @a decoded at @a rate Hz, through the engine's band-limited (windowed-sinc) resampler: a linear one folds a
//! high-rate response's content above the new Nyquist frequency back into the audible band (review lane10 #4).
//! Only the frames that can be kept are converted - a few more for the filter's reach - and a response so short
//! that it would resample to nothing stays as it is: one frame, not silence.
std::shared_ptr<const SampleBuffer> atRate(const std::shared_ptr<const SampleBuffer>& decoded, float rate, std::size_t limit)
{
	const double ratio = static_cast<double>(decoded->sampleRate()) / static_cast<double>(rate);
	const auto wanted = static_cast<std::size_t>(std::ceil(static_cast<double>(limit) * ratio)) + kFilterReach;
	std::shared_ptr<const SampleBuffer> source = decoded;
	if (decoded->size() > wanted)
	{
		source = std::make_shared<const SampleBuffer>(decoded->data(), wanted, decoded->sampleRate());
	}
	const std::shared_ptr<const SampleBuffer> resampled = resampleBuffer(source, static_cast<sample_rate_t>(rate));
	return resampled != nullptr && resampled->size() > 0 ? resampled : source;
}

//! One channel of @a buffer, at most @a limit long.
std::vector<float> channelOf(const SampleBuffer& buffer, int channel, std::size_t limit)
{
	const SampleFrame* frames = buffer.data();
	std::vector<float> out(std::min(limit, buffer.size()));
	for (std::size_t i = 0; i < out.size(); ++i) { out[i] = frames[i][channel]; }
	return out;
}

} // namespace

ConvolutionReverbControls::ConvolutionReverbControls(ConvolutionReverbEffect* effect) :
	ParameterControls(effect, QStringLiteral("ConvolutionReverbControls")),
	m_predelayModel(0.f, 0.f, ConvolutionReverbEffect::MaxPredelayMs, 0.1f, this, tr("Pre-delay")),
	m_lowCutModel(20.f, 20.f, 1000.f, 1.f, this, tr("Low cut")),
	m_highCutModel(20000.f, 1000.f, 20000.f, 1.f, this, tr("High cut")),
	m_outputModel(0.f, -24.f, 12.f, 0.1f, this, tr("Output")),
	m_effect(effect)
{
	addParameter(&m_predelayModel, QStringLiteral("predelay"));
	addParameter(&m_lowCutModel, QStringLiteral("lowcut"));
	addParameter(&m_highCutModel, QStringLiteral("highcut"));
	addParameter(&m_outputModel, QStringLiteral("output"));
}

void ConvolutionReverbControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	ParameterControls::saveSettings(doc, parent);
	if (!m_effect->impulsePath().isEmpty()) { parent.setAttribute(QStringLiteral("impulse"), m_effect->impulsePath()); }
}

void ConvolutionReverbControls::loadSettings(const QDomElement& parent)
{
	ParameterControls::loadSettings(parent);
	const QString path = parent.attribute(QStringLiteral("impulse"));
	if (path != m_effect->impulsePath()) { m_effect->loadImpulse(path); }
}

ConvolutionReverbEffect::ConvolutionReverbEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&convolutionreverb_plugin_descriptor, parent, key),
	m_controls(this)
{
	const auto longest = static_cast<std::size_t>(MaxPredelayMs * 0.001f * kMaxRate) + 1;
	for (std::vector<float>& line : m_predelay) { line.assign(longest, 0.f); }
	loadImpulse(QString());
	// The kernel is resampled to the engine's rate when it is built; a new rate (another device, an export at another
	// rate) builds it again, on this side, under the engine's change lock (review W3-agent-devices #2).
	connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this, [this] { loadImpulse(m_impulsePath); });
}

ConvolutionReverbEffect::~ConvolutionReverbEffect() = default;

void ConvolutionReverbEffect::install(std::unique_ptr<ConvolutionKernel> kernel)
{
	m_tail.store(kernel->length() + ConvolutionKernel::Partition, std::memory_order_relaxed);
	// The swap is the only edit the audio thread can see: under the model-change guard, a block
	// boundary; the old kernel is freed here, on the control thread, after the guard - its FFTW
	// plans die in the scope that quiesces the audio thread (ConvolutionKernel's destructor).
	Engine::audioEngine()->requestChangeInModel();
	m_kernel.swap(kernel);
	Engine::audioEngine()->doneChangeInModel();
}

bool ConvolutionReverbEffect::loadImpulse(const QString& path)
{
	const float rate = Engine::audioEngine()->outputSampleRate();
	const auto limit = static_cast<std::size_t>(MaxSeconds * rate);
	std::vector<float> left;
	std::vector<float> right;
	if (path.isEmpty())
	{
		left = generatedHall(rate, 1u);
		right = generatedHall(rate, 2u);
	}
	else
	{
		const std::shared_ptr<const SampleBuffer> decoded = SampleBuffer::fromFile(path);
		if (decoded == nullptr || decoded->size() == 0 || decoded->sampleRate() == 0) { return false; }
		const std::shared_ptr<const SampleBuffer> impulse = atRate(decoded, rate, limit);
		left = channelOf(*impulse, 0, limit);
		right = channelOf(*impulse, 1, limit);
	}
	normalise(&left, &right);
	std::unique_ptr<ConvolutionKernel> kernel;
	try
	{
		kernel = std::make_unique<ConvolutionKernel>(left, right);
	}
	catch (const std::bad_alloc&)
	{
		return false;
	}
	install(std::move(kernel));
	m_impulsePath = path;
	return true;
}

std::size_t ConvolutionReverbEffect::predelayFrames(float rate) const
{
	return std::min(m_predelay[0].size() - 1, static_cast<std::size_t>(m_controls.m_predelayModel.value() * 0.001f * rate));
}

int ConvolutionReverbEffect::tailFrames() const
{
	const auto predelay = static_cast<int>(predelayFrames(Engine::audioEngine()->outputSampleRate()));
	return m_tail.load(std::memory_order_relaxed) + predelay;
}

void ConvolutionReverbEffect::designFilters(float rate)
{
	const float low = m_controls.m_lowCutModel.value();
	const float high = m_controls.m_highCutModel.value();
	if (low == m_lowHz && high == m_highHz && rate == m_rate) { return; }
	m_lowCut.set(designBiquad(BiquadShape::HighPass, low, 0.7071, rate));
	m_highCut.set(designBiquad(BiquadShape::LowPass, high, 0.7071, rate));
	m_lowHz = low;
	m_highHz = high;
	m_rate = rate;
}

float ConvolutionReverbEffect::wet(int c, float input, std::size_t predelay)
{
	std::vector<float>& line = m_predelay[static_cast<std::size_t>(c)];
	line[m_predelayWrite] = input;
	const std::size_t read = (m_predelayWrite + line.size() - predelay) % line.size();
	m_in[static_cast<std::size_t>(c)][static_cast<std::size_t>(m_position)] = line[read];
	const float out = m_out[static_cast<std::size_t>(c)][static_cast<std::size_t>(m_position)];
	return m_highCut.run(m_lowCut.run(out, c), c);
}

ProcessStatus ConvolutionReverbEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	const float rate = Engine::audioEngine()->outputSampleRate();
	designFilters(rate);
	const std::size_t predelay = predelayFrames(rate);
	const float gain = dbfsToAmp(m_controls.m_outputModel.value()) * wetLevel();
	const float d = dryLevel();
	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		auto& frame = inOut.sampleFrameAt(f);
		SampleFrame out;
		for (int c = 0; c < 2; ++c)
		{
			// The dry signal one partition late, as the wet one is: the mix stays coherent.
			float& dry = m_dry[static_cast<std::size_t>(c)][static_cast<std::size_t>(m_position)];
			out[c] = dry * d + wet(c, frame[c], predelay) * gain;
			dry = frame[c];
		}
		frame = out;
		m_predelayWrite = (m_predelayWrite + 1) % m_predelay[0].size();
		if (++m_position < ConvolutionKernel::Partition) { continue; }
		m_position = 0;
		const float* in[2] = {m_in[0].data(), m_in[1].data()};
		float* outBlock[2] = {m_out[0].data(), m_out[1].data()};
		if (m_kernel != nullptr) { m_kernel->process(in, outBlock); }
	}
	return ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new ConvolutionReverbEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
