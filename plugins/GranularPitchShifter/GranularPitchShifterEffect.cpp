/*
 * GranularPitchShifter.cpp
 *
 * Copyright (c) 2024 Lost Robot <r94231/at/gmail/dot/com>
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

#include "GranularPitchShifterEffect.h"

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
Plugin::Descriptor PLUGIN_EXPORT granularpitchshifter_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Granular Pitch Shifter",
	QT_TRANSLATE_NOOP("PluginBrowser", "Granular pitch shifter"),
	"Lost Robot <r94231/at/gmail/dot/com>",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("zene-plugin-logo"),
	nullptr,
	nullptr,
} ;
}

namespace
{

//! Grains alive at once: reserved off the audio thread; a grain past this cap is not spawned.
constexpr std::size_t MaxGrains = 512;

} // namespace

GranularPitchShifterEffect::GranularPitchShifterEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&granularpitchshifter_plugin_descriptor, parent, key),
	m_granularpitchshifterControls(this),
	m_prefilter({PrefilterLowpass(), PrefilterLowpass()})
{
	autoQuitModel()->setValue(autoQuitModel()->maxValue());
	m_grains.reserve(MaxGrains);
	sampleRateNeedsUpdate();
	QObject::connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this,
		[this] { sampleRateNeedsUpdate(); }, Qt::QueuedConnection);
	m_reclaimTimer = new QTimer(this);
	m_reclaimTimer->setInterval(0);
	QObject::connect(m_reclaimTimer, &QTimer::timeout, this, [this] {
		reclaimRetiredBuffersIfPending();
	});
	m_reclaimTimer->start();
}


GranularPitchShifterEffect::~GranularPitchShifterEffect()
{
	delete m_preparedBuffers.exchange(nullptr, std::memory_order_acq_rel);
	reclaimRetiredBuffers();
}


namespace
{

//! Audio thread: start a grain when a reserved slot is free.
template<class Grain>
void spawnGrain(std::vector<Grain>& grains, int& grainCount, const Grain& grain)
{
	if (grains.size() >= MaxGrains) { return; }
	grains.push_back(grain);
	++grainCount;
}

} // namespace

ProcessStatus GranularPitchShifterEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	applyPreparedBuffers();

	const float d = dryLevel();
	const float w = wetLevel();
	
	const ValueBuffer* pitchBuf = m_granularpitchshifterControls.m_pitchModel.valueBuffer();
	const ValueBuffer* pitchSpreadBuf = m_granularpitchshifterControls.m_pitchSpreadModel.valueBuffer();

	const float size = m_granularpitchshifterControls.m_sizeModel.value();
	const float shape = m_granularpitchshifterControls.m_shapeModel.value();
	const float jitter = m_granularpitchshifterControls.m_jitterModel.value();
	const float twitch = m_granularpitchshifterControls.m_twitchModel.value();
	const float spray = m_granularpitchshifterControls.m_sprayModel.value();
	const float spraySpread = m_granularpitchshifterControls.m_spraySpreadModel.value();
	const float density = m_granularpitchshifterControls.m_densityModel.value();
	const float glide = m_granularpitchshifterControls.m_glideModel.value();
	const int minLatency = m_granularpitchshifterControls.m_minLatencyModel.value() * m_sampleRate;
	const float densityInvRoot = std::sqrt(1.f / density);
	const float feedback = m_granularpitchshifterControls.m_feedbackModel.value();
	const float fadeLength = 1.f / m_granularpitchshifterControls.m_fadeLengthModel.value();
	const bool prefilter = m_granularpitchshifterControls.m_prefilterModel.value();
	
	if (glide != m_oldGlide)
	{
		m_oldGlide = glide;
		m_glideCoef = glide > 0 ? std::exp(-1 / (glide * m_sampleRate)) : 0;
	}
	
	const float shapeK = cosWindowApproxK(shape);
	const int sizeSamples = m_sampleRate / size;
	const float waitMult = sizeSamples / (density * 2);

	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		float* frame = inOut.framePtr(f);

		const double pitch = (pitchBuf ? pitchBuf->value(f) : m_granularpitchshifterControls.m_pitchModel.value()) * (1. / 12.);
		const double pitchSpread = (pitchSpreadBuf ? pitchSpreadBuf->value(f) : m_granularpitchshifterControls.m_pitchSpreadModel.value()) * (1. / 24.);
		
		// interpolate pitch depending on glide
		for (int i = 0; i < 2; ++i)
		{
			double targetVal = pitch + pitchSpread * (i ? 1. : -1.);
			
			if (targetVal == m_truePitch[i]) { continue; }
			m_updatePitches = true;
			
			m_truePitch[i] = m_glideCoef * m_truePitch[i] + (1. - m_glideCoef) * targetVal;
			// we crudely lock the pitch to the target value once it gets close enough, so we can save on CPU
			if (std::abs(targetVal - m_truePitch[i]) < GlideSnagRadius) { m_truePitch[i] = targetVal; }
		}
		
		// this stuff is computationally expensive, so we should only do it when necessary
		if (m_updatePitches)
		{
			m_updatePitches = false;
			
			std::array<double, 2> speed = {
				std::exp2(m_truePitch[0]),
				std::exp2(m_truePitch[1])
			};
			std::array<double, 2> ratio = {
				speed[0] / m_speed[0],
				speed[1] / m_speed[1]
			};
			
			for (int i = 0; i < m_grainCount; ++i)
			{
				for (int j = 0; j < 2; ++j)
				{
					m_grains[i].grainSpeed[j] *= ratio[j];
					
					// we unfortunately need to do extra stuff to ensure these don't shoot past the write index...
					if (m_grains[i].grainSpeed[j] > 1)
					{
						double distance = m_writePoint - m_grains[i].readPoint[j] - SafetyLatency;
						if (distance <= 0) { distance += m_ringBufLength; }
						double grainSpeedRequired = ((m_grains[i].grainSpeed[j] - 1.) / distance) * (1. - m_grains[i].phase);
						m_grains[i].phaseSpeed[j] = std::max(m_grains[i].phaseSpeed[j], grainSpeedRequired);
					}
				}
			}
			m_speed[0] = speed[0];
			m_speed[1] = speed[1];
			
			// prevent aliasing by lowpassing frequencies that the pitch shifting would push above nyquist
			m_prefilter[0].setCoefs(m_sampleRate, std::min(m_nyquist / static_cast<float>(speed[0]), m_nyquist) * PrefilterBandwidth);
			m_prefilter[1].setCoefs(m_sampleRate, std::min(m_nyquist / static_cast<float>(speed[1]), m_nyquist) * PrefilterBandwidth);
		}
		
		std::array<float, 2> s = {0, 0};
		std::array<float, 2> filtered = {frame[0], frame[1]};
		
		// spawn a new grain if it's time
		if (++m_timeSinceLastGrain >= m_nextWaitRandomization * waitMult)
		{
			m_timeSinceLastGrain = 0;
			auto randThing = fastRandInc<double>(-1.0, +1.0);
			m_nextWaitRandomization = std::exp2(randThing * twitch);
			double grainSpeed = 1. / std::exp2(randThing * jitter);

			std::array<float, 2> sprayResult = {0, 0};
			if (spray > 0)
			{
				sprayResult[0] = fastRand(spray * m_sampleRate);
				sprayResult[1] = std::lerp(sprayResult[0], fastRand(spray * m_sampleRate), spraySpread);
			}
			
			std::array<int, 2> readPoint;
			int latency = std::max(static_cast<int>(std::max(sizeSamples * (std::max(m_speed[0], m_speed[1]) * grainSpeed - 1.), 0.) + SafetyLatency), minLatency);
			for (int i = 0; i < 2; ++i)
			{
				readPoint[i] = m_writePoint - latency - sprayResult[i];
				if (readPoint[i] < 0) { readPoint[i] += m_ringBufLength; }
			}
			const double phaseInc = 1. / sizeSamples;
			spawnGrain(m_grains, m_grainCount, Grain(grainSpeed * m_speed[0], grainSpeed * m_speed[1], phaseInc, phaseInc, readPoint[0], readPoint[1]));
		}
		
		for (int i = 0; i < m_grainCount; ++i)
		{
			m_grains[i].phase += std::max(m_grains[i].phaseSpeed[0], m_grains[i].phaseSpeed[1]);
			if (m_grains[i].phase >= 1)
			{
				// grain is done, delete it
				std::swap(m_grains[i], m_grains[m_grainCount-1]);
				m_grains.pop_back();
				--i;
				--m_grainCount;
				continue;
			}
			
			m_grains[i].readPoint[0] += m_grains[i].grainSpeed[0];
			m_grains[i].readPoint[1] += m_grains[i].grainSpeed[1];
			if (m_grains[i].readPoint[0] >= m_ringBufLength) { m_grains[i].readPoint[0] -= m_ringBufLength; }
			if (m_grains[i].readPoint[1] >= m_ringBufLength) { m_grains[i].readPoint[1] -= m_ringBufLength; }
			
			const float fadePos = std::clamp((-std::abs(-2.f * static_cast<float>(m_grains[i].phase) + 1.f) + 0.5f) * fadeLength + 0.5f, 0.f, 1.f);
			const float windowVal = cosHalfWindowApprox(fadePos, shapeK);
			s[0] += getHermiteSample(m_grains[i].readPoint[0], 0) * windowVal;
			s[1] += getHermiteSample(m_grains[i].readPoint[1], 1) * windowVal;
		}
		
		// note that adding two signals together, when uncorrelated, results in a signal power multiplication of sqrt(2), not 2
		s[0] *= densityInvRoot;
		s[1] *= densityInvRoot;
		
		// 1-pole highpass for DC offset removal, to make feedback safer
		s[0] -= (m_dcVal[0] = (1.f - m_dcCoeff) * s[0] + m_dcCoeff * m_dcVal[0]);
		s[1] -= (m_dcVal[1] = (1.f - m_dcCoeff) * s[1] + m_dcCoeff * m_dcVal[1]);
		
		// cheap safety saturator to protect against infinite feedback
		if (feedback > 0)
		{
			s[0] = safetySaturate(s[0]);
			s[1] = safetySaturate(s[1]);
		}
		
		if (++m_writePoint >= m_ringBufLength)
		{
			m_writePoint = 0;
		}
		if (prefilter)
		{
			filtered[0] = m_prefilter[0].process(filtered[0]);
			filtered[1] = m_prefilter[1].process(filtered[1]);
		}
		
		m_ringBuf[m_writePoint][0] = filtered[0] + s[0] * feedback;
		m_ringBuf[m_writePoint][1] = filtered[1] + s[1] * feedback;
			
		frame[0] = d * frame[0] + w * s[0];
		frame[1] = d * frame[1] + w * s[1];
	}
	
	return ProcessStatus::ContinueIfNotQuiet;
}

void GranularPitchShifterEffect::sampleRateNeedsUpdate()
{
	reclaimRetiredBuffers();

	auto prepared = std::make_unique<PreparedBuffers>();
	const int range = std::clamp(m_granularpitchshifterControls.m_rangeModel.value(), 0,
		static_cast<int>(std::size(RangeSeconds)) - 1);
	const float ringBufLength = RangeSeconds[range];
	prepared->sampleRate = Engine::audioEngine()->outputSampleRate();
	prepared->nyquist = prepared->sampleRate / 2;
	prepared->ringBufferLength = static_cast<int>(prepared->sampleRate * ringBufLength);
	prepared->ringBuffer.resize(static_cast<std::size_t>(prepared->ringBufferLength));
	for (auto& frame : prepared->ringBuffer)
	{
		frame = {0, 0};
	}
	prepared->dcCoeff = std::exp(-2 * std::numbers::pi_v<float> * DcRemovalHz / prepared->sampleRate);

	const double pitch = m_granularpitchshifterControls.m_pitchModel.value() * (1. / 12.);
	const double pitchSpread = m_granularpitchshifterControls.m_pitchSpreadModel.value() * (1. / 24.);
	prepared->truePitch[0] = pitch - pitchSpread;
	prepared->truePitch[1] = pitch + pitchSpread;
	prepared->speed[0] = std::exp2(prepared->truePitch[0]);
	prepared->speed[1] = std::exp2(prepared->truePitch[1]);

	delete m_preparedBuffers.exchange(prepared.release(), std::memory_order_acq_rel);
}


void GranularPitchShifterEffect::applyPreparedBuffers() noexcept
{
	std::atomic<PreparedBuffers*>* retiredSlot = nullptr;
	for (auto& slot : m_retiredBuffers)
	{
		if (slot.load(std::memory_order_acquire) == nullptr)
		{
			retiredSlot = &slot;
			break;
		}
	}
	if (retiredSlot == nullptr) { return; }

	PreparedBuffers* prepared = m_preparedBuffers.exchange(nullptr, std::memory_order_acq_rel);
	if (prepared == nullptr) { return; }

	m_ringBuf.swap(prepared->ringBuffer);
	m_sampleRate = prepared->sampleRate;
	m_nyquist = prepared->nyquist;
	m_ringBufLength = prepared->ringBufferLength;
	m_dcCoeff = prepared->dcCoeff;
	m_truePitch = prepared->truePitch;
	m_speed = prepared->speed;
	m_writePoint = 0;
	m_oldGlide = -1;
	m_updatePitches = true;
	m_grains.clear();
	m_grainCount = 0;

	// The prepared object now owns the old ring buffer. Only the control thread
	// reclaims it, so destroying its vector can never run on the audio thread.
	retiredSlot->store(prepared, std::memory_order_release);
	m_retiredPending.store(true, std::memory_order_release);
}


void GranularPitchShifterEffect::reclaimRetiredBuffersIfPending() noexcept
{
	if (!m_retiredPending.load(std::memory_order_acquire)) { return; }
	m_retiredPending.store(false, std::memory_order_release);
	reclaimRetiredBuffers();
}

void GranularPitchShifterEffect::reclaimRetiredBuffers() noexcept
{
	for (auto& slot : m_retiredBuffers)
	{
		delete slot.exchange(nullptr, std::memory_order_acq_rel);
	}
}


extern "C"
{
// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new GranularPitchShifterEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}
}

} // namespace lmms
