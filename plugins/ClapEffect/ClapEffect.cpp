/*
 * ClapEffect.cpp - native CLAP effect host
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

#include "ClapEffect.h"

#include "EffectChain.h"
#include "SidechainInputs.h"

#include <algorithm>
#include <exception>
#include <memory>

#include <QDebug>

#include "ClapEffectControls.h"
#include "ClapSubPluginFeatures.h"
#include "Engine.h"
#include "HostedTrackChannels.h"
#include "LmmsCommonMacros.h"
#include "PluginInstantiationFailure.h"
#include "Song.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT clapeffect_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"CLAP",
	QT_TRANSLATE_NOOP("PluginBrowser", "native CLAP effect host"),
	"LMMS contributors",
	0x0100,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	new ClapSubPluginFeatures(Plugin::Type::Effect)
};

// The module entry point PluginFactory requires of every plug-in library (src/core/PluginFactory.cpp,
// loadPluginDescriptor). Without it the factory recorded this module as "not a plug-in", so no CLAP
// effect could be added to a chain - only tests that construct the class directly ever ran one. A class
// that did not load answers nullptr, so the chain keeps a saved one as its placeholder (DummyEffect, which
// writes the saved state back) instead of a pass-through that would drop that state on the next save.
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	try
	{
		auto effect = std::make_unique<ClapEffect>(parent,
			static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
		return effect->plugin()->isLoaded() ? effect.release() : nullptr;
	}
	catch (const std::exception& error)
	{
		// A throw must not cross the extern "C" boundary.
		qCritical() << "CLAP effect could not be created:" << error.what();
		pluginload::noteInstantiationFailure(QStringLiteral("the CLAP effect could not be created: %1")
			.arg(QString::fromUtf8(error.what())));
		return nullptr;
	}
}

}

ClapEffect::ClapEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key)
	: AudioPluginExt<Effect, ClapEffectAudioPortsSettings>(
			&clapeffect_plugin_descriptor, parent, key)
{
	const auto modulePath = key != nullptr ? key->attributes.value("file") : QString{};
	const auto pluginId = key != nullptr ? key->attributes.value("id") : QString{};

	QString error;
	if (!m_plugin.load(modulePath, pluginId, &error))
	{
		qWarning() << "CLAP: could not load" << modulePath << pluginId << ":" << error;
		pluginload::noteInstantiationFailure(error);
	}
	else
	{
		applyLayout();

		if (auto* engine = Engine::audioEngine())
		{
			m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(), &error);
		}
		cacheLatency();
	}

	// The controls must exist even for a failed load: the effect chain and the
	// .mmp serialization code expect a valid EffectControls instance.
	m_controls = std::make_unique<ClapEffectControls>(this);
}


bool ClapEffect::openNativeEditor(QString* error)
{
	m_plugin.setEditorScale(deviceOptions().editorScale);  // 0.5.0 L10 (MX-094)
	return m_editor.open(gui::PluginNativeEditor::Hooks{
		[this](void* parent, QString* why) { return m_plugin.openEditor(parent, why); },
		[this] { m_plugin.closeEditor(); },
		[this](int* width, int* height) { m_plugin.editorSize(width, height); }}, displayName(), error, gui::PluginNativeEditor::Owner{this, nullptr});
}

void ClapEffect::closeNativeEditor()
{
	m_editor.close();
}

bool ClapEffect::nativeEditorOpen() const
{
	return m_editor.isOpen();
}

ClapEffect::~ClapEffect() = default;

auto ClapEffect::controls() -> EffectControls* { return m_controls.get(); }

void ClapEffect::applyLayout()
{
	const auto& layout = m_plugin.busLayout();
	// The transport is dynamic, but starting with the plug-in's own channel
	// counts means the common case never needs a re-configuration.
	const auto mainInputs = std::max<ch_cnt_t>(1, static_cast<ch_cnt_t>(layout.inputs));
	const auto mainOutputs = std::max<ch_cnt_t>(1, static_cast<ch_cnt_t>(layout.outputs));
	audioPorts().setAllChannelCounts(hostedTrackChannels(mainInputs, mainOutputs), mainInputs, mainOutputs);
	m_appliedLayout = m_plugin.layoutRevision();
}

void ClapEffect::reprepare()
{
	if (auto* engine = Engine::audioEngine())
	{
		// The plug-in may be active and the chain running (a rescan or restart request from the
		// plug-in itself): the audio thread is held off while its buffers are rebuilt (review
		// L01-1 #3).
		engine->requestChangeInModel();
		QString error;
		if (!m_plugin.prepare(engine->outputSampleRate(), engine->framesPerPeriod(), &error))
		{
			qWarning() << "CLAP: re-prepare failed:" << error;
		}
		if (m_plugin.layoutRevision() != m_appliedLayout) { applyLayout(); }
		engine->doneChangeInModel();
	}
	m_needsReprepare.store(false, std::memory_order_relaxed);
	cacheLatency();
}

void ClapEffect::cacheLatency()
{
	// clap_plugin_latency reports a uint32_t: past INT32_MAX it would turn negative as an int.
	const auto latency = std::min<std::uint32_t>(m_plugin.latency(), static_cast<std::uint32_t>(INT32_MAX));
	m_latencyFrames.store(m_plugin.isPrepared() ? static_cast<int>(latency) : 0, std::memory_order_relaxed);
	m_tailFrames.store(m_plugin.isPrepared() ? m_plugin.tail() : 0, std::memory_order_relaxed);
	if (EffectChain* chain = effectChain()) { chain->refreshLatency(); }
}

namespace
{

//! The channels the wet/dry mix covers: every channel of the main input port (a side-chain port's channels
//! follow it, from sideChainChannel, and are not dry signal for the outputs), never fewer than the stereo pair
//! it always covered (round 6: channels past the second stayed fully wet).
auto dryChannels(ch_cnt_t inputs, ch_cnt_t outputs, int sideChainChannel) -> ch_cnt_t
{
	const auto main = sideChainChannel < 0 ? inputs : static_cast<ch_cnt_t>(std::max(2, sideChainChannel));
	return std::min({inputs, outputs, main});
}

//! A plug-in that cannot run hands the chain its dry signal and silence past it, not whatever the output
//! buffer last held (round 6: a failed or unprepared plug-in replayed its last block, or muted the channel).
void passThrough(PlanarBufferView<const float, DynamicChannelCount> in,
	PlanarBufferView<float, DynamicChannelCount> out, int sideChainChannel)
{
	const auto dry = dryChannels(in.channels(), out.channels(), sideChainChannel);
	const auto frames = std::min(in.frames(), out.frames());
	for (ch_cnt_t c = 0; c < out.channels(); ++c)
	{
		std::fill_n(out.bufferPtr(c), out.frames(), 0.f);
		if (c < dry) { std::copy_n(in.bufferPtr(c), frames, out.bufferPtr(c)); }
	}
}

} // namespace

auto ClapEffect::processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
	PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus
{
	if (!m_plugin.isPrepared())
	{
		// Either the plug-in failed to load or the sample rate changed. Do the
		// actual re-configuration on the GUI thread, this path only records it.
		m_needsReprepare.store(true, std::memory_order_relaxed);
		passThrough(in, out, m_plugin.busLayout().sideChainChannel);
		return lmms::ProcessStatus::Continue;
	}

	if (auto* song = Engine::getSong())
	{
		m_plugin.setTempo(song->getTempo());
		m_plugin.setTransportPlaying(song->isPlaying());
	}

	const auto frames = static_cast<int>(std::min(in.frames(), out.frames()));
	// E9: the plug-in's sidechain bus hears the mixer's sidechain send, when this channel has one.
	std::array<const float*, MaxHostedInputChannels> inputs{};
	const float* const* fed = sidechainInputs(in.data(), static_cast<int>(in.channels()), sidechainBuffer(),
		m_plugin.busLayout().sideChainChannel, inputs);
	if (!m_plugin.process(fed, out.data(),
		    static_cast<int>(in.channels()), static_cast<int>(out.channels()), frames))
	{
		// The plug-in reported a processing error: the rest of the output block is whatever the
		// buffer already held, so the chain gets the dry signal for the whole block instead.
		passThrough(in, out, m_plugin.busLayout().sideChainChannel);
		return lmms::ProcessStatus::Continue;
	}

	// AudioPlugin's contract: processImpl is responsible for the wet/dry mix.
	// The plug-in wrote wet output into `out`, `in` still holds the dry signal.
	const auto wet = wetLevel();
	const auto dry = dryLevel();
	const auto mixChannels = dryChannels(in.channels(), out.channels(), m_plugin.busLayout().sideChainChannel);
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
