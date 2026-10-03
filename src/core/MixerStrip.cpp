/*
 * MixerStrip.cpp - a mixer channel's own stages: trim, polarity, mono, pan modes, and what its meters read
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

#include "MixerStrip.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include <QDomElement>

#include "ConfigManager.h"
#include "Effect.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerOptions.h"
#include "SampleFrame.h"
#include "ValueBuffer.h"

namespace lmms::mixerstrip
{

void applyInput(MixerChannel& channel, f_cnt_t frames)
{
	if (channel.m_inputMonoModel.value())  // MX-059: a mono source, both sides alike before the inserts
	{
		for (f_cnt_t f = 0; f < frames; ++f) { channel.m_buffer[f][0] = channel.m_buffer[f][1] = 0.5f * (channel.m_buffer[f][0] + channel.m_buffer[f][1]); }
	}
	const float db = channel.m_trimModel.value();
	if (db == 0.f) { return; }
	const float gain = std::pow(10.f, db / 20.f);
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		channel.m_buffer[f][0] *= gain;
		channel.m_buffer[f][1] *= gain;
	}
}

namespace
{

//! One RBJ biquad's coefficients, normalised (a0 = 1).
struct Biquad
{
	float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
};

//! A shelf (@a high or low) or, with @a bell, a peaking band at @a hz, @a db gain, Q 0.7 (RBJ's cookbook).
Biquad band(double hz, double db, double rate, bool bell, bool high)
{
	const double a = std::pow(10.0, db / 40.0);
	const double w = 2.0 * 3.14159265358979 * hz / rate;
	const double cw = std::cos(w);
	const double alpha = std::sin(w) / (2.0 * 0.7);
	double b0, b1, b2, a0, a1, a2;
	if (bell)
	{
		b0 = 1 + alpha * a; b1 = -2 * cw; b2 = 1 - alpha * a; a0 = 1 + alpha / a; a1 = -2 * cw; a2 = 1 - alpha / a;
	}
	else
	{
		const double s = high ? -1.0 : 1.0;  // the high shelf is the low one mirrored
		const double root = 2 * std::sqrt(a) * alpha;
		b0 = a * ((a + 1) - s * (a - 1) * cw + root); b1 = 2 * s * a * ((a - 1) - s * (a + 1) * cw); b2 = a * ((a + 1) - s * (a - 1) * cw - root);
		a0 = (a + 1) + s * (a - 1) * cw + root; a1 = -2 * s * ((a - 1) + s * (a + 1) * cw); a2 = (a + 1) + s * (a - 1) * cw - root;
	}
	return {static_cast<float>(b0 / a0), static_cast<float>(b1 / a0), static_cast<float>(b2 / a0), static_cast<float>(a1 / a0), static_cast<float>(a2 / a0)};
}

//! Run @a q over both sides of @a channel's buffer, with the band's memories at @a state (8 floats).
void runBand(MixerChannel& channel, const Biquad& q, float* state, f_cnt_t frames)
{
	for (int side = 0; side < 2; ++side)
	{
		float* m = state + side * 4;  // x1, x2, y1, y2
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			const float x = channel.m_buffer[f][side];
			const float y = q.b0 * x + q.b1 * m[0] + q.b2 * m[1] - q.a1 * m[2] - q.a2 * m[3];
			m[1] = m[0]; m[0] = x; m[3] = m[2]; m[2] = y;
			channel.m_buffer[f][side] = y;
		}
	}
}

} // namespace

void applyChannelEq(MixerChannel& channel, f_cnt_t frames)
{
	const float gains[3] = {channel.m_eqLowModel.value(), channel.m_eqMidModel.value(), channel.m_eqHighModel.value()};
	if (gains[0] == 0.f && gains[1] == 0.f && gains[2] == 0.f) { return; }
	const double rate = Engine::audioEngine()->outputSampleRate();
	const double freqs[3] = {100.0, channel.m_eqMidFreqModel.value(), 8000.0};
	for (int b = 0; b < 3; ++b)
	{
		if (gains[b] == 0.f) { continue; }
		runBand(channel, band(freqs[b], gains[b], rate, b == 1, b == 2), channel.m_eqState.data() + b * 8, frames);
	}
}

float eqResponseDb(const MixerChannel& channel, double hz, double rate)
{
	const float gains[3] = {channel.m_eqLowModel.value(), channel.m_eqMidModel.value(), channel.m_eqHighModel.value()};
	const double freqs[3] = {100.0, channel.m_eqMidFreqModel.value(), 8000.0};
	const std::complex<double> z1 = std::polar(1.0, -2.0 * 3.14159265358979 * hz / rate);  // e^-jw
	double db = 0.0;
	for (int b = 0; b < 3; ++b)
	{
		if (gains[b] == 0.f) { continue; }
		const Biquad q = band(freqs[b], gains[b], rate, b == 1, b == 2);
		const double b0 = q.b0, b1 = q.b1, b2 = q.b2, a1 = q.a1, a2 = q.a2;
		const std::complex<double> h = (b0 + z1 * (b1 + z1 * b2)) / (1.0 + z1 * (a1 + z1 * a2));
		db += 20.0 * std::log10(std::max(std::abs(h), 1e-9));
	}
	return static_cast<float>(db);
}

std::array<float, 2> sendLevel(MixerRoute& route)
{
	const MixerChannel* from = route.sender();
	if (!route.enabled() || from->m_muted.load(std::memory_order_relaxed)) { return {0.f, 0.f}; }
	const float gain = route.amount()->value() * (route.preFader() ? 1.f : from->m_volumeModel.value());
	return {from->m_livePeakLeft.get() * gain, from->m_livePeakRight.get() * gain};
}

//! MX-172: a channel on the crossfader takes its side's gain (MixerOptions' balance gains of the position); skipped
//! while it is on neither side, or at full gain.
void applyCrossfade(MixerChannel& channel, f_cnt_t frames)
{
	const int side = channel.m_crossfadeSideModel.value();
	if (side == 0) { return; }
	float a = 1.f;
	float b = 1.f;
	MixerOptions::gains(PanLaw::Balance, MixerOptions::instance().crossfader(), &a, &b);
	const float gain = side == 1 ? a : b;
	if (gain == 1.f) { return; }
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		channel.m_buffer[f][0] *= gain;
		channel.m_buffer[f][1] *= gain;
	}
}

void applyOutput(MixerChannel& channel, f_cnt_t frames)
{
	applyChannelEq(channel, frames);  // MX-085: the strip's EQ, after the inserts
	applyCrossfade(channel, frames);  // MX-172
	const float left = channel.m_polarityLeftModel.value() ? -1.f : 1.f;
	const float right = channel.m_polarityRightModel.value() ? -1.f : 1.f;
	const bool mono = channel.m_monoModel.value();
	const std::size_t from = channel.m_swapModel.value() ? 1 : 0;  // MX-033: the sides exchanged
	if (left == 1.f && right == 1.f && !mono && from == 0) { return; }
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		float l = channel.m_buffer[f][from] * left;
		float r = channel.m_buffer[f][1 - from] * right;
		if (mono) { l = r = 0.5f * (l + r); }
		channel.m_buffer[f][0] = l;
		channel.m_buffer[f][1] = r;
	}
}

//! MX-025: the balance pan under the project's pan law; false (the classic stage runs) for the balance law.
bool applyPanLaw(MixerChannel& channel, f_cnt_t frames)
{
	const PanLaw law = MixerOptions::instance().panLaw();
	if (law == PanLaw::Balance) { return false; }
	const ValueBuffer* buf = channel.m_panModel.valueBuffer();
	float left = 1.f;
	float right = 1.f;
	MixerOptions::gains(law, channel.m_panModel.value(), &left, &right);
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		if (buf != nullptr) { MixerOptions::gains(law, buf->values()[f], &left, &right); }
		channel.m_buffer[f][0] *= left;
		channel.m_buffer[f][1] *= right;
	}
	return true;
}

bool applyPanMode(MixerChannel& channel, f_cnt_t frames)
{
	const auto mode = static_cast<PanMode>(channel.m_panModeModel.value());
	if (mode == PanMode::Balance) { return applyPanLaw(channel, frames); }
	// An automated pan is read per frame from its sample-exact buffer, as the balance stage reads it.
	const ValueBuffer* leftBuf = channel.m_panModel.valueBuffer();
	const float leftPan = channel.m_panModel.value();
	const auto panAt = [](const ValueBuffer* buf, float fixed, f_cnt_t f) { return buf != nullptr ? buf->values()[f] : fixed; };
	if (mode == PanMode::Dual)
	{
		// Each side placed on its own: the left input at m_panModel, the right input at m_panRightModel.
		const ValueBuffer* rightBuf = channel.m_panRightModel.valueBuffer();
		const float rightPan = channel.m_panRightModel.value();
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			const float pl = (panAt(leftBuf, leftPan, f) + 1.f) * 0.5f;
			const float pr = (panAt(rightBuf, rightPan, f) + 1.f) * 0.5f;
			const float l = channel.m_buffer[f][0];
			const float r = channel.m_buffer[f][1];
			channel.m_buffer[f][0] = l * (1.f - pl) + r * (1.f - pr);
			channel.m_buffer[f][1] = l * pl + r * pr;
		}
		return true;
	}
	// Width: mid/side, the side scaled (0 mono, 1 as recorded, 2 twice as wide); then the balance pan.
	const float width = channel.m_widthModel.value();
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		const float pan = panAt(leftBuf, leftPan, f);
		const float mid = 0.5f * (channel.m_buffer[f][0] + channel.m_buffer[f][1]);
		const float side = 0.5f * (channel.m_buffer[f][0] - channel.m_buffer[f][1]) * width;
		channel.m_buffer[f][0] = (mid + side) * std::min(1.f, 1.f - pan);
		channel.m_buffer[f][1] = (mid - side) * std::min(1.f, 1.f + pan);
	}
	return true;
}

namespace
{

float chainReductionDb(const EffectChain& chain, float reduction)
{
	for (const Effect* effect : chain.effects()) { reduction = std::max(reduction, effect->gainReductionDb()); }
	return reduction;
}

//! The strip's gain-reduction reading: the most any device reports, in either insert chain or in a rack
//! chain that runs (every one in parallel, else the selected one; chain 0 is m_fxChain). A rack's chain
//! list only changes while no period is processed (Rack.h), so this read is as safe as its process().
float reductionDb(const MixerChannel& channel)
{
	float reduction = chainReductionDb(channel.m_postInserts.chain(), chainReductionDb(channel.m_fxChain, 0.f));
	const int selected = channel.m_rack.selectedChain();
	for (int i = 1; i < channel.m_rack.chainCount(); ++i)
	{
		const EffectChain* chain = channel.m_rack.chain(i);
		if (chain != nullptr && (selected == Rack::Parallel || selected == i)) { reduction = chainReductionDb(*chain, reduction); }
	}
	return reduction;
}

} // namespace

void updateMeters(MixerChannel& channel, const SampleFrame* buffer, float volume, f_cnt_t frames)
{
	float peakL = 0.f;
	float peakR = 0.f;
	double sumL = 0.0;
	double sumR = 0.0;
	double sumLR = 0.0;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		peakL = std::max(peakL, std::abs(buffer[f][0]));
		peakR = std::max(peakR, std::abs(buffer[f][1]));
		sumL += static_cast<double>(buffer[f][0]) * buffer[f][0];
		sumR += static_cast<double>(buffer[f][1]) * buffer[f][1];
		sumLR += static_cast<double>(buffer[f][0]) * buffer[f][1];
	}
	// MX-137: the phase correlation, +1 in phase (mono), 0 unrelated, -1 out of phase; 1 for silence.
	channel.m_correlation.set(sumL > 1e-12 && sumR > 1e-12 ? static_cast<float>(sumLR / std::sqrt(sumL * sumR)) : 1.f);
	// MX-082: baked post-fader inserts put the fader (and their devices) into the buffer; the pre-fader
	// readings, and the send meters that apply the fader themselves, take the copy kept before them.
	const SampleFrame pre = channel.m_postInserts.baked() ? getAbsPeakValues(channel.m_postInserts.preFader(), frames)
		: SampleFrame(peakL, peakR);
	channel.m_livePeakLeft.set(pre[0]);  // MX-053: what the send meters read
	channel.m_livePeakRight.set(pre[1]);
	channel.m_prePeakLeft.raise(pre[0]);
	channel.m_prePeakRight.raise(pre[1]);
	channel.m_preMeterLeft.raise(pre[0]);
	channel.m_preMeterRight.raise(pre[1]);
	const float n = static_cast<float>(std::max<f_cnt_t>(1, frames));
	channel.m_rmsLeft.set(std::sqrt(static_cast<float>(sumL) / n) * volume);
	channel.m_rmsRight.set(std::sqrt(static_cast<float>(sumR) / n) * volume);
	if (peakL * volume >= 1.f || peakR * volume >= 1.f) { channel.m_clipped.store(true, std::memory_order_relaxed); }
	channel.m_gainReduction.set(reductionDb(channel));
}

void silenceMeters(MixerChannel& channel)
{
	channel.m_rmsLeft.set(0.f);
	channel.m_rmsRight.set(0.f);
	channel.m_livePeakLeft.set(0.f);
	channel.m_livePeakRight.set(0.f);
	channel.m_correlation.set(1.f);  // what silence reads (updateMeters)
	channel.m_gainReduction.set(0.f);
}

void save(const MixerChannel& c, QDomElement& e)
{
	if (c.m_trimModel.value() != 0.f) { e.setAttribute(QStringLiteral("trim"), static_cast<double>(c.m_trimModel.value())); }
	const auto flag = [&e](const BoolModel& model, const char* name) { if (model.value()) { e.setAttribute(QLatin1String(name), 1); } };
	flag(c.m_polarityLeftModel, "polarityl");
	flag(c.m_polarityRightModel, "polarityr");
	flag(c.m_monoModel, "mono");
	flag(c.m_swapModel, "swap");
	flag(c.m_inputMonoModel, "inputmono");
	if (c.m_crossfadeSideModel.value() != 0) { e.setAttribute(QStringLiteral("xfade"), c.m_crossfadeSideModel.value()); }
	const auto level = [&e](const FloatModel& model, const char* name, float neutral) {
		if (model.value() != neutral) { e.setAttribute(QLatin1String(name), static_cast<double>(model.value())); }
	};
	level(c.m_eqLowModel, "eqlow", 0.f);
	level(c.m_eqMidModel, "eqmid", 0.f);
	level(c.m_eqMidFreqModel, "eqmidf", 1000.f);
	level(c.m_eqHighModel, "eqhigh", 0.f);
	if (c.m_panModeModel.value() != 0) { e.setAttribute(QStringLiteral("panmode"), c.m_panModeModel.value()); }
	if (c.m_panRightModel.value() != 0.f) { e.setAttribute(QStringLiteral("panright"), static_cast<double>(c.m_panRightModel.value())); }
	if (c.m_widthModel.value() != 1.f) { e.setAttribute(QStringLiteral("width"), static_cast<double>(c.m_widthModel.value())); }
	flag(c.m_soloSafeModel, "solosafe");
	if (!c.m_note.isEmpty()) { e.setAttribute(QStringLiteral("note"), c.m_note); }
}

void load(MixerChannel& c, const QDomElement& e)
{
	c.m_trimModel.setValue(e.attribute(QStringLiteral("trim"), QStringLiteral("0")).toFloat());
	c.m_polarityLeftModel.setValue(e.attribute(QStringLiteral("polarityl")).toInt() != 0);
	c.m_polarityRightModel.setValue(e.attribute(QStringLiteral("polarityr")).toInt() != 0);
	c.m_monoModel.setValue(e.attribute(QStringLiteral("mono")).toInt() != 0);
	c.m_swapModel.setValue(e.attribute(QStringLiteral("swap")).toInt() != 0);
	c.m_inputMonoModel.setValue(e.attribute(QStringLiteral("inputmono")).toInt() != 0);
	c.m_crossfadeSideModel.setValue(e.attribute(QStringLiteral("xfade"), QStringLiteral("0")).toInt());
	c.m_eqLowModel.setValue(e.attribute(QStringLiteral("eqlow"), QStringLiteral("0")).toFloat());
	c.m_eqMidModel.setValue(e.attribute(QStringLiteral("eqmid"), QStringLiteral("0")).toFloat());
	c.m_eqMidFreqModel.setValue(e.attribute(QStringLiteral("eqmidf"), QStringLiteral("1000")).toFloat());
	c.m_eqHighModel.setValue(e.attribute(QStringLiteral("eqhigh"), QStringLiteral("0")).toFloat());
	c.m_panModeModel.setValue(e.attribute(QStringLiteral("panmode"), QStringLiteral("0")).toInt());
	c.m_panRightModel.setValue(e.attribute(QStringLiteral("panright"), QStringLiteral("0")).toFloat());
	c.m_widthModel.setValue(e.attribute(QStringLiteral("width"), QStringLiteral("1")).toFloat());
	c.m_soloSafeModel.setValue(e.attribute(QStringLiteral("solosafe")).toInt() != 0);
	c.m_note = e.attribute(QStringLiteral("note"));
}

bool additiveSolo()
{
	return ConfigManager::inst()->value(QStringLiteral("mixer"), QStringLiteral("additivesolo"), QStringLiteral("0")).toInt() != 0;
}

void setAdditiveSolo(bool additive)
{
	ConfigManager::inst()->setValue(QStringLiteral("mixer"), QStringLiteral("additivesolo"), additive ? QStringLiteral("1") : QStringLiteral("0"));
}

} // namespace lmms::mixerstrip

namespace lmms
{

void Mixer::applyAdditiveSolo()
{
	// Every soloed channel is heard (and what it routes through, as an exclusive solo hears it); the
	// rest are muted, solo-safe ones excepted. The mute state from before the first solo comes back
	// when the last solo goes.
	std::vector<MixerChannel*> soloed;
	for (MixerChannel* channel : m_mixerChannels)
	{
		if (channel->m_soloModel.value()) { soloed.push_back(channel); }
	}
	if (m_lastSoloed != -1) { deactivateSolo(); }
	if (soloed.empty())
	{
		m_lastSoloed = -1;
		return;
	}
	activateSolo();
	for (MixerChannel* channel : soloed) { channel->unmuteForSolo(); }
	m_lastSoloed = soloed.front()->index();
}

} // namespace lmms
