/*
 * ScriptBindings.h - Lua-facing wrapper classes for the LMMS Lua API v0
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

#ifndef LMMS_SCRIPT_BINDINGS_H
#define LMMS_SCRIPT_BINDINGS_H

#include <QString>
#include <QStringList>

#include <cstdint>

#include "Note.h"

struct lua_State;

namespace lmms
{

class ScriptEngine;
class MidiClip;
class Track;
class InstrumentTrack;
class FloatModel;
class BoolModel;
class Song;
class LuaPatternClip;

// ---------------------------------------------------------------------------
// Notes (spec section 3: Note, NoteBuilder)
// ---------------------------------------------------------------------------
//! Lua-facing note value. Registered as `Note`.
class LuaNote
{
public:
	LuaNote() = default;
	explicit LuaNote(const Note& note);

	Note toNote() const;

	int key() const { return m_key; }
	int pos() const { return m_pos; }
	int length() const { return m_length; }
	int volume() const { return m_volume; }
	int panning() const { return m_panning; }

	void setKey(int key) { m_key = key; }
	void setPos(int ticks) { m_pos = ticks; }
	void setLength(int ticks) { m_length = ticks; }
	void setVolume(int volume) { m_volume = volume; }
	void setPanning(int panning) { m_panning = panning; }

private:
	int m_key{60};
	int m_pos{0};
	int m_length{TimePos::ticksPerBar() / 4};
	int m_volume{100};
	int m_panning{0};
};

//! Fluent note construction (spec section 3: NoteBuilder).
//! `lmms.note():at(60):pos(0):len(24):vol(90):addTo(clip)`
class LuaNoteBuilder
{
public:
	LuaNoteBuilder& at(int key);
	LuaNoteBuilder& pos(int ticks);
	LuaNoteBuilder& len(int ticks);
	LuaNoteBuilder& vol(int volume);
	LuaNoteBuilder& pan(int panning);

	int key() const { return m_key; }
	int position() const { return m_pos; }
	int length() const { return m_length; }
	int volume() const { return m_volume; }
	int panning() const { return m_panning; }

	//! Materialise this builder into a pooled `Note` wrapper.
	LuaNote& build() const;
	//! Append the note to \a clip through the command queue.
	void addTo(LuaPatternClip& clip) const;

private:
	int m_key{60};
	int m_pos{0};
	int m_length{TimePos::ticksPerBar() / 4};
	int m_volume{100};
	int m_panning{0};
};

// ---------------------------------------------------------------------------
// Tracks / instruments (spec section 3: Track, InstrumentTrack, FloatModel,
// BoolModel)
// ---------------------------------------------------------------------------

//! Lua-facing wrapper for an AutomatableModel (registered as `FloatModel`).
class LuaFloatModel
{
public:
	LuaFloatModel() = default;
	explicit LuaFloatModel(FloatModel* model) : m_model(model) {}

	bool isValid() const { return m_model != nullptr; }
	float value() const;
	void setValue(float value);
	float minValue() const;
	float maxValue() const;
	QString name() const;

private:
	FloatModel* m_model{nullptr};
};

//! Lua-facing wrapper for a BoolModel (registered as `BoolModel`).
class LuaBoolModel
{
public:
	LuaBoolModel() = default;
	explicit LuaBoolModel(BoolModel* model) : m_model(model) {}

	bool isValid() const { return m_model != nullptr; }
	bool value() const;
	void setValue(bool value);
	QString name() const;

private:
	BoolModel* m_model{nullptr};
};

//! Lua-facing wrapper for an instrument track (registered as `InstrumentTrack`).
class LuaInstrumentTrack
{
public:
	LuaInstrumentTrack() = default;
	explicit LuaInstrumentTrack(InstrumentTrack* track) : m_track(track) {}

	bool isValid() const { return m_track != nullptr; }
	QString name() const;
	void setName(const QString& name);
	QString instrumentName() const;
	int volume() const;
	void setVolume(int volume);
	int panning() const;
	void setPanning(int panning);
	LuaFloatModel& volumeModel() const;
	LuaFloatModel& panningModel() const;

private:
	InstrumentTrack* m_track{nullptr};
};

//! Lua-facing wrapper for a track (registered as `Track`).
class LuaTrack
{
public:
	LuaTrack() = default;
	explicit LuaTrack(Track* track) : m_track(track) {}

	bool isValid() const { return m_track != nullptr; }
	QString name() const;
	void setName(const QString& name);
	QString type() const;
	int index() const;
	int clipCount() const;
	QString instrumentName() const;

	LuaInstrumentTrack& asInstrumentTrack() const;
	LuaFloatModel& volumeModel() const;
	LuaBoolModel& muteModel() const;

private:
	Track* m_track{nullptr};
};

// ---------------------------------------------------------------------------
// Pattern data (spec section 3: PatternClip)
// ---------------------------------------------------------------------------

/*! Lua-facing wrapper for one pattern's note data (registered as
 *  `PatternClip`). In the engine this data is stored in a MidiClip owned by an
 *  InstrumentTrack inside the PatternStore; the C++ `PatternClip` class is the
 *  timeline placeholder used by PatternTracks, which is why the wrapper name
 *  and the engine type differ.
 */
class LuaPatternClip
{
public:
	LuaPatternClip() = default;
	LuaPatternClip(MidiClip* clip, int patternIndex, int trackIndex)
		: m_clip(clip), m_patternIndex(patternIndex), m_trackIndex(trackIndex) {}

	bool isValid() const { return m_clip != nullptr; }
	int patternIndex() const { return m_patternIndex; }
	int trackIndex() const { return m_trackIndex; }

	QString name() const;
	void setName(const QString& name);
	int lengthTicks() const;
	void setLengthTicks(int ticks);

	int noteCount() const;
	LuaNote& note(int index) const;
	void addNote(const LuaNote& note);
	void addNoteAt(int key, int pos, int length, int volume);
	void removeNote(int index);
	void clearNotes();

	//! Journal checkpoint: makes subsequent edits undoable as one step.
	void addCheckPoint();

private:
	MidiClip* m_clip{nullptr};
	int m_patternIndex{-1};
	int m_trackIndex{-1};
};

// ---------------------------------------------------------------------------
// Transport / Song / PatternStore (spec section 3)
// ---------------------------------------------------------------------------

//! Lua-facing transport (registered as `Transport`).
class LuaTransport
{
public:
	bool isPlaying() const;
	int position() const;
	void play();
	void stop();

private:
	Song* song() const;
};

//! Lua-facing pattern store (registered as `PatternStore`).
class LuaPatternStore
{
public:
	int patternCount() const;
	int trackCount() const;
	LuaTrack& track(int index) const;
	LuaInstrumentTrack& addInstrumentTrack() const;
	LuaPatternClip& patternClip(int patternIndex, int trackIndex) const;
	LuaPatternClip& addPattern() const;
};

//! Lua-facing song (registered as `Song`).
class LuaSong
{
public:
	LuaPatternStore& patternStore() const;
	LuaTransport& transport() const;

	LuaPatternClip& addPattern() const;
	LuaPatternClip& pattern(int patternIndex, int trackIndex) const;
	int patternCount() const;

	int tempo() const;
	void setTempo(int bpm);
	int masterVolume() const;
	void setMasterVolume(int volume);

	QString projectName() const;
	bool saveProject(const QString& path);

private:
	Song* song() const;
};

// ---------------------------------------------------------------------------
// Project files / logging / MIDI (spec sections 3 and 5)
// ---------------------------------------------------------------------------

//! Sandboxed project-directory file access (registered as `ProjectFile`).
class LuaProjectFile
{
public:
	bool exists(const QString& path) const;
	QString read(const QString& path) const;
	bool write(const QString& path, const QString& contents) const;
	QStringList list(const QString& path) const;
	QString projectDir() const;
};

//! Script console output (registered as `LuaLog`).
class LuaLog
{
public:
	void info(const QString& message) const;
	void warn(const QString& message) const;
	void error(const QString& message) const;
	void write(const QString& message) const;
};

//! One polled MIDI-in event (registered as `MidiEvent`).
class LuaMidiEvent
{
public:
	bool isValid() const { return m_valid; }
	int channel() const { return m_channel; }
	int key() const { return m_key; }
	int velocity() const { return m_velocity; }
	bool isNoteOn() const { return m_noteOn; }

	LuaMidiEvent& set(int channel, int key, int velocity, bool noteOn);

private:
	bool m_valid{false};
	bool m_noteOn{false};
	int m_channel{-1};
	int m_key{-1};
	int m_velocity{0};
};

//! Polled MIDI input (registered as `MidiIn`).
class LuaMidiIn
{
public:
	bool hasEvent() const;
	LuaMidiEvent& next() const;
};

//! MIDI output (registered as `MidiOut`). v0 records the events and logs them.
class LuaMidiOut
{
public:
	void noteOn(int channel, int key, int velocity) const;
	void noteOff(int channel, int key) const;
};

// ---------------------------------------------------------------------------
// Binding registration + view lifetime
// ---------------------------------------------------------------------------

namespace ScriptBindings
{

//! Register every Lua class and the `lmms` namespace on \a L.
void registerAll(lua_State* L, ScriptEngine* engine);

/*! Reset the view pools. Must be called before each script run: Lua userdata
 *  holds non-owning references to pooled wrapper objects, so the pools may only
 *  be recycled once the previous lua_State has been closed. */
void beginRun();

//! Construct a fresh pooled wrapper (stable address, owned by the binder).
LuaNote& newNote(const LuaNote& note);
LuaNoteBuilder& newNoteBuilder();
LuaPatternClip& newPatternClip(MidiClip* clip, int patternIndex, int trackIndex);
LuaTrack& newTrack(Track* track);
LuaInstrumentTrack& newInstrumentTrack(InstrumentTrack* track);
LuaFloatModel& newFloatModel(FloatModel* model);
LuaBoolModel& newBoolModel(BoolModel* model);
LuaMidiEvent& newMidiEvent();

} // namespace ScriptBindings

} // namespace lmms

#endif // LMMS_SCRIPT_BINDINGS_H
