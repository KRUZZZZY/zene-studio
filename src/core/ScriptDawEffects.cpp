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

#include <QString>

#include <algorithm>
#include <vector>

namespace lmms
{

namespace
{

//! The effect the wrapper addresses, but only while it is STILL in its chain:
//! a script may remove an effect and then read the wrapper it kept, and the
//! pointer the wrapper holds would otherwise dangle.
Effect* resolveLiveEffect(EffectChain* chain, Effect* effect)
{
	if (chain == nullptr || effect == nullptr)
	{
		return nullptr;
	}
	const std::vector<Effect*>& effects = chain->effects();
	if (std::find(effects.begin(), effects.end(), effect) == effects.end())
	{
		return nullptr;
	}
	return effect;
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

int LuaEffectChain::effectCount() const
{
	return m_chain == nullptr ? 0 : static_cast<int>(m_chain->effects().size());
}

LuaEffect& LuaEffectChain::effect(int index) const
{
	ScriptDawBindings::syncForRead();
	if (m_chain == nullptr || index < 0 || index >= effectCount())
	{
		return ScriptDawBindings::newEffect(m_chain, nullptr);
	}
	return ScriptDawBindings::newEffect(m_chain, m_chain->effects()[index]);
}

LuaEffect& LuaEffectChain::effectById(const QString& id) const
{
	return effect(control::idToIndex(id, QStringLiteral("fx-")));
}

// ---------------------------------------------------------------------------
// LuaEffect
// ---------------------------------------------------------------------------

Effect* LuaEffect::m_effective() const
{
	if (m_effect == nullptr) { return nullptr; }
	// Chain-less construction (a bare `Effect()` from Lua) still resolves:
	// only a wrapper that HAS a chain checks that the effect is still in it.
	if (m_chain == nullptr) { return m_effect; }
	return resolveLiveEffect(m_chain, m_effect);
}

int LuaEffect::index() const
{
	Effect* effect = m_effective();
	if (effect == nullptr || m_chain == nullptr) { return -1; }
	const std::vector<Effect*>& effects = m_chain->effects();
	for (std::size_t i = 0; i < effects.size(); ++i)
	{
		if (effects[i] == effect) { return static_cast<int>(i); }
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

AutomatableModel* LuaEffect::parameter(int index) const
{
	const QList<AutomatableModel*> models = effectParameters(m_effective());
	if (index < 0 || index >= models.size()) { return nullptr; }
	return models.at(index);
}

} // namespace lmms
