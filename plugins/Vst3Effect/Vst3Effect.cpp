/*
 * Vst3Effect.cpp - native VST3 effect host
 *
 * Copyright (c) 2026 LMMS contributors
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

#include "Vst3Effect.h"

#include <algorithm>

#include <QDebug>

#include "Engine.h"
#include "LmmsCommonMacros.h"
#include "Song.h"
#include "Vst3EffectControls.h"
#include "Vst3SubPluginFeatures.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vst3effect_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST3",
	QT_TRANSLATE_NOOP("PluginBrowser", "native VST3 effect host"),
	"LMMS contributors",
	0x0100,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	new Vst3SubPluginFeatures(Plugin::Type::Effect)
};

}

Vst3Effect::Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key)
	: AudioPluginExt<Effect, Vst3EffectAudioPortsSettings>(
			&vst3effect_plugin_descriptor, parent, key)
{
	const auto modulePath = key != nullptr ? key->attributes.value("file") : QString{};
	const auto classId = key != nullptr ? key->attributes.value("class") : QString{};

	QString error;
	if (!m_plugin.load(modulePath, classId, &error))
	{
		qWarning() << "VST3: could not load" << modulePath << classId << ":" << error;
	}
	else
	{
		const auto& layout = m_plugin.busLayout();
		// The transport is dynamic, but starting with the plug-in's own channel
		// counts means the common case never needs a re-configuration.
		const auto mainInputs = std::max<ch_cnt_t>(1, static_cast<ch_cnt_t>(layout.inputs));
		const auto mainOutputs = std::max<ch_cnt_t>(1, static_cast<ch_cnt_t>(layout.outputs));
		audioPorts().setAllChannelCounts(
			std::max<ch_cnt_t>(ch_cnt_t{2}, std::max(mainInputs, mainOutputs)),
			mainInputs, mainOutputs);

		if (auto* engine = Engine::audioEngine())
		{
			m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(), &error);
		}
	}

	// The controls must exist even for a failed load: the effect chain and the
	// .mmp serialization code expect a valid EffectControls instance.
	m_controls = std::make_unique<Vst3EffectControls>(this);
}

Vst3Effect::~Vst3Effect() = default;

auto Vst3Effect::controls() -> EffectControls* { return m_controls.get(); }

void Vst3Effect::reprepare()
{
	if (auto* engine = Engine::audioEngine())
	{
		QString error;
		if (!m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(), &error))
		{
			qWarning() << "VST3: re-prepare failed:" << error;
		}
	}
	m_needsReprepare.store(false, std::memory_order_relaxed);
}

auto Vst3Effect::processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
	PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus
{
	if (!m_plugin.isPrepared())
	{
		// Either the plug-in failed to load or the sample rate changed. Do the
		// actual re-configuration on the GUI thread, this path only records it.
		m_needsReprepare.store(true, std::memory_order_relaxed);
		return lmms::ProcessStatus::Continue;
	}

	if (auto* song = Engine::getSong())
	{
		m_plugin.setTempo(song->getTempo());
		m_plugin.setTransportPlaying(song->isPlaying());
	}

	const auto frames = static_cast<int>(std::min(in.frames(), out.frames()));
	m_plugin.process(in.data(), out.data(),
		static_cast<int>(in.channels()), static_cast<int>(out.channels()), frames);

	// AudioPlugin's contract: processImpl is responsible for the wet/dry mix.
	// The plug-in wrote wet output into `out`, `in` still holds the dry signal.
	const auto wet = wetLevel();
	const auto dry = dryLevel();
	const auto mixChannels = std::min({in.channels(), out.channels(), ch_cnt_t{2}});
	for (ch_cnt_t c = 0; c < mixChannels; ++c)
	{
		auto* outChannel = out.bufferPtr(c);
		const auto* inChannel = in.bufferPtr(c);
		for (int f = 0; f < frames; ++f)
		{
			outChannel[f] = dry * inChannel[f] + wet * outChannel[f];
		}
	}

	return lmms::ProcessStatus::Continue;
}

} // namespace lmms
