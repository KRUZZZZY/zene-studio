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
#include "ControlMixerSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "ScriptApiVersion.h"
#include "ScriptBindings.h"
#include "ScriptCommandBridge.h"
#include "ScriptEngine.h"
// Stack<QString>/Stack<QStringList>. WITHOUT this include every function here
// that returns a QString instantiates LuaBridge's GENERIC Stack and raises
// "The class is not registered in LuaBridge" at the first call (measured on a
// real instance). ScriptBindings.cpp includes it for the same reason; this is a
// second translation unit over the same bridge.
#include "ScriptLuaQtTypes.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "LuaBridge/LuaBridge.h"

#include <QHash>
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
//
// The pool holds ONE wrapper per OBJECT, not one per access. The wrappers are
// C++ objects the Lua collector cannot reclaim, so a pool that grew with the
// accesses grew outside the Lua memory budget: a script that read the same
// channel a thousand times left a thousand wrappers behind (new-p12-245). The
// key is the object's stable identity - the channel's id, the chain's channel
// id, the device's own id - and a wrapper never changes after it is made, so
// every reference Lua holds keeps naming the same view.
// ---------------------------------------------------------------------------

template<class T>
class ViewPool
{
public:
	//! The view for \a key: the one the pool already holds, or a new one made
	//! by \a make.
	template<class Maker>
	T& add(const QString& key, Maker make)
	{
		const auto found = m_index.constFind(key);
		if (found != m_index.constEnd())
		{
			const int at = found.value();
			if (at >= 0 && at < static_cast<int>(m_items.size())) { return m_items[at]; }
		}
		m_items.push_back(make());
		m_index[key] = static_cast<int>(m_items.size()) - 1;
		return m_items.back();
	}

	void clear() { m_items.clear(); m_index.clear(); }

	//! How many views the pool holds.
	int size() const { return static_cast<int>(m_items.size()); }

private:
	std::deque<T> m_items;
	QHash<QString, int> m_index;
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
// by tests/lua-api-surface.py, whose output docs/LUA-API-SURFACE.md is the
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

//! The `functions` array: every name in @a names, 1-based, in the order given.
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
	// function_count belongs to the SURFACE, not to the nested array: written
	// inside `functions` it was nil at the documented read
	// (`zene.apiSurface().function_count`), which raised the Lua error
	// "attempt to concatenate a nil value (field 'function_count')".
	lua_pushinteger(L, names.size());
	lua_setfield(L, -2, "function_count");
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

AutomatableModel* mixerChannelGainModel(const QString& mixerChannelId)
{
	if (mixerChannelId.isEmpty()) { return nullptr; }
	ControlResult error;
	MixerChannel* channel = control::resolveMixerChannel(mixerChannelId, &error);
	return channel == nullptr ? nullptr : &channel->m_volumeModel;
}

EffectChain* mixerChannelEffectChain(const QString& mixerChannelId)
{
	if (mixerChannelId.isEmpty()) { return nullptr; }
	ControlResult error;
	MixerChannel* channel = control::resolveMixerChannel(mixerChannelId, &error);
	return channel == nullptr ? nullptr : &channel->m_fxChain;
}

LuaMixerChannel& newMixerChannel(MixerChannel* channel)
{
	// The channel's own id is the key: the same channel read twice is the same
	// view, and a channel that is gone is a view that reports itself invalid.
	return mixerChannelPool().add(control::channelIdOf(channel),
		[channel]() { return LuaMixerChannel(channel); });
}

LuaEffectChain& newEffectChain(const QString& mixerChannelId)
{
	return effectChainPool().add(mixerChannelId,
		[mixerChannelId]() { return LuaEffectChain(mixerChannelId); });
}

LuaEffect& newEffect(const QString& mixerChannelId, Effect* effect)
{
	// The device's own id is the key: it is unique for the life of the project,
	// so the pool holds one view per device and never one per access. A view of
	// no device is keyed by the channel it was asked about, so the invalid
	// views do not pile up either.
	const QString key = effect == nullptr
		? QStringLiteral("none:%1").arg(mixerChannelId)
		: control::effectIdOf(effect);
	return effectPool().add(key, [mixerChannelId, effect]() {
		return LuaEffect(mixerChannelId, effect == nullptr ? -1 : effect->id());
	});
}

LuaMixer& mixerView()
{
	static LuaMixer view;
	return view;
}

int viewPoolSize()
{
	return mixerChannelPool().size() + effectChainPool().size() + effectPool().size();
}

void registerAll(lua_State* L)
{
	ScriptCommandBridge::registerAll(L);
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
	//
	// RAW write, on purpose: beginNamespace installs `__newindex` on the `zene`
	// table and lua_setfield takes the metamethod, so a plain lua_setfield here
	// raised `No writable member 'apiSurface'` out of the script worker and
	// aborted the process (SIGABRT) before a script ran. LuaBridge registers its
	// own members with a raw write for the same reason. Do not "tidy" this.
	lua_getglobal(L, "zene");
	if (lua_istable(L, -1))
	{
		lua_pushstring(L, "apiSurface");
		lua_pushcfunction(L, luaApiSurface);
		lua_rawset(L, -3);
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
	ScriptDawBindings::syncForRead();
	const int wanted = control::idToIndex(id, QStringLiteral("ch-"));
	Mixer* mixer = liveMixer();
	// BY ID, never by position: ch-<n> names the channel OBJECT (SPEC-stable-ids
	// slice 2) - measured, the master is ch-1 and the first added channel ch-7.
	// Passing the parsed number to channel() read it as a POSITION, so
	// `mixer:channelById(channel:id())` answered an invalid view. The surface's
	// resolveChannel looks it up the same way.
	if (mixer != nullptr && wanted >= 0)
	{
		for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
		{
			MixerChannel* candidate = mixer->mixerChannel(i);
			if (candidate != nullptr && candidate->id() == wanted)
			{
				return ScriptDawBindings::newMixerChannel(candidate);
			}
		}
	}
	return ScriptDawBindings::newMixerChannel(nullptr);
}

LuaMixerChannel& LuaMixer::master() const
{
	return channel(0);
}

QStringList LuaMixer::ids() const
{
	QStringList out;
	ScriptDawBindings::syncForRead();
	Mixer* mixer = liveMixer();
	if (mixer == nullptr) { return out; }
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		MixerChannel* channel = mixer->mixerChannel(i);
		// The channel OBJECT's own id - the same number `channel:id()` publishes
		// and the same one channelById() resolves. channelId(i) names a POSITION,
		// so this list started at "ch-0" for a master that is ch-1, and the
		// entries were ids no channel carried: feeding one back to channelById()
		// returned an invalid wrapper instead of the channel it named.
		if (channel != nullptr) { out.append(control::channelIdOf(channel)); }
	}
	return out;
}

// ---------------------------------------------------------------------------
// LuaMixerChannel
// ---------------------------------------------------------------------------

LuaMixerChannel::LuaMixerChannel(MixerChannel* channel) :
	m_id(control::channelIdOf(channel))
{
}

MixerChannel* LuaMixerChannel::resolve() const
{
	if (m_id.isEmpty()) { return nullptr; }
	ControlResult error;
	return control::resolveMixerChannel(m_id, &error);
}

bool LuaMixerChannel::isValid() const
{
	ScriptDawBindings::syncForRead();
	return resolve() != nullptr;
}

int LuaMixerChannel::index() const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return channel == nullptr ? -1 : channel->index();
}

QString LuaMixerChannel::id() const
{
	return m_id;
}

QString LuaMixerChannel::name() const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return channel == nullptr ? QString() : channel->m_name;
}

bool LuaMixerChannel::isMaster() const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return channel != nullptr && channel->isMaster();
}

bool LuaMixerChannel::isBus() const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return channel != nullptr && channel->isBus();
}

float LuaMixerChannel::gain() const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return channel == nullptr ? 0.0f : channel->m_volumeModel.value();
}

bool LuaMixerChannel::muted() const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return channel != nullptr && channel->m_muteModel.value();
}

bool LuaMixerChannel::soloed() const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return channel != nullptr && channel->m_soloModel.value();
}

LuaFloatModel& LuaMixerChannel::gainModel() const
{
	return ScriptBindings::newMixerGainModel(m_id);
}

LuaBoolModel& LuaMixerChannel::muteModel() const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return ScriptBindings::newBoolModel(channel == nullptr ? nullptr : &channel->m_muteModel);
}

LuaBoolModel& LuaMixerChannel::soloModel() const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return ScriptBindings::newBoolModel(channel == nullptr ? nullptr : &channel->m_soloModel);
}

LuaEffectChain& LuaMixerChannel::chain() const
{
	return ScriptDawBindings::newEffectChain(m_id);
}

int LuaMixerChannel::sendCount() const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return channel == nullptr ? 0 : static_cast<int>(channel->m_sends.size());
}

QString LuaMixerChannel::sendTarget(int index) const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	if (channel == nullptr || index < 0 || index >= static_cast<int>(channel->m_sends.size()))
	{
		return QString();
	}
	return control::channelIdOf(channel->m_sends[index]->receiver());
}

float LuaMixerChannel::sendAmount(int index) const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	if (channel == nullptr || index < 0 || index >= static_cast<int>(channel->m_sends.size()))
	{
		return 0.0f;
	}
	return channel->m_sends[index]->amount()->value();
}

bool LuaMixerChannel::sendPreFader(int index) const
{
	ScriptDawBindings::syncForRead();
	MixerChannel* channel = resolve();
	return channel != nullptr && index >= 0 && index < static_cast<int>(channel->m_sends.size())
		&& channel->m_sends[index]->preFader();
}

// ---------------------------------------------------------------------------
// LuaFloatModel
// ---------------------------------------------------------------------------

LuaFloatModel::LuaFloatModel(AutomatableModel* model) : m_model(model) {}

AutomatableModel* LuaFloatModel::resolve() const
{
	if (!m_mixerChannelId.isEmpty())
	{
		ScriptDawBindings::syncForRead();
		return ScriptDawBindings::mixerChannelGainModel(m_mixerChannelId);
	}
	return m_model;
}

bool LuaFloatModel::isValid() const
{
	return resolve() != nullptr;
}

float LuaFloatModel::value() const
{
	AutomatableModel* model = resolve();
	if (!model) { return 0.0f; }
	if (m_mixerChannelId.isEmpty()) { ScriptEngine::instance()->flushCommandsForRead(); }
	return model->value<float>();
}

void LuaFloatModel::setValue(float value)
{
	AutomatableModel* model = resolve();
	if (!model) { return; }
	ScriptCommand command;
	command.type = ScriptCommand::Type::SetModelValue;
	command.object0 = model;
	command.f0 = value;
	if (!ScriptEngine::instance()->enqueue(command))
	{
		// The bounded queue was full, so the write was dropped (spec section 4:
		// the producer is never blocked). The script is told, rather than left
		// to read back a value it never wrote - the same refusal the DAW
		// write half reports (src/core/ScriptDawEdit.cpp).
		ScriptEngine::instance()->logMessage(QStringLiteral(
			"[error] setting this parameter refused: the script command queue is full (%1 commands); "
			"the edit was dropped").arg(ScriptCommandQueueCapacity));
	}
}

float LuaFloatModel::minValue() const
{
	AutomatableModel* model = resolve();
	return model ? model->minValue<float>() : 0.0f;
}

float LuaFloatModel::maxValue() const
{
	AutomatableModel* model = resolve();
	return model ? model->maxValue<float>() : 0.0f;
}

QString LuaFloatModel::name() const
{
	AutomatableModel* model = resolve();
	return model ? model->displayName() : QString();
}

QString LuaFloatModel::type() const
{
	AutomatableModel* model = resolve();
	if (dynamic_cast<IntModel*>(model) != nullptr) { return QStringLiteral("int"); }
	if (dynamic_cast<BoolModel*>(model) != nullptr) { return QStringLiteral("bool"); }
	if (dynamic_cast<FloatModel*>(model) != nullptr) { return QStringLiteral("float"); }
	return QStringLiteral("unknown");
}

} // namespace lmms
