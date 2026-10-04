/*
 * MixerSurround.cpp - surround mixer channels: the layouts, the panner's gains, the downmix, saving
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

#include "MixerSurround.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QDomDocument>
#include <QDomElement>

namespace lmms
{

namespace surround
{

namespace
{

struct Speaker
{
	Role role;
	float x;
	float y;
};

struct Layout
{
	int count;
	std::array<Speaker, MaxChannels> speakers;
	const char* name;
};

//! The formats in the WAVE channel order, each speaker at its point of the panner's square room: the
//! front pair at the front corners, the surrounds at the back corners (7.1: its side pair at the sides).
const std::array<Layout, FormatCount> kLayouts{{
	{2, {{{Role::L, -1.f, 1.f}, {Role::R, 1.f, 1.f}}}, "stereo"},
	{3, {{{Role::L, -1.f, 1.f}, {Role::R, 1.f, 1.f}, {Role::C, 0.f, 1.f}}}, "lcr"},
	{4, {{{Role::L, -1.f, 1.f}, {Role::R, 1.f, 1.f}, {Role::Ls, -1.f, -1.f}, {Role::Rs, 1.f, -1.f}}}, "quad"},
	{6, {{{Role::L, -1.f, 1.f}, {Role::R, 1.f, 1.f}, {Role::C, 0.f, 1.f}, {Role::Lfe, 0.f, 0.f}, {Role::Ls, -1.f, -1.f},
		{Role::Rs, 1.f, -1.f}}}, "5.1"},
	{8, {{{Role::L, -1.f, 1.f}, {Role::R, 1.f, 1.f}, {Role::C, 0.f, 1.f}, {Role::Lfe, 0.f, 0.f}, {Role::Lrs, -1.f, -1.f},
		{Role::Rrs, 1.f, -1.f}, {Role::Ls, -1.f, 0.f}, {Role::Rs, 1.f, 0.f}}}, "7.1"},
}};

const std::array<const char*, MaxChannels> kRoleNames{"L", "R", "C", "LFE", "Ls", "Rs", "Lrs", "Rrs"};

constexpr float TwoPi = 2.f * std::numbers::pi_v<float>;
constexpr float HalfPi = 0.5f * std::numbers::pi_v<float>;

const Layout& layoutOf(Format format)
{
	return kLayouts[static_cast<std::size_t>(std::clamp(static_cast<int>(format), 0, FormatCount - 1))];
}

//! An angle in [0, 2 pi).
float wrapped(float angle)
{
	return angle - TwoPi * std::floor(angle / TwoPi);
}

float azimuth(const Speaker& speaker)
{
	return std::atan2(speaker.x, speaker.y);
}

//! The speaker at or just before @a theta, going round the room, and how far before it is.
int speakerBefore(const Layout& layout, float theta, float* offset)
{
	int best = -1;
	*offset = TwoPi;
	for (int c = 0; c < layout.count; ++c)
	{
		const Speaker& speaker = layout.speakers[static_cast<std::size_t>(c)];
		const float d = wrapped(theta - azimuth(speaker));
		if (speaker.role != Role::Lfe && d < *offset)
		{
			*offset = d;
			best = c;
		}
	}
	return best;
}

//! The next speaker round the room after speaker @a from, and the angle between them.
int speakerAfter(const Layout& layout, int from, float* span)
{
	const float start = azimuth(layout.speakers[static_cast<std::size_t>(from)]);
	int best = from;
	*span = TwoPi;
	for (int c = 0; c < layout.count; ++c)
	{
		const Speaker& speaker = layout.speakers[static_cast<std::size_t>(c)];
		const float d = wrapped(azimuth(speaker) - start);
		if (c != from && speaker.role != Role::Lfe && d > 0.f && d < *span)
		{
			*span = d;
			best = c;
		}
	}
	return best;
}

//! Constant-power pairwise panning: the two speakers either side of direction @a theta.
void pointGains(const Layout& layout, float theta, Gains& gains)
{
	float offset = 0.f;
	const int a = speakerBefore(layout, theta, &offset);
	if (a < 0) { return; }
	float span = 0.f;
	const int b = speakerAfter(layout, a, &span);
	const float t = b == a || span <= 0.f ? 0.f : std::clamp(offset / span, 0.f, 1.f);
	gains[static_cast<std::size_t>(a)] += std::cos(t * HalfPi);
	gains[static_cast<std::size_t>(b)] += std::sin(t * HalfPi);
}

int speakerCount(const Layout& layout)
{
	return static_cast<int>(std::count_if(layout.speakers.begin(), layout.speakers.begin() + layout.count,
		[](const Speaker& speaker) { return speaker.role != Role::Lfe; }));
}

//! Blend @a gains toward every speaker alike by @a amount, then bring their power back to one.
void spreadAndNormalise(const Layout& layout, Gains& gains, float amount)
{
	const float even = 1.f / std::sqrt(static_cast<float>(std::max(1, speakerCount(layout))));
	float power = 0.f;
	for (int c = 0; c < layout.count; ++c)
	{
		if (layout.speakers[static_cast<std::size_t>(c)].role == Role::Lfe) { continue; }
		float& g = gains[static_cast<std::size_t>(c)];
		g = (1.f - amount) * g + amount * even;
		power += g * g;
	}
	const float scale = power > 0.f ? 1.f / std::sqrt(power) : 0.f;
	for (float& g : gains) { g *= scale; }
}

struct Fold
{
	Role role;
	float gain;
};

//! Where a speaker the receiver lacks goes instead (ITU-R BS.775): its folds into @a out, how many.
int foldsOf(Role role, const DownmixOptions& options, std::array<Fold, 2>& out)
{
	switch (role)
	{
	case Role::C: out = {{{Role::L, options.centre}, {Role::R, options.centre}}}; return 2;
	case Role::Lfe: out = {{{Role::L, options.lfe}, {Role::R, options.lfe}}}; return options.lfeOn ? 2 : 0;
	case Role::Ls: out[0] = {Role::L, options.surround}; return 1;
	case Role::Rs: out[0] = {Role::R, options.surround}; return 1;
	case Role::Lrs: out[0] = {Role::Ls, 1.f}; return 1;
	case Role::Rrs: out[0] = {Role::Rs, 1.f}; return 1;
	default: return 0;
	}
}

//! Sender channel @a from (speaker @a role) into @a to: carried, or folded - at most two folds deep.
void addRole(Matrix& m, Format to, int from, Role role, float gain, const DownmixOptions& options, int depth)
{
	const int target = channelOf(to, role);
	if (target >= 0)
	{
		m[static_cast<std::size_t>(target)][static_cast<std::size_t>(from)] += gain;
		return;
	}
	std::array<Fold, 2> folds{};
	const int count = depth < 3 ? foldsOf(role, options, folds) : 0;
	for (int k = 0; k < count; ++k)
	{
		addRole(m, to, from, folds[static_cast<std::size_t>(k)].role, gain * folds[static_cast<std::size_t>(k)].gain, options, depth + 1);
	}
}

Matrix buildMatrix(Format from, Format to, const DownmixOptions& options)
{
	Matrix m{};
	for (int c = 0; c < channelCount(from); ++c) { addRole(m, to, c, roleOf(from, c), 1.f, options, 0); }
	return m;
}

using MatrixTable = std::array<std::array<Matrix, FormatCount>, FormatCount>;

MatrixTable buildMatrices(const DownmixOptions& options)
{
	MatrixTable table{};
	for (int from = 0; from < FormatCount; ++from)
	{
		for (int to = 0; to < FormatCount; ++to)
		{
			table[static_cast<std::size_t>(from)][static_cast<std::size_t>(to)] =
				buildMatrix(static_cast<Format>(from), static_cast<Format>(to), options);
		}
	}
	return table;
}

DownmixOptions s_options;
MatrixTable s_matrices = buildMatrices(DownmixOptions{});

float attribute(const QDomElement& element, const char* name, float fallback, float low, float high)
{
	bool ok = false;
	const float value = element.attribute(QString::fromLatin1(name)).toFloat(&ok);
	return ok ? std::clamp(value, low, high) : fallback;
}

} // namespace

int channelCount(Format format)
{
	return layoutOf(format).count;
}

Role roleOf(Format format, int channel)
{
	const Layout& layout = layoutOf(format);
	return layout.speakers[static_cast<std::size_t>(std::clamp(channel, 0, layout.count - 1))].role;
}

int channelOf(Format format, Role role)
{
	const Layout& layout = layoutOf(format);
	for (int c = 0; c < layout.count; ++c)
	{
		if (layout.speakers[static_cast<std::size_t>(c)].role == role) { return c; }
	}
	return -1;
}

QString formatName(Format format)
{
	return QString::fromLatin1(layoutOf(format).name);
}

bool formatFromName(const QString& name, Format* format)
{
	for (int k = 0; k < FormatCount; ++k)
	{
		if (name.compare(QString::fromLatin1(kLayouts[static_cast<std::size_t>(k)].name), Qt::CaseInsensitive) != 0) { continue; }
		*format = static_cast<Format>(k);
		return true;
	}
	return false;
}

QStringList formatNames()
{
	QStringList names;
	for (const Layout& layout : kLayouts) { names.append(QString::fromLatin1(layout.name)); }
	return names;
}

QStringList speakerNames(Format format)
{
	QStringList names;
	for (int c = 0; c < channelCount(format); ++c) { names.append(QString::fromLatin1(kRoleNames[static_cast<std::size_t>(roleOf(format, c))])); }
	return names;
}

void speakerPoint(Format format, int channel, float* x, float* y)
{
	const Layout& layout = layoutOf(format);
	const Speaker& speaker = layout.speakers[static_cast<std::size_t>(std::clamp(channel, 0, layout.count - 1))];
	*x = speaker.x;
	*y = speaker.y;
}

Gains panGains(Format format, const PanPosition& pan)
{
	const Layout& layout = layoutOf(format);
	const float x = std::clamp(pan.x, -1.f, 1.f);
	const float y = std::clamp(pan.y, -1.f, 1.f);
	Gains gains{};
	pointGains(layout, std::atan2(x, y), gains);
	// A point nearer the middle of the room is heard from more of it: at the centre, every speaker alike.
	const float distance = std::min(1.f, std::hypot(x, y));
	const float spread = std::clamp(pan.spread, 0.f, 1.f);
	spreadAndNormalise(layout, gains, spread + (1.f - spread) * (1.f - distance));
	const int lfe = channelOf(format, Role::Lfe);
	if (lfe >= 0) { gains[static_cast<std::size_t>(lfe)] = std::clamp(pan.lfe, 0.f, 1.f); }
	return gains;
}

const Matrix& matrix(Format from, Format to)
{
	const auto f = static_cast<std::size_t>(std::clamp(static_cast<int>(from), 0, FormatCount - 1));
	const auto t = static_cast<std::size_t>(std::clamp(static_cast<int>(to), 0, FormatCount - 1));
	return s_matrices[f][t];
}

const DownmixOptions& downmixOptions()
{
	return s_options;
}

void setDownmixOptions(const DownmixOptions& options)
{
	s_options = options;
	s_matrices = buildMatrices(options);
}

void saveMixer(QDomDocument& doc, QDomElement& mixerElement)
{
	if (s_options == DownmixOptions{}) { return; }
	QDomElement element = doc.createElement(QStringLiteral("surrounddownmix"));
	element.setAttribute(QStringLiteral("centre"), static_cast<double>(s_options.centre));
	element.setAttribute(QStringLiteral("surround"), static_cast<double>(s_options.surround));
	element.setAttribute(QStringLiteral("lfeon"), s_options.lfeOn ? 1 : 0);
	element.setAttribute(QStringLiteral("lfe"), static_cast<double>(s_options.lfe));
	mixerElement.appendChild(element);
}

void loadMixer(const QDomElement& mixerElement)
{
	const QDomElement element = mixerElement.firstChildElement(QStringLiteral("surrounddownmix"));
	DownmixOptions options;
	if (!element.isNull())
	{
		options.centre = attribute(element, "centre", options.centre, 0.f, 2.f);
		options.surround = attribute(element, "surround", options.surround, 0.f, 2.f);
		options.lfeOn = element.attribute(QStringLiteral("lfeon")).toInt() != 0;
		options.lfe = attribute(element, "lfe", options.lfe, 0.f, 4.f);
	}
	setDownmixOptions(options);
}

void resetMixer()
{
	setDownmixOptions(DownmixOptions{});
}

} // namespace surround

void ChannelSurround::init(f_cnt_t frames)
{
	m_frames = std::max<f_cnt_t>(frames, 1);
	m_side.assign(static_cast<std::size_t>(surround::SideChannels) * m_frames, 0.f);
	m_written = false;
	setPan(m_pan);
}

void ChannelSurround::setFormat(surround::Format format)
{
	m_format = format;
	std::fill(m_side.begin(), m_side.end(), 0.f);
	m_written = false;
}

void ChannelSurround::setPan(const surround::PanPosition& pan)
{
	m_pan = pan;
	for (int k = 0; k < surround::FormatCount; ++k)
	{
		m_gains[static_cast<std::size_t>(k)] = surround::panGains(static_cast<surround::Format>(k), pan);
	}
}

void ChannelSurround::clearPeriod() noexcept
{
	if (!m_written) { return; }
	std::fill(m_side.begin(), m_side.end(), 0.f);
	m_written = false;
}

void ChannelSurround::save(QDomDocument& doc, QDomElement& channelElement) const
{
	if (!isSurround() && m_pan == surround::PanPosition{}) { return; }
	QDomElement element = doc.createElement(QStringLiteral("surround"));
	element.setAttribute(QStringLiteral("format"), surround::formatName(m_format));
	if (!(m_pan == surround::PanPosition{}))
	{
		element.setAttribute(QStringLiteral("pan"), m_pan.enabled ? 1 : 0);
		element.setAttribute(QStringLiteral("x"), static_cast<double>(m_pan.x));
		element.setAttribute(QStringLiteral("y"), static_cast<double>(m_pan.y));
		element.setAttribute(QStringLiteral("lfe"), static_cast<double>(m_pan.lfe));
		element.setAttribute(QStringLiteral("spread"), static_cast<double>(m_pan.spread));
	}
	channelElement.appendChild(element);
}

void ChannelSurround::load(const QDomElement& channelElement)
{
	reset();
	const QDomElement element = channelElement.firstChildElement(QStringLiteral("surround"));
	if (element.isNull()) { return; }
	surround::Format format = surround::Format::Stereo;
	surround::formatFromName(element.attribute(QStringLiteral("format")), &format);
	setFormat(format);
	surround::PanPosition pan;
	pan.enabled = element.attribute(QStringLiteral("pan")).toInt() != 0;
	pan.x = surround::attribute(element, "x", pan.x, -1.f, 1.f);
	pan.y = surround::attribute(element, "y", pan.y, -1.f, 1.f);
	pan.lfe = surround::attribute(element, "lfe", pan.lfe, 0.f, 1.f);
	pan.spread = surround::attribute(element, "spread", pan.spread, 0.f, 1.f);
	setPan(pan);
}

void ChannelSurround::reset()
{
	setFormat(surround::Format::Stereo);
	setPan(surround::PanPosition{});
}

} // namespace lmms
