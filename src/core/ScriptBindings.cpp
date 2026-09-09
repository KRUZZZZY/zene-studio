/*
 * ScriptBindings.cpp - Lua-facing wrapper classes for the LMMS Lua API v0
 *
 * Copyright (c) 2026 LMMS contributors
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

#include "ScriptBindings.h"

#include "AutomatableModel.h"
#include "Clip.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "PatternStore.h"
#include "Plugin.h"
#include "ScriptEngine.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include "LuaBridge/LuaBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <deque>
#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// luabridge::Stack specialisations for Qt value types
// ---------------------------------------------------------------------------

namespace luabridge
{

template<>
struct Stack<QString>
{
	static void push(lua_State* L, const QString& value)
	{
		const QByteArray utf8 = value.toUtf8();
		lua_pushlstring(L, utf8.constData(), static_cast<std::size_t>(utf8.size()));
	}

	static QString get(lua_State* L, int index)
	{
		std::size_t length = 0;
		const char* text = luaL_checklstring(L, index, &length);
		return QString::fromUtf8(text, static_cast<int>(length));
	}

	static bool isInstance(lua_State* L, int index) { return lua_type(L, index) == LUA_TSTRING; }
};

template<>
struct Stack<QStringList>
{
	static void push(lua_State* L, const QStringList& value)
	{
		lua_createtable(L, static_cast<int>(value.size()), 0);
		int index = 1;
		for (const QString& entry : value)
		{
			Stack<QString>::push(L, entry);
			lua_rawseti(L, -2, index++);
		}
	}

	static QStringList get(lua_State* L, int index)
	{
		QStringList result;
		if (!lua_istable(L, index)) { return result; }
		const int length = static_cast<int>(lua_rawlen(L, index));
		for (int i = 1; i <= length; ++i)
		{
			lua_rawgeti(L, index, i);
			result << Stack<QString>::get(L, -1);
			lua_pop(L, 1);
		}
		return result;
	}

	static bool isInstance(lua_State* L, int index) { return lua_istable(L, index); }
};

} // namespace luabridge

namespace lmms
{

// ---------------------------------------------------------------------------
// View pools
//
// LuaBridge userdata may hold non-owning references to wrapper objects, so
// every wrapper handed to Lua lives in a deque pool: deque never invalidates
// references to existing elements on push_back, and the pool is only recycled
// once the previous lua_State has been closed (ScriptBindings::beginRun).
// ---------------------------------------------------------------------------

namespace
{

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

ViewPool<LuaNote>& notePool()
{
	static ViewPool<LuaNote> pool;
	return pool;
}

ViewPool<LuaNoteBuilder>& noteBuilderPool()
{
	static ViewPool<LuaNoteBuilder> pool;
	return pool;
}

ViewPool<LuaPatternClip>& patternClipPool()
{
	static ViewPool<LuaPatternClip> pool;
	return pool;
}

ViewPool<LuaTrack>& trackPool()
{
	static ViewPool<LuaTrack> pool;
	return pool;
}

ViewPool<LuaInstrumentTrack>& instrumentTrackPool()
{
	static ViewPool<LuaInstrumentTrack> pool;
	return pool;
}

ViewPool<LuaInstrument>& instrumentPool()
{
	static ViewPool<LuaInstrument> pool;
	return pool;
}

ViewPool<LuaFloatModel>& floatModelPool()
{
	static ViewPool<LuaFloatModel> pool;
	return pool;
}

ViewPool<LuaBoolModel>& boolModelPool()
{
	static ViewPool<LuaBoolModel> pool;
	return pool;
}

ViewPool<LuaMidiEvent>& midiEventPool()
{
	static ViewPool<LuaMidiEvent> pool;
	return pool;
}

LuaSong& songView()
{
	static LuaSong view;
	return view;
}

LuaPatternStore& patternStoreView()
{
	static LuaPatternStore view;
	return view;
}

LuaLog& logView()
{
	static LuaLog view;
	return view;
}

LuaProjectFile& projectFileView()
{
	static LuaProjectFile view;
	return view;
}

LuaMidiIn& midiInView()
{
	static LuaMidiIn view;
	return view;
}

LuaMidiOut& midiOutView()
{
	static LuaMidiOut view;
	return view;
}

LuaNote& newNote()
{
	return notePool().add(LuaNote());
}

//! Convenience: build a command and hand it to the engine queue.
ScriptCommand makeCommand(ScriptCommand::Type type)
{
	ScriptCommand command;
	command.type = type;
	return command;
}

//! Queue a track rename. Engine state is never mutated on the worker thread
//! (spec section 4 / section 10); the apply side performs the rename.
void enqueueTrackRename(Track* track, const QString& name)
{
	if (track == nullptr)
	{
		return;
	}
	ScriptCommand command = makeCommand(ScriptCommand::Type::SetTrackName);
	command.object0 = track;
	const QByteArray utf8 = name.toUtf8();
	std::strncpy(command.text, utf8.constData(), sizeof(command.text) - 1);
	ScriptEngine::instance()->enqueue(command);
}

} // namespace

namespace ScriptBindings
{

void beginRun()
{
	notePool().clear();
	noteBuilderPool().clear();
	patternClipPool().clear();
	trackPool().clear();
	instrumentTrackPool().clear();
	instrumentPool().clear();
	floatModelPool().clear();
	boolModelPool().clear();
	midiEventPool().clear();
}

LuaNote& newNote(const LuaNote& note)
{
	return notePool().add(note);
}

LuaNoteBuilder& newNoteBuilder()
{
	return noteBuilderPool().add(LuaNoteBuilder());
}

LuaPatternClip& newPatternClip(MidiClip* clip, int patternIndex, int trackIndex)
{
	return patternClipPool().add(LuaPatternClip(clip, patternIndex, trackIndex));
}

LuaTrack& newTrack(Track* track)
{
	return trackPool().add(LuaTrack(track));
}

LuaInstrumentTrack& newInstrumentTrack(InstrumentTrack* track)
{
	return instrumentTrackPool().add(LuaInstrumentTrack(track));
}

LuaInstrument& newInstrument(Instrument* instrument)
{
	return instrumentPool().add(LuaInstrument(instrument));
}

LuaFloatModel& newFloatModel(AutomatableModel* model)
{
	return floatModelPool().add(LuaFloatModel(model));
}

LuaBoolModel& newBoolModel(BoolModel* model)
{
	return boolModelPool().add(LuaBoolModel(model));
}

LuaMidiEvent& newMidiEvent()
{
	return midiEventPool().add(LuaMidiEvent());
}

void registerAll(lua_State* L, ScriptEngine* engine)
{
	Q_UNUSED(engine)

	luabridge::getGlobalNamespace(L)
		.beginNamespace("lmms")
			.addFunction("version", +[]() -> std::string { return std::string("0.1"); })
			.addFunction("ticksPerBar", +[]() -> int { return TimePos::ticksPerBar(); })
			.addFunction("stepsPerBar", +[]() -> int { return TimePos::stepsPerBar(); })
			.addFunction("song", +[]() -> LuaSong& { return songView(); })
			.addFunction("patternStore", +[]() -> LuaPatternStore& { return patternStoreView(); })
			.addFunction("transport", +[]() -> LuaTransport& { return songView().transport(); })
			.addFunction("note", +[]() -> LuaNoteBuilder& { return newNoteBuilder(); })
			.addFunction("log", +[]() -> LuaLog& { return logView(); })
			.addFunction("projectFile", +[]() -> LuaProjectFile& { return projectFileView(); })
			.addFunction("midiIn", +[]() -> LuaMidiIn& { return midiInView(); })
			.addFunction("midiOut", +[]() -> LuaMidiOut& { return midiOutView(); })
			.addFunction("setInstructionBudget", +[](int budget) {
				ScriptEngine::instance()->setInstructionBudget(static_cast<quint64>(budget));
			})
			.addFunction("instructionBudget", +[]() -> int {
				return static_cast<int>(ScriptEngine::instance()->instructionBudget());
			})
			.addFunction("projectDir", +[]() -> QString {
				return ScriptEngine::instance()->projectDir();
			})
		.endNamespace()
		.beginClass<LuaNote>("Note")
			.addConstructor<void (*)()>()
			.addFunction("key", &LuaNote::key)
			.addFunction("setKey", &LuaNote::setKey)
			.addFunction("pos", &LuaNote::pos)
			.addFunction("setPos", &LuaNote::setPos)
			.addFunction("length", &LuaNote::length)
			.addFunction("setLength", &LuaNote::setLength)
			.addFunction("volume", &LuaNote::volume)
			.addFunction("setVolume", &LuaNote::setVolume)
			.addFunction("panning", &LuaNote::panning)
			.addFunction("setPanning", &LuaNote::setPanning)
		.endClass()
		.beginClass<LuaNoteBuilder>("NoteBuilder")
			.addConstructor<void (*)()>()
			.addFunction("at", &LuaNoteBuilder::at)
			.addFunction("pos", &LuaNoteBuilder::pos)
			.addFunction("len", &LuaNoteBuilder::len)
			.addFunction("vol", &LuaNoteBuilder::vol)
			.addFunction("pan", &LuaNoteBuilder::pan)
			.addFunction("key", &LuaNoteBuilder::key)
			.addFunction("position", &LuaNoteBuilder::position)
			.addFunction("length", &LuaNoteBuilder::length)
			.addFunction("volume", &LuaNoteBuilder::volume)
			.addFunction("panning", &LuaNoteBuilder::panning)
			.addFunction("build", &LuaNoteBuilder::build)
			.addFunction("addTo", &LuaNoteBuilder::addTo)
		.endClass()
		.beginClass<LuaFloatModel>("FloatModel")
			.addConstructor<void (*)()>()
			.addFunction("isValid", &LuaFloatModel::isValid)
			.addFunction("value", &LuaFloatModel::value)
			.addFunction("setValue", &LuaFloatModel::setValue)
			.addFunction("minValue", &LuaFloatModel::minValue)
			.addFunction("maxValue", &LuaFloatModel::maxValue)
			.addFunction("name", &LuaFloatModel::name)
			.addFunction("type", &LuaFloatModel::type)
		.endClass()
		.beginClass<LuaBoolModel>("BoolModel")
			.addConstructor<void (*)()>()
			.addFunction("isValid", &LuaBoolModel::isValid)
			.addFunction("value", &LuaBoolModel::value)
			.addFunction("setValue", &LuaBoolModel::setValue)
			.addFunction("name", &LuaBoolModel::name)
		.endClass()
		.beginClass<LuaInstrument>("Instrument")
			.addConstructor<void (*)()>()
			.addFunction("isValid", &LuaInstrument::isValid)
			.addFunction("name", &LuaInstrument::name)
			.addFunction("parameterCount", &LuaInstrument::parameterCount)
			.addFunction("parameterName", &LuaInstrument::parameterName)
			.addFunction("parameterModel", &LuaInstrument::parameterModel)
		.endClass()
		.beginClass<LuaInstrumentTrack>("InstrumentTrack")
			.addConstructor<void (*)()>()
			.addFunction("isValid", &LuaInstrumentTrack::isValid)
			.addFunction("name", &LuaInstrumentTrack::name)
			.addFunction("setName", &LuaInstrumentTrack::setName)
			.addFunction("instrumentName", &LuaInstrumentTrack::instrumentName)
			.addFunction("instrument", &LuaInstrumentTrack::instrument)
			.addFunction("volume", &LuaInstrumentTrack::volume)
			.addFunction("setVolume", &LuaInstrumentTrack::setVolume)
			.addFunction("panning", &LuaInstrumentTrack::panning)
			.addFunction("setPanning", &LuaInstrumentTrack::setPanning)
			.addFunction("volumeModel", &LuaInstrumentTrack::volumeModel)
			.addFunction("panningModel", &LuaInstrumentTrack::panningModel)
		.endClass()
		.beginClass<LuaTrack>("Track")
			.addConstructor<void (*)()>()
			.addFunction("isValid", &LuaTrack::isValid)
			.addFunction("name", &LuaTrack::name)
			.addFunction("setName", &LuaTrack::setName)
			.addFunction("type", &LuaTrack::type)
			.addFunction("index", &LuaTrack::index)
			.addFunction("clipCount", &LuaTrack::clipCount)
			.addFunction("instrumentName", &LuaTrack::instrumentName)
			.addFunction("asInstrumentTrack", &LuaTrack::asInstrumentTrack)
			.addFunction("volumeModel", &LuaTrack::volumeModel)
			.addFunction("muteModel", &LuaTrack::muteModel)
		.endClass()
		.beginClass<LuaPatternClip>("PatternClip")
			.addConstructor<void (*)()>()
			.addFunction("isValid", &LuaPatternClip::isValid)
			.addFunction("patternIndex", &LuaPatternClip::patternIndex)
			.addFunction("trackIndex", &LuaPatternClip::trackIndex)
			.addFunction("name", &LuaPatternClip::name)
			.addFunction("setName", &LuaPatternClip::setName)
			.addFunction("lengthTicks", &LuaPatternClip::lengthTicks)
			.addFunction("setLengthTicks", &LuaPatternClip::setLengthTicks)
			.addFunction("noteCount", &LuaPatternClip::noteCount)
			.addFunction("note", &LuaPatternClip::note)
			.addFunction("addNote", &LuaPatternClip::addNote)
			.addFunction("addNoteAt", &LuaPatternClip::addNoteAt)
			.addFunction("removeNote", &LuaPatternClip::removeNote)
			.addFunction("clearNotes", &LuaPatternClip::clearNotes)
			.addFunction("addCheckPoint", &LuaPatternClip::addCheckPoint)
		.endClass()
		.beginClass<LuaTransport>("Transport")
			.addConstructor<void (*)()>()
			.addFunction("isPlaying", &LuaTransport::isPlaying)
			.addFunction("position", &LuaTransport::position)
			.addFunction("play", &LuaTransport::play)
			.addFunction("stop", &LuaTransport::stop)
		.endClass()
		.beginClass<LuaPatternStore>("PatternStore")
			.addConstructor<void (*)()>()
			.addFunction("patternCount", &LuaPatternStore::patternCount)
			.addFunction("trackCount", &LuaPatternStore::trackCount)
			.addFunction("track", &LuaPatternStore::track)
			.addFunction("addInstrumentTrack", &LuaPatternStore::addInstrumentTrack)
			.addFunction("patternClip", &LuaPatternStore::patternClip)
			.addFunction("addPattern", &LuaPatternStore::addPattern)
		.endClass()
		.beginClass<LuaSong>("Song")
			.addConstructor<void (*)()>()
			.addFunction("patternStore", &LuaSong::patternStore)
			.addFunction("transport", &LuaSong::transport)
			.addFunction("addPattern", &LuaSong::addPattern)
			.addFunction("pattern", &LuaSong::pattern)
			.addFunction("patternCount", &LuaSong::patternCount)
			.addFunction("tempo", &LuaSong::tempo)
			.addFunction("setTempo", &LuaSong::setTempo)
			.addFunction("masterVolume", &LuaSong::masterVolume)
			.addFunction("setMasterVolume", &LuaSong::setMasterVolume)
			.addFunction("saveProject", &LuaSong::saveProject)
		.endClass()
		.beginClass<LuaProjectFile>("ProjectFile")
			.addConstructor<void (*)()>()
			.addFunction("exists", &LuaProjectFile::exists)
			.addFunction("read", &LuaProjectFile::read)
			.addFunction("write", &LuaProjectFile::write)
			.addFunction("list", &LuaProjectFile::list)
			.addFunction("projectDir", &LuaProjectFile::projectDir)
		.endClass()
		.beginClass<LuaLog>("LuaLog")
			.addConstructor<void (*)()>()
			.addFunction("info", &LuaLog::info)
			.addFunction("warn", &LuaLog::warn)
			.addFunction("error", &LuaLog::error)
			.addFunction("write", &LuaLog::write)
		.endClass()
		.beginClass<LuaMidiEvent>("MidiEvent")
			.addConstructor<void (*)()>()
			.addFunction("isValid", &LuaMidiEvent::isValid)
			.addFunction("channel", &LuaMidiEvent::channel)
			.addFunction("key", &LuaMidiEvent::key)
			.addFunction("velocity", &LuaMidiEvent::velocity)
			.addFunction("isNoteOn", &LuaMidiEvent::isNoteOn)
		.endClass()
		.beginClass<LuaMidiIn>("MidiIn")
			.addConstructor<void (*)()>()
			.addFunction("hasEvent", &LuaMidiIn::hasEvent)
			.addFunction("next", &LuaMidiIn::next)
		.endClass()
		.beginClass<LuaMidiOut>("MidiOut")
			.addConstructor<void (*)()>()
			.addFunction("noteOn", &LuaMidiOut::noteOn)
			.addFunction("noteOff", &LuaMidiOut::noteOff)
		.endClass();
}

} // namespace ScriptBindings

// ---------------------------------------------------------------------------
// LuaNote
// ---------------------------------------------------------------------------

LuaNote::LuaNote(const Note& note)
	: m_key(note.key())
	, m_pos(note.pos().getTicks())
	, m_length(note.length().getTicks())
	, m_volume(note.getVolume())
	, m_panning(note.getPanning())
{
}

Note LuaNote::toNote() const
{
	return Note(TimePos(m_length), TimePos(m_pos), m_key,
		static_cast<volume_t>(std::clamp(m_volume, 0, 255)),
		static_cast<panning_t>(std::clamp(m_panning, -128, 127)));
}

// ---------------------------------------------------------------------------
// LuaNoteBuilder
// ---------------------------------------------------------------------------

LuaNoteBuilder& LuaNoteBuilder::at(int key)
{
	m_key = std::clamp(key, 0, NumKeys - 1);
	return *this;
}

LuaNoteBuilder& LuaNoteBuilder::pos(int ticks)
{
	m_pos = std::max(0, ticks);
	return *this;
}

LuaNoteBuilder& LuaNoteBuilder::len(int ticks)
{
	m_length = std::max(1, ticks);
	return *this;
}

LuaNoteBuilder& LuaNoteBuilder::vol(int volume)
{
	m_volume = std::clamp(volume, 0, 255);
	return *this;
}

LuaNoteBuilder& LuaNoteBuilder::pan(int panning)
{
	m_panning = std::clamp(panning, -128, 127);
	return *this;
}

LuaNote& LuaNoteBuilder::build() const
{
	LuaNote note;
	note.setKey(m_key);
	note.setPos(m_pos);
	note.setLength(m_length);
	note.setVolume(m_volume);
	note.setPanning(m_panning);
	return ScriptBindings::newNote(note);
}

void LuaNoteBuilder::addTo(LuaPatternClip& clip) const
{
	clip.addNoteAt(m_key, m_pos, m_length, m_volume);
}

// ---------------------------------------------------------------------------
// LuaFloatModel / LuaBoolModel
// ---------------------------------------------------------------------------

float LuaFloatModel::value() const
{
	if (!m_model) { return 0.0f; }
	// Reads see writes queued earlier in the same script: the apply side
	// drains pending commands before the value is sampled.
	ScriptEngine::instance()->flushCommandsForRead();
	return m_model->value<float>();
}

void LuaFloatModel::setValue(float value)
{
	if (!m_model) { return; }
	// Model writes are queued, never applied on the worker thread: the apply
	// side owns the model graph (spec section 4). Read-back flushes the queue.
	ScriptCommand command = makeCommand(ScriptCommand::Type::SetModelValue);
	command.object0 = m_model;
	command.f0 = value;
	ScriptEngine::instance()->enqueue(command);
}

float LuaFloatModel::minValue() const
{
	return m_model ? m_model->minValue<float>() : 0.0f;
}

float LuaFloatModel::maxValue() const
{
	return m_model ? m_model->maxValue<float>() : 0.0f;
}

QString LuaFloatModel::name() const
{
	return m_model ? m_model->displayName() : QString();
}

QString LuaFloatModel::type() const
{
	if (dynamic_cast<IntModel*>(m_model) != nullptr) { return QStringLiteral("int"); }
	if (dynamic_cast<BoolModel*>(m_model) != nullptr) { return QStringLiteral("bool"); }
	if (dynamic_cast<FloatModel*>(m_model) != nullptr) { return QStringLiteral("float"); }
	return QStringLiteral("unknown");
}

bool LuaBoolModel::value() const
{
	if (!m_model) { return false; }
	ScriptEngine::instance()->flushCommandsForRead();
	// BoolModel::value() hides the base template, so call the typed overload.
	return m_model->value();
}

void LuaBoolModel::setValue(bool value)
{
	if (!m_model) { return; }
	ScriptCommand command = makeCommand(ScriptCommand::Type::SetModelValue);
	command.object0 = m_model;
	command.f0 = value ? 1.0f : 0.0f;
	ScriptEngine::instance()->enqueue(command);
}

QString LuaBoolModel::name() const
{
	return m_model ? m_model->displayName() : QString();
}

// ---------------------------------------------------------------------------
// LuaInstrumentTrack
// ---------------------------------------------------------------------------

QString LuaInstrumentTrack::name() const
{
	if (!m_track) { return QString(); }
	// Reads see prior writes from the same script (the apply side drains).
	ScriptEngine::instance()->flushCommandsForRead();
	return m_track->name();
}

void LuaInstrumentTrack::setName(const QString& name)
{
	enqueueTrackRename(m_track, name);
}

QString LuaInstrumentTrack::instrumentName() const
{
	if (!m_track || !m_track->instrument()) { return QString(); }
	return m_track->instrument()->displayName();
}

LuaInstrument& LuaInstrumentTrack::instrument() const
{
	return ScriptBindings::newInstrument(m_track ? m_track->instrument() : nullptr);
}

int LuaInstrumentTrack::volume() const
{
	if (!m_track) { return 0; }
	ScriptEngine::instance()->flushCommandsForRead();
	return m_track->getVolume();
}

void LuaInstrumentTrack::setVolume(int volume)
{
	if (!m_track) { return; }
	// Queued, not applied here: the apply side owns the track graph.
	ScriptCommand command = makeCommand(ScriptCommand::Type::SetTrackVolume);
	command.object0 = m_track;
	command.f0 = static_cast<float>(volume);
	ScriptEngine::instance()->enqueue(command);
}

int LuaInstrumentTrack::panning() const
{
	if (!m_track) { return 0; }
	ScriptEngine::instance()->flushCommandsForRead();
	return static_cast<int>(m_track->panningModel()->value());
}

void LuaInstrumentTrack::setPanning(int panning)
{
	if (!m_track) { return; }
	ScriptCommand command = makeCommand(ScriptCommand::Type::SetModelValue);
	command.object0 = m_track->panningModel();
	command.f0 = static_cast<float>(panning);
	ScriptEngine::instance()->enqueue(command);
}

LuaFloatModel& LuaInstrumentTrack::volumeModel() const
{
	return ScriptBindings::newFloatModel(m_track ? m_track->volumeModel() : nullptr);
}

LuaFloatModel& LuaInstrumentTrack::panningModel() const
{
	return ScriptBindings::newFloatModel(m_track ? m_track->panningModel() : nullptr);
}

// ---------------------------------------------------------------------------
// LuaInstrument
// ---------------------------------------------------------------------------

QString LuaInstrument::name() const
{
	return m_instrument ? m_instrument->displayName() : QString();
}

int LuaInstrument::parameterCount() const
{
	return m_instrument ? m_instrument->parameterCount() : 0;
}

QString LuaInstrument::parameterName(int index) const
{
	return m_instrument ? m_instrument->parameterName(index) : QString();
}

LuaFloatModel& LuaInstrument::parameterModel(int index) const
{
	return ScriptBindings::newFloatModel(m_instrument ? m_instrument->parameterModel(index) : nullptr);
}

// ---------------------------------------------------------------------------
// LuaTrack
// ---------------------------------------------------------------------------

QString LuaTrack::name() const
{
	if (!m_track) { return QString(); }
	// Reads see prior writes from the same script (the apply side drains).
	ScriptEngine::instance()->flushCommandsForRead();
	return m_track->name();
}

void LuaTrack::setName(const QString& name)
{
	enqueueTrackRename(m_track, name);
}

QString LuaTrack::type() const
{
	if (!m_track) { return QStringLiteral("invalid"); }
	switch (m_track->type())
	{
	case Track::Type::Instrument: return QStringLiteral("instrument");
	case Track::Type::Pattern: return QStringLiteral("pattern");
	case Track::Type::Sample: return QStringLiteral("sample");
	case Track::Type::Event: return QStringLiteral("event");
	case Track::Type::Video: return QStringLiteral("video");
	case Track::Type::Automation: return QStringLiteral("automation");
	case Track::Type::HiddenAutomation: return QStringLiteral("hidden_automation");
	case Track::Type::Count: break;
	}
	return QStringLiteral("unknown");
}

int LuaTrack::index() const
{
	if (!m_track || !m_track->trackContainer()) { return -1; }
	const TrackContainer::TrackList& tracks = m_track->trackContainer()->tracks();
	for (std::size_t i = 0; i < tracks.size(); ++i)
	{
		if (tracks[i] == m_track) { return static_cast<int>(i); }
	}
	return -1;
}

int LuaTrack::clipCount() const
{
	return m_track ? m_track->numOfClips() : 0;
}

QString LuaTrack::instrumentName() const
{
	return asInstrumentTrack().instrumentName();
}

LuaInstrumentTrack& LuaTrack::asInstrumentTrack() const
{
	return ScriptBindings::newInstrumentTrack(dynamic_cast<InstrumentTrack*>(m_track));
}

LuaFloatModel& LuaTrack::volumeModel() const
{
	auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(m_track);
	return ScriptBindings::newFloatModel(instrumentTrack ? instrumentTrack->volumeModel() : nullptr);
}

LuaBoolModel& LuaTrack::muteModel() const
{
	return ScriptBindings::newBoolModel(m_track ? m_track->getMutedModel() : nullptr);
}

// ---------------------------------------------------------------------------
// LuaPatternClip
// ---------------------------------------------------------------------------

QString LuaPatternClip::name() const
{
	if (!m_clip) { return QString(); }
	// Reads see prior writes from the same script (the apply side drains).
	ScriptEngine::instance()->flushCommandsForRead();
	return m_clip->name();
}

void LuaPatternClip::setName(const QString& name)
{
	if (!m_clip) { return; }
	ScriptCommand command = makeCommand(ScriptCommand::Type::SetClipName);
	command.object0 = m_clip;
	const QByteArray utf8 = name.toUtf8();
	std::strncpy(command.text, utf8.constData(), sizeof(command.text) - 1);
	ScriptEngine::instance()->enqueue(command);
}

int LuaPatternClip::lengthTicks() const
{
	return m_clip ? m_clip->length().getTicks() : 0;
}

void LuaPatternClip::setLengthTicks(int ticks)
{
	if (!m_clip) { return; }
	ScriptCommand command = makeCommand(ScriptCommand::Type::SetClipLength);
	command.object0 = m_clip;
	command.i0 = std::max(1, ticks);
	ScriptEngine::instance()->enqueue(command);
}

int LuaPatternClip::noteCount() const
{
	if (!m_clip) { return 0; }
	ScriptEngine::instance()->flushCommandsForRead();
	return static_cast<int>(m_clip->notes().size());
}

LuaNote& LuaPatternClip::note(int index) const
{
	ScriptEngine::instance()->flushCommandsForRead();
	if (!m_clip) { return newNote(); }
	const NoteVector& notes = m_clip->notes();
	if (index < 0 || index >= static_cast<int>(notes.size()))
	{
		throw std::out_of_range("PatternClip:note(): index out of range");
	}
	return ScriptBindings::newNote(LuaNote(*notes[index]));
}

void LuaPatternClip::addNote(const LuaNote& note)
{
	addNoteAt(note.key(), note.pos(), note.length(), note.volume());
}

void LuaPatternClip::addNoteAt(int key, int pos, int length, int volume)
{
	if (!m_clip) { return; }
	ScriptCommand command = makeCommand(ScriptCommand::Type::AddNote);
	command.object0 = m_clip;
	command.i0 = std::clamp(key, 0, NumKeys - 1);
	command.i1 = std::max(0, pos);
	command.i2 = std::max(1, length);
	command.i3 = std::clamp(volume, 0, 255);
	command.f0 = 0.0f;
	ScriptEngine::instance()->enqueue(command);
}

void LuaPatternClip::removeNote(int index)
{
	if (!m_clip) { return; }
	ScriptCommand command = makeCommand(ScriptCommand::Type::RemoveNote);
	command.object0 = m_clip;
	command.i0 = index;
	ScriptEngine::instance()->enqueue(command);
}

void LuaPatternClip::clearNotes()
{
	if (!m_clip) { return; }
	ScriptCommand command = makeCommand(ScriptCommand::Type::ClearNotes);
	command.object0 = m_clip;
	ScriptEngine::instance()->enqueue(command);
}

void LuaPatternClip::addCheckPoint()
{
	if (!m_clip) { return; }
	ScriptCommand command = makeCommand(ScriptCommand::Type::AddCheckPoint);
	command.object0 = static_cast<JournallingObject*>(m_clip);
	ScriptEngine::instance()->enqueue(command);
}

// ---------------------------------------------------------------------------
// LuaTransport
// ---------------------------------------------------------------------------

Song* LuaTransport::song() const
{
	return Engine::getSong();
}

bool LuaTransport::isPlaying() const
{
	return song() && song()->isPlaying();
}

int LuaTransport::position() const
{
	return song() ? song()->getPlayPos().getTicks() : 0;
}

void LuaTransport::play()
{
	ScriptEngine::instance()->enqueue(makeCommand(ScriptCommand::Type::Play));
}

void LuaTransport::stop()
{
	ScriptEngine::instance()->enqueue(makeCommand(ScriptCommand::Type::Stop));
}

// ---------------------------------------------------------------------------
// LuaPatternStore
// ---------------------------------------------------------------------------

int LuaPatternStore::patternCount() const
{
	return Engine::patternStore()->numOfPatterns();
}

int LuaPatternStore::trackCount() const
{
	return static_cast<int>(Engine::patternStore()->tracks().size());
}

LuaTrack& LuaPatternStore::track(int index) const
{
	return ScriptBindings::newTrack(ScriptEngine::instance()->trackAt(index));
}

LuaInstrumentTrack& LuaPatternStore::addInstrumentTrack() const
{
	auto* engine = ScriptEngine::instance();
	engine->enqueue(makeCommand(ScriptCommand::Type::AddInstrumentTrack));
	engine->flushCommandsForRead();
	return ScriptBindings::newInstrumentTrack(
		dynamic_cast<InstrumentTrack*>(engine->trackAt(engine->patternTrackCount() - 1)));
}

LuaPatternClip& LuaPatternStore::patternClip(int patternIndex, int trackIndex) const
{
	return ScriptBindings::newPatternClip(
		ScriptEngine::instance()->patternClipAt(patternIndex, trackIndex), patternIndex, trackIndex);
}

LuaPatternClip& LuaPatternStore::addPattern() const
{
	auto* engine = ScriptEngine::instance();

	// Adding the first track to the store makes LMMS create pattern 0
	// implicitly (PatternStore::updateAfterTrackAdd -> Song::addPatternTrack),
	// so only add a pattern track when the project has none. The clip then
	// lives at the current pattern index on the newest track.
	if (engine->patternTrackCount() == 0)
	{
		engine->enqueue(makeCommand(ScriptCommand::Type::AddInstrumentTrack));
		engine->flushCommandsForRead();
	}
	if (engine->patternCount() == 0)
	{
		engine->enqueue(makeCommand(ScriptCommand::Type::AddPatternTrack));
		engine->flushCommandsForRead();
	}

	const int patternIndex = engine->patternCount() - 1;
	const int trackIndex = engine->patternTrackCount() - 1;
	return ScriptBindings::newPatternClip(
		engine->patternClipAt(patternIndex, trackIndex), patternIndex, trackIndex);
}

// ---------------------------------------------------------------------------
// LuaSong
// ---------------------------------------------------------------------------

Song* LuaSong::song() const
{
	return Engine::getSong();
}

LuaPatternStore& LuaSong::patternStore() const
{
	return patternStoreView();
}

LuaTransport& LuaSong::transport() const
{
	static LuaTransport view;
	return view;
}

LuaPatternClip& LuaSong::addPattern() const
{
	return patternStoreView().addPattern();
}

LuaPatternClip& LuaSong::pattern(int patternIndex, int trackIndex) const
{
	return patternStoreView().patternClip(patternIndex, trackIndex);
}

int LuaSong::patternCount() const
{
	return patternStoreView().patternCount();
}

int LuaSong::tempo() const
{
	return song() ? song()->getTempo() : 0;
}

void LuaSong::setTempo(int bpm)
{
	ScriptCommand command = makeCommand(ScriptCommand::Type::SetTempo);
	command.i0 = std::clamp(bpm, 1, 999);
	ScriptEngine::instance()->enqueue(command);
}

int LuaSong::masterVolume() const
{
	return song() ? song()->masterVolume() : 0;
}

void LuaSong::setMasterVolume(int volume)
{
	ScriptCommand command = makeCommand(ScriptCommand::Type::SetMasterVolume);
	command.i0 = std::clamp(volume, 0, 200);
	ScriptEngine::instance()->enqueue(command);
}

bool LuaSong::saveProject(const QString& path)
{
	QString error;
	const QString resolved = ScriptEngine::instance()->resolveProjectPath(path, true, &error);
	if (resolved.isEmpty())
	{
		throw std::runtime_error(QStringLiteral("saveProject: %1").arg(error).toStdString());
	}
	if (!song()) { return false; }
	return song()->saveProjectFile(resolved);
}

// ---------------------------------------------------------------------------
// LuaProjectFile
// ---------------------------------------------------------------------------

bool LuaProjectFile::exists(const QString& path) const
{
	QString error;
	const QString resolved = ScriptEngine::instance()->resolveProjectPath(path, false, &error);
	if (resolved.isEmpty())
	{
		throw std::runtime_error(QStringLiteral("ProjectFile.exists: %1").arg(error).toStdString());
	}
	return QFileInfo::exists(resolved);
}

QString LuaProjectFile::read(const QString& path) const
{
	QString error;
	const QString resolved = ScriptEngine::instance()->resolveProjectPath(path, false, &error);
	if (resolved.isEmpty())
	{
		throw std::runtime_error(QStringLiteral("ProjectFile.read: %1").arg(error).toStdString());
	}
	QFile file(resolved);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		throw std::runtime_error(QStringLiteral("ProjectFile.read: cannot open %1").arg(path).toStdString());
	}
	return QString::fromUtf8(file.readAll());
}

bool LuaProjectFile::write(const QString& path, const QString& contents) const
{
	QString error;
	const QString resolved = ScriptEngine::instance()->resolveProjectPath(path, true, &error);
	if (resolved.isEmpty())
	{
		throw std::runtime_error(QStringLiteral("ProjectFile.write: %1").arg(error).toStdString());
	}
	QFile file(resolved);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		throw std::runtime_error(QStringLiteral("ProjectFile.write: cannot open %1").arg(path).toStdString());
	}
	return file.write(contents.toUtf8()) >= 0;
}

QStringList LuaProjectFile::list(const QString& path) const
{
	QString error;
	const QString resolved = ScriptEngine::instance()->resolveProjectPath(path, false, &error);
	if (resolved.isEmpty())
	{
		throw std::runtime_error(QStringLiteral("ProjectFile.list: %1").arg(error).toStdString());
	}
	QDir dir(resolved);
	return dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

QString LuaProjectFile::projectDir() const
{
	return ScriptEngine::instance()->projectDir();
}

// ---------------------------------------------------------------------------
// LuaLog
// ---------------------------------------------------------------------------

void LuaLog::info(const QString& message) const
{
	ScriptEngine::instance()->logMessage(QStringLiteral("[info] ") + message);
}

void LuaLog::warn(const QString& message) const
{
	ScriptEngine::instance()->logMessage(QStringLiteral("[warn] ") + message);
}

void LuaLog::error(const QString& message) const
{
	ScriptEngine::instance()->logMessage(QStringLiteral("[error] ") + message);
}

void LuaLog::write(const QString& message) const
{
	ScriptEngine::instance()->logMessage(message);
}

// ---------------------------------------------------------------------------
// LuaMidiEvent / LuaMidiIn / LuaMidiOut
// ---------------------------------------------------------------------------

LuaMidiEvent& LuaMidiEvent::set(int channel, int key, int velocity, bool noteOn)
{
	m_valid = true;
	m_channel = channel;
	m_key = key;
	m_velocity = velocity;
	m_noteOn = noteOn;
	return *this;
}

bool LuaMidiIn::hasEvent() const
{
	return ScriptEngine::instance()->midiInPending() > 0;
}

LuaMidiEvent& LuaMidiIn::next() const
{
	ScriptMidiEvent event;
	if (!ScriptEngine::instance()->popMidiInEvent(&event))
	{
		return ScriptBindings::newMidiEvent();
	}
	return ScriptBindings::newMidiEvent().set(event.channel, event.key, event.velocity, event.noteOn != 0);
}

void LuaMidiOut::noteOn(int channel, int key, int velocity) const
{
	ScriptCommand command = makeCommand(ScriptCommand::Type::EmitMidiNote);
	command.i0 = key;
	command.i1 = velocity;
	command.i2 = channel;
	command.i3 = 1;
	ScriptEngine::instance()->enqueue(command);
}

void LuaMidiOut::noteOff(int channel, int key) const
{
	ScriptCommand command = makeCommand(ScriptCommand::Type::EmitMidiNote);
	command.i0 = key;
	command.i1 = 0;
	command.i2 = channel;
	command.i3 = 0;
	ScriptEngine::instance()->enqueue(command);
}

} // namespace lmms
