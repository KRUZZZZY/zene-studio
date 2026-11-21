/*
 * ScriptDawEffects.cpp - Lua-facing effect chain and device wrappers.
 *
 * The third file of the DAW-control binding, split from ScriptDawBindings.cpp
 * by the 500-line file cap (tests/file-length-gate.sh): the docs are in
 * include/ScriptDawBindings.h. Read-only; every write lives in
 * src/core/ScriptDawEdit.cpp.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "ScriptDawBindings.h"

#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "ScriptBindings.h"
#include "ScriptEngine.h"

#include <QString>

#include <algorithm>
#include <vector>

namespace lmms
{

namespace
{

//! The chain the wrapper names, resolved through the stable id of the channel
//! that owns it: a channel that is gone has no chain, and a chain that is gone
//! is never read (cluster 1663 - the wrapper holds no pointer to it).
EffectChain* liveChain(const QString& mixerChannelId)
{
	if (mixerChannelId.isEmpty()) { return nullptr; }
	ScriptDawBindings::syncForRead();
	return ScriptDawBindings::mixerChannelEffectChain(mixerChannelId);
}

//! The device the wrapper names, but only while it is STILL in its chain: a
//! script may remove a device and then read the wrapper it kept. The device
//! is found by its own id, not by its position: a position names a slot, and a
//! slot can hold another device after a removal.
Effect* liveEffect(EffectChain* chain, int effectId)
{
	if (chain == nullptr || effectId < 0) { return nullptr; }
	const std::vector<Effect*>& effects = chain->effects();
	for (Effect* effect : effects)
	{
		if (effect != nullptr && effect->id() == effectId) { return effect; }
	}
	return nullptr;
}

//! The parameter models of \a effect, in the engine's own order - the list
//! plugin.param_get reads, so the two surfaces cannot disagree.
QList<AutomatableModel*> effectParameters(Effect* effect)
{
	return effect == nullptr ? QList<AutomatableModel*>() : controlEffectParameters(effect);
}

} // namespace

// ---------------------------------------------------------------------------
// LuaEffectChain
// ---------------------------------------------------------------------------

EffectChain* LuaEffectChain::resolve() const
{
	if (m_mixerChannelId.isEmpty()) { return nullptr; }
	ScriptDawBindings::syncForRead();
	return ScriptDawBindings::mixerChannelEffectChain(m_mixerChannelId);
}

bool LuaEffectChain::isValid() const
{
	return resolve() != nullptr;
}

int LuaEffectChain::effectCount() const
{
	EffectChain* chain = resolve();
	return chain == nullptr ? 0 : static_cast<int>(chain->effects().size());
}

LuaEffect& LuaEffectChain::effect(int index) const
{
	EffectChain* chain = resolve();
	if (chain == nullptr || index < 0 || index >= static_cast<int>(chain->effects().size()))
	{
		return ScriptDawBindings::newEffect(m_mixerChannelId, nullptr);
	}
	return ScriptDawBindings::newEffect(m_mixerChannelId, chain->effects()[index]);
}

LuaEffect& LuaEffectChain::effectById(const QString& id) const
{
	return effect(control::idToIndex(id, QStringLiteral("fx-")));
}

// ---------------------------------------------------------------------------
// LuaEffect
// ---------------------------------------------------------------------------

EffectChain* LuaEffect::m_chain() const
{
	return liveChain(m_mixerChannelId);
}

Effect* LuaEffect::m_effective() const
{
	return liveEffect(m_chain(), m_effectId);
}

int LuaEffect::index() const
{
	EffectChain* chain = m_chain();
	if (chain == nullptr) { return -1; }
	const std::vector<Effect*>& effects = chain->effects();
	for (std::size_t i = 0; i < effects.size(); ++i)
	{
		if (effects[i] != nullptr && effects[i]->id() == m_effectId)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

QString LuaEffect::id() const
{
	const int at = index();
	return at < 0 ? QString() : control::effectId(at);
}

QString LuaEffect::pluginName() const
{
	Effect* effect = m_effective();
	return effect == nullptr ? QString() : QString::fromUtf8(effect->descriptor()->name);
}

QString LuaEffect::displayName() const
{
	Effect* effect = m_effective();
	return effect == nullptr ? QString() : QString::fromUtf8(effect->descriptor()->displayName);
}

bool LuaEffect::enabled() const
{
	// Reads see prior writes from the same script (the apply side drains).
	ScriptEngine::instance()->flushCommandsForRead();
	Effect* effect = m_effective();
	return effect != nullptr && effect->isEnabled();
}

int LuaEffect::parameterCount() const
{
	return static_cast<int>(effectParameters(m_effective()).size());
}

QString LuaEffect::parameterName(int index) const
{
	const QList<AutomatableModel*> models = effectParameters(m_effective());
	if (index < 0 || index >= models.size()) { return QString(); }
	return models.at(index)->displayName();
}

LuaFloatModel& LuaEffect::parameter(int index) const
{
	const QList<AutomatableModel*> models = effectParameters(m_effective());
	// The pooled FloatModel, like LuaInstrument::parameterModel: a raw
	// AutomatableModel* is not a class the bridge has registered, so returning
	// one raised "The class is not registered in LuaBridge" in the script.
	return ScriptBindings::newFloatModel(
		index < 0 || index >= models.size() ? nullptr : models.at(index));
}

} // namespace lmms
