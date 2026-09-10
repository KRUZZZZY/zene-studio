/*
 * PhaseDMixerTestSupport.h - shared helpers for the Phase D tests (task #587)
 *
 * Copyright (c) 2026 LMMS developers
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

#ifndef LMMS_TESTS_PHASE_D_MIXER_TEST_SUPPORT_H
#define LMMS_TESTS_PHASE_D_MIXER_TEST_SUPPORT_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "AudioBus.h"
#include "AudioBuffer.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "Plugin.h"
#include "SampleFrame.h"

namespace partd
{

using namespace lmms; // SampleFrame, AudioBus, Effect, f_cnt_t, mix_ch_t

// ---------------------------------------------------------------------------
// engine lifecycle
// ---------------------------------------------------------------------------

//! Headless engine with the dummy device stopped so a test can drive the
//! mixer synchronously (same setup as the committed D1 A/B harness).
inline void initEngine()
{
	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();
}

inline void destroyEngine()
{
	Engine::destroy();
}

inline f_cnt_t periodFrames()
{
	return Engine::audioEngine()->framesPerPeriod();
}

// ---------------------------------------------------------------------------
// deterministic signals (integer arithmetic only, no libm)
// ---------------------------------------------------------------------------

//! Constant value in [-1, 1); used for DC-style level measurements.
inline float dcSignal(int value, int scale)
{
	return static_cast<float>(value) / static_cast<float>(scale);
}

//! Deterministic pseudo-random sample in [-1, 1) from an integer seed.
inline float hashSignal(std::uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return static_cast<float>(x >> 9) * (1.0f / 4194304.0f) - 1.0f;
}

// ---------------------------------------------------------------------------
// period harness: feed channel inputs, render one period, read the master
// ---------------------------------------------------------------------------

class PeriodHarness
{
public:
	explicit PeriodHarness(Mixer* mixer) :
		m_mixer(mixer),
		m_fpp(periodFrames()),
		m_in(m_fpp),
		m_out(m_fpp)
	{
		m_busData[0] = m_in.data();
		settleValueRamps();
	}

	f_cnt_t fpp() const { return m_fpp; }

	//! Per-channel scratch input buffer; write m_in[f] before feed().
	std::vector<SampleFrame>& in() { return m_in; }

	//! Mix the scratch buffer into a mixer channel (instrument input path).
	void feed(mix_ch_t channel)
	{
		const AudioBus bus{m_busData, 1, m_fpp};
		m_mixer->mixToChannel(bus, channel);
	}

	void zeroInput()
	{
		zeroSampleFrames(m_in.data(), m_fpp);
	}

	//! Render one period; returns the master output for that period.
	const std::vector<SampleFrame>& render()
	{
		m_mixer->prepareMasterMix();
		zeroSampleFrames(m_out.data(), m_fpp);
		m_mixer->masterMix(m_out.data());
		// AudioEngine::renderStageMix() advances the automation period
		// counter at the end of every real period. Synchronous renders must
		// do the same, otherwise a value changed before the render stays
		// "pending" and its ramp from the default value is replayed in
		// every measured period.
		AutomatableModel::incrementPeriodCounter();
		return m_out;
	}

private:
	//! Consume pending automation ramps for the routing models and close the
	//! period, mirroring the one real period the engine renders between a
	//! model change and the next query. Without this the first measured
	//! period ramps every freshly-set value from its default (volume 1.0,
	//! send amount 1.0), which silently breaks exact level assertions.
	void settleValueRamps()
	{
		for (int i = 0; i < m_mixer->numChannels(); ++i)
		{
			MixerChannel* ch = m_mixer->mixerChannel(i);
			ch->m_volumeModel.valueBuffer();
			for (MixerRoute* route : ch->m_sends)
			{
				route->amount()->valueBuffer();
			}
			for (MixerSidechainRoute* route : ch->m_sidechainSends)
			{
				route->amount()->valueBuffer();
			}
		}
		AutomatableModel::incrementPeriodCounter();
	}

private:
	Mixer* m_mixer;
	f_cnt_t m_fpp;
	std::vector<SampleFrame> m_in;
	std::vector<SampleFrame> m_out;
	SampleFrame* m_busData[1];
};

// ---------------------------------------------------------------------------
// measurement helpers
// ---------------------------------------------------------------------------

//! Mean absolute level (average of |L| and |R|) over [from, to).
inline double meanAbs(const std::vector<SampleFrame>& buf, int from, int to)
{
	double sum = 0.0;
	int n = 0;
	for (int f = from; f < to; ++f)
	{
		sum += 0.5 * (std::fabs(buf[f][0]) + std::fabs(buf[f][1]));
		++n;
	}
	return n > 0 ? sum / n : 0.0;
}

//! RMS over [from, to).
inline double rms(const std::vector<SampleFrame>& buf, int from, int to)
{
	double sum = 0.0;
	int n = 0;
	for (int f = from; f < to; ++f)
	{
		sum += 0.5 * (static_cast<double>(buf[f][0]) * buf[f][0]
			+ static_cast<double>(buf[f][1]) * buf[f][1]);
		++n;
	}
	return n > 0 ? std::sqrt(sum / n) : 0.0;
}

inline double toDb(double linear)
{
	return 20.0 * std::log10(std::max(linear, 1e-12));
}

inline QByteArray sha256(const std::vector<SampleFrame>& buf)
{
	const QByteArray bytes{reinterpret_cast<const char*>(buf.data()),
		static_cast<int>(buf.size() * sizeof(SampleFrame))};
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

//! Unconditional, flushed evidence line for the PART-D-SIDECHAIN.md paste.
//! The format string is a literal at every call site, but the compiler cannot
//! see through the variadic indirection, so -Wformat-security / -Wformat-nonliteral
//! fire here and the CI builds pass -DUSE_WERROR=ON. Suppress them locally.
template <typename... Args>
void evidence(const char* fmt, Args... args)
{
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
#endif
	std::fprintf(stdout, fmt, args...);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	std::fputc('\n', stdout);
	std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// test effects
// ---------------------------------------------------------------------------

//! Constant stereo gain, to make a bus's FX chain observable.
class FixedGainEffect : public Effect
{
public:
	FixedGainEffect(Model* parent, float gainL, float gainR) :
		Effect{&descriptor(), parent, nullptr},
		m_gainL(gainL),
		m_gainR(gainR)
	{
	}

	EffectControls* controls() override { return nullptr; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			buf[f][0] *= m_gainL;
			buf[f][1] *= m_gainR;
		}
		return ProcessStatus::Continue;
	}

private:
	static const Plugin::Descriptor& descriptor()
	{
		static const Plugin::Descriptor d{
			"partdfixedgain", "Phase D fixed gain", "Deterministic gain for Phase D tests",
			"LMMS", 0x0100, Plugin::Type::Effect, nullptr, nullptr, nullptr};
		return d;
	}

	float m_gainL;
	float m_gainR;
};

//! Records the sidechain input the effect is handed for the current block.
class TapProbeEffect : public Effect
{
public:
	explicit TapProbeEffect(Model* parent) :
		Effect{&descriptor(), parent, nullptr}
	{
	}

	EffectControls* controls() override { return nullptr; }

	bool sawSidechain() const { return m_sawSidechain; }
	float firstL() const { return m_firstL; }
	float firstR() const { return m_firstR; }
	int calls() const { return m_calls; }
	void reset()
	{
		m_sawSidechain = false;
		m_firstL = 0.0f;
		m_firstR = 0.0f;
		m_calls = 0;
	}

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		Q_UNUSED(buf);
		++m_calls;
		const AudioBuffer* sc = sidechainBuffer();
		m_sawSidechain = sc != nullptr;
		if (sc != nullptr && frames > 0)
		{
			m_firstL = sc->buffer(0).data()[0];
			m_firstR = sc->buffer(1).data()[0];
		}
		return ProcessStatus::Continue;
	}

private:
	static const Plugin::Descriptor& descriptor()
	{
		static const Plugin::Descriptor d{
			"partdtapprobe", "Phase D tap probe", "Records the sidechain tap for Phase D tests",
			"LMMS", 0x0100, Plugin::Type::Effect, nullptr, nullptr, nullptr};
		return d;
	}

	bool m_sawSidechain = false;
	float m_firstL = 0.0f;
	float m_firstR = 0.0f;
	int m_calls = 0;
};

//! Deterministic keyed ducker: gain = 1 / (1 + amount * |key|). Reads the
//! sidechain input delivered by the mixer; without one it passes audio.
class KeyedDuckEffect : public Effect
{
public:
	explicit KeyedDuckEffect(Model* parent, float amount = 8.0f) :
		Effect{&descriptor(), parent, nullptr},
		m_amount(amount)
	{
	}

	EffectControls* controls() override { return nullptr; }

	float lastKeyPeak() const { return m_lastKeyPeak; }
	int keyedBlocks() const { return m_keyedBlocks; }
	void resetStats()
	{
		m_lastKeyPeak = 0.0f;
		m_keyedBlocks = 0;
	}

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		const AudioBuffer* sc = sidechainBuffer();
		float peak = 0.0f;
		if (sc != nullptr)
		{
			const float* s0 = sc->buffer(0).data();
			const float* s1 = sc->buffer(1).data();
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				const float key = std::max(std::fabs(s0[f]), std::fabs(s1[f]));
				peak = std::max(peak, key);
				const float gain = 1.0f / (1.0f + m_amount * key);
				buf[f][0] *= gain;
				buf[f][1] *= gain;
			}
			if (peak > 1.0e-6f)
			{
				++m_keyedBlocks;
			}
		}
		m_lastKeyPeak = peak;
		return ProcessStatus::Continue;
	}

private:
	static const Plugin::Descriptor& descriptor()
	{
		static const Plugin::Descriptor d{
			"partdkeyedduck", "Phase D keyed ducker", "Deterministic sidechain ducker for Phase D tests",
			"LMMS", 0x0100, Plugin::Type::Effect, nullptr, nullptr, nullptr};
		return d;
	}

	float m_amount;
	float m_lastKeyPeak = 0.0f;
	int m_keyedBlocks = 0;
};

//! Pure integer stereo delay that reports its latency to the host (task #605).
//! `reportedLatencyFrames` may differ from the true DSP delay: the same effect
//! then acts as the causal control for PDC, because under-reporting is exactly
//! the pre-PDC situation (the DSP is late, the host does not know it).
class LatentDelayEffect : public Effect
{
public:
	LatentDelayEffect(Model* parent, int delayFrames, int reportedLatencyFrames = -1) :
		Effect{&descriptor(), parent, nullptr},
		m_delay(delayFrames),
		m_reported(reportedLatencyFrames >= 0 ? reportedLatencyFrames : delayFrames),
		m_ring(static_cast<std::size_t>(std::max(delayFrames, 0)) + periodFrames(), SampleFrame{}),
		m_write(0)
	{
	}

	EffectControls* controls() override { return nullptr; }

	int latencyFrames() const override { return m_reported; }
	int trueDelayFrames() const { return m_delay; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		if (m_delay <= 0 || m_ring.empty()) { return ProcessStatus::Continue; }
		const std::size_t capacity = m_ring.size();
		for (f_cnt_t i = 0; i < frames; ++i)
		{
			const std::size_t write = (m_write + i) % capacity;
			const std::size_t read =
				(write + capacity - static_cast<std::size_t>(m_delay)) % capacity;
			const SampleFrame dry = buf[i];
			buf[i] = m_ring[read];
			m_ring[write] = dry;
		}
		m_write = (m_write + frames) % capacity;
		return ProcessStatus::Continue;
	}

private:
	static const Plugin::Descriptor& descriptor()
	{
		static const Plugin::Descriptor d{
			"pdclatentdelay", "PDC latent delay",
			"Deterministic delay with a reportable latency for PDC tests",
			"Zene Studio", 0x0100, Plugin::Type::Effect, nullptr, nullptr, nullptr};
		return d;
	}

	int m_delay;
	int m_reported;
	std::vector<SampleFrame> m_ring;
	std::size_t m_write;
};

//! Records the sample-exact difference between the audio it processes (the
//! receiver's main input) and the sidechain input the mixer hands it (task #605).
//! Pass-through: it does not modify the main signal.
class SidechainDiffProbe : public Effect
{
public:
	explicit SidechainDiffProbe(Model* parent) :
		Effect{&descriptor(), parent, nullptr}
	{
	}

	EffectControls* controls() override { return nullptr; }

	void reset()
	{
		m_blocks = 0;
		m_maxAbs = 0.0;
		m_sumSq = 0.0;
		m_frames = 0;
	}

	int blocks() const { return m_blocks; }
	double maxAbs() const { return m_maxAbs; }
	double rms() const { return m_frames > 0 ? std::sqrt(m_sumSq / m_frames) : 0.0; }

protected:
	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override
	{
		++m_blocks;
		const AudioBuffer* sc = sidechainBuffer();
		if (sc == nullptr) { return ProcessStatus::Continue; }
		const float* const s0 = sc->buffer(0).data();
		const float* const s1 = sc->buffer(1).data();
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			const double dl = static_cast<double>(buf[f][0]) - static_cast<double>(s0[f]);
			const double dr = static_cast<double>(buf[f][1]) - static_cast<double>(s1[f]);
			m_maxAbs = std::max(m_maxAbs, std::max(std::fabs(dl), std::fabs(dr)));
			m_sumSq += dl * dl + dr * dr;
			++m_frames;
		}
		return ProcessStatus::Continue;
	}

private:
	static const Plugin::Descriptor& descriptor()
	{
		static const Plugin::Descriptor d{
			"pdcsidechaindiff", "PDC sidechain diff probe",
			"Measures main-input minus sidechain-input for PDC tests",
			"Zene Studio", 0x0100, Plugin::Type::Effect, nullptr, nullptr, nullptr};
		return d;
	}

	double m_maxAbs = 0.0;
	double m_sumSq = 0.0;
	std::uint64_t m_frames = 0;
	int m_blocks = 0;
};

} // namespace partd

#endif // LMMS_TESTS_PHASE_D_MIXER_TEST_SUPPORT_H
