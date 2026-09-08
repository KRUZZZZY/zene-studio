/*
 * RoutingNodes.h
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

#ifndef LMMS_ROUTING_NODES_H
#define LMMS_ROUTING_NODES_H

#include <vector>

#include "RoutingNode.h"
#include "lmms_export.h"

namespace lmms
{

/**
 * @brief Emits a constant value on every channel.
 *
 * MVP stand-in for an instrument source (the full Patcher would wrap real
 * instruments/effects as nodes).
 */
class LMMS_EXPORT ConstantSourceNode : public RoutingNode
{
public:
	ConstantSourceNode() = default;
	explicit ConstantSourceNode(float value) : m_value(value) {}

	auto typeName() const -> QString override { return QStringLiteral("constant"); }
	auto inputCount() const -> int override { return 0; }

	void process(f_cnt_t frames) override;

	auto value() const -> float { return m_value; }
	void setValue(float value) { m_value = value; }

	void saveSettings(QDomElement& element) const override;
	void loadSettings(const QDomElement& element) override;

private:
	float m_value = 1.0f;
};

//! One-pole low-pass filter: y[n] = y[n-1] + alpha * (x[n] - y[n-1])
class LMMS_EXPORT OnePoleLowPassNode : public RoutingNode
{
public:
	OnePoleLowPassNode() = default;
	explicit OnePoleLowPassNode(float alpha) : m_alpha(alpha) {}

	auto typeName() const -> QString override { return QStringLiteral("onepole_lowpass"); }

	void prepare(f_cnt_t frames, ch_cnt_t channels) override;
	void process(f_cnt_t frames) override;

	auto alpha() const -> float { return m_alpha; }
	void setAlpha(float alpha) { m_alpha = alpha; }
	void reset();

	void saveSettings(QDomElement& element) const override;
	void loadSettings(const QDomElement& element) override;

private:
	float m_alpha = 0.5f;
	std::vector<float> m_state;
};

//! Multiplies the summed input by a gain factor
class LMMS_EXPORT GainNode : public RoutingNode
{
public:
	GainNode() = default;
	explicit GainNode(float gain) : m_gain(gain) {}

	auto typeName() const -> QString override { return QStringLiteral("gain"); }

	void process(f_cnt_t frames) override;

	auto gain() const -> float { return m_gain; }
	void setGain(float gain) { m_gain = gain; }

	void saveSettings(QDomElement& element) const override;
	void loadSettings(const QDomElement& element) override;

private:
	float m_gain = 1.0f;
};

//! Sums all inputs into its output; the graph's boundary to the host channel
class LMMS_EXPORT SinkNode : public RoutingNode
{
public:
	auto typeName() const -> QString override { return QStringLiteral("sink"); }

	void process(f_cnt_t frames) override;
};

} // namespace lmms

#endif // LMMS_ROUTING_NODES_H
