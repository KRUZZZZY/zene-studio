/*
 * AuEffect.cpp - an Audio Unit (AUv2) effect in a device chain (Q050-491, macOS only)
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
 */

#include "AuEffect.h"

#include <algorithm>
#include <exception>

#include <QDebug>

#include "AuEffectControls.h"
#include "AuSubPluginFeatures.h"
#include "AudioEngine.h"
#include "EffectChain.h"
#include "Engine.h"
#include "HostedTrackChannels.h"
#include "LmmsCommonMacros.h"
#include "PluginInstantiationFailure.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT aueffect_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Audio Unit",
	QT_TRANSLATE_NOOP("PluginBrowser", "Audio Unit (AUv2) effect host"),
	"Zene Studio contributors",
	0x0100,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	new AuSubPluginFeatures(Plugin::Type::Effect)
};

// A component that does not instantiate answers nullptr with its typed reason noted, so plugin.load
// refuses it by name and a saved project keeps it as a placeholder that writes its state back.
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	try
	{
		auto effect = std::make_unique<AuEffect>(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
		if (effect->unit()->isLoaded()) { return effect.release(); }
		pluginload::noteInstantiationFailure(effect->unavailableReason());
		return nullptr;
	}
	catch (const std::exception& error)
	{
		pluginload::noteInstantiationFailure(QStringLiteral("the Audio Unit effect could not be created: %1")
			.arg(QString::fromUtf8(error.what())));
		return nullptr;
	}
}

}

AuEffect::AuEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPluginExt<Effect, AuEffectAudioPortsSettings>(&aueffect_plugin_descriptor, parent, key)
{
	au::ComponentId id;
	const QString component = key != nullptr ? key->attributes.value(QStringLiteral("component")) : QString();
	if (!au::parseComponentKey(component, &id))
	{
		m_loadError = au::instantiationError(id, component, QStringLiteral("its key names no component"), 0);
	}
	else if (!m_unit.load(id, &m_loadError)) { qWarning() << "AU:" << m_loadError; }
	else
	{
		audioPorts().setAllChannelCounts(hostedTrackChannels(2, 2), 2, 2);
		if (AudioEngine* engine = Engine::audioEngine())
		{
			QString error;
			if (!m_unit.prepare(engine->outputSampleRate(), engine->framesPerPeriod(), 2, &error))
			{
				qWarning() << "AU:" << error;
			}
			connect(engine, &AudioEngine::sampleRateChanged, this, [this] { reprepare(); });
		}
		cacheLatency();
	}
	// The controls exist for a failed load too: the chain and the project writer expect them.
	m_controls = std::make_unique<AuEffectControls>(this);
}

AuEffect::~AuEffect()
{
	m_editor.close();
}

auto AuEffect::controls() -> EffectControls* { return m_controls.get(); }

bool AuEffect::openNativeEditor(QString* error)
{
	return m_editor.open(gui::PluginNativeEditor::Hooks{
		[this](void* parent, QString* why) { return m_unit.openEditor(parent, why); },
		[this] { m_unit.closeEditor(); },
		[this](int* width, int* height) { m_unit.editorSize(width, height); }},
		displayName(), error, gui::PluginNativeEditor::Owner{this, nullptr});
}

void AuEffect::closeNativeEditor() { m_editor.close(); }

bool AuEffect::nativeEditorOpen() const { return m_editor.isOpen(); }

void AuEffect::reprepare()
{
	AudioEngine* engine = Engine::audioEngine();
	if (engine == nullptr || !m_unit.isLoaded()) { return; }
	// The audio thread is held off while the unit is uninitialized and its buffers rebuilt.
	engine->requestChangeInModel();
	QString error;
	if (!m_unit.prepare(engine->outputSampleRate(), engine->framesPerPeriod(), 2, &error)) { qWarning() << "AU:" << error; }
	engine->doneChangeInModel();
	m_needsReprepare.store(false, std::memory_order_relaxed);
	cacheLatency();
}

void AuEffect::cacheLatency()
{
	m_latencyFrames.store(m_unit.isPrepared() ? m_unit.latencyFrames() : 0, std::memory_order_relaxed);
	m_tailFrames.store(m_unit.isPrepared() ? m_unit.tailFrames() : 0, std::memory_order_relaxed);
	if (EffectChain* chain = effectChain()) { chain->refreshLatency(); }
}

auto AuEffect::processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
	PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus
{
	const auto frames = static_cast<int>(std::min(in.frames(), out.frames()));
	const auto dryChannels = std::min(in.channels(), out.channels());
	if (!m_unit.isPrepared() || !m_unit.process(in.data(), out.data(), static_cast<int>(in.channels()),
			static_cast<int>(out.channels()), frames))
	{
		// Not prepared (the GUI thread re-prepares) or the unit failed this block: the dry signal, never stale output.
		if (!m_unit.isPrepared()) { m_needsReprepare.store(true, std::memory_order_relaxed); }
		for (ch_cnt_t c = 0; c < out.channels(); ++c)
		{
			std::fill_n(out.bufferPtr(c), out.frames(), 0.f);
			if (c < dryChannels) { std::copy_n(in.bufferPtr(c), frames, out.bufferPtr(c)); }
		}
		return lmms::ProcessStatus::Continue;
	}
	// AudioPlugin's contract: processImpl does the wet/dry mix.
	const auto wet = wetLevel();
	const auto dry = dryLevel();
	for (ch_cnt_t c = 0; c < dryChannels; ++c)
	{
		float* wetChannel = out.bufferPtr(c);
		const float* dryChannel = in.bufferPtr(c);
		for (int f = 0; f < frames; ++f) { wetChannel[f] = dry * dryChannel[f] + wet * wetChannel[f]; }
	}
	return lmms::ProcessStatus::Continue;
}

} // namespace lmms
