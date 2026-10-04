/*
 * ConvolutionReverb.h - a native convolution reverb: a room's impulse response played through
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

#ifndef LMMS_CONVOLUTION_REVERB_H
#define LMMS_CONVOLUTION_REVERB_H

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "AudioPlugin.h"
#include "BiquadDesign.h"
#include "ConvolutionKernel.h"
#include "ParameterControls.h"

namespace lmms
{

class ConvolutionReverbEffect;

//! The reverb's parameters - pre-delay, the wet signal's low and high cut, its level - and the
//! impulse response file, saved as the 'impulse' attribute only when one is loaded.
class ConvolutionReverbControls : public ParameterControls
{
public:
	explicit ConvolutionReverbControls(ConvolutionReverbEffect* effect);

	void saveSettings(QDomDocument& doc, QDomElement& parent) override;
	void loadSettings(const QDomElement& parent) override;

	FloatModel m_predelayModel;
	FloatModel m_lowCutModel;
	FloatModel m_highCutModel;
	FloatModel m_outputModel;

private:
	ConvolutionReverbEffect* m_effect;
};

/*! 0.5.0 L10 (MX-152): a convolution reverb. The wet signal is the input, delayed by 'pre-delay',
 *  convolved with an impulse response (a WAV / FLAC / OGG file - stereo or mono, up to 5 seconds,
 *  resampled band-limited to the engine's rate - loaded with plugin.impulse_load; without one, a generated
 *  two-second hall), then low- and high-cut. It runs one ConvolutionKernel partition late, reports
 *  that as its latency for delay compensation, and delays its dry signal by the same so the mix is
 *  coherent; its tail is the impulse response's length plus the pre-delay. Everything the audio
 *  thread touches is allocated beforehand (the pre-delay line for 192 kHz, the kernel on the control
 *  thread). */
class ConvolutionReverbEffect : public DefaultEffect
{
public:
	static constexpr float MaxSeconds = 5.f;
	static constexpr float MaxPredelayMs = 200.f;

	ConvolutionReverbEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~ConvolutionReverbEffect() override;

	EffectControls* controls() override { return &m_controls; }
	int latencyFrames() const override { return ConvolutionKernel::Partition; }
	//! The impulse response's length plus the pre-delay the input still waits in.
	int tailFrames() const override;

	//! Control thread: the impulse response in @a path (empty: the generated hall). False, keeping
	//! the current one, when the file cannot be read.
	bool loadImpulse(const QString& path);
	const QString& impulsePath() const { return m_impulsePath; }

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;
	void install(std::unique_ptr<ConvolutionKernel> kernel);
	void designFilters(float rate);
	//! The pre-delay in frames at @a rate, as long as the line holds at most.
	std::size_t predelayFrames(float rate) const;
	//! The wet sample of channel @a c for this frame, from the block the kernel produced.
	float wet(int c, float input, std::size_t predelay);

	ConvolutionReverbControls m_controls;
	std::unique_ptr<ConvolutionKernel> m_kernel;
	QString m_impulsePath;
	std::atomic<int> m_tail{0};
	std::array<std::vector<float>, 2> m_predelay;
	std::size_t m_predelayWrite = 0;
	std::array<std::array<float, ConvolutionKernel::Partition>, 2> m_in{};
	std::array<std::array<float, ConvolutionKernel::Partition>, 2> m_out{};
	std::array<std::array<float, ConvolutionKernel::Partition>, 2> m_dry{};
	int m_position = 0;
	BiquadSection<2> m_lowCut;
	BiquadSection<2> m_highCut;
	float m_lowHz = -1.f;
	float m_highHz = -1.f;
	float m_rate = 0.f;
};

} // namespace lmms

#endif // LMMS_CONVOLUTION_REVERB_H
