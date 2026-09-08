/*
 * RoutingNodes.cpp
 *
 * Copyright (c) 2026 Zachariah Markusson <zachariahmarkusson@gmail.com>
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

#include "RoutingNodes.h"

#include <algorithm>

namespace lmms
{

namespace
{

void saveParam(QDomElement& element, const QString& name, float value)
{
	QDomElement param = element.ownerDocument().createElement(QStringLiteral("param"));
	param.setAttribute(QStringLiteral("name"), name);
	param.setAttribute(QStringLiteral("value"), QString::number(value, 'g', 9));
	element.appendChild(param);
}

auto loadParam(const QDomElement& element, const QString& name, float fallback) -> float
{
	for (QDomElement param = element.firstChildElement(QStringLiteral("param"));
		!param.isNull(); param = param.nextSiblingElement(QStringLiteral("param")))
	{
		if (param.attribute(QStringLiteral("name")) != name) { continue; }
		bool ok = false;
		const float value = param.attribute(QStringLiteral("value")).toFloat(&ok);
		if (ok) { return value; }
	}
	return fallback;
}

} // namespace

void ConstantSourceNode::process(f_cnt_t frames)
{
	zeroOutputs();
	AudioBuffer& out = output(0);
	for (ch_cnt_t c = 0; c < channels(); ++c)
	{
		float* dst = out.buffer(c).data();
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			dst[f] = m_value;
		}
	}
	markOutputsNonSilent();
}

void ConstantSourceNode::saveSettings(QDomElement& element) const
{
	saveParam(element, QStringLiteral("value"), m_value);
}

void ConstantSourceNode::loadSettings(const QDomElement& element)
{
	m_value = loadParam(element, QStringLiteral("value"), m_value);
}

void OnePoleLowPassNode::prepare(f_cnt_t frames, ch_cnt_t channels)
{
	RoutingNode::prepare(frames, channels);
	m_state.assign(channels, 0.0f);
}

void OnePoleLowPassNode::reset()
{
	std::fill(m_state.begin(), m_state.end(), 0.0f);
}

void OnePoleLowPassNode::process(f_cnt_t frames)
{
	zeroOutputs();
	AudioBuffer& out = output(0);
	sumInputs(0, frames, out);

	for (ch_cnt_t c = 0; c < channels(); ++c)
	{
		float* dst = out.buffer(c).data();
		float state = c < m_state.size() ? m_state[c] : 0.0f;
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			state += m_alpha * (dst[f] - state);
			dst[f] = state;
		}
		if (c < m_state.size()) { m_state[c] = state; }
	}
	markOutputsNonSilent();
}

void OnePoleLowPassNode::saveSettings(QDomElement& element) const
{
	saveParam(element, QStringLiteral("alpha"), m_alpha);
}

void OnePoleLowPassNode::loadSettings(const QDomElement& element)
{
	m_alpha = loadParam(element, QStringLiteral("alpha"), m_alpha);
}

void GainNode::process(f_cnt_t frames)
{
	zeroOutputs();
	AudioBuffer& out = output(0);
	sumInputs(0, frames, out);
	for (ch_cnt_t c = 0; c < channels(); ++c)
	{
		float* dst = out.buffer(c).data();
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			dst[f] *= m_gain;
		}
	}
	markOutputsNonSilent();
}

void GainNode::saveSettings(QDomElement& element) const
{
	saveParam(element, QStringLiteral("gain"), m_gain);
}

void GainNode::loadSettings(const QDomElement& element)
{
	m_gain = loadParam(element, QStringLiteral("gain"), m_gain);
}

void SinkNode::process(f_cnt_t frames)
{
	zeroOutputs();
	sumInputs(0, frames, output(0));
	markOutputsNonSilent();
}

} // namespace lmms
