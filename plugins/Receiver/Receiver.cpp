/*
 * Receiver.cpp - a device that mixes another channel's audio in at its slot (0.5.0 MX-063)
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
#include "Receiver.h"

#include "AudioBuffer.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT receiver_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Receiver",
	QT_TRANSLATE_NOOP("PluginBrowser", "Mixes another channel's audio (sent with chain.receive or a sidechain send) in at its slot"),
	"Zene Studio contributors",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("zene-plugin-logo"),
	nullptr,
	nullptr,
};

}

ReceiverEffect::ReceiverEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&receiver_plugin_descriptor, parent, key),
	m_controls(this)
{
}

ProcessStatus ReceiverEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	const AudioBuffer* input = sidechainBuffer();
	const float level = m_controls.m_levelModel.value();
	const bool replace = m_controls.m_replaceModel.value();
	const float* in0 = input != nullptr ? input->buffer(0).data() : nullptr;
	const float* in1 = input != nullptr ? input->buffer(1).data() : nullptr;
	const float d = dryLevel();
	const float w = wetLevel();
	for (f_cnt_t f = 0; f < inOut.frames(); ++f)
	{
		auto& frame = inOut.sampleFrameAt(f);
		const float left = mix(frame[0], in0 != nullptr ? in0[f] : 0.f, level, replace);
		const float right = mix(frame[1], in1 != nullptr ? in1[f] : 0.f, level, replace);
		frame[0] = frame[0] * d + left * w;
		frame[1] = frame[1] * d + right * w;
	}
	// The received audio arrives whether or not the chain's own signal is quiet: never sleep on it.
	return ProcessStatus::Continue;
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new ReceiverEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
