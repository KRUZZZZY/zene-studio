/*
 * Vst3Effect.h - native VST3 effect host
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

#ifndef LMMS_VST3_EFFECT_H
#define LMMS_VST3_EFFECT_H

#include <memory>

#include "AudioPlugin.h"
#include "Vst3Host.h"
#include "PluginNativeEditor.h"

namespace lmms
{

class Vst3EffectControls;

//! VST3 effects are always fed through the non-interleaved multi-channel
//! transport of AudioPorts, with a dynamic number of channels on both sides.
inline constexpr AudioPortsSettings Vst3EffectAudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = false,
	.inputs = DynamicChannelCount,
	.outputs = DynamicChannelCount,
	.inplace = false,
	.buffered = false};

class Vst3Effect : public AudioPluginExt<Effect, Vst3EffectAudioPortsSettings>
{
public:
	Vst3Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~Vst3Effect() override;

	auto controls() -> EffectControls* override;

	auto vst3Controls() -> Vst3EffectControls* { return m_controls.get(); }
	auto plugin() -> vst3::HostedPlugin* { return &m_plugin; }

	//! R4.4: the plug-in's own editor, in its own window (PluginNativeEditor + Vst3EditorSession).
	bool openNativeEditor(QString* error) override;
	void closeNativeEditor() override;
	bool nativeEditorOpen() const override;

	//! GUI thread: re-create the processing setup, e.g. after a sample rate
	//! change. Not real-time safe, never call from the audio thread.
	void reprepare();

#ifdef LMMS_TESTING
	//! Test-only pause point after admission closes, so wrapper processing can be driven during re-prepare.
	using ReprepareTestHook = void (*)(void*);
	void setReprepareTestHook(ReprepareTestHook hook, void* context) noexcept
	{
		m_reprepareTestHook = hook;
		m_reprepareTestContext = context;
	}
#endif

	//! BUGS 11.34: the latency the plug-in reported after its last successful prepare, so the
	//! effect chain's delay compensation sees it (read on the control thread).
	int latencyFrames() const override { return m_latencyFrames.load(std::memory_order_relaxed); }
	//! E6: the tail the plug-in declared after its last prepare, so silence does not cut it.
	int tailFrames() const override { return m_tailFrames.load(std::memory_order_relaxed); }

	//! Set from the audio thread when the plug-in is not prepared, polled by
	//! Vst3EffectControls on the GUI thread.
	auto needsReprepare() const -> bool
	{
		return m_needsReprepare.load(std::memory_order_relaxed);
	}

protected:
	auto processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
		PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus override;

private:
	vst3::AudioProcessGate m_processGate;
	vst3::HostedPlugin m_plugin;
	gui::PluginNativeEditor m_editor;
	std::unique_ptr<Vst3EffectControls> m_controls;
#ifdef LMMS_TESTING
	ReprepareTestHook m_reprepareTestHook = nullptr;
	void* m_reprepareTestContext = nullptr;
#endif
	//! Set from the audio thread when the plug-in is not prepared, polled by
	//! Vst3EffectControls on the GUI thread which then calls reprepare().
	std::atomic<bool> m_needsReprepare{false};
	std::atomic<int> m_latencyFrames{0};
	std::atomic<int> m_tailFrames{0};

	//! GUI thread: read the plug-in's latency and tail, and let the owning chain re-align.
	void cacheLatency();
};

} // namespace lmms

#endif // LMMS_VST3_EFFECT_H
