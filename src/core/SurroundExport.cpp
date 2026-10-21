/*
 * SurroundExport.cpp - the multichannel WAV export of a surround master (0.5.0 F7)
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

#include "SurroundExport.h"

#include <algorithm>
#include <atomic>
#include <vector>

#include <sndfile.h>

#include "Engine.h"
#include "Mixer.h"

namespace lmms::surroundexport
{

namespace
{

//! One speaker as a WAV file names it: libsndfile's map entry and the dwChannelMask bit it stands for.
struct Speaker
{
	int map;
	std::uint32_t bit;
};

//! By surround::Role (L R C LFE Ls Rs Lrs Rrs). LCR, quad and 5.1 put Ls/Rs on the back pair, as
//! Microsoft's KSAUDIO_SPEAKER_QUAD / _5POINT1 do (mask 0x33 / 0x3F); 7.1 has both pairs, Lrs/Rrs at
//! the back and Ls/Rs at the sides (KSAUDIO_SPEAKER_7POINT1_SURROUND, mask 0x63F).
constexpr std::array<Speaker, surround::MaxChannels> BackSurrounds{{
	{SF_CHANNEL_MAP_LEFT, 0x1}, {SF_CHANNEL_MAP_RIGHT, 0x2}, {SF_CHANNEL_MAP_CENTER, 0x4}, {SF_CHANNEL_MAP_LFE, 0x8},
	{SF_CHANNEL_MAP_REAR_LEFT, 0x10}, {SF_CHANNEL_MAP_REAR_RIGHT, 0x20},
	{SF_CHANNEL_MAP_REAR_LEFT, 0x10}, {SF_CHANNEL_MAP_REAR_RIGHT, 0x20},
}};
constexpr std::array<Speaker, surround::MaxChannels> SideSurrounds{{
	{SF_CHANNEL_MAP_LEFT, 0x1}, {SF_CHANNEL_MAP_RIGHT, 0x2}, {SF_CHANNEL_MAP_CENTER, 0x4}, {SF_CHANNEL_MAP_LFE, 0x8},
	{SF_CHANNEL_MAP_SIDE_LEFT, 0x200}, {SF_CHANNEL_MAP_SIDE_RIGHT, 0x400},
	{SF_CHANNEL_MAP_REAR_LEFT, 0x10}, {SF_CHANNEL_MAP_REAR_RIGHT, 0x20},
}};

Speaker speakerAt(surround::Format format, int channel)
{
	if (format == surround::Format::Mono) { return {SF_CHANNEL_MAP_MONO, 0x4}; }  // SPEAKER_FRONT_CENTER
	if (surround::isDiscrete(format)) { return {SF_CHANNEL_MAP_INVALID, 0}; }  // no speaker positions
	const auto& table = format == surround::Format::Surround71 ? SideSurrounds : BackSurrounds;
	return table[static_cast<std::size_t>(surround::roleOf(format, channel))];
}

//! The tap: two sides (the period being mixed, the one the renderer reads) of SideChannels planes each.
struct Tap
{
	std::vector<float> data;
	f_cnt_t frames = 0;
	int write = 0;
	std::atomic<bool> armed{false};

	float* plane(int side, int channel)
	{
		const auto index = static_cast<std::size_t>(side * surround::SideChannels + channel - 2);
		return data.data() + index * static_cast<std::size_t>(frames);
	}
};

Tap& tap()
{
	static Tap instance;
	return instance;
}

} // namespace

QString channelModeName(ChannelMode mode)
{
	return mode == ChannelMode::Surround ? QStringLiteral("surround") : QStringLiteral("stereo");
}

bool channelModeFromName(const QString& name, ChannelMode* mode)
{
	if (name == QLatin1String("stereo")) { *mode = ChannelMode::Stereo; return true; }
	if (name == QLatin1String("surround")) { *mode = ChannelMode::Surround; return true; }
	return false;
}

QStringList channelModeNames()
{
	return {QStringLiteral("stereo"), QStringLiteral("surround")};
}

surround::Format masterFormat()
{
	Mixer* mixer = Engine::mixer();
	return mixer != nullptr ? mixer->mixerChannel(0)->m_surround.format() : surround::Format::Stereo;
}

int channelsFor(const OutputSettings& settings)
{
	return settings.channelMode() == ChannelMode::Surround ? surround::channelCount(masterFormat())
		: static_cast<int>(DEFAULT_CHANNELS);
}

QString refusal(const OutputSettings& settings, bool wave)
{
	if (settings.channelMode() == ChannelMode::Stereo) { return QString(); }
	if (masterFormat() == surround::Format::Stereo)
	{
		return QStringLiteral("a surround export needs a surround master bus, and the master is stereo: set the "
			"master's format (5.1, 7.1, quad or LCR) or export stereo");
	}
	if (!wave)
	{
		return QStringLiteral("only WAV carries a surround export (WAVE_FORMAT_EXTENSIBLE); FLAC, OGG and MP3 "
			"export stereo, with the BS.775 downmix");
	}
	if (settings.loudnessReport())
	{
		return QStringLiteral("the loudness report measures stereo, so a surround export cannot carry one: turn "
			"the report off or export stereo");
	}
	return QString();
}

surround::Format formatForChannels(int channels, surround::Format preferred)
{
	if (channels == static_cast<int>(DEFAULT_CHANNELS)) { return surround::Format::Stereo; }  // a plain stereo file
	// The selected master keeps its identity even when a named layout has the same channel count:
	// discrete-6 must stay discrete-6 (mask 0), not become 5.1.
	if (preferred != surround::Format::Stereo && surround::channelCount(preferred) == channels) { return preferred; }
	for (int f = 1; f < surround::FormatCount; ++f)
	{
		const auto format = static_cast<surround::Format>(f);
		if (surround::channelCount(format) == channels) { return format; }
	}
	return surround::Format::Stereo;
}

std::array<int, surround::MaxChannels> sndfileChannelMap(surround::Format format)
{
	std::array<int, surround::MaxChannels> map{};
	for (int c = 0; c < surround::channelCount(format); ++c) { map[static_cast<std::size_t>(c)] = speakerAt(format, c).map; }
	return map;
}

std::uint32_t channelMask(surround::Format format)
{
	std::uint32_t mask = 0;
	for (int c = 0; c < surround::channelCount(format); ++c) { mask |= speakerAt(format, c).bit; }
	return mask;
}

void arm(f_cnt_t frames)
{
	Tap& t = tap();
	t.frames = frames;
	t.write = 0;
	t.data.assign(static_cast<std::size_t>(2 * surround::SideChannels) * static_cast<std::size_t>(frames), 0.f);
	t.armed.store(true, std::memory_order_release);
}

void disarm()
{
	Tap& t = tap();
	t.armed.store(false, std::memory_order_release);
	std::vector<float>().swap(t.data);
	t.frames = 0;
}

bool armed() noexcept
{
	return tap().armed.load(std::memory_order_acquire);
}

void beginPeriod() noexcept
{
	Tap& t = tap();
	t.write = 1 - t.write;
	std::fill_n(t.plane(t.write, 2), static_cast<std::size_t>(surround::SideChannels) * t.frames, 0.f);
}

float* writePlane(int channel) noexcept
{
	return tap().plane(tap().write, channel);
}

const float* readPlane(int channel) noexcept
{
	return tap().plane(1 - tap().write, channel);
}

f_cnt_t frames() noexcept
{
	return tap().frames;
}

} // namespace lmms::surroundexport
