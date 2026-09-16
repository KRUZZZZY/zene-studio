/*
 * ScriptDawBindings.h - Lua-facing DAW-control objects for the Zene Lua API
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

#ifndef LMMS_SCRIPT_DAW_BINDINGS_H
#define LMMS_SCRIPT_DAW_BINDINGS_H

#include <QString>
#include <QStringList>

#include <cstdint>

struct lua_State;

namespace lmms
{

class Mixer;
class MixerChannel;
class EffectChain;
class Effect;
class AutomatableModel;

//! Forward declaration at namespace scope: naming it as `const struct
//! ScriptCommand&` inside ScriptDawBindings would declare a NEW nested type.
struct ScriptCommand;

// ---------------------------------------------------------------------------
// Mixer (registered as `Mixer`; reached as `zene.mixer()`)
// ---------------------------------------------------------------------------

/*! Lua-facing wrapper for one mixer channel.
 *
 *  Every read flushes the script's own command queue first (the apply side
 *  owns the object graph), exactly like `LuaFloatModel::value()`, so a read
 *  inside one script sees the writes that script already made.
 *
 *  Deliberately absent: `pan()`. lmms::MixerChannel carries no pan control in
 *  this tree (only InstrumentTrack/SampleTrack panningModel and per-note
 *  panning exist), which is the same reason the control surface's
 *  mixer.set_pan refuses rather than inventing one.
 */
class LuaEffectChain;  // declared below: chain() returns a reference to it

class LuaMixerChannel
{
public:
	LuaMixerChannel() = default;
	explicit LuaMixerChannel(MixerChannel* channel) : m_channel(channel) {}

	bool isValid() const { return m_channel != nullptr; }
	int index() const;
	//! "ch-<n>", the same stable id mixer.* uses.
	QString id() const;
	QString name() const;
	void setName(const QString& name);
	bool isMaster() const;
	bool isBus() const;

	//! Fader position, 0..2 (MixerChannel::m_volumeModel).
	float gain() const;
	void setGain(float gain);
	//! Mute / solo, the channel's own BoolModels.
	bool muted() const;
	void setMuted(bool muted);
	bool soloed() const;
	void setSoloed(bool soloed);

	//! The channel's fader / mute / solo as model wrappers, so a script can
	//! reach the model (min, max, display name) rather than only the value.
	AutomatableModel* gainModel() const;
	AutomatableModel* muteModel() const;
	AutomatableModel* soloModel() const;

	//! The channel's effect chain (rack chain 0), as the Lua view of it: the
	//! registered class is LuaEffectChain ("EffectChain"), so a raw
	//! lmms::EffectChain* could not cross the bridge.
	LuaEffectChain& chain() const;

	//! Number of outgoing sends, and one send as {to, amount, pre_fader}.
	int sendCount() const;
	QString sendTarget(int index) const;
	float sendAmount(int index) const;
	bool sendPreFader(int index) const;

private:
	MixerChannel* m_channel{nullptr};
};

//! Lua-facing mixer (registered as `Mixer`).
class LuaMixer
{
public:
	int channelCount() const;
	LuaMixerChannel& channel(int index) const;
	LuaMixerChannel& channelById(const QString& id) const;
	LuaMixerChannel& master() const;
	QStringList ids() const;
	//! Append a channel and return it. Queued and flushed, so the wrapper a
	//! script gets back addresses the channel the apply side created.
	LuaMixerChannel& addChannel() const;
};

// ---------------------------------------------------------------------------
// Effect chain / effect (registered as `EffectChain` and `Effect`)
// ---------------------------------------------------------------------------

/*! Lua-facing wrapper for one device instance in a chain.
 *
 *  Parameters are the engine's own AutomatableModels (the same list
 *  `plugin.param_get` reads), so a parameter read here and a parameter read
 *  through the control surface cannot disagree. */
class LuaEffect
{
public:
	LuaEffect() = default;
	LuaEffect(EffectChain* chain, Effect* effect) : m_chain(chain), m_effect(effect) {}

	bool isValid() const { return m_effective() != nullptr; }
	int index() const;
	//! "fx-<n>" inside its chain - the id plugin.* uses.
	QString id() const;
	QString pluginName() const;
	QString displayName() const;

	bool enabled() const;
	void setEnabled(bool enabled);

	int parameterCount() const;
	QString parameterName(int index) const;
	//! Parameter \a index (-1 when out of range).
	AutomatableModel* parameter(int index) const;

private:
	Effect* m_effective() const;
	EffectChain* m_chain{nullptr};
	Effect* m_effect{nullptr};
};

//! Lua-facing effect chain (registered as `EffectChain`).
class LuaEffectChain
{
public:
	LuaEffectChain() = default;
	explicit LuaEffectChain(EffectChain* chain) : m_chain(chain) {}

	bool isValid() const { return m_chain != nullptr; }
	int effectCount() const;
	LuaEffect& effect(int index) const;
	LuaEffect& effectById(const QString& id) const;
	//! Load a device onto the end of the chain. \a device is a "dev-<n>"
	//! catalogue id (plugin.list) or a plugin name this build can load.
	LuaEffect& loadEffect(const QString& device) const;
	//! Unload the effect at \a index. Refused when the index is not there.
	void removeEffect(int index);

private:
	EffectChain* m_chain{nullptr};
};

// ---------------------------------------------------------------------------
// Registration + view lifetime + the apply side
// ---------------------------------------------------------------------------

namespace ScriptDawBindings
{

/*! The DAW-control ops a script may queue.
 *
 *  One route for every structural or non-model write, because the apply-side
 *  dispatch (ScriptEngine::applyCommand) is a grandfathered cyclomatic-ratchet
 *  entry: the fixed command vocabulary grows by ONE type, not one per op, the
 *  shape ScriptCommand::Type::EmitMidiNote already uses for note-on/note-off. */
enum class Op : std::int32_t
{
	CreateChannel,     //!< object0 = Mixer*
	LoadEffect,        //!< object0 = EffectChain*, text = "dev-<n>" or a plugin name
	RemoveEffect,      //!< object0 = EffectChain*, i1 = effect index
	SetEffectEnabled,  //!< object0 = Effect*, i1 = 0/1
	SetChannelName,    //!< object0 = MixerChannel*, text = the new name
};

//! Register every DAW-control class on \a L and extend the `zene` namespace.
void registerAll(lua_State* L);

//! Reset the view pools (called once per run, before the lua_State opens).
void beginRun();

//! Flush the script's own pending commands, so a read sees the writes this
//! script has already queued. Every wrapper read that can follow a write calls
//! it (ScriptBindings.cpp's LuaFloatModel::value() does the same).
void syncForRead();

//! Pooled wrappers with stable addresses (Lua userdata holds references to
//! them). Used by the read side above and by the edit side in ScriptDawEdit.cpp.
LuaMixerChannel& newMixerChannel(MixerChannel* channel);
LuaEffectChain& newEffectChain(EffectChain* chain);
LuaEffect& newEffect(EffectChain* chain, Effect* effect);
LuaMixer& mixerView();

//! Apply one queued command on the apply side. Returns true when \a type was
//! a DAW-control op, i.e. when the caller must not look at it again.
bool applyQueuedCommand(const ScriptCommand& command);

} // namespace ScriptDawBindings

} // namespace lmms

#endif // LMMS_SCRIPT_DAW_BINDINGS_H
