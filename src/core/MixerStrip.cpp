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
#include <vector>

#include <QDomElement>

#include "ConfigManager.h"
#include "Effect.h"
#include "Mixer.h"
#include "SampleFrame.h"
#include "ValueBuffer.h"

namespace lmms::mixerstrip
{

void applyInput(MixerChannel& channel, f_cnt_t frames)
{
	const float db = channel.m_trimModel.value();
	if (db == 0.f) { return; }
	const float gain = std::pow(10.f, db / 20.f);
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		channel.m_buffer[f][0] *= gain;
		channel.m_buffer[f][1] *= gain;
	}
}

void applyOutput(MixerChannel& channel, f_cnt_t frames)
{
	const float left = channel.m_polarityLeftModel.value() ? -1.f : 1.f;
	const float right = channel.m_polarityRightModel.value() ? -1.f : 1.f;
	const bool mono = channel.m_monoModel.value();
	if (left == 1.f && right == 1.f && !mono) { return; }
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		float l = channel.m_buffer[f][0] * left;
		float r = channel.m_buffer[f][1] * right;
		if (mono) { l = r = 0.5f * (l + r); }
		channel.m_buffer[f][0] = l;
		channel.m_buffer[f][1] = r;
	}
}

bool applyPanMode(MixerChannel& channel, f_cnt_t frames)
{
	const auto mode = static_cast<PanMode>(channel.m_panModeModel.value());
	if (mode == PanMode::Balance) { return false; }
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

void updateMeters(MixerChannel& channel, const SampleFrame* buffer, float volume, f_cnt_t frames)
{
	float peakL = 0.f;
	float peakR = 0.f;
	double sumL = 0.0;
	double sumR = 0.0;
	for (f_cnt_t f = 0; f < frames; ++f)
	{
		peakL = std::max(peakL, std::abs(buffer[f][0]));
		peakR = std::max(peakR, std::abs(buffer[f][1]));
		sumL += static_cast<double>(buffer[f][0]) * buffer[f][0];
		sumR += static_cast<double>(buffer[f][1]) * buffer[f][1];
	}
	channel.m_prePeakLeft.raise(peakL);
	channel.m_prePeakRight.raise(peakR);
	channel.m_preMeterLeft.raise(peakL);
	channel.m_preMeterRight.raise(peakR);
	const float n = static_cast<float>(std::max<f_cnt_t>(1, frames));
	channel.m_rmsLeft.set(std::sqrt(static_cast<float>(sumL) / n) * volume);
	channel.m_rmsRight.set(std::sqrt(static_cast<float>(sumR) / n) * volume);
	if (peakL * volume >= 1.f || peakR * volume >= 1.f) { channel.m_clipped.store(true, std::memory_order_relaxed); }
	float reduction = 0.f;
	for (const Effect* effect : channel.m_fxChain.effects()) { reduction = std::max(reduction, effect->gainReductionDb()); }
	channel.m_gainReduction.set(reduction);
}

void save(const MixerChannel& c, QDomElement& e)
{
	if (c.m_trimModel.value() != 0.f) { e.setAttribute(QStringLiteral("trim"), static_cast<double>(c.m_trimModel.value())); }
	if (c.m_polarityLeftModel.value()) { e.setAttribute(QStringLiteral("polarityl"), 1); }
	if (c.m_polarityRightModel.value()) { e.setAttribute(QStringLiteral("polarityr"), 1); }
	if (c.m_monoModel.value()) { e.setAttribute(QStringLiteral("mono"), 1); }
	if (c.m_panModeModel.value() != 0) { e.setAttribute(QStringLiteral("panmode"), c.m_panModeModel.value()); }
	if (c.m_panRightModel.value() != 0.f) { e.setAttribute(QStringLiteral("panright"), static_cast<double>(c.m_panRightModel.value())); }
	if (c.m_widthModel.value() != 1.f) { e.setAttribute(QStringLiteral("width"), static_cast<double>(c.m_widthModel.value())); }
	if (c.m_soloSafeModel.value()) { e.setAttribute(QStringLiteral("solosafe"), 1); }
	if (!c.m_note.isEmpty()) { e.setAttribute(QStringLiteral("note"), c.m_note); }
}

void load(MixerChannel& c, const QDomElement& e)
{
	c.m_trimModel.setValue(e.attribute(QStringLiteral("trim"), QStringLiteral("0")).toFloat());
	c.m_polarityLeftModel.setValue(e.attribute(QStringLiteral("polarityl")).toInt() != 0);
	c.m_polarityRightModel.setValue(e.attribute(QStringLiteral("polarityr")).toInt() != 0);
	c.m_monoModel.setValue(e.attribute(QStringLiteral("mono")).toInt() != 0);
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
