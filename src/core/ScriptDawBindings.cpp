/*
 * ScriptDawBindings.cpp - the Lua API's DAW-control half (SPEC A11-A16).
 *
 * The v0 binding reached Note / PatternClip / Track / InstrumentTrack /
 * Instrument / Transport / Song: a pattern-editing API. It reached NO mixer
 * channel, effect chain, plugin, send, PDC, automation clip, controller or
 * settings object (0.3.0 feature row 50, measured 66%), so a script could
 * write notes and could not drive the DAW it wrote them into.
 *
 * This file is the read half: the mixer / channel / effect-chain / effect
 * wrappers and their registration. The write half (queued ops + the apply-side
 * handlers) is ScriptDawEdit.cpp. Both address the SAME objects the control
 * surface's mixer.* / chain.* / plugin.* groups address, through the same
 * stable ids (ch-<n>, fx-<n>, dev-<n>), so the two surfaces cannot disagree
 * about what a channel or an effect is.
 *
 * Threading: a script runs on the worker thread and never mutates engine state
 * there. Reads flush the script's own pending commands first (the apply side
 * owns the object graph), which is the pattern ScriptBindings.cpp's
 * LuaFloatModel::value() already uses.
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
#include "Engine.h"
#include "Mixer.h"
#include "ScriptApiVersion.h"
#include "ScriptEngine.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "LuaBridge/LuaBridge.h"

#include <QString>
#include <QStringList>

#include <algorithm>
#include <deque>

namespace lmms
{

namespace
{

// ---------------------------------------------------------------------------
// View pools (same contract as ScriptBindings.cpp's: LuaBridge userdata holds
// non-owning references, so a wrapper lives in a deque - push_back never
// invalidates an existing element - and the pool is recycled only once the
// previous lua_State has been closed).
// ---------------------------------------------------------------------------

template<class T>
class ViewPool
{
public:
	T& add(const T& value)
	{
		m_items.push_back(value);
		return m_items.back();
	}

	void clear() { m_items.clear(); }

private:
	std::deque<T> m_items;
};

ViewPool<LuaMixerChannel>& mixerChannelPool()
{
	static ViewPool<LuaMixerChannel> pool;
	return pool;
}

ViewPool<LuaEffectChain>& effectChainPool()
{
	static ViewPool<LuaEffectChain> pool;
	return pool;
}

ViewPool<LuaEffect>& effectPool()
{
	static ViewPool<LuaEffect> pool;
	return pool;
}

Mixer* liveMixer()
{
	return Engine::mixer();
}


// ---------------------------------------------------------------------------
// zene.apiSurface(): the version facts and the LIVE namespace surface.
//
// The function list is read out of the running lua_State, not from a table
// written by hand: a second list of names is a list that can be wrong. The
// class-member half of the surface is enumerated from the registration sources
// by tools/lua-api-surface.py, whose output docs/lua-api-surface.txt is the
// committed ratchet (tests/CMakeLists.txt: LuaApiSurface).
// ---------------------------------------------------------------------------

void pushVersionFacts(lua_State* L)
{
	lua_pushstring(L, ScriptApi::version().toUtf8().constData());
	lua_setfield(L, -2, "version");
	lua_pushstring(L, ScriptApi::fullVersion().toUtf8().constData());
	lua_setfield(L, -2, "full_version");
	lua_pushinteger(L, ScriptApi::major());
	lua_setfield(L, -2, "major");
	lua_pushinteger(L, ScriptApi::minor());
	lua_setfield(L, -2, "minor");
	lua_pushstring(L, ScriptApi::stability().toUtf8().constData());
	lua_setfield(L, -2, "stability");
}

//! Sorted names of every field of the `zene` table - the namespace surface.
QStringList namespaceFunctionNames(lua_State* L)
{
	QStringList names;
	lua_getglobal(L, "zene");
	if (!lua_istable(L, -1))
	{
		lua_pop(L, 1);
		return names;
	}
	lua_pushnil(L);
	while (lua_next(L, -2) != 0)
	{
		// Key at -2, value at -1. A non-string key is not part of the surface.
		if (lua_type(L, -2) == LUA_TSTRING)
		{
			names.append(QString::fromUtf8(lua_tostring(L, -2)));
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	names.sort();
	return names;
}

void pushFunctionTable(lua_State* L, const QStringList& names)
{
	lua_newtable(L);
	int index = 1;
	for (const QString& name : names)
	{
		lua_pushstring(L, name.toUtf8().constData());
		lua_rawseti(L, -2, index);
		++index;
	}
	lua_pushinteger(L, names.size());
	lua_setfield(L, -2, "function_count");
}

//! `zene.apiSurface()` -> { version, full_version, major, minor, stability,
//! functions = {...}, function_count = n, has_mixer_binding = 0/1 }
int luaApiSurface(lua_State* L)
{
	// Read the namespace BEFORE building the result: the walk needs the stack
	// free of half-built tables.
	const QStringList names = namespaceFunctionNames(L);
	lua_newtable(L);                                            // surface
	pushVersionFacts(L);                                        // .version, ...
	pushFunctionTable(L, names);                                // .functions
	lua_setfield(L, -2, "functions");
	lua_pushinteger(L, names.contains(QStringLiteral("mixer")) ? 1 : 0);
	lua_setfield(L, -2, "has_mixer_binding");                   // .has_mixer_binding
	return 1;
}

} // namespace

namespace ScriptDawBindings
{

void beginRun()
{
	mixerChannelPool().clear();
	effectChainPool().clear();
	effectPool().clear();
}

void syncForRead()
{
	ScriptEngine::instance()->flushCommandsForRead();
}

LuaMixerChannel& newMixerChannel(MixerChannel* channel)
{
	return mixerChannelPool().add(LuaMixerChannel(channel));
}

LuaEffectChain& newEffectChain(EffectChain* chain)
{
	return effectChainPool().add(LuaEffectChain(chain));
}

LuaEffect& newEffect(EffectChain* chain, Effect* effect)
{
	return effectPool().add(LuaEffect(chain, effect));
}

LuaMixer& mixerView()
{
	static LuaMixer view;
	return view;
}

void registerAll(lua_State* L)
{
	luabridge::getGlobalNamespace(L)
		.beginNamespace("zene")
			.addFunction("mixer", +[]() -> LuaMixer& { return mixerView(); })
		.endNamespace()
		.beginClass<LuaMixer>("Mixer")
			.addConstructor<void (*)()>()
			.addFunction("channelCount", &LuaMixer::channelCount)
			.addFunction("channel", &LuaMixer::channel)
			.addFunction("channelById", &LuaMixer::channelById)
			.addFunction("master", &LuaMixer::master)
			.addFunction("ids", &LuaMixer::ids)
			.addFunction("addChannel", &LuaMixer::addChannel)
		.endClass()
		.beginClass<LuaMixerChannel>("MixerChannel")
			.addConstructor<void (*)()>()
			.addFunction("isValid", &LuaMixerChannel::isValid)
			.addFunction("index", &LuaMixerChannel::index)
			.addFunction("id", &LuaMixerChannel::id)
			.addFunction("name", &LuaMixerChannel::name)
			.addFunction("setName", &LuaMixerChannel::setName)
			.addFunction("isMaster", &LuaMixerChannel::isMaster)
			.addFunction("isBus", &LuaMixerChannel::isBus)
			.addFunction("gain", &LuaMixerChannel::gain)
			.addFunction("setGain", &LuaMixerChannel::setGain)
			.addFunction("muted", &LuaMixerChannel::muted)
			.addFunction("setMuted", &LuaMixerChannel::setMuted)
			.addFunction("soloed", &LuaMixerChannel::soloed)
			.addFunction("setSoloed", &LuaMixerChannel::setSoloed)
			.addFunction("gainModel", &LuaMixerChannel::gainModel)
			.addFunction("muteModel", &LuaMixerChannel::muteModel)
			.addFunction("soloModel", &LuaMixerChannel::soloModel)
			.addFunction("chain", &LuaMixerChannel::chain)
			.addFunction("sendCount", &LuaMixerChannel::sendCount)
			.addFunction("sendTarget", &LuaMixerChannel::sendTarget)
			.addFunction("sendAmount", &LuaMixerChannel::sendAmount)
			.addFunction("sendPreFader", &LuaMixerChannel::sendPreFader)
		.endClass()
		.beginClass<LuaEffectChain>("EffectChain")
			.addConstructor<void (*)()>()
			.addFunction("isValid", &LuaEffectChain::isValid)
			.addFunction("effectCount", &LuaEffectChain::effectCount)
			.addFunction("effect", &LuaEffectChain::effect)
			.addFunction("effectById", &LuaEffectChain::effectById)
			.addFunction("loadEffect", &LuaEffectChain::loadEffect)
			.addFunction("removeEffect", &LuaEffectChain::removeEffect)
		.endClass()
		.beginClass<LuaEffect>("Effect")
			.addConstructor<void (*)()>()
			.addFunction("isValid", &LuaEffect::isValid)
			.addFunction("index", &LuaEffect::index)
			.addFunction("id", &LuaEffect::id)
			.addFunction("pluginName", &LuaEffect::pluginName)
			.addFunction("displayName", &LuaEffect::displayName)
			.addFunction("enabled", &LuaEffect::enabled)
			.addFunction("setEnabled", &LuaEffect::setEnabled)
			.addFunction("parameterCount", &LuaEffect::parameterCount)
			.addFunction("parameterName", &LuaEffect::parameterName)
			.addFunction("parameter", &LuaEffect::parameter)
		.endClass();

	// `zene.apiSurface` is a plain C closure rather than a LuaBridge function:
	// it walks the namespace table it is being registered into, which is a
	// stack operation, not a wrapped C++ call.
	lua_getglobal(L, "zene");
	if (lua_istable(L, -1))
	{
		lua_pushcfunction(L, luaApiSurface);
		lua_setfield(L, -2, "apiSurface");
	}
	lua_pop(L, 1);
}

} // namespace ScriptDawBindings

// ---------------------------------------------------------------------------
// LuaMixer
// ---------------------------------------------------------------------------

int LuaMixer::channelCount() const
{
	ScriptDawBindings::syncForRead();
	Mixer* mixer = liveMixer();
	return mixer == nullptr ? 0 : static_cast<int>(mixer->numChannels());
}

LuaMixerChannel& LuaMixer::channel(int index) const
{
	ScriptDawBindings::syncForRead();
	Mixer* mixer = liveMixer();
	if (mixer == nullptr || index < 0 || index >= static_cast<int>(mixer->numChannels()))
	{
		return ScriptDawBindings::newMixerChannel(nullptr);
	}
	return ScriptDawBindings::newMixerChannel(mixer->mixerChannel(index));
}

LuaMixerChannel& LuaMixer::channelById(const QString& id) const
{
	return channel(control::idToIndex(id, QStringLiteral("ch-")));
}

LuaMixerChannel& LuaMixer::master() const
{
	return channel(0);
}

QStringList LuaMixer::ids() const
{
	QStringList out;
	const int count = channelCount();
	for (int i = 0; i < count; ++i)
	{
		out.append(control::channelId(i));
	}
	return out;
}

// ---------------------------------------------------------------------------
// LuaMixerChannel
// ---------------------------------------------------------------------------

int LuaMixerChannel::index() const
{
	return m_channel == nullptr ? -1 : m_channel->index();
}

QString LuaMixerChannel::id() const
{
	const int at = index();
	return at < 0 ? QString() : control::channelId(at);
}

QString LuaMixerChannel::name() const
{
	ScriptDawBindings::syncForRead();
	return m_channel == nullptr ? QString() : m_channel->m_name;
}

bool LuaMixerChannel::isMaster() const
{
	return m_channel != nullptr && m_channel->isMaster();
}

bool LuaMixerChannel::isBus() const
{
	return m_channel != nullptr && m_channel->isBus();
}

float LuaMixerChannel::gain() const
{
	if (m_channel == nullptr) { return 0.0f; }
	ScriptDawBindings::syncForRead();
	return m_channel->m_volumeModel.value();
}

bool LuaMixerChannel::muted() const
{
	if (m_channel == nullptr) { return false; }
	ScriptDawBindings::syncForRead();
	return m_channel->m_muteModel.value();
}

bool LuaMixerChannel::soloed() const
{
	if (m_channel == nullptr) { return false; }
	ScriptDawBindings::syncForRead();
	return m_channel->m_soloModel.value();
}

AutomatableModel* LuaMixerChannel::gainModel() const
{
	return m_channel == nullptr ? nullptr : &m_channel->m_volumeModel;
}

AutomatableModel* LuaMixerChannel::muteModel() const
{
	return m_channel == nullptr ? nullptr : &m_channel->m_muteModel;
}

AutomatableModel* LuaMixerChannel::soloModel() const
{
	return m_channel == nullptr ? nullptr : &m_channel->m_soloModel;
}

EffectChain* LuaMixerChannel::chain() const
{
	return m_channel == nullptr ? nullptr : &m_channel->m_fxChain;
}

int LuaMixerChannel::sendCount() const
{
	return m_channel == nullptr ? 0 : static_cast<int>(m_channel->m_sends.size());
}

QString LuaMixerChannel::sendTarget(int index) const
{
	if (m_channel == nullptr || index < 0 || index >= sendCount()) { return QString(); }
	return control::channelId(m_channel->m_sends[index]->receiverIndex());
}

float LuaMixerChannel::sendAmount(int index) const
{
	if (m_channel == nullptr || index < 0 || index >= sendCount()) { return 0.0f; }
	ScriptDawBindings::syncForRead();
	return m_channel->m_sends[index]->amount()->value();
}

bool LuaMixerChannel::sendPreFader(int index) const
{
	if (m_channel == nullptr || index < 0 || index >= sendCount()) { return false; }
	return m_channel->m_sends[index]->preFader();
}

} // namespace lmms
