/*
 * AuEffect.h - an Audio Unit (AUv2) effect in a device chain (Q050-491, macOS only)
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

#ifndef LMMS_AU_EFFECT_H
#define LMMS_AU_EFFECT_H

#include <atomic>
#include <memory>

#include "AudioPlugin.h"
#include "AuHost.h"
#include "PluginNativeEditor.h"

namespace lmms
{

class AuEffectControls;

inline constexpr AudioPortsSettings AuEffectAudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = false,
	.inputs = DynamicChannelCount,
	.outputs = DynamicChannelCount,
	.inplace = false,
	.buffered = false};

/*! An aufx / aumf component on a track's or mixer channel's chain: the same device and chain model as
 *  every hosted format (bypass, wet/dry, latency, the Device panel, plugin.* commands). Stereo in and out.
 *  The unit's state (kAudioUnitProperty_ClassInfo) and its parameters are saved by AuEffectControls. */
class AuEffect : public AudioPluginExt<Effect, AuEffectAudioPortsSettings>
{
public:
	AuEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~AuEffect() override;
	QString unavailableReason() const override { return m_unit.isLoaded() ? QString() : m_loadError; }

	auto controls() -> EffectControls* override;
	auto unit() -> au::HostedUnit* { return &m_unit; }

	bool openNativeEditor(QString* error) override;
	void closeNativeEditor() override;
	bool nativeEditorOpen() const override;

	int latencyFrames() const override { return m_latencyFrames.load(std::memory_order_relaxed); }
	int tailFrames() const override { return m_tailFrames.load(std::memory_order_relaxed); }

	//! GUI thread: initialize the unit again (a new sample rate, or after a failed prepare).
	void reprepare();
	bool needsReprepare() const { return m_needsReprepare.load(std::memory_order_relaxed); }

protected:
	auto processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
		PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus override;

private:
	void cacheLatency();

	au::HostedUnit m_unit;
	QString m_loadError;
	gui::PluginNativeEditor m_editor;
	std::unique_ptr<AuEffectControls> m_controls;
	std::atomic<bool> m_needsReprepare{false};
	std::atomic<int> m_latencyFrames{0};
	std::atomic<int> m_tailFrames{0};
};

} // namespace lmms

#endif // LMMS_AU_EFFECT_H
