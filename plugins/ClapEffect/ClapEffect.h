/*
 * ClapEffect.h - native CLAP effect host
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

#ifndef LMMS_CLAP_EFFECT_H
#define LMMS_CLAP_EFFECT_H

#include <memory>

#include "AudioPlugin.h"
#include "ClapHost.h"

namespace lmms
{

class ClapEffectControls;

//! CLAP effects are always fed through the non-interleaved multi-channel
//! transport of AudioPorts, with a dynamic number of channels on both sides.
inline constexpr AudioPortsSettings ClapEffectAudioPortsSettings{
	.kind = AudioDataKind::F32,
	.interleaved = false,
	.inputs = DynamicChannelCount,
	.outputs = DynamicChannelCount,
	.inplace = false,
	.buffered = false};

class ClapEffect : public AudioPluginExt<Effect, ClapEffectAudioPortsSettings>
{
public:
	ClapEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~ClapEffect() override;

	auto controls() -> EffectControls* override;

	auto clapControls() -> ClapEffectControls* { return m_controls.get(); }
	auto plugin() -> clap::HostedPlugin* { return &m_plugin; }

	//! GUI thread: re-create the processing setup, e.g. after a sample rate
	//! change or a restart request from the plug-in.
	void reprepare();

	//! Set from the audio thread when the plug-in is not prepared, polled by
	//! ClapEffectControls on the GUI thread.
	auto needsReprepare() const -> bool
	{
		return m_needsReprepare.load(std::memory_order_relaxed);
	}

protected:
	auto processImpl(PlanarBufferView<const float, DynamicChannelCount> in,
		PlanarBufferView<float, DynamicChannelCount> out) -> lmms::ProcessStatus override;

private:
	clap::HostedPlugin m_plugin;
	std::unique_ptr<ClapEffectControls> m_controls;
	//! Set from the audio thread when the plug-in is not prepared, polled by
	//! ClapEffectControls on the GUI thread which then calls reprepare().
	std::atomic<bool> m_needsReprepare{false};
};

} // namespace lmms

#endif // LMMS_CLAP_EFFECT_H
