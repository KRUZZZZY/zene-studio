/*
 * EffectChain.h - class for processing and effects chain
 *
 * Copyright (c) 2006-2008 Danny McRae <khjklujn/at/users.sourceforge.net>
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_EFFECT_CHAIN_H
#define LMMS_EFFECT_CHAIN_H

#include "Model.h"
#include "SerializingObject.h"
#include "AutomatableModel.h"

#include <atomic>

namespace lmms
{

class AudioBuffer;
class AudioBus;
class Effect;

namespace gui
{

class EffectRackView;

} // namespace gui


class LMMS_EXPORT EffectChain : public Model, public SerializingObject
{
	Q_OBJECT
public:
	EffectChain( Model * _parent );
	~EffectChain() override;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;

	inline QString nodeName() const override
	{
		return "fxchain";
	}

	void appendEffect( Effect * _effect );
	void removeEffect( Effect * _effect );
	void moveDown( Effect * _effect );
	void moveUp( Effect * _effect );
	bool processAudioBuffer(AudioBuffer& buffer,
		const AudioBuffer* sidechainBuffer = nullptr);
	//! Processes the effects chain on a multi-channel audio bus
	bool processAudioBuffer(AudioBus& bus,
		const AudioBuffer* sidechainBuffer = nullptr);

	//! Sidechain input for the current processing block, or nullptr when the
	//! owning mixer channel has no incoming sidechain sends (Phase D). Effects
	//! query this through Effect::sidechainBuffer().
	auto sidechainBuffer() const -> const AudioBuffer* { return m_sidechainBuffer; }

	//! Frames of latency this chain adds to the signal path: the sum over the
	//! effects that will actually process audio (enabled, okay, not bypassed),
	//! or zero when the chain itself is disabled. Cached; refreshed by
	//! refreshLatency(). Read by the mixer's PDC graph on the audio thread
	//! (#605), which is why it is an atomic load and never a virtual call.
	auto latencyFrames() const -> int
	{
		return m_latencyFrames.load(std::memory_order_relaxed);
	}

	//! Control thread: recompute the cached latency after an effect was added,
	//! removed, bypassed or reported a new latency (#605).
	void refreshLatency();

	void clear();


private:
	using EffectList = std::vector<Effect*>;
	EffectList m_effects;

	BoolModel m_enabledModel;

	//! Non-owning; set by processAudioBuffer() for the duration of the call
	const AudioBuffer* m_sidechainBuffer = nullptr;

	//! Cached chain latency for the PDC graph (#605); see latencyFrames().
	std::atomic<int> m_latencyFrames{0};

	//! One-shot guard for the "chain latency exceeds the delay-line capacity"
	//! diagnostic; only touched on the control thread.
	bool m_latencyClampWarned = false;


	friend class gui::EffectRackView;


signals:
	void aboutToClear();

} ;

} // namespace lmms

#endif // LMMS_EFFECT_CHAIN_H
