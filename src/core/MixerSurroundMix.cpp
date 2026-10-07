/*
 * MixerSurroundMix.cpp - surround mixer channels on the render path: panning, carrying, folding, output
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

/*! 0.5.0 F7 (MX-060): everything here runs on a mixer worker or the render thread. No allocation and no
 *  lock: the side buffers are the channels' own (allocated with them), the gain tables and the matrices
 *  are built on the control thread (MixerSurround.cpp), and the only model reads are the ones the stereo
 *  path already makes on the same thread (a send's level, a fader, a trim, a pan, the VCA gain).
 *
 *  What the side channels do NOT get, and say so: the sends' delay compensation (a surround send's side
 *  channels arrive undelayed, so a send compensated for latency elsewhere is off by that delay on them),
 *  the channel's devices and post-fader inserts, its mono/polarity/swap/width/EQ stages and its meters. */

#include <algorithm>
#include <cmath>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "HardwareIo.h"
#include "HardwareOutputBus.h"
#include "Mixer.h"
#include "MixerListen.h"
#include "MixerSendPan.h"
#include "MixerSurround.h"
#include "SampleFrame.h"
#include "Song.h"
#include "ValueBuffer.h"

namespace lmms::surround
{

namespace
{

//! A gain read per frame: a model's sample-exact buffer when it has one this period, else its value.
struct FrameGain
{
	const ValueBuffer* buffer = nullptr;
	float value = 1.f;
	float at(f_cnt_t f) const { return buffer != nullptr ? buffer->values()[f] : value; }
};

//! A send's level times the sender's fader (none for a pre-fader send).
struct SendGain
{
	FrameGain level;
	FrameGain fader;
	float at(f_cnt_t f) const { return level.at(f) * fader.at(f); }
};

FrameGain modelGain(FloatModel& model)
{
	const ValueBuffer* buffer = model.valueBuffer();
	return {buffer, model.value()};
}

//! The gain on @a route's stereo signal: the read the stereo send makes (post-fader inserts may bake it).
SendGain stereoSendGain(MixerRoute& route)
{
	MixerChannel& sender = *route.sender();
	const FrameGain fader = route.preFader() ? FrameGain{}
		: FrameGain{postinserts::faderBuffer(sender, &route), postinserts::faderValue(sender, &route)};
	return {modelGain(*route.amount()), fader};
}

//! The gain on @a route's side channels: the raw fader - they never pass the post-fader inserts.
SendGain sideSendGain(MixerRoute& route)
{
	return {modelGain(*route.amount()), route.preFader() ? FrameGain{} : modelGain(route.sender()->m_volumeModel)};
}

bool exporting()
{
	const Song* song = Engine::getSong();
	return song != nullptr && song->isExporting();
}

void addInto(SampleFrame* out, int side, const float* in, float k, const SendGain& gain, f_cnt_t frames)
{
	for (f_cnt_t f = 0; f < frames; ++f) { out[f][side] += in[f] * k * gain.at(f); }
}

void addInto(float* out, const float* in, float k, const SendGain& gain, f_cnt_t frames)
{
	for (f_cnt_t f = 0; f < frames; ++f) { out[f] += in[f] * k * gain.at(f); }
}

//! Sender side channel @a from (@a in) into every receiver channel @a m sends it to.
void mixSideChannel(MixerChannel& receiver, const Matrix& m, int from, const float* in, const SendGain& gain, f_cnt_t frames)
{
	ChannelSurround& to = receiver.m_surround;
	for (int c = 0; c < channelCount(to.format()); ++c)
	{
		const float k = m[static_cast<std::size_t>(c)][static_cast<std::size_t>(from)];
		if (k == 0.f) { continue; }
		if (c < 2)
		{
			addInto(receiver.m_buffer, c, in, k, gain, frames);
			// The pair now carries what the sender's own pair may not have: never let it be called quiet.
			receiver.m_bus.quietChannels().reset(static_cast<std::size_t>(c));
			continue;
		}
		addInto(to.plane(c), in, k, gain, frames);
		to.markWritten();
	}
}

//! @a s's side channels folded to stereo into @a out, at @a fader.
void foldInto(SampleFrame* out, const ChannelSurround& s, const FrameGain& fader, f_cnt_t frames)
{
	const Matrix& m = matrix(s.format(), Format::Stereo);
	const SendGain gain{FrameGain{}, fader};
	for (int c = 2; c < channelCount(s.format()); ++c)
	{
		const auto from = static_cast<std::size_t>(c);
		if (m[0][from] != 0.f) { addInto(out, 0, s.plane(c), m[0][from], gain, frames); }
		if (m[1][from] != 0.f) { addInto(out, 1, s.plane(c), m[1][from], gain, frames); }
	}
}

//! @a s's side channels onto the outputs after the pair at @a start, at @a fader.
void placeSides(HardwareOutputBus& bus, const ChannelSurround& s, int start, const FrameGain& fader, f_cnt_t frames)
{
	const float* gains = fader.buffer != nullptr ? fader.buffer->values() : nullptr;
	for (int c = 2; c < channelCount(s.format()); ++c) { bus.addPlane(start + c, s.plane(c), frames, fader.value, gains); }
}

//! How much a side speaker takes of the channel's pan, a balance on each left/right pair.
float balanceFor(Role role, float left, float right)
{
	switch (role)
	{
	case Role::Ls:
	case Role::Lrs: return left;
	case Role::Rs:
	case Role::Rrs: return right;
	default: return 1.f;
	}
}

//! Whether the master's side channels play on the device: not in an export, its main output a pair with
//! room after it, and not while a listen replaces the main output.
bool masterPlaysWide(const audioio::Destination& main, int count)
{
	const bool listenReplaces = mixerlisten::anyListening() && !hardwareio::listenSparesMain();
	return !exporting() && !listenReplaces && main.width == 2 && deviceCarries(main.start, count);
}

} // namespace

bool panTakesSend(const MixerRoute& route)
{
	return route.sender()->m_surround.pan().enabled && !route.sender()->m_surround.isSurround()
		&& route.receiver()->m_surround.isSurround();
}

void panSend(MixerRoute& route, const SampleFrame* src, f_cnt_t frames)
{
	MixerChannel& receiver = *route.receiver();
	ChannelSurround& to = receiver.m_surround;
	const Gains& g = route.sender()->m_surround.gains(to.format());
	const SendGain gain = stereoSendGain(route);
	const int count = channelCount(to.format());
	const f_cnt_t n = std::min(frames, to.frames());
	for (f_cnt_t f = 0; f < n; ++f)
	{
		const float s = 0.5f * (src[f][0] + src[f][1]) * gain.at(f);
		receiver.m_buffer[f][0] += s * g[0];
		receiver.m_buffer[f][1] += s * g[1];
		for (int c = 2; c < count; ++c) { to.plane(c)[f] += s * g[static_cast<std::size_t>(c)]; }
	}
	to.markWritten();
}

void addSideChannels(MixerRoute& route, f_cnt_t frames)
{
	const ChannelSurround& from = route.sender()->m_surround;
	if (!from.isSurround() || !from.written() || route.feedback()) { return; }
	MixerChannel& receiver = *route.receiver();
	const Matrix& m = matrix(from.format(), receiver.m_surround.format());
	const SendGain gain = sideSendGain(route);
	const f_cnt_t n = std::min(frames, std::min(from.frames(), receiver.m_surround.frames()));
	for (int c = 2; c < channelCount(from.format()); ++c) { mixSideChannel(receiver, m, c, from.plane(c), gain, n); }
}

void processChannel(MixerChannel& channel, f_cnt_t frames)
{
	ChannelSurround& s = channel.m_surround;
	if (!s.isSurround() || !s.written()) { return; }
	const float trim = channel.m_trimModel.value();
	const float gain = (trim == 0.f ? 1.f : std::pow(10.f, trim / 20.f)) * channel.vcaGain();
	const ValueBuffer* panBuffer = channel.m_panModel.valueBuffer();
	const float pan = channel.m_panModel.value();
	float left = 1.f;
	float right = 1.f;
	if (panBuffer == nullptr) { balanceGains(pan, &left, &right); }
	const f_cnt_t n = std::min(frames, s.frames());
	for (int c = 2; c < channelCount(s.format()); ++c)
	{
		const Role role = roleOf(s.format(), c);
		float* plane = s.plane(c);
		if (panBuffer != nullptr)
		{
			for (f_cnt_t f = 0; f < n; ++f)
			{
				balanceGains(panBuffer->values()[f], &left, &right);
				plane[f] *= gain * balanceFor(role, left, right);
			}
			continue;
		}
		const float k = gain * balanceFor(role, left, right);
		if (k != 1.f) { for (f_cnt_t f = 0; f < n; ++f) { plane[f] *= k; } }
	}
}

void masterOutput(Mixer& mixer, SampleFrame* out, f_cnt_t frames)
{
	MixerChannel& master = *mixer.mixerChannel(0);
	const ChannelSurround& s = master.m_surround;
	if (!s.isSurround() || !s.written()) { return; }
	const FrameGain fader = modelGain(master.m_volumeModel);
	const f_cnt_t n = std::min(frames, s.frames());
	const audioio::Destination main = master.m_hardwareIo.effectiveOutput(true);
	if (masterPlaysWide(main, channelCount(s.format())))
	{
		placeSides(Engine::audioEngine()->hardwareOutputs(), s, main.start, fader, n);
		return;
	}
	foldInto(out, s, fader, n);
}

void directOut(HardwareOutputBus& bus, MixerChannel& channel, int start, int width, SampleFrame* stereo, f_cnt_t frames)
{
	const ChannelSurround& s = channel.m_surround;
	if (!s.isSurround() || !s.written()) { return; }
	const FrameGain fader = modelGain(channel.m_volumeModel);
	const f_cnt_t n = std::min(frames, s.frames());
	if (width == 2 && deviceCarries(start, channelCount(s.format())))
	{
		placeSides(bus, s, start, fader, n);
		return;
	}
	foldInto(stereo, s, fader, n);
}

bool deviceCarries(int start, int count)
{
	const AudioDevice* device = Engine::audioEngine()->audioDev();
	return start >= 0 && device != nullptr && start + count <= static_cast<int>(device->outputChannels())
		&& start + count <= HardwareOutputBus::MaxChannels;
}

} // namespace lmms::surround
